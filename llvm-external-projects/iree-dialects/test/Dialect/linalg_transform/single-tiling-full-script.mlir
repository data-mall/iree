// RUN: iree-dialects-opt --transform-dialect-interpreter %s | FileCheck %s

// CHECK-LABEL: func @matmul_tensors
// CHECK-NOT: linalg
// CHECK: llvm
func.func @matmul_tensors(
  %arg0: tensor<128x128xf32>, %arg1: tensor<128x128xf32>, %arg2: tensor<128x128xf32> { linalg.inplaceable = true})
    -> tensor<128x128xf32> {
  %0 = linalg.matmul  ins(%arg0, %arg1: tensor<128x128xf32>, tensor<128x128xf32>)
                     outs(%arg2: tensor<128x128xf32>)
    -> tensor<128x128xf32>

  return %0 : tensor<128x128xf32>
}

transform.structured.canonicalized_sequence failures(propagate) {
^bb1(%module_op: !pdl.operation):
  %0 = transform.structured.match ops{["linalg.matmul"]} in %module_op
  %1, %loops:3 = transform.structured.tile %0 [4, 4, 4]
  %2 = get_closest_isolated_parent %1
  transform.structured.vectorize %2 {vectorize_padding = true}
  bufferize
  lower_vectors { multireduction_lowering = "innerreduce"}
  lower_to_llvm
}
