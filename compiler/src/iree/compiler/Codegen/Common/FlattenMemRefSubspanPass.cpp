// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

//===- FlattenMemRefSubspanPass.cpp - Flatten n-D MemRef subspan ----------===//
//
// This file implements a pass to flatten n-D MemRef subspan ops to 1-D MemRef
// ones and folds the byte offsets on subspan ops to the consumer load/store
// ops, in preparation for lowering to the final target.
//
// This pass is needed because of how MemRef is used by subspan ops:
//
// 1) Normally MemRef should capture the mapping to the underlying buffer with
// its internal strides and offsets. However, although subspan ops in IREE are
// subview-like constructs, they carry the offset directly on the ops themselves
// and return MemRefs with the identity layout map. This is due to that IREE can
// perform various optimizations over buffer allocation and decide, for example,
// to use the same underlying buffer for two MemRefs, which are converted form
// disjoint tensors initially.
// 2) The byte offset on subspan ops is an offset into the final planned 1-D
// byte buffer, while the MemRef can be n-D without considering a backing
// buffer and its data layout.
//
// So to bridge the gap, we need to linearize the MemRef dimensions to bring it
// onto the same view as IREE: buffers are just a bag of bytes. Then we need to
// fold the byte offset on subspan ops to the consumer load/store ops, so that
// we can rely on transformations in MLIR core, because they assume MemRefs map
// to the underlying buffers with its internal strides and offsets.
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "iree/compiler/Codegen/PassDetail.h"
#include "iree/compiler/Codegen/Passes.h"
#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "llvm/Support/Debug.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/Vector/IR/VectorOps.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#define DEBUG_TYPE "iree-flatten-memref-subspan"

namespace mlir {
namespace iree_compiler {

namespace {

//===----------------------------------------------------------------------===//
// Type Conversion
//===----------------------------------------------------------------------===//

/// Returns true if the given `type` is a MemRef of rank 1.
static bool isRankOneMemRef(Type type) {
  if (auto memrefType = type.dyn_cast<MemRefType>()) {
    return memrefType.hasRank() && memrefType.getRank() == 1;
  }
  return false;
}

/// Flattens n-D MemRef to 1-D MemRef and allows other types.
struct FlattenMemRefTypeConverter final : public TypeConverter {
  FlattenMemRefTypeConverter() {
    // Allow all other types.
    addConversion([](Type type) -> Optional<Type> { return type; });

    // Convert n-D MemRef to 1-D MemRef.
    addConversion([](MemRefType type) -> Optional<Type> {
      // Convert to a MemRef with unknown dimension. This is actually more akin
      // to how IREE uses memref types: they are for representing a view from a
      // byte buffer with potentially unknown total size, as transformation
      // passes can concatenate buffers, etc.
      return MemRefType::get(ShapedType::kDynamicSize, type.getElementType(),
                             AffineMap(), type.getMemorySpace());
    });
  }
};

//===----------------------------------------------------------------------===//
// Flattening Patterns
//===----------------------------------------------------------------------===//

/// Creates a value for the total element count in `shape`, which may have
/// dynamic dimensions in `dynamicDims`.
static Value createTotalElementCountValue(ShapedType type,
                                          ValueRange dynamicDims, Location loc,
                                          OpBuilder &builder) {
  MLIRContext *context = builder.getContext();

  if (type.hasStaticShape()) {
    assert(dynamicDims.empty());
    return builder.create<arith::ConstantIndexOp>(loc, type.getNumElements());
  }

  int dynamicDimIndex = 0;
  SmallVector<Value, 4> dims;
  auto shape = type.getShape();
  AffineExpr sizeExpr = getAffineConstantExpr(1, context);
  for (int i = 0; i < shape.size(); ++i) {
    sizeExpr = sizeExpr * getAffineSymbolExpr(i, context);
    if (ShapedType::isDynamic(shape[i])) {
      dims.push_back(dynamicDims[dynamicDimIndex++]);
    } else {
      dims.push_back(builder.create<arith::ConstantIndexOp>(loc, shape[i]));
    }
  }
  return makeComposedAffineApply(builder, loc, sizeExpr, dims);
}

// Flattens memref allocation ops with more than 1 dimensions to 1 dimension.
template <typename AllocOpTy>
struct FlattenAlloc final : public OpConversionPattern<AllocOpTy> {
  using OpConversionPattern<AllocOpTy>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      AllocOpTy allocOp, typename AllocOpTy::Adaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto oldType = allocOp.getType().template dyn_cast<MemRefType>();
    if (!oldType || !oldType.getLayout().isIdentity()) return failure();

