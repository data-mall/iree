// Copyright 2022 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_CODEGEN_LLVMCPU_TARGETMLTRANSFORMINFO_H_
#define IREE_COMPILER_CODEGEN_LLVMCPU_TARGETMLTRANSFORMINFO_H_

#include <limits>

#include "iree/compiler/Dialect/HAL/IR/HALOps.h"

namespace mlir {
namespace iree_compiler {

/// Holds target specific information to specialize ML transformations.
// TODO(dcaballe): Move to a Concept-Model implementation when it's worth it.
struct TargetMLTransformInfo {
  unsigned defaultMaxReductionUnrollFactor = 8;
  unsigned defaultMaxTransposeUnrollFactor =
      std::numeric_limits<unsigned>::max();

  static const TargetMLTransformInfo getTargetMLTransformInfo(
      IREE::HAL::ExecutableVariantOp variantOp);
};

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_CODEGEN_LLVMCPU_TARGETMLTRANSFORMINFO_H_
