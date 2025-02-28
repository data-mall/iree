// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/HAL/Target/VMVX/VMVXTarget.h"

#include "iree/compiler/Codegen/Dialect/IREECodegenDialect.h"
#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"
#include "iree/compiler/Dialect/HAL/Target/TargetRegistry.h"
#include "iree/compiler/Dialect/VM/Conversion/ConversionTarget.h"
#include "iree/compiler/Dialect/VM/IR/VMDialect.h"
#include "iree/compiler/Dialect/VM/Target/Bytecode/BytecodeModuleTarget.h"
#include "iree/compiler/Dialect/VM/Transforms/Passes.h"
#include "iree/compiler/Dialect/VMVX/IR/VMVXDialect.h"
#include "iree/compiler/Dialect/VMVX/Transforms/Passes.h"
#include "iree/compiler/Utils/FlatbufferUtils.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormatVariadic.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/LogicalResult.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

class VMVXTargetBackend final : public TargetBackend {
 public:
  VMVXTargetBackend() = default;

  std::string name() const override { return "vmvx"; }

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<IREE::Codegen::IREECodegenDialect, IREE::VM::VMDialect,
                    IREE::VMVX::VMVXDialect>();
  }

  IREE::HAL::DeviceTargetAttr getDefaultDeviceTarget(
      MLIRContext *context) const override {
    Builder b(context);
    SmallVector<NamedAttribute> configItems;

    // Indicates that the runtime HAL driver operates only in the legacy
    // synchronous mode.
    configItems.emplace_back(b.getStringAttr("legacy_sync"), b.getUnitAttr());

    configItems.emplace_back(b.getStringAttr("executable_targets"),
                             getExecutableTargets(context));

    auto configAttr = b.getDictionaryAttr(configItems);
    return IREE::HAL::DeviceTargetAttr::get(
        context, b.getStringAttr(deviceID()), configAttr);
  }

  void buildTranslationPassPipeline(OpPassManager &passManager) override {
    IREE::VMVX::buildVMVXTransformPassPipeline(passManager);

    OpPassManager &nestedModulePM = passManager.nest<ModuleOp>();

    // TODO(benvanik): derive these from a vm target triple.
    auto vmOptions = IREE::VM::TargetOptions::FromFlags::get();
    vmOptions.f32Extension = true;
    vmOptions.optimizeForStackSize = false;
    IREE::VM::buildVMTransformPassPipeline(nestedModulePM, vmOptions);
  }

  LogicalResult linkExecutables(mlir::ModuleOp moduleOp) override {
    OpBuilder builder = OpBuilder::atBlockBegin(moduleOp.getBody());

    auto sourceExecutableOps =
        llvm::to_vector<8>(moduleOp.getOps<IREE::HAL::ExecutableOp>());
    if (sourceExecutableOps.size() <= 1) return success();

    // TODO(benvanik): rework linking to support multiple formats.
    auto sharedTargetAttr = getExecutableTarget(builder.getContext());

    // Create our new "linked" hal.executable.
    std::string linkedExecutableName = llvm::formatv("{0}_linked", name());
    auto linkedExecutableOp = builder.create<IREE::HAL::ExecutableOp>(
        moduleOp.getLoc(), linkedExecutableName);
    linkedExecutableOp.setVisibility(
        sourceExecutableOps.front().getVisibility());

    // Add our VMVX hal.executable.variant with an empty module.
    builder.setInsertionPointToStart(&linkedExecutableOp.getBlock());
    auto linkedTargetOp = builder.create<IREE::HAL::ExecutableVariantOp>(
        moduleOp.getLoc(), sharedTargetAttr.getSymbolNameFragment(),
        sharedTargetAttr);
    builder.setInsertionPoint(&linkedTargetOp.getBlock().back());
    auto linkedModuleOp = builder.create<ModuleOp>(moduleOp.getLoc());

    // Add an empty vm.module to that module (as our vm.funcs must live in it).
    builder.setInsertionPointToStart(linkedModuleOp.getBody());
    builder.create<IREE::VM::ModuleOp>(moduleOp.getLoc(), "linked_module");

    // Try linking together all executables in moduleOp.
    return linkExecutablesInto(
        moduleOp, sourceExecutableOps, linkedExecutableOp, linkedTargetOp,
        [](mlir::ModuleOp moduleOp) {
          return *moduleOp.getOps<IREE::VM::ModuleOp>().begin();
        },
        builder);
  }

  LogicalResult serializeExecutable(const SerializationOptions &options,
                                    IREE::HAL::ExecutableVariantOp variantOp,
                                    OpBuilder &executableBuilder) override {
    // Add reflection information used at runtime specific to the HAL interface.
    SymbolTable symbolTable(variantOp.getInnerModule());
    for (auto exportOp : variantOp.getBlock().getOps<ExecutableExportOp>()) {
      auto funcOp = symbolTable.lookup<IREE::VM::FuncOp>(exportOp.getName());

      // Optionally entry points may specify that they require workgroup local
      // memory. We fetch that value here and plumb it through so the runtime
      // knows how much memory to reserve and pass in.
      auto localMemorySizeAttr = exportOp.getWorkgroupLocalMemoryAttr();
      if (localMemorySizeAttr) {
        funcOp.setReflectionAttr("local_memory", localMemorySizeAttr);
      }
    }

    // Serialize the VM module to bytes and embed it directly.
    SmallVector<char> moduleData;
    {
      IREE::VM::BytecodeTargetOptions bytecodeOptions;
      llvm::raw_svector_ostream stream(moduleData);
      if (failed(translateModuleToBytecode(variantOp.getInnerModule(),
                                           bytecodeOptions, stream))) {
        return variantOp.emitOpError()
               << "failed to serialize VM bytecode module";
      }
    }
    if (!options.dumpBinariesPath.empty()) {
      dumpDataToPath<char>(options.dumpBinariesPath, options.dumpBaseName,
                           variantOp.getName(), ".vmfb", moduleData);
    }

    auto bufferAttr = DenseIntElementsAttr::get(
        VectorType::get({static_cast<int64_t>(moduleData.size())},
                        IntegerType::get(executableBuilder.getContext(), 8)),
        std::move(moduleData));

    // Add the binary data to the target executable.
    // NOTE: this snapshots the FlatBuffer builder data at the time it is called
    // and future changes to the target op will not be observed.
    auto binaryOp = executableBuilder.create<IREE::HAL::ExecutableBinaryOp>(
        variantOp.getLoc(), variantOp.getSymName(),
        variantOp.getTarget().getFormat(), bufferAttr);
    binaryOp.setMimeTypeAttr(
        executableBuilder.getStringAttr("application/x-flatbuffers"));

    return success();
  }

 private:
  ArrayAttr getExecutableTargets(MLIRContext *context) const {
    SmallVector<Attribute> targetAttrs;
    // This is where we would multiversion.
    targetAttrs.push_back(getExecutableTarget(context));
    return ArrayAttr::get(context, targetAttrs);
  }

  IREE::HAL::ExecutableTargetAttr getExecutableTarget(
      MLIRContext *context) const {
    return IREE::HAL::ExecutableTargetAttr::get(context, "vmvx",
                                                "vmvx-bytecode-fb");
  }
};

