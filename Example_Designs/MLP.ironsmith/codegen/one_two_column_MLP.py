#
# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# (c) Copyright 2025 Advanced Micro Devices, Inc. or its affiliates

# example_jit_mlp.py – 2-layer MLP: column 0 and column 2, each 4 tiles, bf16
#
# Column 0: tiles (0,2)..(0,5)  — layer 1: relu(A @ W0)
# Column 2: tiles (2,2)..(2,5)  — layer 2: relu(H @ W1)
# Layers run as two separate dispatches; inter-layer activation de-tiled to
# row-major on the host between dispatches (cross-column FIFO routing unsupported).
#
# Data path (each column):
#   input  (64x64): host → Shim(col,0) → MemTile(col,1) → split 4x(16x64) → tiles
#   weights(64x64): host → Shim(col,0) → MemTile(col,1) → broadcast to 4 tiles
#   output (64x64): each tile → (16x64) join at MemTile(col,1) → Shim(col,0) → host
#
# mm.o compiled with DIM_M=16, DIM_K=64, DIM_N=64.
# De-tile: C_raw.reshape(4,4,16,4,4).transpose(0,1,3,2,4).reshape(64,64)

import os
import numpy as np
from ml_dtypes import bfloat16

from aie.iron import Kernel, ObjectFifo, Program, Runtime, Worker
from aie.iron.placers import SequentialPlacer
from aie.iron.device import Tile
from aie.dialects import arith
from aie.ir import IntegerType, IntegerAttr
import aie.iron as iron

from aie.helpers.taplib import TensorAccessPattern


@iron.jit(is_placed=False)
def matmul_col0_jit(input_activation, weights_col0, output_activation):
    M               = 64   # rows of A and C
    K               = 64   # cols of A, rows of B
    N               = 64   # cols of B and C (total)
    n_tiles         = 4
    M_tile          = M // n_tiles  # 16 rows per tile

    micro_r, micro_s, micro_t = 4, 8, 4  # bf16 mmul<r=4,s=8,t=4>

    # A tile (16×64): row block tiled for micro-kernel, streamed per-split by MemTile
    activation_layout_dims = [
        (M_tile // micro_r,         micro_r * K),     # (4, 256)
        (K // micro_s,              micro_s),         # (8,   8)
        (micro_r,                   K),               # (4,  64)
        (micro_s,                   1),               # (8,   1)
    ]
    # B (K=64, N=64): full weight matrix tiled for micro-kernel, broadcast to all tiles
    weight_layout_dims = [
        (K // micro_s,              micro_s * N),     # (8, 512)
        (N // micro_t,              micro_t),         # (16,  4)
        (micro_s,                   N),               # (8,  64)
        (micro_t,                   1),               # (4,   1)
    ]

    # Tensor Types
    activation_flat_type  = np.ndarray[(M * K,),     np.dtype[bfloat16]]
    weight_full_flat_type = np.ndarray[(K * N,),     np.dtype[bfloat16]]
    output_full_flat_type = np.ndarray[(M * N,),     np.dtype[bfloat16]]
    activation_tile_type  = np.ndarray[(M_tile, K),  np.dtype[bfloat16]]
    weight_full_type      = np.ndarray[(K, N),       np.dtype[bfloat16]]
    output_tile_type      = np.ndarray[(M_tile, N),  np.dtype[bfloat16]]
    output_full_type      = np.ndarray[(M, N),       np.dtype[bfloat16]]

    # Data Movement
    # Object Fifos
    input_activation_fifo  = ObjectFifo(obj_type=activation_flat_type,  depth=2, name="input_activation_fifo")
    weights_col0_fifo      = ObjectFifo(obj_type=weight_full_type,       depth=1, name="weights_col0_fifo")
    output_activation_fifo = ObjectFifo(obj_type=output_full_type,       depth=2, name="output_activation_fifo")

    # Splits
    activation_tile_split_col0 = input_activation_fifo.cons().split(
        names=["act_col0_0", "act_col0_1", "act_col0_2", "act_col0_3"],
        obj_types=[activation_tile_type] * n_tiles,
        offsets=[i * M_tile * K for i in range(n_tiles)],
        dims_to_stream=[activation_layout_dims] * n_tiles,  # type: ignore[arg-type]
        placement=Tile(0, 1),
    )

    # Joins
    activation_tile_join_col0 = output_activation_fifo.prod().join(
        names=["act_join_col0_0", "act_join_col0_1", "act_join_col0_2", "act_join_col0_3"],
        obj_types=[output_tile_type] * n_tiles,
        offsets=[i * M_tile * N for i in range(n_tiles)],
        placement=Tile(0, 1),
    )

    # Broadcasts
    weights_col0_broadcast = weights_col0_fifo.cons().forward(
        name="weights_col0_broadcast",
        dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(0, 1),
    )

    # Compute Kernels
    build_dir     = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
    mm_obj        = os.path.join(build_dir, "mm.o")
    relu_obj      = os.path.join(build_dir, "relu.o")
    zero_kernel   = Kernel("zero_bf16",        mm_obj,   [output_tile_type])
    matmul_kernel = Kernel("matmul_bf16_bf16", mm_obj,   [activation_tile_type, weight_full_type, output_tile_type])
    relu_kernel   = Kernel("bf16_relu",        relu_obj, [output_tile_type, output_tile_type])

    # Core Body Functions
    def core_shared_matmul_relu(zero, matmul, relu, in0, in1, out0):
        buf_in0  = in0.acquire(1)
        buf_in1  = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        zero(buf_out0)
        matmul(buf_in0, buf_in1, buf_out0)
        relu(buf_out0, buf_out0)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    # Workers
    Workers = []
    worker_aie0_2 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, activation_tile_split_col0[0].cons(), weights_col0_broadcast.cons(), activation_tile_join_col0[0].prod()], placement=Tile(0, 2), stack_size=0x2000)
    worker_aie0_3 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, activation_tile_split_col0[1].cons(), weights_col0_broadcast.cons(), activation_tile_join_col0[1].prod()], placement=Tile(0, 3), stack_size=0x2000)
    worker_aie0_4 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, activation_tile_split_col0[2].cons(), weights_col0_broadcast.cons(), activation_tile_join_col0[2].prod()], placement=Tile(0, 4), stack_size=0x2000)
    worker_aie0_5 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, activation_tile_split_col0[3].cons(), weights_col0_broadcast.cons(), activation_tile_join_col0[3].prod()], placement=Tile(0, 5), stack_size=0x2000)
    Workers = [worker_aie0_2, worker_aie0_3, worker_aie0_4, worker_aie0_5]

    # Runtime
    tap_act = TensorAccessPattern(tensor_dims=[M * K], offset=0, sizes=[1, M * K], strides=[M * K, 1])
    tap_w   = TensorAccessPattern(tensor_dims=[K * N], offset=0, sizes=[1, K * N], strides=[K * N, 1])
    tap_out = TensorAccessPattern(tensor_dims=[M * N], offset=0, sizes=[1, M * N], strides=[M * N, 1])
    rt = Runtime()
    with rt.sequence(activation_flat_type, weight_full_flat_type, output_full_flat_type) as (input_activation_in, weights_col0_in, output_activation_out):
        # Start Workers
        rt.start(*Workers)
        # Fills
        rt.fill(input_activation_fifo.prod(),  input_activation_in, tap=tap_act, placement=Tile(0, 0))
        rt.fill(weights_col0_fifo.prod(),       weights_col0_in,     tap=tap_w,   placement=Tile(0, 0))
        # Drains
        rt.drain(output_activation_fifo.cons(), output_activation_out, wait=True, tap=tap_out, placement=Tile(0, 0))

    # Program
    my_program = Program(iron.get_current_device(), rt)

    # Placement
    return my_program.resolve_program(SequentialPlacer())


