// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/Stream/Conversion/HALToStream/ConvertHALToStream.h"

#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/Stream/Conversion/PatternUtils.h"
#include "iree/compiler/Dialect/Stream/IR/StreamDialect.h"
#include "iree/compiler/Dialect/Stream/IR/StreamOps.h"
#include "mlir/Dialect/Arithmetic/IR/Arithmetic.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

namespace mlir {
namespace iree_compiler {

namespace {

// %1 = hal.tensor.import %0 : !hal.buffer_view -> tensor<4xf32>
// ->
// %1 = stream.tensor.import %0 : !hal.buffer_view ->
//                                tensor<4xf32> in !stream.resource<*>
struct ConvertTensorImportOp
    : public OpConversionPattern<IREE::HAL::TensorImportOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(
      IREE::HAL::TensorImportOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto sourceType = op.getSource().getType();
    auto targetType = op.getTargetEncoding();
    if (!sourceType.isa<IREE::HAL::BufferType>() &&
        !sourceType.isa<IREE::HAL::BufferViewType>()) {
      return rewriter.notifyMatchFailure(op, "unsupported HAL cast conversion");
    }

    // Assert the shape of the buffer view matches the expected encoding
    // shape. We can only do this when we are importing a buffer view as that's
    // what carries the information we need to validate.
    if (sourceType.isa<IREE::HAL::BufferViewType>()) {
      // NOTE: we do this before the other checks as it's the most likely
      // mistake and it's better to know of a shape mismatch than just buffer
      // byte length difference.
      if (auto tensorType = targetType.dyn_cast<RankedTensorType>()) {
        // TODO(benvanik): get a name for the tensor (argument name/etc).
        auto message = rewriter.getStringAttr("tensor");
        if (failed(buildEncodingAssertions(op.getLoc(), adaptor.getSource(),
                                           message, tensorType,
                                           op.getTargetDims(), rewriter))) {
          return rewriter.notifyMatchFailure(op, "unsupported tensor type");
        }
      }
    }

    // Import (buffer view to stream resource).
    auto resultType = rewriter.getType<IREE::Stream::ResourceType>(
        IREE::Stream::Lifetime::External);
    auto resultSize = rewriter.createOrFold<IREE::Stream::TensorSizeOfOp>(
        op.getLoc(), rewriter.getIndexType(),
        TypeAttr::get(op.getTarget().getType()), adaptor.getTargetDims(),
        /*affinity=*/nullptr);
    auto newOp = rewriter.create<IREE::Stream::TensorImportOp>(
        op.getLoc(), resultType, adaptor.getSource(), TypeAttr::get(targetType),
        adaptor.getTargetDims(), resultSize,
        /*affinity=*/nullptr);

    auto unknownType = rewriter.getType<IREE::Stream::ResourceType>();
    rewriter.replaceOpWithNewOp<IREE::Stream::AsyncTransferOp>(
        op, unknownType, newOp.getResult(), resultSize, resultSize,
        /*source_affinity=*/nullptr,
        /*result_affinity=*/nullptr);
    return success();
  }