    Value dynamicDim = createTotalElementCountValue(
        oldType, allocOp.getDynamicSizes(), allocOp.getLoc(), rewriter);
    Type newType = this->getTypeConverter()->convertType(oldType);

    rewriter.replaceOpWithNewOp<AllocOpTy>(allocOp, newType.cast<MemRefType>(),
                                           ValueRange{dynamicDim});

    return success();
  }
};

/// Flattens memref global ops with more than 1 dimensions to 1 dimension.
struct FlattenGlobal final : public OpConversionPattern<memref::GlobalOp> {
  using OpConversionPattern::OpConversionPattern;

  static Attribute flattenAttribute(Attribute value, ShapedType newType) {
    if (!value) return value;
    if (auto splatAttr = value.dyn_cast<SplatElementsAttr>()) {
      return splatAttr.reshape(newType);
    } else if (auto denseAttr = value.dyn_cast<DenseElementsAttr>()) {
      return denseAttr.reshape(newType);
    }
    return {};
  }

  LogicalResult matchAndRewrite(
      memref::GlobalOp globalOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto oldType = globalOp.getType().dyn_cast<MemRefType>();
    if (!oldType || !oldType.getLayout().isIdentity()) return failure();

    auto tensorType = RankedTensorType::get({oldType.getNumElements()},
                                            oldType.getElementType());
    auto memRefType =
        MemRefType::get({oldType.getNumElements()}, oldType.getElementType(),
                        AffineMap(), oldType.getMemorySpace());
    auto newInitialValue =
        flattenAttribute(globalOp.getInitialValueAttr(), tensorType);
    rewriter.replaceOpWithNewOp<memref::GlobalOp>(
        globalOp, globalOp.getSymName(), globalOp.getSymVisibilityAttr(),
        memRefType, newInitialValue, globalOp.getConstant(),
        /*alignment=*/IntegerAttr());
    return success();
  }
};

/// Flattens memref global load ops with more than 1 dimensions to 1 dimension.
struct FlattenGetGlobal final
    : public OpConversionPattern<memref::GetGlobalOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(
      memref::GetGlobalOp getOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto oldType = getOp.getType().dyn_cast<MemRefType>();
    if (!oldType || !oldType.getLayout().isIdentity()) return failure();

    auto globalOp = dyn_cast_or_null<memref::GlobalOp>(
        SymbolTable::lookupNearestSymbolFrom(getOp, getOp.getNameAttr()));
    if (!globalOp) return failure();

    auto loadedValue = rewriter.createOrFold<memref::GetGlobalOp>(
        getOp.getLoc(), globalOp.getType(), getOp.getNameAttr());

    auto newType = getTypeConverter()->convertType(oldType).cast<ShapedType>();
    rewriter.replaceOpWithNewOp<memref::CastOp>(getOp, newType, loadedValue);
    return success();
  }
};

/// Flattens memref subspan ops with more than 1 dimensions to 1 dimension.
struct FlattenBindingSubspan final
    : public OpConversionPattern<IREE::HAL::InterfaceBindingSubspanOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(
      IREE::HAL::InterfaceBindingSubspanOp subspanOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto oldType = subspanOp.getType().dyn_cast<MemRefType>();
    // IREE subspan ops only use memref types with the default identity
    // layout maps.
    if (!oldType || !oldType.getLayout().isIdentity()) return failure();

    Value dynamicDim = createTotalElementCountValue(
        oldType, subspanOp.getDynamicDims(), subspanOp.getLoc(), rewriter);
    Type newType = getTypeConverter()->convertType(oldType);

    auto newOp = rewriter.create<IREE::HAL::InterfaceBindingSubspanOp>(
        subspanOp.getLoc(), newType, subspanOp.getSet(), subspanOp.getBinding(),
        subspanOp.getDescriptorType(), subspanOp.getByteOffset(), dynamicDim,
        subspanOp.getAlignmentAttr());
    if (isRankOneMemRef(oldType)) {
      rewriter.replaceOpWithNewOp<memref::CastOp>(subspanOp, oldType, newOp);
    } else {
      rewriter.replaceOp(subspanOp, newOp.getResult());
    }
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Linearizing Patterns
//===----------------------------------------------------------------------===//

/// Generates IR to perform index linearization with the given `indices`
/// indexing into the given memref `sourceValue`.
static Value linearizeIndices(Value sourceValue, ValueRange indices,
                              Location loc, OpBuilder &builder) {
  MemRefType sourceType = sourceValue.getType().cast<MemRefType>();
  assert(sourceType.hasRank());

  int64_t rank = sourceType.getRank();
  if (rank == 0) {
    // For source 0-D MemRef, we convert them into 1-D MemRef with unknown
    // dimension size. To convert its consumer load/store ops, we also need to
    // create an index for it.
    return builder.create<arith::ConstantIndexOp>(loc, 0);
  }

  // First try to get the strides from the MemRef type itself. This applies to
  // cases where we have static shapes and only the leading dimension is
  // dynamic.
  if (AffineMap linearLayoutMap = getStridedLinearLayoutMap(sourceType)) {
    // Dynamic strides/offset will create symbols. There should be none for the
    // static case.
    if (linearLayoutMap.getNumSymbols() == 0) {
      return makeComposedAffineApply(builder, loc, linearLayoutMap, indices);
    }
  }

  // Then try to see if the source op carries the dynamic dimensions itself.
  // If so we can still get the strides for dimensions to linearize.
  Operation *sourceOp = sourceValue.getDefiningOp();
  SmallVector<Value, 4> dims;
  dims.reserve(rank);
  if (auto shapeAwareOp =
          dyn_cast<IREE::Util::ShapeAwareOpInterface>(sourceOp)) {
    dims = shapeAwareOp.buildResultValueShape(sourceValue, builder);
  } else {
    auto getDimValues = [&](MemRefType type, ValueRange dynamicDims) {
      auto shape = type.getShape();
      int dynamicDimIndex = 0;
      for (int i = 0; i < shape.size(); ++i) {
        if (ShapedType::isDynamic(shape[i])) {
          dims.push_back(dynamicDims[dynamicDimIndex++]);
        } else {
          dims.push_back(builder.create<arith::ConstantIndexOp>(loc, shape[i]));
        }
      }
    };

    if (auto allocOp = dyn_cast<memref::AllocOp>(sourceOp)) {
      getDimValues(sourceType, allocOp.getDynamicSizes());
    } else if (auto allocaOp = dyn_cast<memref::AllocaOp>(sourceOp)) {
      getDimValues(sourceType, allocaOp.getDynamicSizes());
    } else {
      return nullptr;
    }
  }

  AffineExpr sym0, sym1, sym2;
  bindSymbols(builder.getContext(), sym0, sym1, sym2);
  MLIRContext *context = builder.getContext();
  auto mulAddMap = AffineMap::get(0, 3, {sym0 * sym1 + sym2}, context);

  Value linearIndex = indices.front();
  for (int i = 1; i < indices.size(); ++i) {
    linearIndex = builder.create<AffineApplyOp>(
        loc, mulAddMap, ValueRange{linearIndex, dims[i], indices[i]});
  }
  return linearIndex;
}

/// Linearizes indices in memref.load ops.
struct LinearizeLoadIndices final : public OpConversionPattern<memref::LoadOp> {
  using OpConversionPattern<memref::LoadOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      memref::LoadOp loadOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    if (!isRankOneMemRef(adaptor.getMemref().getType())) {
      return rewriter.notifyMatchFailure(
          loadOp, "expected converted memref of rank == 1");
    }

    Value linearIndex = linearizeIndices(
        loadOp.getMemref(), loadOp.getIndices(), loadOp.getLoc(), rewriter);
    if (!linearIndex) {
      return loadOp.emitOpError() << "failed to linearize index";
    }

    rewriter.replaceOpWithNewOp<memref::LoadOp>(loadOp, adaptor.getMemref(),
                                                linearIndex);
    return success();
  }
};

/// Linearizes indices in memref.store ops.
struct LinearizeStoreIndices final
    : public OpConversionPattern<memref::StoreOp> {
  using OpConversionPattern<memref::StoreOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      memref::StoreOp storeOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    if (!isRankOneMemRef(adaptor.getMemref().getType())) {
      return rewriter.notifyMatchFailure(
          storeOp, "expected converted memref of rank == 1");
    }

    Value linearIndex = linearizeIndices(
        storeOp.getMemref(), storeOp.getIndices(), storeOp.getLoc(), rewriter);
    if (!linearIndex) {
      return storeOp.emitOpError() << "failed to linearize index";
    }

    rewriter.replaceOpWithNewOp<memref::StoreOp>(
        storeOp, adaptor.getValue(), adaptor.getMemref(), linearIndex);
    return success();
  }
};

