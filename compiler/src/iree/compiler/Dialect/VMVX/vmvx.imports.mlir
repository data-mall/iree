// VMVX (Virtual Machine-based Vector eXtensions) runtime module imports.
//
// This is embedded in the compiler binary and inserted into any module
// containing VMVX dialect ops (vmvx.*) that is lowered to the VM dialect.
//
// Element types are embedded in the function. The convention used (mostly)
// follows MLIR type names:
// * 'x' : don't-care, bit-depth only.
// * 'i' : signless integer (+ bit depth)   ex: i1 i8 i16 i32 i64
// * 'si': signed integer (+ bit depth)     ex: si32 ...
// * 'ui': unsigned integer (+ bit depth)   ex: ui32 ...
// * 'f' : IREE float (+ bit depth)         ex: f32 f64
//
// See the README.md for more more details on the implementation.
//
// NOTE: each method added here requires a corresponding method in
// `iree/modules/vmvx/exports.inl` and `iree/modules/vmvx/module.c`.
//
// NOTE: there's a maintenance burden to adding new ops as they may have to be
// carried around forever. Always try to convert to the ops that exist unless
// it's performance critical - a few lines of a conversion pattern saves future
// us a lot of pain and breaking changes.
//
// NOTE: experimental functions that are not yet ready to be parts of the core
// module must be prefixed with `ex.` like `vmvx.ex.my_test_op`.
vm.module @vmvx {

//===----------------------------------------------------------------------===//
// VMVX Binary Elementwise Kernels
// Each is specialized by opcode, rank and type width.
//===----------------------------------------------------------------------===//

vm.import @add.2d.f32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @add.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @and.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @div.2d.f32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @divs.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @divu.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @mul.2d.f32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @mul.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @or.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @shl.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @shrs.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @shru.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @sub.2d.f32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @sub.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

vm.import @xor.2d.i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_strides : tuple<i64, i64>,

  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_strides : tuple<i64, i64>,

  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,

  %sizes : tuple<i64, i64>
)

//===----------------------------------------------------------------------===//
// VMVX Unary Elementwise Kernels
// Each is specialized by opcode, rank and type width.
//===----------------------------------------------------------------------===//

vm.import @abs.2d.f32(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @ceil.2d.f32(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @ctlz.2d.i32(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @exp.2d.f32(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @floor.2d.f32(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @log.2d.f32(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @neg.2d.f32(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @rsqrt.2d.f32(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

//==============================================================================
// Strided copy ops
// Variants of copy ops exist for power of two rank and datatype sizes.
// Current max rank is 2d.
//==============================================================================
vm.import @copy.2d.x8(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @copy.2d.x16(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @copy.2d.x32(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

vm.import @copy.2d.x64(
  %in_buffer : !vm.buffer,
  %in_offset : i64,
  %in_strides : tuple<i64, i64>,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_strides : tuple<i64, i64>,
  %sizes : tuple<i64, i64>
)

//==============================================================================
// Strided fill ops
//==============================================================================

vm.import @fill.2d.x32(
  %fill_value : i32,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_row_stride : i64,
  %size_m : i64,
  %size_n : i64
)

//==============================================================================
// matmul ops
//==============================================================================

vm.import @matmul.f32f32f32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_row_stride : i64,
  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_row_stride : i64,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_row_stride : i64,
  %m : i64,
  %n : i64,
  %k : i64,
  %flags : i32
)

vm.import @matmul.i8i8i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_row_stride : i64,
  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_row_stride : i64,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_row_stride : i64,
  %m : i64,
  %n : i64,
  %k : i64,
  %flags : i32
)

//==============================================================================
// mmt4d ops
//==============================================================================

vm.import @mmt4d.f32f32f32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_row_stride : i64,
  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_row_stride : i64,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_row_stride : i64,
  %m : i64,
  %n : i64,
  %k : i64,
  %m0 : i32,
  %n0 : i32,
  %k0 : i32,
  %flags : i32
)

vm.import @mmt4d.i8i8i32(
  %lhs_buffer : !vm.buffer,
  %lhs_offset : i64,
  %lhs_row_stride : i64,
  %rhs_buffer : !vm.buffer,
  %rhs_offset : i64,
  %rhs_row_stride : i64,
  %out_buffer : !vm.buffer,
  %out_offset : i64,
  %out_row_stride : i64,
  %m : i64,
  %n : i64,
  %k : i64,
  %m0 : i32,
  %n0 : i32,
  %k0 : i32,
  %flags : i32
)

}  // module
