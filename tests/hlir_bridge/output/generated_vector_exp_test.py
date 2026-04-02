# vector_exp_test.py -*- Python -*-


import numpy as np
from ml_dtypes import bfloat16

from aie.iron import Program, Runtime, Worker, ObjectFifo
from aie.iron.placers import SequentialPlacer
from aie.iron.device.tile import AnyComputeTile
from aie.iron import ExternalFunction, jit
from aie.iron.dataflow import ObjectFifoLink
from aie.iron.device import Tile
from aie.iron.device import NPU1Col1, NPU2Col1, XCVC1902
import aie.iron as iron

from aie.helpers.taplib import TensorAccessPattern
from aie.iron.controlflow import range_


@iron.jit(is_placed=False)
def vector_exp_test_jit(inputA, outputC):
    # Constants
    N = 65536

    # Tensor Types
    data_ty = np.ndarray[(N,), np.dtype[bfloat16]]
    memtile_ty = np.ndarray[(N // 16,), np.dtype[bfloat16]]
    tile_ty = np.ndarray[(N // 64,), np.dtype[bfloat16]]
    data_a_ty = np.ndarray[(inputA.numel(),), np.dtype[bfloat16]]
    data_c_ty = np.ndarray[(outputC.numel(),), np.dtype[bfloat16]]

    # Data Movement
    # Object Fifos
    of_in_a = ObjectFifo(obj_type=memtile_ty, depth=2, name="of_in_a")
    of_out_c = ObjectFifo(obj_type=memtile_ty, depth=2, name="of_out_c")
    # Splits
    MEM_L2_L1_A1A2A3A4_col0 = of_in_a.cons().split(names=["MEM_L2_L1_A1_col0", "MEM_L2_L1_A2_col0", "MEM_L2_L1_A3_col0", "MEM_L2_L1_A4_col0"], obj_types=[tile_ty, tile_ty, tile_ty, tile_ty], offsets=[0, 1024, 2048, 3072], placement=Tile(0, 1))
    # Joins
    MEM_L1_L2_C1C2C3C4_col0 = of_out_c.prod().join(names=["MEM_L1_L2_C1_col0", "MEM_L1_L2_C2_col0", "MEM_L1_L2_C3_col0", "MEM_L1_L2_C4_col0"], obj_types=[tile_ty, tile_ty, tile_ty, tile_ty], offsets=[0, 1024, 2048, 3072], placement=Tile(0, 1))

    # Compute Kernels
    exp_bf16_1024 = ExternalFunction(
        name="exp_bf16_1024", source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/bf16_exp.cc", arg_types=[tile_ty, tile_ty], include_dirs=["/scratch/IRONSmithTesting/mlir-aie/aie_kernels", "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"]
    )

    # Core Body Functions
    def corefunc_exp(kernel, inputA, outputC):
        for _ in range_(((65536) // 4096)):
            elem_out = outputC.acquire(1)
            elem_in = inputA.acquire(1)
            kernel(elem_in, elem_out)
            inputA.release(1)
            outputC.release(1)

    # Workers
    Workers = []
    worker0 = Worker(core_fn=corefunc_exp, fn_args=[exp_bf16_1024, MEM_L2_L1_A1A2A3A4_col0[0].cons(), MEM_L1_L2_C1C2C3C4_col0[0].prod()], placement=Tile(0, 2))
    worker1 = Worker(core_fn=corefunc_exp, fn_args=[exp_bf16_1024, MEM_L2_L1_A1A2A3A4_col0[1].cons(), MEM_L1_L2_C1C2C3C4_col0[1].prod()], placement=Tile(0, 3))
    worker2 = Worker(core_fn=corefunc_exp, fn_args=[exp_bf16_1024, MEM_L2_L1_A1A2A3A4_col0[2].cons(), MEM_L1_L2_C1C2C3C4_col0[2].prod()], placement=Tile(0, 4))
    worker3 = Worker(core_fn=corefunc_exp, fn_args=[exp_bf16_1024, MEM_L2_L1_A1A2A3A4_col0[3].cons(), MEM_L1_L2_C1C2C3C4_col0[3].prod()], placement=Tile(0, 5))

    Workers = [worker0, worker1, worker2, worker3]

    # Runtime
    rt = Runtime()
    with rt.sequence(memtile_ty, memtile_ty) as (inputa_in, outputc_out):
        # Start Workers
        rt.start(*Workers)
        # Fills
        rt.fill(of_in_a.prod(), inputa_in, placement=Tile(0, 0))
        # Drains
        rt.drain(of_out_c.cons(), outputc_out, wait=True, placement=Tile(0, 0))

    # Program
    my_program = Program(iron.get_current_device(), rt)

    # Placement
    return my_program.resolve_program(SequentialPlacer())


def main():
    N = 65536
    inputA = iron.arange(N, dtype=bfloat16, device="npu")
    outputC = iron.zeros(N, dtype=bfloat16, device="npu")
    vector_exp_test_jit(inputA, outputC)



if __name__ == "__main__":
    main()