/// Linearizes indices in vector.transfer_read ops.
struct LinearizeTransferReadIndices final
    : public OpConversionPattern<vector::TransferReadOp> {
  using OpConversionPattern<vector::TransferReadOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      vector::TransferReadOp transferReadOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    if (!transferReadOp.getPermutationMap().isMinorIdentity()) {
      return rewriter.notifyMatchFailure(
          transferReadOp, "cannot convert op with non-minor identity map");
    }
    if (!isRankOneMemRef(adaptor.getSource().getType())) {
      return rewriter.notifyMatchFailure(
          transferReadOp, "expected converted memref of rank == 1");
    }
    Value linearIndex = linearizeIndices(transferReadOp.getSource(),
                                         transferReadOp.getIndices(),
                                         transferReadOp.getLoc(), rewriter);
    if (!linearIndex) {
      return transferReadOp.emitOpError() << "failed to linearize index";
    }

    rewriter.replaceOpWithNewOp<vector::TransferReadOp>(
        transferReadOp, transferReadOp.getVectorType(), adaptor.getSource(),
        linearIndex, AffineMapAttr::get(rewriter.getDimIdentityMap()),
        transferReadOp.getPadding(), /*mask=*/Value(),
        transferReadOp.getInBoundsAttr());
    return success();
  }
};

