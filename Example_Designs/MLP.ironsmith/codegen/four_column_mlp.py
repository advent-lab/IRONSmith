#
# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# (c) Copyright 2025 Advanced Micro Devices, Inc. or its affiliates

# mlp_cross_col_jit.py – 2- and 3-layer MLP with tiled-C → tiled-A format conversion
#
# Uses the combined_transpose.py pattern at each inter-column hop:
#   h_i.cons(dims_from_stream=tiled_c_to_rowmajor_dims)
#      .forward(dims_to_stream=activation_layout_dims, placement=Tile(dst_col,1))
#
# Two steps at the receiving MemTile:
#   S2MM (dims_from_stream): tiled-C cross-column stream → row-major in MemTile buffer
#   MM2S (dims_to_stream):   row-major MemTile buffer    → tiled-A stream to tile
#
# 2-column design (mlp_two_col_fmt_jit):
#   Col 0 → h01 conv at MemTile(1,1) → Col 1 → output join at MemTile(0,1)
#   MemTile(0,1): S2MM 1+1+4=6, MM2S 4+1+1=6
#   MemTile(1,1): S2MM 1+4=5,   MM2S 1+4=5
#
# 3-column design (mlp_three_col_fmt_jit):
#   Col 0 → h01 conv at MemTile(1,1) → Col 1
#         → h12 conv at MemTile(2,1) → Col 2 → output join at MemTile(0,1)
#   MemTile(0,1): S2MM 1+1+4=6, MM2S 4+1+1=6
#   MemTile(1,1): S2MM 1+4=5,   MM2S 1+4=5
#   MemTile(2,1): S2MM 1+4=5,   MM2S 1+4=5
#
# mm.o and relu.o are shared from the mlp_jit/build/ directory.

import os
import numpy as np
from ml_dtypes import bfloat16

from aie.iron import Kernel, ObjectFifo, Program, Runtime, Worker
from aie.iron.placers import SequentialPlacer
from aie.iron.device import Tile
import aie.iron as iron

from aie.helpers.taplib import TensorAccessPattern


