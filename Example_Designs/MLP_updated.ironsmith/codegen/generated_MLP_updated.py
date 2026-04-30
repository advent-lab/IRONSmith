# gui_design.py -*- Python -*-


import numpy as np
from ml_dtypes import bfloat16

import os
from aie.iron import Program, Runtime, Worker, ObjectFifo
from aie.iron.placers import SequentialPlacer
from aie.iron.device.tile import AnyComputeTile
from aie.iron import Kernel, jit
from aie.iron.dataflow import ObjectFifoLink
from aie.iron.device import Tile
from aie.iron.device import NPU1Col1, NPU2Col1, XCVC1902
import aie.iron as iron

from aie.helpers.taplib import TensorAccessPattern


@iron.jit(is_placed=False)
def gui_design_jit(Input_A, W, OutputC):
    # Constants
    N = 64
    M = 64
    K = 64
    n_tiles = 4
    M_tile = 16
    micro_r = 4
    const = 1
    micro_s = 8
    micro_t = 4
    activation_layout_dims = [(M_tile // micro_r, micro_r * K), (K // micro_s, micro_s), (micro_r, K), (micro_s, 1)]
    weight_layout_dims = [(K // micro_s, micro_s * N), (N // micro_t, micro_t), (micro_s, N), (micro_t, 1)]
    layout_dims = [(M_tile // micro_r, micro_r * N), (N // micro_t, micro_t), (micro_r, N), (micro_t, 1)]

    # Tensor Types
    activation_flat_type = np.ndarray[(M * K,), np.dtype[bfloat16]]
    weight_all_flat_type = np.ndarray[(4 * K * N,), np.dtype[bfloat16]]
    output_full_flat_type = np.ndarray[(M * N,), np.dtype[bfloat16]]
    activation_tile_type = np.ndarray[(M_tile, K), np.dtype[bfloat16]]
    weight_full_type = np.ndarray[(K, N), np.dtype[bfloat16]]
    output_full_type = np.ndarray[(M, N), np.dtype[bfloat16]]
    output_tile_type = np.ndarray[(M_tile, N), np.dtype[bfloat16]]
    type_int32_16x64 = np.ndarray[(16, 64), np.dtype[np.int32]]
    type_int32_M_tilexN = np.ndarray[(M_tile, N), np.dtype[np.int32]]

    # Data Movement
    # Object Fifos
    of_inA = ObjectFifo(obj_type=activation_flat_type, depth=2, name="of_inA")
    of_W0 = ObjectFifo(obj_type=weight_full_type, depth=1, name="of_W0")
    of_W1 = ObjectFifo(obj_type=weight_full_type, depth=1, name="of_W1")
    of_W2 = ObjectFifo(obj_type=weight_full_type, depth=1, name="of_W2")
    of_W3 = ObjectFifo(obj_type=weight_full_type, depth=1, name="of_W3")
    of_outC = ObjectFifo(obj_type=output_full_type, depth=2, name="of_outC")
    h01_0 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_0")
    h12_0 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h12_0")
    h23_0 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h23_0")
    h01_1 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_1")
    h12_1 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h12_1")
    h23_1 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h23_1")
    h01_2 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_2")
    h12_2 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h12_2")
    h23_2 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h23_2")
    h01_3 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h01_3")
    h12_3 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h12_3")
    h23_3 = ObjectFifo(obj_type=output_tile_type, depth=2, name="h23_3")
    # Splits
    split_inA = of_inA.cons().split(dims_to_stream=[activation_layout_dims, activation_layout_dims, activation_layout_dims, activation_layout_dims], names=["split_inA_out1", "split_inA_out2", "split_inA_out3", "split_inA_out4"], obj_types=[activation_tile_type, activation_tile_type, activation_tile_type, activation_tile_type], offsets=[0, 1024, 2048, 3072], placement=Tile(0, 1))
    # Joins
    join1 = of_outC.prod().join(names=["join1_in1", "join1_in2", "join1_in3", "join1_in4"], obj_types=[output_tile_type, output_tile_type, output_tile_type, output_tile_type], offsets=[0, 1024, 2048, 3072], placement=Tile(0, 1))
    # Broadcasts
    weights_col0_broadcast = of_W0.cons().forward(dims_to_stream=weight_layout_dims, placement=Tile(0, 1))
    weights_col1_broadcast = of_W1.cons().forward(dims_to_stream=weight_layout_dims, placement=Tile(1, 1))
    weights_col2_broadcast = of_W2.cons().forward(dims_to_stream=weight_layout_dims, placement=Tile(2, 1))
    weights_col3_broadcast = of_W3.cons().forward(dims_to_stream=weight_layout_dims, placement=Tile(3, 1))
    h01_3_fwd = h01_3.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(1, 1))
    h01_2_fwd = h01_2.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(1, 1))
    h01_1_fwd = h01_1.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(1, 1))
    h01_0_fwd = h01_0.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(1, 1))
    h12_3_fwd = h12_3.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(2, 1))
    h12_2_fwd = h12_2.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(2, 1))
    h12_1_fwd = h12_1.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(2, 1))
    h12_0_fwd = h12_0.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(2, 1))
    h23_3_fwd = h23_3.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(3, 1))
    h23_2_fwd = h23_2.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(3, 1))
    h23_1_fwd = h23_1.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(3, 1))
    h23_0_fwd = h23_0.cons(dims_from_stream=layout_dims).forward(dims_to_stream=activation_layout_dims, placement=Tile(3, 1))

    # Compute Kernels
    build_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
    mm_archive = os.path.join(build_dir, "mm.a")
    gemm_bf16_archive = os.path.join(build_dir, "gemm_bf16.a")
    relu_archive = os.path.join(build_dir, "relu.a")
    softmax_archive = os.path.join(build_dir, "softmax.a")
    kernel_zero_i16 = Kernel("zero_i16", mm_archive, [type_int16_64])

    kernel_gemm_bf16 = Kernel("gemm_bf16", gemm_bf16_archive, [type_bf16_256, type_bf16_64, type_bf16_64])

    kernel_relu_bf16 = Kernel("bf16_relu", relu_archive, [type_bf16_256])

    kernel_softmax_bf16 = Kernel("softmax_bf16", softmax_archive, [type_bfloat16_256, type_bfloat16_256])

    # Core Body Functions
    def core_shared_matmul_relu(relu, matmul, zero, activation, weights, output):
        buf_in_act = activation.acquire(1)
        buf_in_w = weights.acquire(1)
        buf_out0 = output.acquire(1)
        zero(buf_out0)
        matmul(buf_in_act, buf_in_w, buf_out0)
        relu(buf_out0, buf_out0)
        activation.release(1)
        weights.release(1)
        output.release(1)

    def core_shared_softmax_matmul(zero, matmul, softmax, activation, weights, out0):
        buf_in_act = activation.acquire(1)
        buf_in_w = weights.acquire(1)
        buf_out0 = out0.acquire(1)
        zero(buf_out0)
        matmul(buf_in_act, buf_in_w, buf_out0)
        softmax(buf_out0)
        activation.release(1)
        weights.release(1)
        out0.release(1)

    # Workers
    Workers = []
    worker_aie0_2 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, split_inA[0].cons(), weights_col0_broadcast.cons(), h01_3.prod()], placement=Tile(0, 2))
    worker_aie1_2 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, h01_3_fwd.cons(), weights_col1_broadcast.cons(), h12_3.prod()], placement=Tile(1, 2))
    worker_aie2_2 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, h12_3_fwd.cons(), weights_col2_broadcast.cons(), h23_3.prod()], placement=Tile(2, 2))
    worker_aie3_2 = Worker(core_fn=core_shared_softmax_matmul, fn_args=[kernel_zero_i16, kernel_gemm_bf16, kernel_softmax_bf16, h23_3_fwd.cons(), weights_col3_broadcast.cons(), join1[0].prod()], placement=Tile(3, 2))
    worker_aie0_3 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, split_inA[1].cons(), weights_col0_broadcast.cons(), h01_2.prod()], placement=Tile(0, 3))
    worker_aie1_3 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, h01_2_fwd.cons(), weights_col1_broadcast.cons(), h12_2.prod()], placement=Tile(1, 3))
    worker_aie2_3 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, h12_2_fwd.cons(), weights_col2_broadcast.cons(), h23_2.prod()], placement=Tile(2, 3))
    worker_aie3_3 = Worker(core_fn=core_shared_softmax_matmul, fn_args=[kernel_zero_i16, kernel_gemm_bf16, kernel_softmax_bf16, h23_2_fwd.cons(), weights_col3_broadcast.cons(), join1[1].prod()], placement=Tile(3, 3))
    worker_aie0_4 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, split_inA[2].cons(), weights_col0_broadcast.cons(), h01_1.prod()], placement=Tile(0, 4))
    worker_aie1_4 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, h01_1_fwd.cons(), weights_col1_broadcast.cons(), h12_1.prod()], placement=Tile(1, 4))
    worker_aie2_4 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, h12_1_fwd.cons(), weights_col2_broadcast.cons(), h23_1.prod()], placement=Tile(2, 4))
    worker_aie3_4 = Worker(core_fn=core_shared_softmax_matmul, fn_args=[kernel_zero_i16, kernel_gemm_bf16, kernel_softmax_bf16, h23_1_fwd.cons(), weights_col3_broadcast.cons(), join1[2].prod()], placement=Tile(3, 4))
    worker_aie0_5 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, split_inA[3].cons(), weights_col0_broadcast.cons(), h01_0.prod()], placement=Tile(0, 5))
    worker_aie1_5 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, h01_0_fwd.cons(), weights_col1_broadcast.cons(), h12_0.prod()], placement=Tile(1, 5))
    worker_aie2_5 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_relu_bf16, kernel_gemm_bf16, kernel_zero_i16, h12_0_fwd.cons(), weights_col2_broadcast.cons(), h23_0.prod()], placement=Tile(2, 5))
    worker_aie3_5 = Worker(core_fn=core_shared_softmax_matmul, fn_args=[kernel_zero_i16, kernel_gemm_bf16, kernel_softmax_bf16, h23_0_fwd.cons(), weights_col3_broadcast.cons(), join1[3].prod()], placement=Tile(3, 5))

    Workers = [worker_aie0_2, worker_aie1_2, worker_aie2_2, worker_aie3_2, worker_aie0_3, worker_aie1_3, worker_aie2_3, worker_aie3_3, worker_aie0_4, worker_aie1_4, worker_aie2_4, worker_aie3_4, worker_aie0_5, worker_aie1_5, worker_aie2_5, worker_aie3_5]

    # Tensor Access Patterns (TAPs)
    tap_act = TensorAccessPattern(tensor_dims=[4096], offset=0, sizes=[1, 4096], strides=[4096, 1])
    tap_out = TensorAccessPattern(tensor_dims=[4096], offset=0, sizes=[1, 4096], strides=[4096, 1])
    tap_w_0 = TensorAccessPattern(tensor_dims=[16384], offset=0, sizes=[1, 4096], strides=[4096, 1])
    tap_w_1 = TensorAccessPattern(tensor_dims=[16384], offset=4096, sizes=[1, 4096], strides=[4096, 1])
    tap_w_2 = TensorAccessPattern(tensor_dims=[16384], offset=8192, sizes=[1, 4096], strides=[4096, 1])
    tap_w_3 = TensorAccessPattern(tensor_dims=[16384], offset=12288, sizes=[1, 4096], strides=[4096, 1])

    # Runtime
    rt = Runtime()
    with rt.sequence(activation_flat_type, weight_all_flat_type, output_full_flat_type) as (input_a_in, w_in, outputc_out):
        # Start Workers
        rt.start(*Workers)
        # Fills
        rt.fill(placement=Tile(0, 0), in_fifo=of_inA.prod(), source=input_a_in, tap=tap_act)
        rt.fill(placement=Tile(0, 0), in_fifo=of_W0.prod(), source=w_in, tap=tap_w_0)
        rt.fill(placement=Tile(1, 0), in_fifo=of_W1.prod(), source=w_in, tap=tap_w_1)
        rt.fill(placement=Tile(2, 0), in_fifo=of_W2.prod(), source=w_in, tap=tap_w_2)
        rt.fill(placement=Tile(3, 0), in_fifo=of_W3.prod(), source=w_in, tap=tap_w_3)
        # Drains
        rt.drain(placement=Tile(0, 0), out_fifo=of_outC.cons(), dest=outputc_out, wait=True, tap=tap_out)

    # Program
    my_program = Program(iron.get_current_device(), rt)

    # Placement
    return my_program.resolve_program(SequentialPlacer())


def main():
    N = 64
    M = 64
    K = 64
    n_tiles = 4
    M_tile = 16
    micro_r = 4
    const = 1
    micro_s = 8
    micro_t = 4
    activation_layout_dims = [(M_tile // micro_r, micro_r * K), (K // micro_s, micro_s), (micro_r, K), (micro_s, 1)]
    weight_layout_dims = [(K // micro_s, micro_s * N), (N // micro_t, micro_t), (micro_s, N), (micro_t, 1)]
    layout_dims = [(M_tile // micro_r, micro_r * N), (N // micro_t, micro_t), (micro_r, N), (micro_t, 1)]
    Input_A = iron.arange(M * K, dtype=bfloat16, device="npu")
    W = iron.arange(4 * K * N, dtype=bfloat16, device="npu")
    OutputC = iron.zeros(M * N, dtype=bfloat16, device="npu")
    gui_design_jit(Input_A, W, OutputC)



if __name__ == "__main__":
    main()