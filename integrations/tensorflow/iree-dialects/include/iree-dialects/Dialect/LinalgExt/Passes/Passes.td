// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_DIALECT_LINALGEXT_PASSES
#define IREE_DIALECT_LINALGEXT_PASSES

include "mlir/Pass/PassBase.td"

def LinalgExtToLoops :
    Pass<"iree-linalg-ext-to-loops", "func::FuncOp"> {
  let summary = "Convert LinalgExt ops to loops and Linalg ops.";
  let constructor = "mlir::iree_compiler::IREE::LinalgExt::createLinalgExtToLoopsPass()";
}

def TilingInterfaceTiling :
    Pass<"iree-linalg-ext-tile", "func::FuncOp"> {
  let summary = "Test pass for tiling using TiledOpInterface";
  let constructor = "mlir::iree_compiler::IREE::LinalgExt::createTilingInterfaceTilingPass()";
}

def PadContractionToBlockSize :
    Pass<"iree-linalg-pad-contraction-to-block-size", ""> {
  let summary = "Pads contraction (matmul) ops to next multiple of block size";
  let description = [{
    This pass will apply padding to any supported linalg contractions:
      * Row-major matmul:
          Padded to <rowAlignment x columnAlignment>

    Both rowAlignment and columnAlignment must be power-of-two values. If an
    op is already statically padded properly, no change will be made. However,
    if dynamic dimensions exist, padding will be applied regardless. Because
    of the dynamic case, applying this pass multiple times can result in
    mutation on each run.
  }];
  let constructor = "mlir::iree_compiler::IREE::LinalgExt::createPadContractionToBlockSizePass()";
  let options = [
    Option<"rowAlignment", "rowAlignment", "int", /*default=*/"16",
           "The row-wise output block size">,
    Option<"columnAlignment", "columnAlignment", "int", /*default=*/"16",
           "The column-wise output block size">,
  ];
}

def TopkSplitReduction:
    Pass<"iree-linalg-ext-topk-split-reduction", "func::FuncOp"> {
  let summary = "Topk split reduction pass.";
  let description = [{
    Produces a "map-reduce" style of parallelizing a Topk Op. The op is split
    into two, on containing reducitons in parallel and the other contianing the
    combination of the parallel reductions into a final result.
  }];
  let constructor = "mlir::iree_compiler::IREE::LinalgExt::createTopkSplitReductionPass()";
  let options = [
    ListOption<"splitRatios", "split-ratios", "int",
           "List of split reduction ratios">,
  ];
}

#endif  // IREE_DIALECT_LINALGEXT_PASSES