  // Inserts IR to assert that the buffer view shape and encoding matches the
  // expected encoding we have in the program. This ensures that the user didn't
  // pass a 4x8xf32 when they originally compiled the model for a 2x8x1xi8.
  static LogicalResult buildEncodingAssertions(Location loc, Value bufferView,
                                               StringAttr message,
                                               RankedTensorType tensorType,
                                               ValueRange dynamicDims,
                                               OpBuilder &builder) {
    auto elementType =
        IREE::HAL::getElementTypeValue(tensorType.getElementType());
    if (!elementType.has_value()) {
      return mlir::emitError(loc)
             << "invalid tensor element type: " << tensorType.getElementType();
    }
    auto expectedElementType =
        builder.create<arith::ConstantIntOp>(loc, elementType.value(), 32);

    auto encodingType =
        IREE::HAL::getEncodingTypeValue(tensorType.getEncoding());
    if (!encodingType.has_value()) {
      return mlir::emitError(loc)
             << "invalid tensor encoding: " << tensorType.getEncoding();
    }
    auto expectedEncodingType =
        builder.create<arith::ConstantIntOp>(loc, encodingType.value(), 32);

    SmallVector<Value> shapeDims;
    if (tensorType.getRank() > 0) {
      unsigned dynamicIdx = 0;
      for (int64_t idx = 0; idx < tensorType.getRank(); ++idx) {
        Value expectedDim;
        if (tensorType.isDynamicDim(idx)) {
          expectedDim = dynamicDims[dynamicIdx++];
        } else {
          expectedDim = builder.create<arith::ConstantIndexOp>(
              loc, tensorType.getDimSize(idx));
        }
        shapeDims.push_back(expectedDim);
      }
    }

    builder.create<IREE::HAL::BufferViewAssertOp>(
        loc, bufferView, message, expectedElementType, expectedEncodingType,
        shapeDims);
    return success();
  }
};

// %1 = hal.tensor.export %0 : tensor<4xf32> -> !hal.buffer_view
// ->
// %1 = stream.tensor.export %0 : tensor<4xf32> in !stream.resource<*> ->
//                                !hal.buffer_view
struct ConvertTensorExportOp
    : public OpConversionPattern<IREE::HAL::TensorExportOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(
      IREE::HAL::TensorExportOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto sourceType = op.getSourceEncoding();
    auto targetType = op.getTarget().getType();
    if (!targetType.isa<IREE::HAL::BufferType>() &&
        !targetType.isa<IREE::HAL::BufferViewType>()) {
      return rewriter.notifyMatchFailure(op, "unsupported HAL cast conversion");
    }

    auto source =
        consumeTensorOperand(op.getLoc(), adaptor.getSource(), rewriter);
    auto externalType = rewriter.getType<IREE::Stream::ResourceType>(
        IREE::Stream::Lifetime::External);
    auto exportSource = adaptor.getSource();
    auto exportSize = source.resourceSize;
    if (adaptor.getTargetStorage()) {
      // Query the target storage buffer length; we will only populate up to
      // what is required for the output.
      auto storageSize =
          rewriter
              .create<IREE::HAL::BufferLengthOp>(
                  op.getLoc(), rewriter.getIndexType(), op.getTargetStorage())
              .getResult();

      // Import the target storage as a resource that we can use as an update
      // target. We overwrite the contents and just cast the storage to the
      // target type so we know we can update it.
      auto importOp = rewriter.create<IREE::Stream::TensorImportOp>(
          op.getLoc(), externalType, adaptor.getTargetStorage(),
          TypeAttr::get(sourceType), adaptor.getSourceDims(), storageSize,
          /*affinity=*/nullptr);

      // Copy the source value into the imported target storage.
      auto zeroOffset = rewriter.create<arith::ConstantIndexOp>(op.getLoc(), 0);
      auto updateOp = rewriter.create<IREE::Stream::AsyncUpdateOp>(
          op.getLoc(), externalType, importOp.getResult(),
          importOp.getResultSize(), zeroOffset, source.resourceSize,
          source.resource, source.resourceSize,
          /*affinity=*/nullptr);

      // Export the updated resource.
      // NOTE: the buffer size wrapped in the buffer view is the full size of
      // the input buffer. This is so that we don't insert a data dependency on
      // sparse operations or data-dependent dynamic shape dimensions.
      exportSource = updateOp.getResult();
      exportSize = updateOp.getTargetSize();
    } else {
      // Exporting a produced value - transfer our source value to an externally
      // usable resource and directly export it. This will cause an allocation.
      if (source.resource.getType() != externalType) {
        exportSource = rewriter.create<IREE::Stream::AsyncTransferOp>(
            op.getLoc(), externalType, source.resource, source.resourceSize,
            source.resourceSize,
            /*source_affinity=*/nullptr,
            /*result_affinity=*/nullptr);
      }
    }

    // Export (stream resource to buffer view).
    rewriter.replaceOpWithNewOp<IREE::Stream::TensorExportOp>(
        op, targetType, exportSource, TypeAttr::get(sourceType),
        adaptor.getSourceDims(), exportSize,
        /*affinity=*/nullptr);
    return success();
  }
};

}  // namespace

void populateHALToStreamConversionPatterns(MLIRContext *context,
                                           TypeConverter &typeConverter,
                                           RewritePatternSet &patterns) {
  typeConverter.addConversion(
      [](IREE::HAL::BufferViewType type) { return type; });
  patterns.insert<ConvertTensorImportOp>(typeConverter, context);
  patterns.insert<ConvertTensorExportOp>(typeConverter, context);
}

void populateHALToStreamConversionPatterns(MLIRContext *context,
                                           ConversionTarget &conversionTarget,
                                           TypeConverter &typeConverter,
                                           RewritePatternSet &patterns) {
  // Allow executables through without modification.
  conversionTarget.addLegalOp<IREE::HAL::ExecutableOp>();
  conversionTarget.markOpRecursivelyLegal<IREE::HAL::ExecutableOp>();

  conversionTarget.addDynamicallyLegalOp<IREE::HAL::TensorImportOp>(
      [&](IREE::HAL::TensorImportOp op) {
        return typeConverter.isLegal(op.getSource().getType()) &&
               typeConverter.isLegal(op.getTarget().getType());
      });
  conversionTarget.addDynamicallyLegalOp<IREE::HAL::TensorExportOp>(
      [&](IREE::HAL::TensorExportOp op) {
        return typeConverter.isLegal(op.getSource().getType()) &&
               typeConverter.isLegal(op.getTarget().getType());
      });

  populateHALToStreamConversionPatterns(context, typeConverter, patterns);
}

}  // namespace iree_compiler
}  // namespace mlir
