// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

// softmax_i16 — in-place INT16 softmax over DIM_N elements.
//
// Computes softmax via integer approximation:
//   1. Find max value in the tile for numerical stability.
//   2. Compute shifted exp approximation using int32 accumulation.
//   3. Normalize back to int16 range.
//
// DIM_N defaults to 64 to match the per-tile output buffer size.

#ifndef DIM_N
#define DIM_N 64
#endif

#include <aie_api/aie.hpp>
#include <aie_api/aie_adf.hpp>
#include <stdint.h>

void softmax_i16(int16_t* __restrict buf) {
    // Step 1: find maximum.
    int16_t max_val = buf[0];
    for (int i = 1; i < DIM_N; ++i)
        if (buf[i] > max_val) max_val = buf[i];

    // Step 2: compute shifted exponentials (integer approximation) and sum.
    int32_t exp_vals[DIM_N];
    int32_t sum = 0;
    for (int i = 0; i < DIM_N; ++i) {
        // Approximate exp(x - max) as (1 << (x - max)) clamped to avoid overflow.
        int32_t shifted = (int32_t)(buf[i]) - (int32_t)max_val;
        // Clamp to [-15, 0] so shift stays in [0, 15] for int16 output range.
        if (shifted < -15) shifted = -15;
        exp_vals[i] = (int32_t)(1 << (-shifted < 15 ? -shifted : 15));
        sum += exp_vals[i];
    }

    // Step 3: normalize to [0, 32767] int16 range.
    if (sum == 0) sum = 1;
    for (int i = 0; i < DIM_N; ++i) {
        int32_t norm = (exp_vals[i] * (int32_t)32767) / sum;
        buf[i] = (int16_t)(norm > 32767 ? 32767 : norm);
    }
}
