// RUN: iree-opt -split-input-file -iree-spirv-tile %s | FileCheck %s

func.func @innermost_reduction() {
  %c1 = arith.constant 1 : index
  %c128 = arith.constant 128 : index
  %cst = arith.constant -0.000000e+00 : f32
  %0 = hal.interface.constant.load[0] : i32
  %1 = hal.interface.constant.load[1] : i32
  %2 = hal.interface.constant.load[2] : i32
  %3 = arith.index_cast %0 {stream.alignment = 512 : index, stream.values = [0 : index, 394752 : index, 984064 : index]} : i32 to index
  %4 = arith.index_cast %1 {stream.alignment = 512 : index, stream.values = [0 : index, 196608 : index, 197120 : index]} : i32 to index
  %5 = arith.index_cast %2 {stream.alignment = 512 : index, stream.values = [512 : index, 197120 : index, 197632 : index]} : i32 to index
  %6 = hal.interface.binding.subspan set(0) binding(0) type(storage_buffer) offset(%3) alignment(64) : !flow.dispatch.tensor<readonly:128x384xf32>
  %7 = hal.interface.binding.subspan set(0) binding(0) type(storage_buffer) offset(%4) alignment(64) : !flow.dispatch.tensor<readonly:128xf32>
  %8 = hal.interface.binding.subspan set(0) binding(1) type(storage_buffer) offset(%5) alignment(64) : !flow.dispatch.tensor<writeonly:128xf32>
  %workgroup_id_x = hal.interface.workgroup.id[0] : index
  %workgroup_count_x = hal.interface.workgroup.count[0] : index
  %9 = affine.apply affine_map<()[s0] -> (s0 * 128)>()[%workgroup_id_x]
  %10 = affine.apply affine_map<()[s0] -> (s0 * 128)>()[%workgroup_count_x]
  scf.for %arg0 = %9 to %c128 step %10 {
    %11 = flow.dispatch.tensor.load %6, offsets = [%arg0, 0], sizes = [128, 384], strides = [1, 1] : !flow.dispatch.tensor<readonly:128x384xf32> -> tensor<128x384xf32>
    %12 = flow.dispatch.tensor.load %7, offsets = [%arg0], sizes = [128], strides = [1] : !flow.dispatch.tensor<readonly:128xf32> -> tensor<128xf32>
    %13 = linalg.init_tensor [128] : tensor<128xf32>
    %14 = linalg.fill ins(%cst : f32) outs(%13 : tensor<128xf32>) -> tensor<128xf32>
    %15 = linalg.generic {
      indexing_maps = [affine_map<(d0, d1) -> (d0, d1)>, affine_map<(d0, d1) -> (d0)>, affine_map<(d0, d1) -> (d0)>],
      iterator_types = ["parallel", "reduction"]
    } ins(%11, %12 : tensor<128x384xf32>, tensor<128xf32>) outs(%14 : tensor<128xf32>)
    attrs = {lowering_config = #iree_codegen.lowering_config<tile_sizes = [[128], [4], [0, 4]]>} {
    ^bb0(%arg1: f32, %arg2: f32, %arg3: f32):
      %16 = arith.subf %arg1, %arg2 : f32
      %17 = arith.mulf %16, %16 : f32
      %18 = arith.addf %17, %arg3 : f32
      linalg.yield %18 : f32
    } -> tensor<128xf32>
    flow.dispatch.tensor.store %15, %8, offsets = [%arg0], sizes = [128], strides = [%c1] : tensor<128xf32> -> !flow.dispatch.tensor<writeonly:128xf32>
  }
  return
}

// CHECK-LABEL: func @innermost_reduction()

//  CHECK-DAG:  %[[C0:.+]] = arith.constant 0 : index
//  CHECK-DAG:  %[[C4:.+]] = arith.constant 4 : index
//  CHECK-DAG:  %[[C128:.+]] = arith.constant 128 : index
//  CHECK-DAG:  %[[C384:.+]] = arith.constant 384 : index

//      CHECK: scf.for
//      CHECK:   scf.for %{{.+}} = %[[C0]] to %[[C128]] step %[[C4]]
//      CHECK:     linalg.fill
//      CHECK:     scf.for %{{.+}} = %[[C0]] to %[[C384]] step %[[C4]]
//      CHECK:       linalg.generic
// CHECK-SAME:         ins(%{{.+}}, %{{.+}} : tensor<4x4xf32>, tensor<4xf32>)
// CHECK-SAME:         outs(%{{.+}}g4 : tensor<4xf32>)