/// Linearizes indices in vector.transfer_write ops.
struct LinearizeTransferWriteIndices final
    : public OpConversionPattern<vector::TransferWriteOp> {
  using OpConversionPattern<vector::TransferWriteOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      vector::TransferWriteOp transferWriteOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    if (!transferWriteOp.getPermutationMap().isMinorIdentity()) {
      return rewriter.notifyMatchFailure(
          transferWriteOp, "cannot convert op with non-minor identity map");
    }
    if (!isRankOneMemRef(adaptor.getSource().getType())) {
      return rewriter.notifyMatchFailure(
          transferWriteOp, "expected converted memref of rank == 1");
    }
    Value linearIndex = linearizeIndices(transferWriteOp.getSource(),
                                         transferWriteOp.getIndices(),
                                         transferWriteOp.getLoc(), rewriter);
    if (!linearIndex) {
      return transferWriteOp.emitOpError() << "failed to linearize index";
    }

    rewriter.replaceOpWithNewOp<vector::TransferWriteOp>(
        transferWriteOp, adaptor.getVector(), adaptor.getSource(), linearIndex,
        AffineMapAttr::get(rewriter.getDimIdentityMap()),
        transferWriteOp.getInBoundsAttr());
    return success();
  }
};

/// Adjusts unrealized_conversion_cast ops' inputs to flattened memref values.
struct AdjustConversionCast final
    : public OpConversionPattern<UnrealizedConversionCastOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(
      UnrealizedConversionCastOp castOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    if (castOp->getNumOperands() != 1) return failure();

    Value input = adaptor.getOperands().front();
    // We only want to handle cases where the cast op handles memref types.
    if (!input.getType().isa<BaseMemRefType>()) return failure();

    if (!isRankOneMemRef(input.getType())) {
      return rewriter.notifyMatchFailure(
          castOp, "expected converted memref of rank == 1");
    }
    rewriter.replaceOpWithNewOp<UnrealizedConversionCastOp>(
        castOp, castOp.getResultTypes(), input);
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Folding Patterns
//===----------------------------------------------------------------------===//

/// Removes MemRef reshape ops given that we'll linearize both the source and
/// target type to the same one.
template <typename ReshapeOpTy>
struct FoldMemRefReshape final : public OpConversionPattern<ReshapeOpTy> {
  using OpConversionPattern<ReshapeOpTy>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      ReshapeOpTy op, typename ReshapeOpTy::Adaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto typeConverter = OpConversionPattern<ReshapeOpTy>::typeConverter;
    if (!isRankOneMemRef(adaptor.getSrc().getType())) {
      return rewriter.notifyMatchFailure(
          op, "expected converted memref of rank == 1");
    }

    // If the types are the same, just elide. Otherwise, introduce a cast
    // so long as both are 1D. This is most often the result of needing
    // to become more static (i.e. memref<?xf32> -> memref<5xf32>).
    // Refuse to match if the cast would be illegal.
    Type newSourceType = adaptor.getSrc().getType();
    Type neededResultType =
        typeConverter->convertType(op.getResult().getType());
    if (!neededResultType) return failure();
    if (newSourceType == neededResultType) {
      rewriter.replaceOp(op, adaptor.getSrc());
      return success();
    } else if (isRankOneMemRef(neededResultType)) {
      rewriter.replaceOpWithNewOp<memref::CastOp>(op, neededResultType,
                                                  adaptor.getSrc());
      return success();
    }
    return rewriter.notifyMatchFailure(op, [&](Diagnostic &d) {
      d << "replacement type incompatible (" << newSourceType << " vs "
        << neededResultType << ")";
    });
  };
};

