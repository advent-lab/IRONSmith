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
def gui_design_jit(input_activation, weight_layer0, weight_layer1, weight_layer2, weight_layer3, output_activation):
    # Tensor Types
    output_type = np.ndarray[(256,), np.dtype[np.float16]]
    type_int16_256 = np.ndarray[(256,), np.dtype[np.int16]]
    type_bfloat16_256 = np.ndarray[(256,), np.dtype[bfloat16]]
    type_int16_64 = np.ndarray[(64,), np.dtype[np.int16]]
    type_bfloat16_64 = np.ndarray[(64,), np.dtype[bfloat16]]
    type_int16_256x256 = np.ndarray[(256, 256), np.dtype[np.int16]]

    # Data Movement
    # Object Fifos
    input_activation_fifo = ObjectFifo(obj_type=type_int16_256, depth=2, name="input_activation_fifo")
    inter_activation_fifo1 = ObjectFifo(obj_type=type_int16_256, depth=2, name="inter_activation_fifo1")
    weights_col0_fifo = ObjectFifo(obj_type=type_int16_256, depth=2, name="weights_col0_fifo")
    weights_col1_fifo = ObjectFifo(obj_type=type_int16_256, depth=2, name="weights_col1_fifo")
    weights_col2_fifo = ObjectFifo(obj_type=type_int16_256, depth=2, name="weights_col2_fifo")
    weights_col3_fifo = ObjectFifo(obj_type=type_int16_256, depth=2, name="weights_col3_fifo")
    output_activation_fifo = ObjectFifo(obj_type=type_bfloat16_256, depth=2, name="output_activation_fifo")
    inter_activation_fifo2 = ObjectFifo(obj_type=type_int16_256, depth=2, name="inter_activation_fifo2")
    inter_activation_fifo3 = ObjectFifo(obj_type=type_int16_256, depth=2, name="inter_activation_fifo3")
    # Splits
    weight_tile_split_col0 = weights_col0_fifo.cons().split(names=["weight_tile_split_col0_out1", "weight_tile_split_col0_out2", "weight_tile_split_col0_out3", "weight_tile_split_col0_out4"], obj_types=[type_int16_64, type_int16_64, type_int16_64, type_int16_64], offsets=[0, 64, 128, 192], placement=Tile(0, 1))
    weight_tile_split_col1 = weights_col1_fifo.cons().split(names=["weight_tile_split_col1_out1", "weight_tile_split_col1_out2", "weight_tile_split_col1_out3", "weight_tile_split_col1_out4"], obj_types=[type_int16_64, type_int16_64, type_int16_64, type_int16_64], offsets=[0, 64, 128, 192], placement=Tile(1, 1))
    weight_tile_split_col2 = weights_col2_fifo.cons().split(names=["weight_tile_split_col2_out1", "weight_tile_split_col2_out2", "weight_tile_split_col2_out3", "weight_tile_split_col2_out4"], obj_types=[type_int16_64, type_int16_64, type_int16_64, type_int16_64], offsets=[0, 64, 128, 192], placement=Tile(2, 1))
    weight_tile_split_col3 = weights_col3_fifo.cons().split(names=["weight_tile_split_col3_out1", "weight_tile_split_col3_out2", "weight_tile_split_col3_out3", "weight_tile_split_col3_out4"], obj_types=[type_int16_64, type_int16_64, type_int16_64, type_int16_64], offsets=[0, 64, 128, 192], placement=Tile(3, 1))
    # Joins
    activation_tile_join_col0 = inter_activation_fifo1.prod().join(names=["activation_tile_join_col0_in1", "activation_tile_join_col0_in2", "activation_tile_join_col0_in3", "activation_tile_join_col0_in4"], obj_types=[type_int16_64, type_int16_64, type_int16_64, type_int16_64], offsets=[0, 64, 128, 192], placement=Tile(0, 1))
    activation_tile_join_col1 = inter_activation_fifo2.prod().join(names=["activation_tile_join_col1_in1", "activation_tile_join_col1_in2", "activation_tile_join_col1_in3", "activation_tile_join_col1_in4"], obj_types=[type_int16_64, type_int16_64, type_int16_64, type_int16_64], offsets=[0, 64, 128, 192], placement=Tile(1, 1))
    activation_tile_join_col2 = inter_activation_fifo3.prod().join(names=["activation_tile_join_col2_in1", "activation_tile_join_col2_in2", "activation_tile_join_col2_in3", "activation_tile_join_col2_in4"], obj_types=[type_int16_64, type_int16_64, type_int16_64, type_int16_64], offsets=[0, 64, 128, 192], placement=Tile(2, 1))
    activation_tile_join_col3 = output_activation_fifo.prod().join(names=["activation_tile_join_col3_in1", "activation_tile_join_col3_in2", "activation_tile_join_col3_in3", "activation_tile_join_col3_in4"], obj_types=[type_bfloat16_64, type_bfloat16_64, type_bfloat16_64, type_bfloat16_64], offsets=[0, 64, 128, 192], placement=Tile(3, 1))
    # Broadcasts
    input_activation_col0_fifo = input_activation_fifo.cons().forward(placement=Tile(0, 1))

    # Compute Kernels
    build_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
    mm_archive = os.path.join(build_dir, "mm.a")
    softmax_archive = os.path.join(build_dir, "softmax.a")
    kernel_matmul_i16_i16 = Kernel("matmul_i16_i16", mm_archive, [type_int16_256, type_int16_64, type_int16_64])

    kernel_relu_i16 = Kernel("relu_i16", mm_archive, [type_int16_64])

    kernel_zero_i16 = Kernel("zero_i16", mm_archive, [type_int16_64])

    kernel_softmax_bf16 = Kernel("softmax_bf16", softmax_archive, [type_bf16_256, type_bf16_256])

    # Core Body Functions
    def core_shared_matmul_relu(Kernel1, Kernel2, Kernel3, in0, in1, out0):
        buf_in0 = in0.acquire(1)
        buf_in1 = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        Kernel1(buf_in0, buf_in1, buf_out0)
        Kernel2(buf_in0, buf_in1, buf_out0)
        Kernel3(buf_in0, buf_in1, buf_out0)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    def core_shared_matmul_softmax(Kernel1, Kernel2, Kernel3, in0, in1, out0):
        buf_in0 = in0.acquire(1)
        buf_in1 = in1.acquire(1)
        buf_out0 = out0.acquire(1)
        Kernel1(buf_in0, buf_in1, buf_out0)
        Kernel2(buf_in0, buf_in1, buf_out0)
        Kernel3(buf_in0, buf_in1, buf_out0)
        in0.release(1)
        in1.release(1)
        out0.release(1)

    # Workers
    Workers = []
    worker_aie0_2 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, input_activation_col0_fifo.cons(), weight_tile_split_col0[0].cons(), activation_tile_join_col0[0].prod()], placement=Tile(0, 2))
    worker_aie1_2 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, inter_activation_fifo1.cons(), weight_tile_split_col1[0].cons(), activation_tile_join_col1[0].prod()], placement=Tile(1, 2))
    worker_aie2_2 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, inter_activation_fifo2.cons(), weight_tile_split_col2[0].cons(), activation_tile_join_col2[0].prod()], placement=Tile(2, 2))
    worker_aie3_2 = Worker(core_fn=core_shared_matmul_softmax, fn_args=[kernel_matmul_i16_i16, kernel_zero_i16, kernel_softmax_bf16, inter_activation_fifo3.cons(), weight_tile_split_col3[0].cons(), activation_tile_join_col3[0].prod()], placement=Tile(3, 2))
    worker_aie0_3 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, input_activation_col0_fifo.cons(), weight_tile_split_col0[1].cons(), activation_tile_join_col0[1].prod()], placement=Tile(0, 3))
    worker_aie1_3 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, inter_activation_fifo1.cons(), weight_tile_split_col1[1].cons(), activation_tile_join_col1[1].prod()], placement=Tile(1, 3))
    worker_aie2_3 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, inter_activation_fifo2.cons(), weight_tile_split_col2[1].cons(), activation_tile_join_col2[1].prod()], placement=Tile(2, 3))
    worker_aie3_3 = Worker(core_fn=core_shared_matmul_softmax, fn_args=[kernel_matmul_i16_i16, kernel_zero_i16, kernel_softmax_bf16, inter_activation_fifo3.cons(), weight_tile_split_col3[1].cons(), activation_tile_join_col3[1].prod()], placement=Tile(3, 3))
    worker_aie0_4 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, input_activation_col0_fifo.cons(), weight_tile_split_col0[2].cons(), activation_tile_join_col0[2].prod()], placement=Tile(0, 4))
    worker_aie1_4 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, inter_activation_fifo1.cons(), weight_tile_split_col1[2].cons(), activation_tile_join_col1[2].prod()], placement=Tile(1, 4))
    worker_aie2_4 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, inter_activation_fifo2.cons(), weight_tile_split_col2[2].cons(), activation_tile_join_col2[2].prod()], placement=Tile(2, 4))
    worker_aie3_4 = Worker(core_fn=core_shared_matmul_softmax, fn_args=[kernel_matmul_i16_i16, kernel_zero_i16, kernel_softmax_bf16, inter_activation_fifo3.cons(), weight_tile_split_col3[2].cons(), activation_tile_join_col3[2].prod()], placement=Tile(3, 4))
    worker_aie0_5 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, input_activation_col0_fifo.cons(), weight_tile_split_col0[3].cons(), activation_tile_join_col0[3].prod()], placement=Tile(0, 5))
    worker_aie1_5 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, inter_activation_fifo1.cons(), weight_tile_split_col1[3].cons(), activation_tile_join_col1[3].prod()], placement=Tile(1, 5))
    worker_aie2_5 = Worker(core_fn=core_shared_matmul_relu, fn_args=[kernel_matmul_i16_i16, kernel_relu_i16, kernel_zero_i16, inter_activation_fifo2.cons(), weight_tile_split_col2[3].cons(), activation_tile_join_col2[3].prod()], placement=Tile(2, 5))
    worker_aie3_5 = Worker(core_fn=core_shared_matmul_softmax, fn_args=[kernel_matmul_i16_i16, kernel_zero_i16, kernel_softmax_bf16, inter_activation_fifo3.cons(), weight_tile_split_col3[3].cons(), activation_tile_join_col3[3].prod()], placement=Tile(3, 5))

    Workers = [worker_aie0_2, worker_aie1_2, worker_aie2_2, worker_aie3_2, worker_aie0_3, worker_aie1_3, worker_aie2_3, worker_aie3_3, worker_aie0_4, worker_aie1_4, worker_aie2_4, worker_aie3_4, worker_aie0_5, worker_aie1_5, worker_aie2_5, worker_aie3_5]

    # Runtime
    rt = Runtime()
    with rt.sequence(type_int16_256x256, type_int16_256, type_int16_256, type_int16_256, type_int16_256, type_bfloat16_256) as (input_activation_in, weight_layer0_in, weight_layer1_in, weight_layer2_in, weight_layer3_in, output_activation_out):
        # Start Workers
        rt.start(*Workers)
        # Fills
        rt.fill(input_activation_fifo.prod(), input_activation_in, placement=Tile(0, 0))
        rt.fill(weights_col0_fifo.prod(), weight_layer0_in, placement=Tile(0, 0))
        rt.fill(weights_col1_fifo.prod(), weight_layer1_in, placement=Tile(1, 0))
        rt.fill(weights_col2_fifo.prod(), weight_layer2_in, placement=Tile(2, 0))
        rt.fill(weights_col3_fifo.prod(), weight_layer3_in, placement=Tile(3, 0))
        # Drains
        rt.drain(output_activation_fifo.cons(), output_activation_out, wait=True, placement=Tile(3, 0))

    # Program
    my_program = Program(iron.get_current_device(), rt)

    # Placement
    return my_program.resolve_program(SequentialPlacer())


def main():
    input_activation = iron.zeros(256 * 256, dtype=np.int16, device="npu")
    input_activation.data[:] = np.arange(256 * 256, dtype=np.int16)
    input_activation._sync_to_device()
    weight_layer0 = iron.arange(256, dtype=np.int16, device="npu")
    weight_layer1 = iron.arange(256, dtype=np.int16, device="npu")
    weight_layer2 = iron.arange(256, dtype=np.int16, device="npu")
    weight_layer3 = iron.arange(256, dtype=np.int16, device="npu")
    output_activation = iron.zeros(256, dtype=bfloat16, device="npu")
    gui_design_jit(input_activation, weight_layer0, weight_layer1, weight_layer2, weight_layer3, output_activation)



if __name__ == "__main__":
    main()