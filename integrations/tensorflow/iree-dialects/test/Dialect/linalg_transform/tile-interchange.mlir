// RUN: iree-dialects-opt --transform-dialect-interpreter --split-input-file %s | FileCheck %s

#map0 = affine_map<(d0, d1, d2) -> (d0, d2)>
#map1 = affine_map<(d0, d1, d2) -> (d2, d1)>
#map2 = affine_map<(d0, d1, d2) -> (d0, d1)>

// Check that vectorization applies after interchange+tiling.

// CHECK-LABEL: @matmul_021
// CHECK-NOT: linalg.generic
// CHECK: vector.contract
func.func public @matmul_021(%arg0: tensor<39x154xf32> {linalg.buffer_layout = affine_map<(d0, d1) -> (d0, d1)>, linalg.inplaceable = false}, %arg1: tensor<154x5xf32> {linalg.buffer_layout = affine_map<(d0, d1) -> (d0, d1)>, linalg.inplaceable = false}, %arg2: tensor<39x5xf32> {linalg.buffer_layout = affine_map<(d0, d1) -> (d0, d1)>, linalg.inplaceable = true}) -> tensor<39x5xf32> attributes {passthrough = ["noinline", ["target-cpu", "skylake-avx512"], ["prefer-vector-width", "512"]]} {
  %0 = linalg.generic {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} ins(%arg0, %arg1 : tensor<39x154xf32>, tensor<154x5xf32>) outs(%arg2 : tensor<39x5xf32>) {
  ^bb0(%arg3: f32, %arg4: f32, %arg5: f32):
    %1 = arith.mulf %arg3, %arg4 : f32
    %2 = arith.addf %arg5, %1 : f32
    linalg.yield %2 : f32
  } -> tensor<39x5xf32>
  return %0 : tensor<39x5xf32>
}

transform.with_pdl_patterns {
^bb0(%arg0: !pdl.operation):
  pdl.pattern @target_pattern : benefit(1) {
    %0 = operands
    %1 = types
    %2 = operation "linalg.generic"(%0 : !pdl.range<value>)  -> (%1 : !pdl.range<type>)
    %3 = pdl.attribute = @matmul_021
    apply_native_constraint "nestedInFunc"(%2, %3 : !pdl.operation, !pdl.attribute)
    rewrite %2 with "transform.dialect"
  }

  transform.structured.canonicalized_sequence %arg0 {
  ^bb1(%arg1: !pdl.operation):
    %0 = pdl_match @target_pattern in %arg1
    %1, %loops1:3 = transform.structured.tile %0 [3, 5, 14] {interchange = [0, 2, 1]}
    %2, %loops2:3 = transform.structured.tile %1 [3, 5, 2]
    %3 = get_closest_isolated_parent %2
    transform.structured.vectorize %3 {vectorize_padding = true}
  }
}

// -----

#map0 = affine_map<(d0, d1, d2) -> (d0, d2)>
#map1 = affine_map<(d0, d1, d2) -> (d2, d1)>
#map2 = affine_map<(d0, d1, d2) -> (d0, d1)>

// Check that vectorization applies after interchange+tiling.

// CHECK-LABEL: @matmul_210
// CHECK-NOT: linalg.generic
// CHECK: vector.contract
func.func public @matmul_210(%arg0: tensor<39x154xf32> {linalg.buffer_layout = affine_map<(d0, d1) -> (d0, d1)>, linalg.inplaceable = false}, %arg1: tensor<154x5xf32> {linalg.buffer_layout = affine_map<(d0, d1) -> (d0, d1)>, linalg.inplaceable = false}, %arg2: tensor<39x5xf32> {linalg.buffer_layout = affine_map<(d0, d1) -> (d0, d1)>, linalg.inplaceable = true}) -> tensor<39x5xf32> attributes {passthrough = ["noinline", ["target-cpu", "skylake-avx512"], ["prefer-vector-width", "512"]]} {
  %0 = linalg.generic {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} ins(%arg0, %arg1 : tensor<39x154xf32>, tensor<154x5xf32>) outs(%arg2 : tensor<39x5xf32>) {
  ^bb0(%arg3: f32, %arg4: f32, %arg5: f32):
    %1 = arith.mulf %arg3, %arg4 : f32
    %2 = arith.addf %arg5, %1 : f32
    linalg.yield %2 : f32
  } -> tensor<39x5xf32>
  return %0 : tensor<39x5xf32>
}

transform.with_pdl_patterns {
^bb0(%arg0: !pdl.operation):
  pdl.pattern @target_pattern : benefit(1) {
    %0 = operands
    %1 = types
    %2 = operation "linalg.generic"(%0 : !pdl.range<value>)  -> (%1 : !pdl.range<type>)
    %3 = pdl.attribute = @matmul_210
    apply_native_constraint "nestedInFunc"(%2, %3 : !pdl.operation, !pdl.attribute)
    rewrite %2 with "transform.dialect"
  }

  transform.structured.canonicalized_sequence %arg0 {
  ^bb1(%arg1: !pdl.operation):
    %0 = pdl_match @target_pattern in %arg1
    %1, %loops1:3 = transform.structured.tile %0 [3, 5, 14] {interchange = [2, 1, 0]}
    %2, %loops2:3 = transform.structured.tile %1 [3, 5, 2]
    %3 = get_closest_isolated_parent %2
    transform.structured.vectorize %3 {vectorize_padding = true}
  }
}
