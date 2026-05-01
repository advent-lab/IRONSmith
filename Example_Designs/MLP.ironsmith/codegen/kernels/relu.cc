//===- relu.cc --------------------------------------------000---*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2025, Advanced Micro Devices, Inc.
//
//===----------------------------------------------------------------------===//

#define NOCPP

#include <stdint.h>

#include "../aie_kernel_utils.h"
#include <aie_api/aie.hpp>

#ifndef DIM_M
#define DIM_M 64
#endif

#ifndef DIM_N
#define DIM_N 16
#endif

// In-place ReLU: clamp every element of a DIM_M × DIM_N int16 tile to [0, +∞).
// Operates on the same (M, n_tile) activation tile produced by matmul_i16_i16.
//
// AIE2 native SIMD width for int16 compute (aie::max) is 512 bits = 32 lanes.
// Using 32-element vectors matches the bf16_relu pattern in aie_kernels/aie2/relu.cc.
static void relu_i16_impl(int16_t *__restrict c_out) {
  constexpr int N        = DIM_M * DIM_N; // 64 × 16 = 1024 elements
  constexpr int v_factor = 32;            // 32 × int16 = 512-bit native compute width
  static_assert(N % v_factor == 0);
  const aie::vector<int16_t, v_factor> zeros = aie::zeros<int16_t, v_factor>();
  event0();
  for (int i = 0; i < N; i += v_factor) {
    aie::vector<int16_t, v_factor> val = aie::load_v<v_factor>(c_out + i);
    aie::store_v(c_out + i, aie::max(val, zeros));
  }
  event1();
}

extern "C" {

void relu_i16(int16_t *c_out) { relu_i16_impl(c_out); }

} // extern "C"