@iron.jit(is_placed=False)
def mlp_two_col_jit(input_activation, weights_col0, weights_col2, output_activation):
    M               = 64
    K               = 64
    N               = 64
    n_tiles         = 4
    M_tile          = M // n_tiles  # 16

    micro_r, micro_s, micro_t = 4, 8, 4

    # A tile (16×64) → micro-kernel input order
    activation_layout_dims = [
        (M_tile // micro_r,  micro_r * K),        # (4, 256)
        (K // micro_s,       micro_s),            # (8,   8)
        (micro_r,            K),                  # (4,  64)
        (micro_s,            1),                  # (8,   1)
    ]
    # B (64×64) → micro-kernel input order
    weight_layout_dims = [
        (K // micro_s,       micro_s * N),        # (8, 512)
        (N // micro_t,       micro_t),            # (16,  4)
        (micro_s,            N),                  # (8,  64)
        (micro_t,            1),                  # (4,   1)
    ]
    # Tensor Types
    activation_flat_type  = np.ndarray[(M * K,),     np.dtype[bfloat16]]
    weight_full_flat_type = np.ndarray[(K * N,),     np.dtype[bfloat16]]
    output_full_flat_type = np.ndarray[(M * N,),     np.dtype[bfloat16]]
    activation_tile_type  = np.ndarray[(M_tile, K),  np.dtype[bfloat16]]
    weight_full_type      = np.ndarray[(K, N),       np.dtype[bfloat16]]
    output_tile_type      = np.ndarray[(M_tile, N),  np.dtype[bfloat16]]
    output_full_type      = np.ndarray[(M, N),       np.dtype[bfloat16]]

    # Data Movement
    # Object Fifos
    input_activation_fifo  = ObjectFifo(obj_type=activation_flat_type, depth=2, name="input_activation_fifo")
    weights_col0_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col0_fifo")
    weights_col2_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col2_fifo")
    output_activation_fifo = ObjectFifo(obj_type=output_full_type,      depth=2, name="output_activation_fifo")

    # Per-tile inter-column FIFOs: col0 tile(i) → col2 tile(i)
    h_0 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h_0")
    h_1 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h_1")
    h_2 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h_2")
    h_3 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h_3")

    # Splits – A rows → col0 tiles
    activation_tile_split_col0 = input_activation_fifo.cons().split(
        names=["act_col0_0", "act_col0_1", "act_col0_2", "act_col0_3"],
        obj_types=[activation_tile_type] * n_tiles,
        offsets=[i * M_tile * K for i in range(n_tiles)],
        dims_to_stream=[activation_layout_dims] * n_tiles,  # type: ignore[arg-type]
        placement=Tile(0, 1),
    )

    # Joins – col2 outputs → MemTile(2,1) → Shim(2,0) → host
    activation_tile_join_col2 = output_activation_fifo.prod().join(
        names=["act_join_col2_0", "act_join_col2_1", "act_join_col2_2", "act_join_col2_3"],
        obj_types=[output_tile_type] * n_tiles,
        offsets=[i * M_tile * N for i in range(n_tiles)],
        placement=Tile(2, 1),
    )

    # Broadcasts – W0 → col0 tiles, W1 → col2 tiles
    weights_col0_broadcast = weights_col0_fifo.cons().forward(
        name="weights_col0_broadcast",
        dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(0, 1),
    )
    weights_col2_broadcast = weights_col2_fifo.cons().forward(
        name="weights_col2_broadcast",
        dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(2, 1),
    )

    # Compute Kernels
    build_dir     = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
    mm_obj        = os.path.join(build_dir, "mm.o")
    relu_obj      = os.path.join(build_dir, "relu.o")
    zero_kernel   = Kernel("zero_bf16",        mm_obj,   [output_tile_type])
    matmul_kernel = Kernel("matmul_bf16_bf16", mm_obj,   [activation_tile_type, weight_full_type, output_tile_type])
    relu_kernel   = Kernel("bf16_relu",        relu_obj, [output_tile_type, output_tile_type])

    # Core Body Functions
    def core_shared_matmul_relu(zero, matmul, relu, in0, in1, out0):
        buf_in0  = in0.acquire(1)
        buf_in1  = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        zero(buf_out0)
        matmul(buf_in0, buf_in1, buf_out0)
        relu(buf_out0, buf_out0)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    # Workers – Column 0
    Workers = []
    worker_aie0_2 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, activation_tile_split_col0[0].cons(), weights_col0_broadcast.cons(), h_0.prod()], placement=Tile(0, 2), stack_size=0x2000)
    worker_aie0_3 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, activation_tile_split_col0[1].cons(), weights_col0_broadcast.cons(), h_1.prod()], placement=Tile(0, 3), stack_size=0x2000)
    worker_aie0_4 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, activation_tile_split_col0[2].cons(), weights_col0_broadcast.cons(), h_2.prod()], placement=Tile(0, 4), stack_size=0x2000)
    worker_aie0_5 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, activation_tile_split_col0[3].cons(), weights_col0_broadcast.cons(), h_3.prod()], placement=Tile(0, 5), stack_size=0x2000)

    # Workers – Column 2
    worker_aie2_2 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, h_0.cons(), weights_col2_broadcast.cons(), activation_tile_join_col2[0].prod()], placement=Tile(2, 2), stack_size=0x2000)
    worker_aie2_3 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, h_1.cons(), weights_col2_broadcast.cons(), activation_tile_join_col2[1].prod()], placement=Tile(2, 3), stack_size=0x2000)
    worker_aie2_4 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, h_2.cons(), weights_col2_broadcast.cons(), activation_tile_join_col2[2].prod()], placement=Tile(2, 4), stack_size=0x2000)
    worker_aie2_5 = Worker(core_fn=core_shared_matmul_relu, fn_args=[zero_kernel, matmul_kernel, relu_kernel, h_3.cons(), weights_col2_broadcast.cons(), activation_tile_join_col2[3].prod()], placement=Tile(2, 5), stack_size=0x2000)
    Workers = [worker_aie0_2, worker_aie0_3, worker_aie0_4, worker_aie0_5,
               worker_aie2_2, worker_aie2_3, worker_aie2_4, worker_aie2_5]

    # Runtime
    tap_act = TensorAccessPattern(tensor_dims=[M * K], offset=0, sizes=[1, M * K], strides=[M * K, 1])
    tap_w   = TensorAccessPattern(tensor_dims=[K * N], offset=0, sizes=[1, K * N], strides=[K * N, 1])
    tap_out = TensorAccessPattern(tensor_dims=[M * N], offset=0, sizes=[1, M * N], strides=[M * N, 1])
    rt = Runtime()
    with rt.sequence(activation_flat_type, weight_full_flat_type, weight_full_flat_type, output_full_flat_type) as (input_activation_in, weights_col0_in, weights_col2_in, output_activation_out):  # type: ignore[misc]
        # Start Workers
        rt.start(*Workers)
        # Fills
        rt.fill(input_activation_fifo.prod(), input_activation_in, tap=tap_act, placement=Tile(0, 0))
        rt.fill(weights_col0_fifo.prod(),     weights_col0_in,     tap=tap_w,   placement=Tile(0, 0))
        rt.fill(weights_col2_fifo.prod(),     weights_col2_in,     tap=tap_w,   placement=Tile(2, 0))
        # Drains
        rt.drain(output_activation_fifo.cons(), output_activation_out, wait=True, tap=tap_out, placement=Tile(2, 0))

    # Program
    my_program = Program(iron.get_current_device(), rt)

    # Placement
    return my_program.resolve_program(SequentialPlacer())