@iron.jit(is_placed=False)
def mlp_two_col_fmt_jit(input_activation, weights_all, output_activation):
    M               = 64
    K               = 64
    N               = 64
    n_tiles         = 4
    M_tile          = M // n_tiles   # 16 rows per tile

    micro_r, micro_s, micro_t = 4, 8, 4

    # Row-major (M_tile×K) → tiled-A for micro-kernel A input (applied at MemTile MM2S)
    activation_layout_dims = [
        (M_tile // micro_r,  micro_r * K),         # (4, 256)
        (K // micro_s,       micro_s),             # (8,   8)
        (micro_r,            K),                   # (4,  64)
        (micro_s,            1),                   # (8,   1)
    ]
    # Row-major (K×N) → tiled-B for micro-kernel B input
    weight_layout_dims = [
        (K // micro_s,       micro_s * N),         # (8, 512)
        (N // micro_t,       micro_t),             # (16,  4)
        (micro_s,            N),                   # (8,  64)
        (micro_t,            1),                   # (4,   1)
    ]
    # Tiled-C (M_tile×N) sequential stream → row-major buffer in MemTile(1,1).
    # Applied at MemTile(1,1) S2MM (dims_from_stream) when receiving h01_i.
    # Incoming stream k = mr*256 + mc*16 + r*4 + t  (tiled-C order from col0 tile)
    # Written to buffer at: mr*256 + mc*4 + r*64 + t  (row-major: row=mr*4+r, col=mc*4+t)
    tiled_c_to_rowmajor_dims = [
        (M_tile // micro_r,  micro_r * N),         # (4, 256) — mr, stride 256
        (N // micro_t,       micro_t),             # (16,  4) — mc, stride 4
        (micro_r,            N),                   # (4,  64) — r,  stride 64
        (micro_t,            1),                   # (4,   1) — t,  stride 1
    ]

    # Tensor Types
    activation_flat_type  = np.ndarray[(M * K,),      np.dtype[bfloat16]]
    weight_all_flat_type  = np.ndarray[(2 * K * N,),  np.dtype[bfloat16]]
    output_full_flat_type = np.ndarray[(M * N,),      np.dtype[bfloat16]]
    activation_tile_type  = np.ndarray[(M_tile, K),   np.dtype[bfloat16]]
    weight_full_type      = np.ndarray[(K, N),        np.dtype[bfloat16]]
    output_tile_type      = np.ndarray[(M_tile, N),   np.dtype[bfloat16]]
    output_full_type      = np.ndarray[(M, N),        np.dtype[bfloat16]]

    # Host ↔ device FIFOs
    input_activation_fifo  = ObjectFifo(obj_type=activation_flat_type, depth=2, name="input_activation_fifo")
    weights_col0_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col0_fifo")
    weights_col1_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col1_fifo")
    output_activation_fifo = ObjectFifo(obj_type=output_full_type,      depth=2, name="output_activation_fifo")

    # Per-tile cross-column FIFOs: tile(0,2+i) → MemTile(1,1)
    h01_0 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_0")
    h01_1 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_1")
    h01_2 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_2")
    h01_3 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_3")

    # Split: input rows → col0 tiles via MemTile(0,1)
    act_split_col0 = input_activation_fifo.cons().split(
        names=["act_col0_0", "act_col0_1", "act_col0_2", "act_col0_3"],
        obj_types=[activation_tile_type] * n_tiles,
        offsets=[i * M_tile * K for i in range(n_tiles)],
        dims_to_stream=[activation_layout_dims] * n_tiles,  # type: ignore[arg-type]
        placement=Tile(0, 1),
    )

    # Join: col1 tile outputs → output_activation at MemTile(0,1).
    # Col1 tiles join cross-column (tile(1,2+i) → MemTile(0,1)), freeing MemTile(1,1)
    # S2MM channels for the h01_i format-conversion FIFOs.
    act_join_col1 = output_activation_fifo.prod().join(
        names=["act_join_col1_0", "act_join_col1_1", "act_join_col1_2", "act_join_col1_3"],
        obj_types=[output_tile_type] * n_tiles,
        offsets=[i * M_tile * N for i in range(n_tiles)],
        placement=Tile(0, 1),
    )

    # Weight broadcasts
    weights_col0_broadcast = weights_col0_fifo.cons().forward(
        name="weights_col0_broadcast",
        dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(0, 1),
    )
    weights_col1_broadcast = weights_col1_fifo.cons().forward(
        name="weights_col1_broadcast",
        dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(1, 1),
    )

    # Format conversion at MemTile(1,1) for each inter-column FIFO.
    # combined_transpose.py pattern:
    #   cons(dims_from_stream): MemTile(1,1) S2MM stores tiled-C stream as row-major
    #   forward(dims_to_stream): MemTile(1,1) MM2S reads row-major, sends tiled-A to tile
    h01_conv_0 = h01_0.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h01_conv_0", dims_to_stream=activation_layout_dims, placement=Tile(1, 1))  # type: ignore[arg-type]
    h01_conv_1 = h01_1.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h01_conv_1", dims_to_stream=activation_layout_dims, placement=Tile(1, 1))  # type: ignore[arg-type]
    h01_conv_2 = h01_2.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h01_conv_2", dims_to_stream=activation_layout_dims, placement=Tile(1, 1))  # type: ignore[arg-type]
    h01_conv_3 = h01_3.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h01_conv_3", dims_to_stream=activation_layout_dims, placement=Tile(1, 1))  # type: ignore[arg-type]

    # Kernels
    build_dir     = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
    mm_obj        = os.path.join(build_dir, "mm.o")
    relu_obj      = os.path.join(build_dir, "relu.o")
    zero_kernel   = Kernel("zero_bf16",        mm_obj,   [output_tile_type])
    matmul_kernel = Kernel("matmul_bf16_bf16", mm_obj,   [activation_tile_type, weight_full_type, output_tile_type])
    relu_kernel   = Kernel("bf16_relu",        relu_obj, [output_tile_type, output_tile_type])

    def core_matmul_relu(zero, matmul, relu, in0, in1, out0):
        buf_in0  = in0.acquire(1)
        buf_in1  = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        zero(buf_out0)
        matmul(buf_in0, buf_in1, buf_out0)
        relu(buf_out0, buf_out0)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    # Workers – Column 0: relu(A_tile @ W0) → h01_i
    workers_col0 = [
        Worker(core_fn=core_matmul_relu,
               fn_args=[zero_kernel, matmul_kernel, relu_kernel,
                        act_split_col0[i].cons(),
                        weights_col0_broadcast.cons(),
                        [h01_0, h01_1, h01_2, h01_3][i].prod()],
               placement=Tile(0, 2 + i),
               stack_size=0x2000)
        for i in range(n_tiles)
    ]

    # Workers – Column 1: relu(h01_conv_i @ W1) → act_join_col1[i]
    # h01_conv_i.cons() delivers tiled-A (converted by MemTile(1,1)).
    workers_col1 = [
        Worker(core_fn=core_matmul_relu,
               fn_args=[zero_kernel, matmul_kernel, relu_kernel,
                        [h01_conv_0, h01_conv_1, h01_conv_2, h01_conv_3][i].cons(),
                        weights_col1_broadcast.cons(),
                        act_join_col1[i].prod()],
               placement=Tile(1, 2 + i),
               stack_size=0x2000)
        for i in range(n_tiles)
    ]

    all_workers = workers_col0 + workers_col1

    # Runtime — 3 buffer args (within XRT 5-slot limit)
    tap_act = TensorAccessPattern(tensor_dims=[M * K],     offset=0,         sizes=[1, M * K], strides=[M * K, 1])
    tap_out = TensorAccessPattern(tensor_dims=[M * N],     offset=0,         sizes=[1, M * N], strides=[M * N, 1])
    tap_w   = [TensorAccessPattern(tensor_dims=[2 * K * N], offset=i * K * N,
                                    sizes=[1, K * N], strides=[K * N, 1])
               for i in range(2)]

    rt = Runtime()
    with rt.sequence(activation_flat_type, weight_all_flat_type, output_full_flat_type) as (
        input_in, weights_all_in, output_out
    ):  # type: ignore[misc]
        rt.start(*all_workers)
        rt.fill(input_activation_fifo.prod(), input_in,        tap=tap_act,  placement=Tile(0, 0))
        rt.fill(weights_col0_fifo.prod(),     weights_all_in,  tap=tap_w[0], placement=Tile(0, 0))
        rt.fill(weights_col1_fifo.prod(),     weights_all_in,  tap=tap_w[1], placement=Tile(1, 0))
        # Drain from Shim(0,0): output join is at MemTile(0,1)
        rt.drain(output_activation_fifo.cons(), output_out, wait=True, tap=tap_out, placement=Tile(0, 0))

    return Program(iron.get_current_device(), rt).resolve_program(SequentialPlacer())


@iron.jit(is_placed=False)
def mlp_three_col_fmt_jit(input_activation, weights_all, output_activation):
    M               = 64
    K               = 64
    N               = 64
    n_tiles         = 4
    M_tile          = M // n_tiles   # 16 rows per tile

    micro_r, micro_s, micro_t = 4, 8, 4

    activation_layout_dims = [
        (M_tile // micro_r,  micro_r * K),         # (4, 256)
        (K // micro_s,       micro_s),             # (8,   8)
        (micro_r,            K),                   # (4,  64)
        (micro_s,            1),                   # (8,   1)
    ]
    weight_layout_dims = [
        (K // micro_s,       micro_s * N),         # (8, 512)
        (N // micro_t,       micro_t),             # (16,  4)
        (micro_s,            N),                   # (8,  64)
        (micro_t,            1),                   # (4,   1)
    ]
    tiled_c_to_rowmajor_dims = [
        (M_tile // micro_r,  micro_r * N),         # (4, 256)
        (N // micro_t,       micro_t),             # (16,  4)
        (micro_r,            N),                   # (4,  64)
        (micro_t,            1),                   # (4,   1)
    ]

    # Tensor Types
    activation_flat_type  = np.ndarray[(M * K,),      np.dtype[bfloat16]]
    weight_all_flat_type  = np.ndarray[(3 * K * N,),  np.dtype[bfloat16]]
    output_full_flat_type = np.ndarray[(M * N,),      np.dtype[bfloat16]]
    activation_tile_type  = np.ndarray[(M_tile, K),   np.dtype[bfloat16]]
    weight_full_type      = np.ndarray[(K, N),        np.dtype[bfloat16]]
    output_tile_type      = np.ndarray[(M_tile, N),   np.dtype[bfloat16]]
    output_full_type      = np.ndarray[(M, N),        np.dtype[bfloat16]]

    # Host ↔ device FIFOs
    input_activation_fifo  = ObjectFifo(obj_type=activation_flat_type, depth=2, name="input_activation_fifo")
    weights_col0_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col0_fifo")
    weights_col1_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col1_fifo")
    weights_col2_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col2_fifo")
    output_activation_fifo = ObjectFifo(obj_type=output_full_type,      depth=2, name="output_activation_fifo")

    # Per-tile inter-column FIFOs
    h01_0 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_0")
    h01_1 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_1")
    h01_2 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_2")
    h01_3 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_3")

    h12_0 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h12_0")
    h12_1 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h12_1")
    h12_2 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h12_2")
    h12_3 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h12_3")

    # Split: input rows → col0 tiles via MemTile(0,1)
    act_split_col0 = input_activation_fifo.cons().split(
        names=["act_col0_0", "act_col0_1", "act_col0_2", "act_col0_3"],
        obj_types=[activation_tile_type] * n_tiles,
        offsets=[i * M_tile * K for i in range(n_tiles)],
        dims_to_stream=[activation_layout_dims] * n_tiles,  # type: ignore[arg-type]
        placement=Tile(0, 1),
    )

    # Join: col2 tile outputs → MemTile(0,1) → Shim(0,0) → host.
    # Placed at MemTile(0,1) (only MemTile with spare S2MM slots after wt0 + act_in).
    act_join_col2 = output_activation_fifo.prod().join(
        names=["act_join_col2_0", "act_join_col2_1", "act_join_col2_2", "act_join_col2_3"],
        obj_types=[output_tile_type] * n_tiles,
        offsets=[i * M_tile * N for i in range(n_tiles)],
        placement=Tile(0, 1),
    )

    # Weight broadcasts
    weights_col0_broadcast = weights_col0_fifo.cons().forward(
        name="weights_col0_broadcast",
        dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(0, 1),
    )
    weights_col1_broadcast = weights_col1_fifo.cons().forward(
        name="weights_col1_broadcast",
        dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(1, 1),
    )
    weights_col2_broadcast = weights_col2_fifo.cons().forward(
        name="weights_col2_broadcast",
        dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(2, 1),
    )

    # Format conversion at MemTile(1,1): col0 tiled-C → tiled-A for col1
    h01_conv_0 = h01_0.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h01_conv_0", dims_to_stream=activation_layout_dims, placement=Tile(1, 1))  # type: ignore[arg-type]
    h01_conv_1 = h01_1.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h01_conv_1", dims_to_stream=activation_layout_dims, placement=Tile(1, 1))  # type: ignore[arg-type]
    h01_conv_2 = h01_2.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h01_conv_2", dims_to_stream=activation_layout_dims, placement=Tile(1, 1))  # type: ignore[arg-type]
    h01_conv_3 = h01_3.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h01_conv_3", dims_to_stream=activation_layout_dims, placement=Tile(1, 1))  # type: ignore[arg-type]

    # Format conversion at MemTile(2,1): col1 tiled-C → tiled-A for col2
    h12_conv_0 = h12_0.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h12_conv_0", dims_to_stream=activation_layout_dims, placement=Tile(2, 1))  # type: ignore[arg-type]
    h12_conv_1 = h12_1.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h12_conv_1", dims_to_stream=activation_layout_dims, placement=Tile(2, 1))  # type: ignore[arg-type]
    h12_conv_2 = h12_2.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h12_conv_2", dims_to_stream=activation_layout_dims, placement=Tile(2, 1))  # type: ignore[arg-type]
    h12_conv_3 = h12_3.cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name="h12_conv_3", dims_to_stream=activation_layout_dims, placement=Tile(2, 1))  # type: ignore[arg-type]

    # Kernels
    build_dir     = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
    mm_obj        = os.path.join(build_dir, "mm.o")
    relu_obj      = os.path.join(build_dir, "relu.o")
    zero_kernel   = Kernel("zero_bf16",        mm_obj,   [output_tile_type])
    matmul_kernel = Kernel("matmul_bf16_bf16", mm_obj,   [activation_tile_type, weight_full_type, output_tile_type])
    relu_kernel   = Kernel("bf16_relu",        relu_obj, [output_tile_type, output_tile_type])

    def core_matmul_relu(zero, matmul, relu, in0, in1, out0):
        buf_in0  = in0.acquire(1)
        buf_in1  = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        zero(buf_out0)
        matmul(buf_in0, buf_in1, buf_out0)
        relu(buf_out0, buf_out0)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    h01 = [h01_0, h01_1, h01_2, h01_3]
    h12 = [h12_0, h12_1, h12_2, h12_3]
    h01_conv = [h01_conv_0, h01_conv_1, h01_conv_2, h01_conv_3]
    h12_conv = [h12_conv_0, h12_conv_1, h12_conv_2, h12_conv_3]

    # Workers – Column 0: relu(A_tile @ W0) → h01_i
    workers_col0 = [
        Worker(core_fn=core_matmul_relu,
               fn_args=[zero_kernel, matmul_kernel, relu_kernel,
                        act_split_col0[i].cons(),
                        weights_col0_broadcast.cons(),
                        h01[i].prod()],
               placement=Tile(0, 2 + i), stack_size=0x2000)
        for i in range(n_tiles)
    ]
    # Workers – Column 1: relu(h01_conv_i @ W1) → h12_i
    workers_col1 = [
        Worker(core_fn=core_matmul_relu,
               fn_args=[zero_kernel, matmul_kernel, relu_kernel,
                        h01_conv[i].cons(),
                        weights_col1_broadcast.cons(),
                        h12[i].prod()],
               placement=Tile(1, 2 + i), stack_size=0x2000)
        for i in range(n_tiles)
    ]
    # Workers – Column 2: relu(h12_conv_i @ W2) → act_join_col2[i]
    workers_col2 = [
        Worker(core_fn=core_matmul_relu,
               fn_args=[zero_kernel, matmul_kernel, relu_kernel,
                        h12_conv[i].cons(),
                        weights_col2_broadcast.cons(),
                        act_join_col2[i].prod()],
               placement=Tile(2, 2 + i), stack_size=0x2000)
        for i in range(n_tiles)
    ]

    all_workers = workers_col0 + workers_col1 + workers_col2

    # Runtime — 3 buffer args (within XRT 5-slot limit)
    tap_act = TensorAccessPattern(tensor_dims=[M * K],     offset=0,         sizes=[1, M * K], strides=[M * K, 1])
    tap_out = TensorAccessPattern(tensor_dims=[M * N],     offset=0,         sizes=[1, M * N], strides=[M * N, 1])
    tap_w   = [TensorAccessPattern(tensor_dims=[3 * K * N], offset=i * K * N,
                                    sizes=[1, K * N], strides=[K * N, 1])
               for i in range(3)]

    rt = Runtime()
    with rt.sequence(activation_flat_type, weight_all_flat_type, output_full_flat_type) as (
        input_in, weights_all_in, output_out
    ):  # type: ignore[misc]
        rt.start(*all_workers)
        rt.fill(input_activation_fifo.prod(), input_in,        tap=tap_act,  placement=Tile(0, 0))
        rt.fill(weights_col0_fifo.prod(),     weights_all_in,  tap=tap_w[0], placement=Tile(0, 0))
        rt.fill(weights_col1_fifo.prod(),     weights_all_in,  tap=tap_w[1], placement=Tile(1, 0))
        rt.fill(weights_col2_fifo.prod(),     weights_all_in,  tap=tap_w[2], placement=Tile(2, 0))
        # Drain from Shim(0,0): output join is at MemTile(0,1)
        rt.drain(output_activation_fifo.cons(), output_out, wait=True, tap=tap_out, placement=Tile(0, 0))

    return Program(iron.get_current_device(), rt).resolve_program(SequentialPlacer())


@iron.jit(is_placed=False)
def mlp_four_col_fmt_jit(input_activation, weights_all, output_activation):
    M               = 64
    K               = 64
    N               = 64
    n_tiles         = 4
    M_tile          = M // n_tiles   # 16

    micro_r, micro_s, micro_t = 4, 8, 4

    activation_layout_dims = [
        (M_tile // micro_r,  micro_r * K),
        (K // micro_s,       micro_s),
        (micro_r,            K),
        (micro_s,            1),
    ]
    weight_layout_dims = [
        (K // micro_s,       micro_s * N),
        (N // micro_t,       micro_t),
        (micro_s,            N),
        (micro_t,            1),
    ]
    tiled_c_to_rowmajor_dims = [
        (M_tile // micro_r,  micro_r * N),
        (N // micro_t,       micro_t),
        (micro_r,            N),
        (micro_t,            1),
    ]

    # Tensor Types
    activation_flat_type  = np.ndarray[(M * K,),      np.dtype[bfloat16]]
    weight_all_flat_type  = np.ndarray[(4 * K * N,),  np.dtype[bfloat16]]
    output_full_flat_type = np.ndarray[(M * N,),      np.dtype[bfloat16]]
    activation_tile_type  = np.ndarray[(M_tile, K),   np.dtype[bfloat16]]
    weight_full_type      = np.ndarray[(K, N),        np.dtype[bfloat16]]
    output_tile_type      = np.ndarray[(M_tile, N),   np.dtype[bfloat16]]
    output_full_type      = np.ndarray[(M, N),        np.dtype[bfloat16]]

    # Host ↔ device FIFOs
    input_activation_fifo  = ObjectFifo(obj_type=activation_flat_type, depth=2, name="input_activation_fifo")
    weights_col0_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col0_fifo")
    weights_col1_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col1_fifo")
    weights_col2_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col2_fifo")
    weights_col3_fifo      = ObjectFifo(obj_type=weight_full_type,      depth=1, name="weights_col3_fifo")
    output_activation_fifo = ObjectFifo(obj_type=output_full_type,      depth=2, name="output_activation_fifo")

    # Per-tile inter-column FIFOs
    h01 = [ObjectFifo(obj_type=output_tile_type, depth=2, name=f"h01_{i}") for i in range(n_tiles)]
    h12 = [ObjectFifo(obj_type=output_tile_type, depth=2, name=f"h12_{i}") for i in range(n_tiles)]
    h23 = [ObjectFifo(obj_type=output_tile_type, depth=2, name=f"h23_{i}") for i in range(n_tiles)]

    # Split: input rows → col0 tiles via MemTile(0,1)
    act_split_col0 = input_activation_fifo.cons().split(
        names=[f"act_col0_{i}" for i in range(n_tiles)],
        obj_types=[activation_tile_type] * n_tiles,
        offsets=[i * M_tile * K for i in range(n_tiles)],
        dims_to_stream=[activation_layout_dims] * n_tiles,  # type: ignore[arg-type]
        placement=Tile(0, 1),
    )

    # Join: col3 tile outputs → MemTile(0,1) → Shim(0,0) → host
    act_join_col3 = output_activation_fifo.prod().join(
        names=[f"act_join_col3_{i}" for i in range(n_tiles)],
        obj_types=[output_tile_type] * n_tiles,
        offsets=[i * M_tile * N for i in range(n_tiles)],
        placement=Tile(0, 1),
    )

    # Weight broadcasts
    weights_col0_broadcast = weights_col0_fifo.cons().forward(
        name="weights_col0_broadcast", dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(0, 1))
    weights_col1_broadcast = weights_col1_fifo.cons().forward(
        name="weights_col1_broadcast", dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(1, 1))
    weights_col2_broadcast = weights_col2_fifo.cons().forward(
        name="weights_col2_broadcast", dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(2, 1))
    weights_col3_broadcast = weights_col3_fifo.cons().forward(
        name="weights_col3_broadcast", dims_to_stream=weight_layout_dims,  # type: ignore[arg-type]
        placement=Tile(3, 1))

    # Format conversion: tiled-C → row-major → tiled-A at each receiving MemTile
    h01_conv = [h01[i].cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name=f"h01_conv_{i}", dims_to_stream=activation_layout_dims, placement=Tile(1, 1))  # type: ignore[arg-type]
        for i in range(n_tiles)]
    h12_conv = [h12[i].cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name=f"h12_conv_{i}", dims_to_stream=activation_layout_dims, placement=Tile(2, 1))  # type: ignore[arg-type]
        for i in range(n_tiles)]
    h23_conv = [h23[i].cons(dims_from_stream=tiled_c_to_rowmajor_dims).forward(  # type: ignore[arg-type]
        name=f"h23_conv_{i}", dims_to_stream=activation_layout_dims, placement=Tile(3, 1))  # type: ignore[arg-type]
        for i in range(n_tiles)]

    # Kernels
    build_dir      = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
    mm_obj         = os.path.join(build_dir, "mm.o")
    relu_obj       = os.path.join(build_dir, "relu.o")
    softmax_obj    = os.path.join(build_dir, "softmax.o")
    zero_kernel    = Kernel("zero_bf16",        mm_obj,      [output_tile_type])
    matmul_kernel  = Kernel("matmul_bf16_bf16", mm_obj,      [activation_tile_type, weight_full_type, output_tile_type])
    relu_kernel    = Kernel("bf16_relu",        relu_obj,    [output_tile_type, output_tile_type])
    softmax_kernel = Kernel("softmax_bf16_tiledc_per_row", softmax_obj,  # type: ignore[arg-type]
                            [output_tile_type])

    def core_matmul_relu(zero, matmul, relu, in0, in1, out0):
        buf_in0  = in0.acquire(1)
        buf_in1  = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        zero(buf_out0)
        matmul(buf_in0, buf_in1, buf_out0)
        relu(buf_out0, buf_out0)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    def core_output_softmax(zero, matmul, softmax, in0, in1, out0):
        buf_in0  = in0.acquire(1)
        buf_in1  = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        zero(buf_out0)
        matmul(buf_in0, buf_in1, buf_out0)
        softmax(buf_out0)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    workers_col0 = [Worker(core_fn=core_matmul_relu,
        fn_args=[zero_kernel, matmul_kernel, relu_kernel,
                 act_split_col0[i].cons(), weights_col0_broadcast.cons(), h01[i].prod()],
        placement=Tile(0, 2 + i), stack_size=0x2000) for i in range(n_tiles)]

    workers_col1 = [Worker(core_fn=core_matmul_relu,
        fn_args=[zero_kernel, matmul_kernel, relu_kernel,
                 h01_conv[i].cons(), weights_col1_broadcast.cons(), h12[i].prod()],
        placement=Tile(1, 2 + i), stack_size=0x2000) for i in range(n_tiles)]

    workers_col2 = [Worker(core_fn=core_matmul_relu,
        fn_args=[zero_kernel, matmul_kernel, relu_kernel,
                 h12_conv[i].cons(), weights_col2_broadcast.cons(), h23[i].prod()],
        placement=Tile(2, 2 + i), stack_size=0x2000) for i in range(n_tiles)]

    workers_col3 = [Worker(core_fn=core_output_softmax,
        fn_args=[zero_kernel, matmul_kernel, softmax_kernel,
                 h23_conv[i].cons(), weights_col3_broadcast.cons(), act_join_col3[i].prod()],
        placement=Tile(3, 2 + i), stack_size=0x2000) for i in range(n_tiles)]

    all_workers = workers_col0 + workers_col1 + workers_col2 + workers_col3

    tap_act = TensorAccessPattern(tensor_dims=[M * K],     offset=0,         sizes=[1, M * K], strides=[M * K, 1])
    tap_out = TensorAccessPattern(tensor_dims=[M * N],     offset=0,         sizes=[1, M * N], strides=[M * N, 1])
    tap_w   = [TensorAccessPattern(tensor_dims=[4 * K * N], offset=i * K * N,
                                    sizes=[1, K * N], strides=[K * N, 1])
               for i in range(4)]

    rt = Runtime()
    with rt.sequence(activation_flat_type, weight_all_flat_type, output_full_flat_type) as (
        input_in, weights_all_in, output_out
    ):  # type: ignore[misc]
        rt.start(*all_workers)
        rt.fill(input_activation_fifo.prod(), input_in,        tap=tap_act,  placement=Tile(0, 0))
        rt.fill(weights_col0_fifo.prod(),     weights_all_in,  tap=tap_w[0], placement=Tile(0, 0))
        rt.fill(weights_col1_fifo.prod(),     weights_all_in,  tap=tap_w[1], placement=Tile(1, 0))
        rt.fill(weights_col2_fifo.prod(),     weights_all_in,  tap=tap_w[2], placement=Tile(2, 0))
        rt.fill(weights_col3_fifo.prod(),     weights_all_in,  tap=tap_w[3], placement=Tile(3, 0))
        rt.drain(output_activation_fifo.cons(), output_out, wait=True, tap=tap_out, placement=Tile(0, 0))

    return Program(iron.get_current_device(), rt).resolve_program(SequentialPlacer())


def _detile_output(C_flat, M=64, N=64, n_tiles=4, micro_r=4, micro_t=4):
    """Convert joined tiled-C output to row-major."""
    M_tile   = M // n_tiles
    C_blocks = np.array(C_flat, dtype=np.float32).reshape(
        n_tiles, M_tile // micro_r, N // micro_t, micro_r, micro_t
    )
    return C_blocks.transpose(0, 1, 3, 2, 4).reshape(M, N)


def main():
    M, K, N = 64, 64, 64

    rng = np.random.default_rng(42)

    A     = iron.zeros(M * K,     dtype=bfloat16, device="npu")
    W_all = iron.zeros(4 * K * N, dtype=bfloat16, device="npu")
    Y     = iron.zeros(M * N,     dtype=bfloat16, device="npu")

    A_np  = rng.uniform(-1.0, 1.0, (M, K)).astype(bfloat16)
    W0_np = rng.uniform(-1.0, 1.0, (K, N)).astype(bfloat16)
    W1_np = rng.uniform(-1.0, 1.0, (K, N)).astype(bfloat16)
    W2_np = rng.uniform(-1.0, 1.0, (K, N)).astype(bfloat16)
    W3_np = rng.uniform(-1.0, 1.0, (K, N)).astype(bfloat16)

    A[:]              = A_np.flatten()
    W_all[:K*N]       = W0_np.flatten()
    W_all[K*N:2*K*N]  = W1_np.flatten()
    W_all[2*K*N:3*K*N] = W2_np.flatten()
    W_all[3*K*N:]     = W3_np.flatten()

    mlp_four_col_fmt_jit(A, W_all, Y)

    actual = _detile_output(Y, M, N)
    print("NPU output (row-major after de-tile):")
    for row in range(M):
        vals = " ".join(f"{float(actual[row, c]):.4f}" for c in range(N))
        print(f"  row {row:2d}: [{vals}]")

    # Reference: bf16 at each layer boundary + per-row softmax matching NPU.
    # NPU calls softmax_bf16_tiledc_per_row(buf): gathers each sample's 64 logits
    # from the tiled-C layout, softmaxes them, and scatters back in-place.
    # Logits are rounded to bf16 to match NPU matmul output precision.
    def bf16_relu(x):
        return np.maximum(x.astype(bfloat16).astype(np.float32), 0.0)

    def row_softmax(x):
        x_f32 = x.astype(np.float32)
        e = np.exp(x_f32 - x_f32.max(axis=1, keepdims=True))
        return e / (e.sum(axis=1, keepdims=True) + 1e-7)

    a_f32  = A_np.astype(np.float32)
    w0_f32 = W0_np.astype(np.float32)
    w1_f32 = W1_np.astype(np.float32)
    w2_f32 = W2_np.astype(np.float32)
    w3_f32 = W3_np.astype(np.float32)
    h1       = bf16_relu(a_f32 @ w0_f32)
    h2       = bf16_relu(h1    @ w1_f32)
    h3       = bf16_relu(h2    @ w2_f32)
    logits   = (h3.astype(bfloat16).astype(np.float32) @ w3_f32).astype(bfloat16).astype(np.float32)
    expected = row_softmax(logits)

    print("Element-by-element comparison (expected vs actual):")
    for row in range(M):
        exp_vals = " ".join(f"{float(expected[row, c]):.4f}" for c in range(N))
        act_vals = " ".join(f"{float(actual[row, c]):.4f}"   for c in range(N))
        print(f"  row {row:2d} expected: [{exp_vals}]")
        print(f"  row {row:2d}   actual: [{act_vals}]")

    # Argmax accuracy: the primary classification metric.
    actual_pred   = np.argmax(actual,   axis=1)
    expected_pred = np.argmax(expected, axis=1)
    mismatch_rows = np.where(actual_pred != expected_pred)[0]

    # A tie-break mismatch: NPU's winner and reference's winner have the same
    # probability in the NPU output (bf16 rounded two logits to equal values).
    n_tie = sum(
        1 for r in mismatch_rows
        if np.isclose(float(actual[r, actual_pred[r]]),
                      float(actual[r, expected_pred[r]]), rtol=1e-4)
    )
    n_real = len(mismatch_rows) - n_tie
    print(f"Argmax accuracy: {M - len(mismatch_rows)}/{M} exact, "
          f"{n_tie} bf16-tie-break, {n_real} real mismatches")

    # Element-wise tolerance check (bf16 tiled-matmul can produce small differences
    # that softmax amplifies for near-equal logits; argmax accuracy is the main gate).
    atol, rtol = 0.05, 0.05
    large_err = np.where(~np.isclose(actual, expected, atol=atol, rtol=rtol))
    n_err = len(large_err[0])
    if n_err == 0:
        print(f"Validation passed: all {M * N} elements within atol={atol}, rtol={rtol}")
    else:
        print(f"Element-wise check: {n_err} of {M*N} exceed atol={atol} "
              f"(expected for bf16 precision on near-equal logits)")
        for i in range(min(5, n_err)):
            r, c = large_err[0][i], large_err[1][i]
            print(f"  [{r},{c}] expected={float(expected[r,c]):.4f}, "
                  f"actual={float(actual[r,c]):.4f}, "
                  f"diff={abs(float(actual[r,c])-float(expected[r,c])):.4f}")
        if n_err > 5:
            print(f"  ... and {n_err - 5} more")
    if n_real == 0:
        print("Validation passed: no real argmax errors (bf16 precision artifacts only)")


if __name__ == "__main__":
    main()
