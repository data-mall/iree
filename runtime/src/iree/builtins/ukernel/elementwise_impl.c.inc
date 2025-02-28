// Copyright 2022 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "common.h"

// TODO: We should only be including/using this in standalone builds. In others,
// we have to emulate or use other mechanisms. Since this file only contains
// fallback implementations, we don't care about the quality *that* much but
// would still like to avoid the libc dep for compatibility with the bitcode
// path.
#include <math.h>

//===----------------------------------------------------------------------===//
// Helpers for defining generic implementations of elementwise functions.
// Since it affords the best code size tradeoff options, the entrypoint
// is dispatched based on an opcode.
//===----------------------------------------------------------------------===//

// Opcodes for generic functions operating on 32-bit operands and result.
// Since the outer dispatcher only differentiates based on width, all other
// type specificity is carried by the opcode.
// Binary opcodes are named "X32B" and unary opcodes "X32U".
// The initial list was sorted, and it is encouraged to sort extensions, but
// each opcode must be numerically stable, so the list is not expected to
// be sorted over time.
typedef enum {
  IREE_UKERNEL_X32B_ADDF = 0,
  IREE_UKERNEL_X32B_ADDI = 1,
  IREE_UKERNEL_X32B_ANDI = 2,
  IREE_UKERNEL_X32B_DIVF = 3,
  IREE_UKERNEL_X32B_DIVSI = 4,
  IREE_UKERNEL_X32B_DIVUI = 5,
  IREE_UKERNEL_X32B_MULF = 6,
  IREE_UKERNEL_X32B_MULI = 7,
  IREE_UKERNEL_X32B_ORI = 8,
  IREE_UKERNEL_X32B_SHLI = 9,
  IREE_UKERNEL_X32B_SHRSI = 10,
  IREE_UKERNEL_X32B_SHRUI = 11,
  IREE_UKERNEL_X32B_SUBF = 12,
  IREE_UKERNEL_X32B_SUBI = 13,
  IREE_UKENREL_X32B_XORI = 14,
} iree_ukernel_x32b_opcode_t;

typedef enum {
  IREE_UKERNEL_X32U_ABSF,
  IREE_UKERNEL_X32U_CEILF,
  IREE_UKERNEL_X32U_CTLZ,
  IREE_UKERNEL_X32U_EXPF,
  IREE_UKERNEL_X32U_FLOORF,
  IREE_UKERNEL_X32U_LOGF,
  IREE_UKERNEL_X32U_NEGF,
  IREE_UKERNEL_X32U_RSQRTF,
} iree_ukernel_x32u_opcode_t;

// Macros to access various typed, dereferenced pointers.
#define ASF32(ptr) *((float*)ptr)
#define ASUI32(ptr) *((uint32_t*)ptr)
#define ASSI32(ptr) *((int32_t*)ptr)

//===----------------------------------------------------------------------===//
// Math helper functions (extracted from base/internal/math.h and adapted
// to be able to be used standalone).
//===----------------------------------------------------------------------===//

// Clang on Windows has __builtin_clzll; otherwise we need to use the
// windows intrinsic functions.
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward)
#endif  // IREE_COMPILER_MSVC

static inline int iree_ukernel_count_leading_zeros_u32(const uint32_t n) {
#if defined(_MSC_VER)
  unsigned long result = 0;  // NOLINT(runtime/int)
  if (_BitScanReverse(&result, n)) {
    return (int)(31 - result);
  }
  return 32;
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__LCZNT__)
  // NOTE: LZCNT is a risky instruction; it is not supported on architectures
  // before Haswell, yet it is encoded as 'rep bsr', which typically ignores
  // invalid rep prefixes, and interprets it as the 'bsr' instruction, which
  // returns the index of the value rather than the count, resulting in
  // incorrect code.
  return (int)__lzcnt32(n);
#endif  // defined(__LCZNT__)

  // Handle 0 as a special case because __builtin_clz(0) is undefined.
  if (n == 0) return 32;
  // Use __builtin_clz, which uses the following instructions:
  //  x86: bsr
  //  ARM64: clz
  //  PPC: cntlzd
  return (int)__builtin_clz(n);
#else
#error No clz for this arch.
#endif  // MSVC / GCC / CLANG
}

//===----------------------------------------------------------------------===//
// Implementation macros.
//===----------------------------------------------------------------------===//