/// Returns the number of bytes of the given `type`. Returns llvm::None if
/// cannot deduce.
///
/// Note that this should be kept consistent with how the byte offset was
/// calculated in the subspan ops!
Optional<int64_t> getNumBytes(Type type) {
  if (type.isIntOrFloat()) return IREE::Util::getRoundedElementByteWidth(type);
  if (auto vectorType = type.dyn_cast<VectorType>()) {
    auto elementBytes = getNumBytes(vectorType.getElementType());
    if (!elementBytes) return llvm::None;
    return elementBytes.value() * vectorType.getNumElements();
  }
  return llvm::None;
}

/// Folds the byte offset on subspan ops into the consumer load/store ops.
template <typename OpType>
struct FoldSubspanOffsetIntoLoadStore final : public OpRewritePattern<OpType> {
  using OpRewritePattern<OpType>::OpRewritePattern;

  LogicalResult matchAndRewrite(OpType op,
                                PatternRewriter &rewriter) const override {
    auto memrefType = op.getMemref().getType().template cast<MemRefType>();
    if (!isRankOneMemRef(memrefType)) {
      return rewriter.notifyMatchFailure(op, "expected 1-D memref");
    }

    auto subspanOp =
        op.getMemref()
            .template getDefiningOp<IREE::HAL::InterfaceBindingSubspanOp>();
    if (!subspanOp) return failure();

    // If the subspan op has a zero byte offset then we are done.
    if (!subspanOp.getByteOffset() ||
        matchPattern(subspanOp.getByteOffset(), m_Zero())) {
      return failure();
    }

    // Calculate the offset we need to add to the load/store op, in terms of how
    // many elements.
    Optional<int64_t> numBytes = getNumBytes(memrefType.getElementType());
    if (!numBytes) {
      return rewriter.notifyMatchFailure(op,
                                         "cannot deduce element byte count");
    }
    // Create a new subspan op with zero byte offset at the original location.
    auto ip = rewriter.saveInsertionPoint();
    rewriter.setInsertionPointAfter(subspanOp);
    Value zero =
        rewriter.create<arith::ConstantIndexOp>(op.getMemref().getLoc(), 0);
    Value newSubspan = rewriter.create<IREE::HAL::InterfaceBindingSubspanOp>(
        op.getMemref().getLoc(), subspanOp.getType(), subspanOp.getSet(),
        subspanOp.getBinding(), subspanOp.getDescriptorType(), zero,
        subspanOp.getDynamicDims(), subspanOp.getAlignmentAttr());
    rewriter.restoreInsertionPoint(ip);

    MLIRContext *context = rewriter.getContext();
    AffineExpr sym0, sym1;
    bindSymbols(context, sym0, sym1);
    auto addMap = AffineMap::get(0, 2, {sym0 + sym1}, context);
    auto divMap = AffineMap::get(0, 2, {sym0.floorDiv(sym1)}, context);

    Value byteValue = rewriter.create<arith::ConstantIndexOp>(
        op.getMemref().getLoc(), numBytes.value());
    // We assume that upper layers guarantee the byte offset is perfectly
    // divisible by the element byte count so the content is well aligned.
    Value offset = rewriter.create<AffineApplyOp>(
        op.getLoc(), divMap, ValueRange{subspanOp.getByteOffset(), byteValue});

    // Get the new index by adding the old index with the offset.
    Value newIndex = rewriter.create<AffineApplyOp>(
        op.getLoc(), addMap, ValueRange{op.getIndices().front(), offset});

    if (std::is_same<OpType, memref::LoadOp>::value) {
      rewriter.replaceOpWithNewOp<memref::LoadOp>(
          op, memrefType.getElementType(), ValueRange{newSubspan, newIndex});
    } else {
      rewriter.replaceOpWithNewOp<memref::StoreOp>(
          op, TypeRange{}, ValueRange{op.getOperand(0), newSubspan, newIndex});
    }

    return success();
  }
};

