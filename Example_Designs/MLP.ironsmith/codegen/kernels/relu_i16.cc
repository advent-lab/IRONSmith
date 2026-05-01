//===- relu_i16.cc ------------------------------------------------*- C++ -*-===//
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
#include <stdio.h>
#include <stdlib.h>

#include <aie_api/aie.hpp>

template <typename T, int N>
static inline void relu_vectorized(T *__restrict a_in, T *__restrict c_out) {
  constexpr int v_factor = 256 / (sizeof(T) * 8);
  using vec_t = aie::vector<T, v_factor>;
  const vec_t zeroes = aie::zeros<T, v_factor>();

  event0();
  for (int i = 0; i < N; i += v_factor)
    chess_prepare_for_pipelining chess_loop_range(4, ) {
      vec_t v = aie::load_v<v_factor>(a_in + i);
      aie::store_v(c_out + i, aie::max(v, zeroes));
    }
  event1();
}

extern "C" {

void relu_i16(int16_t *restrict a_in, int16_t *restrict c_out) {
  relu_vectorized<int16_t, 256>(a_in, c_out);
}

} // extern "C"