// Defines a generic "dispatched" implementation via opcode_t by invoking
// the function iree_ukernel_generic_{category}_2d.
// Corresponds to the header macro DECLARE_UKERNEL_BINARY_2D.
#define DISPATCH_UKERNEL_BINARY_2D(opcode, opcode_t, dtype, category)         \
  IREE_UKERNEL_EXPORT int iree_ukernel_##category##_##opcode##_2d(            \
      const dtype* lhs, iree_ukernel_size_t lhs_offset,                       \
      iree_ukernel_size_t lhs_stride0, iree_ukernel_size_t lhs_stride1,       \
      const dtype* rhs, iree_ukernel_size_t rhs_offset,                       \
      iree_ukernel_size_t rhs_stride0, iree_ukernel_size_t rhs_stride1,       \
      dtype* IREE_RESTRICT out, iree_ukernel_size_t out_offset,               \
      iree_ukernel_size_t out_stride0, iree_ukernel_size_t out_stride1,       \
      iree_ukernel_size_t size0, iree_ukernel_size_t size1) {                 \
    return iree_ukernel_generic_##category##_2d(                              \
        opcode_t, lhs, lhs_offset, lhs_stride0, lhs_stride1, rhs, rhs_offset, \
        rhs_stride0, rhs_stride1, out, out_offset, out_stride0, out_stride1,  \
        size0, size1);                                                        \
  }

// Defines a generic "dispatched" implementation via opcode_t by invoking
// the function iree_ukernel_generic_{category}_2d.
// Corresponds to the header macro DECLARE_UKERNEL_BINARY_2D.
#define DISPATCH_UKERNEL_UNARY_2D(opcode, opcode_t, dtype, category)      \
  IREE_UKERNEL_EXPORT int iree_ukernel_##category##_##opcode##_2d(        \
      const dtype* in, iree_ukernel_size_t in_offset,                     \
      iree_ukernel_size_t in_stride0, iree_ukernel_size_t in_stride1,     \
      dtype* IREE_RESTRICT out, iree_ukernel_size_t out_offset,           \
      iree_ukernel_size_t out_stride0, iree_ukernel_size_t out_stride1,   \
      iree_ukernel_size_t size0, iree_ukernel_size_t size1) {             \
    return iree_ukernel_generic_##category##_2d(                          \
        opcode_t, in, in_offset, in_stride0, in_stride1, out, out_offset, \
        out_stride0, out_stride1, size0, size1);                          \
  }

//===----------------------------------------------------------------------===//
// Internal helpers.
//===----------------------------------------------------------------------===//

// Computes a single element of an x32b opcode. On error, should set
// |*result_code| to a non-zero value (but should not touch it otherwise).
static void iree_ukernel_generic_x32b_op(iree_ukernel_x32b_opcode_t opcode,
                                         int* result_code, const uint32_t* lhs,
                                         const uint32_t* rhs, uint32_t* out) {
  switch (opcode) {
    case IREE_UKERNEL_X32B_ADDF:
      ASF32(out) = ASF32(lhs) + ASF32(rhs);
      return;
    case IREE_UKERNEL_X32B_ADDI:
      ASUI32(out) = ASUI32(lhs) + ASUI32(rhs);
      return;
    case IREE_UKERNEL_X32B_ANDI:
      ASUI32(out) = ASUI32(lhs) & ASUI32(rhs);
      return;
    case IREE_UKERNEL_X32B_DIVF:
      ASF32(out) = ASF32(lhs) / ASF32(rhs);
      return;
    case IREE_UKERNEL_X32B_DIVSI:
      ASSI32(out) = ASSI32(lhs) / ASSI32(rhs);
      return;
    case IREE_UKERNEL_X32B_DIVUI:
      ASUI32(out) = ASUI32(lhs) / ASUI32(rhs);
      return;
    case IREE_UKERNEL_X32B_MULF:
      ASF32(out) = ASF32(lhs) * ASF32(rhs);
      return;
    case IREE_UKERNEL_X32B_MULI:
      ASUI32(out) = ASUI32(lhs) * ASUI32(rhs);
      return;
    case IREE_UKERNEL_X32B_ORI:
      ASUI32(out) = ASUI32(lhs) | ASUI32(rhs);
      return;
    case IREE_UKERNEL_X32B_SHLI:
      ASUI32(out) = ASUI32(lhs) << ASUI32(rhs);
      return;
    case IREE_UKERNEL_X32B_SHRSI:
      ASSI32(out) = ASSI32(lhs) >> ASSI32(rhs);
      return;
    case IREE_UKERNEL_X32B_SHRUI:
      ASUI32(out) = ASUI32(lhs) >> ASUI32(rhs);
      return;
    case IREE_UKENREL_X32B_XORI:
      ASUI32(out) = ASUI32(lhs) ^ ASUI32(rhs);
      return;
    case IREE_UKERNEL_X32B_SUBF:
      ASF32(out) = ASF32(lhs) - ASF32(rhs);
      return;
    case IREE_UKERNEL_X32B_SUBI:
      ASSI32(out) = ASUI32(lhs) - ASUI32(rhs);
      return;
    default:
      *result_code = 1;
  }
}

