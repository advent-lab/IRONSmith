#
# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# (c) Copyright 2025 Advanced Micro Devices, Inc. or its affiliates

# example_jit_mlp.py – 4-layer MLP on 4 AIE columns (bf16), one layer per column.
#
# All 4 layers execute in a single dispatch.  Each column holds one layer:
#   Column 0: layer 0 (ReLU)
#   Column 1: layer 1 (ReLU)
#   Column 2: layer 2 (ReLU)
#   Column 3: layer 3 (Softmax)
#
# Within each column:
#   Input activation (M=64, K=64) – broadcast to 4 compute tiles
#   Weight slice     (K=64, n=16) – unique per tile (1/4 of the weight matrix)
#   Output activation (M=64, N=64) – joined from 4 tile outputs in MemTile

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
def mlp_4col_jit(input_activation, weight_layer0, weight_layer1, weight_layer2, weight_layer3, output_activation):
    batch_size       = 64
    input_features   = 64
    output_features  = 64
    features_per_tile = 16  # output_features // 4 tiles

    micro_r, micro_s, micro_t = 4, 8, 4  # bf16 micro-kernel dimensions

    activation_layout_dims = [
        (batch_size // micro_r,     micro_r * input_features),
        (input_features // micro_s, micro_s),
        (micro_r,                   input_features),
        (micro_s,                   1),
    ]
    weight_layout_dims = [
        (input_features // micro_s,    micro_s * features_per_tile),
        (features_per_tile // micro_t, micro_t),
        (micro_s,                      features_per_tile),
        (micro_t,                      1),
    ]
    output_layout_dims = [
        (batch_size // micro_r,          micro_r * features_per_tile),
        (micro_r,                        micro_t),
        (features_per_tile // micro_t,   micro_r * micro_t),
        (micro_t,                        1),
    ]

    # Tensor Types
    activation_flat_type = np.ndarray[(batch_size * input_features,),     np.dtype[bfloat16]]
    activation_full_type = np.ndarray[(batch_size, input_features),        np.dtype[bfloat16]]
    activation_tile_type = np.ndarray[(batch_size, features_per_tile),     np.dtype[bfloat16]]
    weight_flat_type     = np.ndarray[(input_features * output_features,), np.dtype[bfloat16]]
    weight_tile_type     = np.ndarray[(input_features, features_per_tile), np.dtype[bfloat16]]

    # Data Movement
    # Object Fifos
    input_activation_fifo   = ObjectFifo(obj_type=activation_full_type, depth=2, name="input_activation_fifo")
    inter_activation_fifo1  = ObjectFifo(obj_type=activation_full_type, depth=2, name="inter_activation_fifo1", dims_to_stream=output_layout_dims)
    inter_activation_fifo2  = ObjectFifo(obj_type=activation_full_type, depth=2, name="inter_activation_fifo2", dims_to_stream=output_layout_dims)
    inter_activation_fifo3  = ObjectFifo(obj_type=activation_full_type, depth=2, name="inter_activation_fifo3", dims_to_stream=output_layout_dims)
    output_activation_fifo  = ObjectFifo(obj_type=activation_full_type, depth=2, name="output_activation_fifo",  dims_to_stream=output_layout_dims)
    weights_col0_fifo       = ObjectFifo(obj_type=weight_flat_type,     depth=1, name="weights_col0_fifo")
    weights_col1_fifo       = ObjectFifo(obj_type=weight_flat_type,     depth=1, name="weights_col1_fifo")
    weights_col2_fifo       = ObjectFifo(obj_type=weight_flat_type,     depth=1, name="weights_col2_fifo")
    weights_col3_fifo       = ObjectFifo(obj_type=weight_flat_type,     depth=1, name="weights_col3_fifo")

    # Splits
    weight_tile_split_col0 = weights_col0_fifo.cons().split(names=["wt_col0_0", "wt_col0_1", "wt_col0_2", "wt_col0_3"], obj_types=[weight_tile_type]*4, offsets=[0, input_features*features_per_tile, 2*input_features*features_per_tile, 3*input_features*features_per_tile], dims_to_stream=[weight_layout_dims]*4, placement=Tile(0, 1))
    weight_tile_split_col1 = weights_col1_fifo.cons().split(names=["wt_col1_0", "wt_col1_1", "wt_col1_2", "wt_col1_3"], obj_types=[weight_tile_type]*4, offsets=[0, input_features*features_per_tile, 2*input_features*features_per_tile, 3*input_features*features_per_tile], dims_to_stream=[weight_layout_dims]*4, placement=Tile(1, 1))
    weight_tile_split_col2 = weights_col2_fifo.cons().split(names=["wt_col2_0", "wt_col2_1", "wt_col2_2", "wt_col2_3"], obj_types=[weight_tile_type]*4, offsets=[0, input_features*features_per_tile, 2*input_features*features_per_tile, 3*input_features*features_per_tile], dims_to_stream=[weight_layout_dims]*4, placement=Tile(2, 1))
    weight_tile_split_col3 = weights_col3_fifo.cons().split(names=["wt_col3_0", "wt_col3_1", "wt_col3_2", "wt_col3_3"], obj_types=[weight_tile_type]*4, offsets=[0, input_features*features_per_tile, 2*input_features*features_per_tile, 3*input_features*features_per_tile], dims_to_stream=[weight_layout_dims]*4, placement=Tile(3, 1))

    # Joins
    activation_tile_join_col0 = inter_activation_fifo1.prod().join(names=["act_join_col0_0", "act_join_col0_1", "act_join_col0_2", "act_join_col0_3"], obj_types=[activation_tile_type]*4, offsets=[0, batch_size*features_per_tile, 2*batch_size*features_per_tile, 3*batch_size*features_per_tile], placement=Tile(0, 1))
    activation_tile_join_col1 = inter_activation_fifo2.prod().join(names=["act_join_col1_0", "act_join_col1_1", "act_join_col1_2", "act_join_col1_3"], obj_types=[activation_tile_type]*4, offsets=[0, batch_size*features_per_tile, 2*batch_size*features_per_tile, 3*batch_size*features_per_tile], placement=Tile(1, 1))
    activation_tile_join_col2 = inter_activation_fifo3.prod().join(names=["act_join_col2_0", "act_join_col2_1", "act_join_col2_2", "act_join_col2_3"], obj_types=[activation_tile_type]*4, offsets=[0, batch_size*features_per_tile, 2*batch_size*features_per_tile, 3*batch_size*features_per_tile], placement=Tile(2, 1))
    activation_tile_join_col3 = output_activation_fifo.prod().join(names=["act_join_col3_0", "act_join_col3_1", "act_join_col3_2", "act_join_col3_3"], obj_types=[activation_tile_type]*4, offsets=[0, batch_size*features_per_tile, 2*batch_size*features_per_tile, 3*batch_size*features_per_tile], placement=Tile(3, 1))

    # Broadcasts
    input_activation_col0_fifo  = input_activation_fifo.cons().forward(name="input_activation_col0_fifo",  dims_to_stream=activation_layout_dims, placement=Tile(0, 1))

    # Compute Kernels
    build_dir      = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
    mm_obj         = os.path.join(build_dir, "mm.o")
    relu_obj       = os.path.join(build_dir, "relu.o")
    softmax_obj    = os.path.join(build_dir, "softmax.o")
    zero_kernel    = Kernel("zero_bf16",        mm_obj,      [activation_tile_type])
    matmul_kernel  = Kernel("matmul_bf16_bf16", mm_obj,      [activation_full_type, weight_tile_type, activation_tile_type])
    relu_kernel    = Kernel("bf16_relu",        relu_obj,    [activation_tile_type, activation_tile_type])
    softmax_kernel = Kernel("softmax_bf16",     softmax_obj, [activation_tile_type, activation_tile_type, np.int32])

    tile_elements = batch_size * features_per_tile  # 1024 – size arg for softmax_bf16

    # Core Body Functions
    def core_shared_matmul_relu(zero, matmul, relu, in0, in1, out0):
        buf_in0 = in0.acquire(1)
        buf_in1 = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        zero(buf_out0)
        matmul(buf_in0, buf_in1, buf_out0)
        relu(buf_out0, buf_out0)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    def core_shared_matmul_softmax(zero, matmul, softmax, in0, in1, out0):
        buf_in0 = in0.acquire(1)
        buf_in1 = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        zero(buf_out0)
        matmul(buf_in0, buf_in1, buf_out0)
        i32 = IntegerType.get_signless(32)
        size = arith.ConstantOp(i32, IntegerAttr.get(i32, tile_elements)).result
        softmax(buf_out0, buf_out0, size)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    # Workers
    worker_aie0_2 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    input_activation_col0_fifo.cons(), weight_tile_split_col0[0].cons(), activation_tile_join_col0[0].prod()], placement=Tile(0, 2), stack_size=0x2000)
    worker_aie1_2 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    inter_activation_fifo1.cons(), weight_tile_split_col1[0].cons(), activation_tile_join_col1[0].prod()], placement=Tile(1, 2), stack_size=0x2000)
    worker_aie2_2 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    inter_activation_fifo2.cons(), weight_tile_split_col2[0].cons(), activation_tile_join_col2[0].prod()], placement=Tile(2, 2), stack_size=0x2000)
    worker_aie3_2 = Worker(core_fn=core_shared_matmul_softmax, fn_args=[zero_kernel, matmul_kernel, softmax_kernel, inter_activation_fifo3.cons(), weight_tile_split_col3[0].cons(), activation_tile_join_col3[0].prod()], placement=Tile(3, 2), stack_size=0x2000)
    worker_aie0_3 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    input_activation_col0_fifo.cons(), weight_tile_split_col0[1].cons(), activation_tile_join_col0[1].prod()], placement=Tile(0, 3), stack_size=0x2000)
    worker_aie1_3 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    inter_activation_fifo1.cons(), weight_tile_split_col1[1].cons(), activation_tile_join_col1[1].prod()], placement=Tile(1, 3), stack_size=0x2000)
    worker_aie2_3 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    inter_activation_fifo2.cons(), weight_tile_split_col2[1].cons(), activation_tile_join_col2[1].prod()], placement=Tile(2, 3), stack_size=0x2000)
    worker_aie3_3 = Worker(core_fn=core_shared_matmul_softmax, fn_args=[zero_kernel, matmul_kernel, softmax_kernel, inter_activation_fifo3.cons(), weight_tile_split_col3[1].cons(), activation_tile_join_col3[1].prod()], placement=Tile(3, 3), stack_size=0x2000)
    worker_aie0_4 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    input_activation_col0_fifo.cons(), weight_tile_split_col0[2].cons(), activation_tile_join_col0[2].prod()], placement=Tile(0, 4), stack_size=0x2000)
    worker_aie1_4 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    inter_activation_fifo1.cons(), weight_tile_split_col1[2].cons(), activation_tile_join_col1[2].prod()], placement=Tile(1, 4), stack_size=0x2000)
    worker_aie2_4 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    inter_activation_fifo2.cons(), weight_tile_split_col2[2].cons(), activation_tile_join_col2[2].prod()], placement=Tile(2, 4), stack_size=0x2000)
    worker_aie3_4 = Worker(core_fn=core_shared_matmul_softmax, fn_args=[zero_kernel, matmul_kernel, softmax_kernel, inter_activation_fifo3.cons(), weight_tile_split_col3[2].cons(), activation_tile_join_col3[2].prod()], placement=Tile(3, 4), stack_size=0x2000)
    worker_aie0_5 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    input_activation_col0_fifo.cons(), weight_tile_split_col0[3].cons(), activation_tile_join_col0[3].prod()], placement=Tile(0, 5), stack_size=0x2000)
    worker_aie1_5 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    inter_activation_fifo1.cons(), weight_tile_split_col1[3].cons(), activation_tile_join_col1[3].prod()], placement=Tile(1, 5), stack_size=0x2000)
    worker_aie2_5 = Worker(core_fn=core_shared_matmul_relu,    fn_args=[zero_kernel, matmul_kernel, relu_kernel,    inter_activation_fifo2.cons(), weight_tile_split_col2[3].cons(), activation_tile_join_col2[3].prod()], placement=Tile(2, 5), stack_size=0x2000)
    worker_aie3_5 = Worker(core_fn=core_shared_matmul_softmax, fn_args=[zero_kernel, matmul_kernel, softmax_kernel, inter_activation_fifo3.cons(), weight_tile_split_col3[3].cons(), activation_tile_join_col3[3].prod()], placement=Tile(3, 5), stack_size=0x2000)

    Workers = [worker_aie0_2, worker_aie1_2, worker_aie2_2, worker_aie3_2, worker_aie0_3, worker_aie1_3, worker_aie2_3, worker_aie3_3, worker_aie0_4, worker_aie1_4, worker_aie2_4, worker_aie3_4, worker_aie0_5, worker_aie1_5, worker_aie2_5, worker_aie3_5]

    # Runtime
    tap_act = TensorAccessPattern(tensor_dims=[batch_size * input_features],  offset=0, sizes=[1, batch_size * input_features],  strides=[batch_size * input_features,  1])
    tap_w   = TensorAccessPattern(tensor_dims=[input_features * output_features], offset=0, sizes=[1, input_features * output_features], strides=[input_features * output_features, 1])
    tap_out = TensorAccessPattern(tensor_dims=[batch_size * output_features], offset=0, sizes=[1, batch_size * output_features], strides=[batch_size * output_features, 1])

    rt = Runtime()
    with rt.sequence(activation_flat_type, weight_flat_type, weight_flat_type, weight_flat_type, weight_flat_type, activation_flat_type) as (input_act_buf, w0_buf, w1_buf, w2_buf, w3_buf, output_act_buf):
        rt.start(*Workers)
        rt.fill(input_activation_fifo.prod(), input_act_buf, tap=tap_act, placement=Tile(0, 0))
        rt.fill(weights_col0_fifo.prod(),     w0_buf,        tap=tap_w,   placement=Tile(0, 0))
        rt.fill(weights_col1_fifo.prod(),     w1_buf,        tap=tap_w,   placement=Tile(1, 0))
        rt.fill(weights_col2_fifo.prod(),     w2_buf,        tap=tap_w,   placement=Tile(2, 0))
        rt.fill(weights_col3_fifo.prod(),     w3_buf,        tap=tap_w,   placement=Tile(3, 0))
        rt.drain(output_activation_fifo.cons(), output_act_buf, wait=True, tap=tap_out, placement=Tile(3, 0))

    # Program
    my_program = Program(iron.get_current_device(), rt)

    # Placement
    return my_program.resolve_program(SequentialPlacer())


def main():
    M, K, N = 64, 64, 64

    rng = np.random.default_rng(42)

    X  = iron.zeros(M * K, dtype=bfloat16, device="npu")
    W0 = iron.zeros(K * N, dtype=bfloat16, device="npu")
    W1 = iron.zeros(K * N, dtype=bfloat16, device="npu")
    W2 = iron.zeros(K * N, dtype=bfloat16, device="npu")
    W3 = iron.zeros(K * N, dtype=bfloat16, device="npu")
    Y  = iron.zeros(M * N, dtype=bfloat16, device="npu")

    X[:]  = rng.uniform(-1.0, 1.0, M * K).astype(bfloat16)
    W0[:] = rng.uniform(-1.0, 1.0, K * N).astype(bfloat16)
    W1[:] = rng.uniform(-1.0, 1.0, K * N).astype(bfloat16)
    W2[:] = rng.uniform(-1.0, 1.0, K * N).astype(bfloat16)
    W3[:] = rng.uniform(-1.0, 1.0, K * N).astype(bfloat16)

    mlp_4col_jit(X, W0, W1, W2, W3, Y)

    print(Y)


if __name__ == "__main__":
    main()