def _detile_output(C_flat, M=64, N=64, n_tiles=4, micro_r=4, micro_t=4):
    """Convert joined-tiled output (4 row blocks of (M/4)×N) to row-major (M×N)."""
    M_tile = M // n_tiles  # 16 rows per tile
    # Each block is a (M_tile×N) matrix in tiled format: [M_tile/r, N/t, r, t]
    # After join: [n_tiles, M_tile/r, N/t, r, t]
    C_blocks = np.array(C_flat, dtype=np.float32).reshape(
        n_tiles, M_tile // micro_r, N // micro_t, micro_r, micro_t
    )
    # Map: row = tile*(M_tile) + mr*micro_r + r_idx, col = mc*micro_t + t_idx
    # Transpose to [tile, mr, r, mc, t] then reshape to [M, N]
    return C_blocks.transpose(0, 1, 3, 2, 4).reshape(M, N)


def main():
    M, K, N = 64, 64, 64

    rng = np.random.default_rng(42)

    A  = iron.zeros(M * K, dtype=bfloat16, device="npu")
    W0 = iron.zeros(K * N, dtype=bfloat16, device="npu")
    W1 = iron.zeros(K * N, dtype=bfloat16, device="npu")
    Y  = iron.zeros(M * N, dtype=bfloat16, device="npu")

    A_np  = rng.uniform(-1.0, 1.0, (M, K)).astype(bfloat16)
    W0_np = rng.uniform(-1.0, 1.0, (K, N)).astype(bfloat16)
    W1_np = rng.uniform(-1.0, 1.0, (K, N)).astype(bfloat16)

    A[:]  = A_np.flatten()
    W0[:] = W0_np.flatten()
    W1[:] = W1_np.flatten()

    mlp_two_col_jit(A, W0, W1, Y)

    # De-tile the output from joined-tiled format to row-major
    actual = _detile_output(Y, M, N)
    print("Y[0,:8] (row-major after de-tile):", actual[0, :8])

    # Reference: relu(relu(A @ W0) @ W1) in float32
    a_f32  = A_np.astype(np.float32)
    w0_f32 = W0_np.astype(np.float32)
    w1_f32 = W1_np.astype(np.float32)
    h      = np.maximum(a_f32 @ w0_f32, 0.0)
    expected = np.maximum(h @ w1_f32, 0.0)

    print("Element-by-element comparison (first 8 of row 0):")
    for c in range(min(8, N)):
        print(f"  [0,{c}] expected={float(expected[0,c]):.4f}  actual={float(actual[0,c]):.4f}")

    atol, rtol = 0.05, 0.02
    if np.allclose(actual, expected, atol=atol, rtol=rtol):
        print(f"Validation passed: all {M * N} elements within atol={atol}, rtol={rtol}")
    else:
        large_err = np.where(~np.isclose(actual, expected, atol=atol, rtol=rtol))
        print(f"Validation failed: {len(large_err[0])} elements exceed tolerance")
        for i in range(min(5, len(large_err[0]))):
            r, c = large_err[0][i], large_err[1][i]
            print(f"  [{r},{c}] expected={float(expected[r,c]):.4f}, actual={float(actual[r,c]):.4f}, diff={abs(float(actual[r,c])-float(expected[r,c])):.4f}")
        if len(large_err[0]) > 5:
            print(f"  ... and {len(large_err[0]) - 5} more")


if __name__ == "__main__":
    main()