// Computes a single element of an x32u opcode. On error, should set
// |*result_code| to a non-zero value (but should not touch it otherwise).
static void iree_ukernel_generic_x32u_op(iree_ukernel_x32u_opcode_t opcode,
                                         int* result_code, const uint32_t* in,
                                         uint32_t* out) {
  switch (opcode) {
    case IREE_UKERNEL_X32U_ABSF:
      ASF32(out) = fabsf(ASF32(in));
      return;
    case IREE_UKERNEL_X32U_CEILF:
      ASF32(out) = ceilf(ASF32(in));
      return;
    case IREE_UKERNEL_X32U_CTLZ:
      ASUI32(out) = iree_ukernel_count_leading_zeros_u32(ASUI32(in));
      return;
    case IREE_UKERNEL_X32U_EXPF:
      ASF32(out) = expf(ASF32(in));
      return;
    case IREE_UKERNEL_X32U_FLOORF:
      ASF32(out) = floorf(ASF32(in));
      return;
    case IREE_UKERNEL_X32U_LOGF:
      ASF32(out) = logf(ASF32(in));
      return;
    case IREE_UKERNEL_X32U_NEGF:
      ASF32(out) = -ASF32(in);
      return;
    case IREE_UKERNEL_X32U_RSQRTF:
      ASF32(out) = 1.0f / sqrtf(ASF32(in));
      return;
    default:
      *result_code = 1;
  }
}

//===----------------------------------------------------------------------===//
// Opcode dispatch entry points.
//===----------------------------------------------------------------------===//

// Generic 32bit binary kernels.
static int iree_ukernel_generic_x32b_2d(
    iree_ukernel_x32b_opcode_t opcode,
    // LHS.
    const uint32_t* lhs, iree_ukernel_size_t lhs_offset,
    iree_ukernel_size_t lhs_stride0, iree_ukernel_size_t lhs_stride1,
    // RHS
    const uint32_t* rhs, iree_ukernel_size_t rhs_offset,
    iree_ukernel_size_t rhs_stride0, iree_ukernel_size_t rhs_stride1,
    // OUT.
    uint32_t* IREE_RESTRICT out, iree_ukernel_size_t out_offset,
    iree_ukernel_size_t out_stride0, iree_ukernel_size_t out_stride1,
    // Sizes.
    iree_ukernel_size_t size0, iree_ukernel_size_t size1) {
  int result_code = 0;
  // TODO: Manually unroll to x4 to trigger vectorization.
  for (iree_ukernel_size_t i = 0; i < size0; ++i) {
    for (iree_ukernel_size_t j = 0; j < size1; ++j) {
      iree_ukernel_generic_x32b_op(opcode, &result_code,
                                   &lhs[i * lhs_stride0 + j * lhs_stride1],
                                   &rhs[i * rhs_stride0 + j * rhs_stride1],
                                   &out[i * out_stride0 + j * out_stride1]);
    }
  }
  return result_code;
}

// Generic 32bit unary kernels.
static int iree_ukernel_generic_x32u_2d(
    iree_ukernel_x32u_opcode_t opcode,
    // IN.
    const uint32_t* in, iree_ukernel_size_t in_offset,
    iree_ukernel_size_t in_stride0, iree_ukernel_size_t in_stride1,
    // OUT.
    uint32_t* IREE_RESTRICT out, iree_ukernel_size_t out_offset,
    iree_ukernel_size_t out_stride0, iree_ukernel_size_t out_stride1,
    // Sizes.
    iree_ukernel_size_t size0, iree_ukernel_size_t size1) {
  int result_code = 0;
  // TODO: Manually unroll to x4 to trigger vectorization.
  for (iree_ukernel_size_t i = 0; i < size0; ++i) {
    for (iree_ukernel_size_t j = 0; j < size1; ++j) {
      iree_ukernel_generic_x32u_op(opcode, &result_code,
                                   &in[i * in_stride0 + j * in_stride1],
                                   &out[i * out_stride0 + j * out_stride1]);
    }
  }
  return result_code;
}