/// Erase alignment hints.
struct RemoveAssumeAlignOp
    : public OpRewritePattern<memref::AssumeAlignmentOp> {
 public:
  using OpRewritePattern<memref::AssumeAlignmentOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(memref::AssumeAlignmentOp op,
                                PatternRewriter &rewriter) const override {
    rewriter.eraseOp(op);
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct FlattenMemRefSubspanPass
    : public FlattenMemRefSubspanBase<FlattenMemRefSubspanPass> {
  FlattenMemRefSubspanPass() {}
  FlattenMemRefSubspanPass(const FlattenMemRefSubspanPass &pass) {}

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<AffineDialect, memref::MemRefDialect>();
  }

  void runOnOperation() override {
    // First flatten the dimensions of subspan op and their consumer load/store
    // ops. This requires setting up conversion targets with type converter.

    MLIRContext &context = getContext();

    // This pass currently doesn't support alignment hints so remove them first.
    RewritePatternSet patterns(&context);
    patterns.add<RemoveAssumeAlignOp>(&context);
    (void)applyPatternsAndFoldGreedily(getOperation(), std::move(patterns));

    RewritePatternSet flattenPatterns(&context);

    // For subspan ops that "generate" MemRef values, convert all MemRef types
    // to 1-D dynamic sized one. This matches how IREE models buffers nicely.
    FlattenMemRefTypeConverter fullyDynamicTypeConverter;
    flattenPatterns.add<FlattenBindingSubspan>(fullyDynamicTypeConverter,
                                               &context);

    // For other ops that generate MemRef values, we may not be able to go fully
    // dynamic (e.g., memref::GlobalOp). Still convert everything to 1-D though.
    FlattenMemRefTypeConverter only1DStaticTypeConverter;
    only1DStaticTypeConverter.addConversion(
        [](MemRefType type) -> Optional<Type> {
          // 1-D MemRef types are okay.
          if (isRankOneMemRef(type)) return type;
          // Fall back to the default conversion flow.
          return llvm::None;
        });
    flattenPatterns
        .add<FlattenAlloc<memref::AllocaOp>, FlattenAlloc<memref::AllocOp>,
             FlattenGlobal, FlattenGetGlobal, LinearizeLoadIndices,
             LinearizeStoreIndices, LinearizeTransferReadIndices,
             LinearizeTransferWriteIndices, AdjustConversionCast,
             FoldMemRefReshape<memref::CollapseShapeOp>,
             FoldMemRefReshape<memref::ExpandShapeOp>>(
            only1DStaticTypeConverter, &context);

    ConversionTarget target(context);
    target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });
    target.addDynamicallyLegalOp<memref::AllocaOp, memref::AllocOp,
                                 memref::GetGlobalOp>([](Operation *op) {
      return isRankOneMemRef(op->getResultTypes().front());
    });
    target
        .addDynamicallyLegalOp<memref::CollapseShapeOp, memref::ExpandShapeOp>(
            [](Operation *op) {
              return isRankOneMemRef(op->getResultTypes().front()) &&
                     isRankOneMemRef(op->getOperandTypes().front());
            });
    target.addDynamicallyLegalOp<IREE::HAL::InterfaceBindingSubspanOp>(
        [](IREE::HAL::InterfaceBindingSubspanOp op) {
          return isRankOneMemRef(op.getType()) &&
                 // Additionally require dynamic sized dimension.
                 op.getType().cast<MemRefType>().isDynamicDim(0);
        });
    target.addDynamicallyLegalOp<memref::GlobalOp>(
        [](memref::GlobalOp op) { return isRankOneMemRef(op.getType()); });
    target.addDynamicallyLegalOp<memref::LoadOp>([](memref::LoadOp loadOp) {
      return isRankOneMemRef(loadOp.getMemRefType());
    });
    target.addDynamicallyLegalOp<memref::StoreOp>([](memref::StoreOp storeOp) {
      return isRankOneMemRef(storeOp.getMemRefType());
    });
    target.addDynamicallyLegalOp<vector::TransferReadOp>(
        [](vector::TransferReadOp readOp) {
          return isRankOneMemRef(
              readOp.getSource().getType().cast<MemRefType>());
        });
    target.addDynamicallyLegalOp<vector::TransferWriteOp>(
        [](vector::TransferWriteOp writeOp) {
          return isRankOneMemRef(
              writeOp.getSource().getType().cast<MemRefType>());
        });
    target.addDynamicallyLegalOp<UnrealizedConversionCastOp>(
        [](UnrealizedConversionCastOp castOp) {
          if (castOp->getNumOperands() != 1) return false;

          Type inputType = castOp->getOperandTypes().front();
          return !inputType.isa<BaseMemRefType>() || isRankOneMemRef(inputType);
        });

    // Use partial conversion here so that we can ignore allocations created by
    // promotion and their load/store ops.
    if (failed(applyPartialConversion(getOperation(), target,
                                      std::move(flattenPatterns)))) {
      return signalPassFailure();
    }

    // Then fold byte offset on subspan ops into consumer load/store ops.

    RewritePatternSet foldPatterns(&context);
    foldPatterns.add<FoldSubspanOffsetIntoLoadStore<memref::LoadOp>,
                     FoldSubspanOffsetIntoLoadStore<memref::StoreOp>>(&context);

    if (failed(applyPatternsAndFoldGreedily(getOperation(),
                                            std::move(foldPatterns)))) {
      return signalPassFailure();
    }
  }
};

}  // namespace

std::unique_ptr<OperationPass<ModuleOp>> createFlattenMemRefSubspanPass() {
  return std::make_unique<FlattenMemRefSubspanPass>();
}

}  // namespace iree_compiler
}  // namespace mlir