class VMVXInlineTargetBackend final : public TargetBackend {
 public:
  VMVXInlineTargetBackend() = default;

  std::string name() const override { return "vmvx-inline"; }

  void getDependentDialects(DialectRegistry &registry) const override {
    registry
        .insert<IREE::Codegen::IREECodegenDialect, IREE::VMVX::VMVXDialect>();
  }

  IREE::HAL::DeviceTargetAttr getDefaultDeviceTarget(
      MLIRContext *context) const override {
    Builder b(context);
    SmallVector<NamedAttribute> configItems;

    configItems.emplace_back(b.getStringAttr("executable_targets"),
                             getExecutableTargets(context));

    auto configAttr = b.getDictionaryAttr(configItems);
    return IREE::HAL::DeviceTargetAttr::get(
        context, b.getStringAttr(deviceID()), configAttr);
  }

  void buildTranslationPassPipeline(OpPassManager &passManager) override {
    IREE::VMVX::buildVMVXTransformPassPipeline(passManager);
  }

 private:
  ArrayAttr getExecutableTargets(MLIRContext *context) const {
    SmallVector<Attribute> targetAttrs;
    // This is where we would multiversion.
    targetAttrs.push_back(getExecutableTarget(context));
    return ArrayAttr::get(context, targetAttrs);
  }

  IREE::HAL::ExecutableTargetAttr getExecutableTarget(
      MLIRContext *context) const {
    return IREE::HAL::ExecutableTargetAttr::get(context, "vmvx-inline",
                                                "vmvx-ir");
  }
};

void registerVMVXTargetBackends() {
  // #hal.device.target<"vmvx", ...
  // #hal.executable.target<"vmvx", ...
  static TargetBackendRegistration registration0(
      "vmvx", [=]() { return std::make_shared<VMVXTargetBackend>(); });
  static TargetBackendRegistration registration1("vmvx-inline", [=]() {
    return std::make_shared<VMVXInlineTargetBackend>();
  });
}

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
