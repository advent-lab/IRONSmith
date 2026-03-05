# vector_vector_mul_hlir_example.py -*- Python -*-


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
def vector_vector_mul_hlir_example_jit(inputA, inputB, outputC):
    # Define tensor types
    data_ty = np.ndarray[(inputA.numel(),), np.dtype[bfloat16]]
    memtile_ty = np.ndarray[(inputA.numel() // 16,), np.dtype[bfloat16]]
    tile_ty = np.ndarray[(inputA.numel() // 64,), np.dtype[bfloat16]]
    data_a_ty = np.ndarray[(inputA.numel(),), np.dtype[bfloat16]]
    data_b_ty = np.ndarray[(inputB.numel(),), np.dtype[bfloat16]]
    data_c_ty = np.ndarray[(outputC.numel(),), np.dtype[bfloat16]]

    # Data movement with ObjectFifos
    of_in_a = ObjectFifo(obj_type=memtile_ty, depth=2, name="of_in_a")
    of_in_b = ObjectFifo(obj_type=memtile_ty, depth=2, name="of_in_b")
    of_out_c = ObjectFifo(obj_type=memtile_ty, depth=2, name="of_out_c")
    MEM_L2_L1_A1A2A3A4_col0 = of_in_a.cons().split(obj_types=[tile_ty, tile_ty, tile_ty, tile_ty], offsets=[((inputA.numel() // 64) * 0), ((inputA.numel() // 64) * 1), ((inputA.numel() // 64) * 2), ((inputA.numel() // 64) * 3)], names=["MEM_L2_L1_A1_col0", "MEM_L2_L1_A2_col0", "MEM_L2_L1_A3_col0", "MEM_L2_L1_A4_col0"], placement=Tile(0, 1))
    MEM_L2_L1_B5B6B7B8_col1 = of_in_b.cons().split(obj_types=[tile_ty, tile_ty, tile_ty, tile_ty], offsets=[((inputB.numel() // 64) * 0), ((inputB.numel() // 64) * 1), ((inputB.numel() // 64) * 2), ((inputB.numel() // 64) * 3)], names=["MEM_L2_L1_B5_col1", "MEM_L2_L1_B6_col1", "MEM_L2_L1_B7_col1", "MEM_L2_L1_B8_col1"], placement=Tile(1, 1))
    MEM_L1_L2_C9C10C11C12_col2 = of_out_c.prod().join(obj_types=[tile_ty, tile_ty, tile_ty, tile_ty], names=["MEM_L1_L2_C9_col2", "MEM_L1_L2_C10_col2", "MEM_L1_L2_C11_col2", "MEM_L1_L2_C12_col2"], placement=Tile(2, 1), offsets=[((outputC.numel() // 64) * 0), ((outputC.numel() // 64) * 1), ((outputC.numel() // 64) * 2), ((outputC.numel() // 64) * 3)])

    #Define kernels here... ------------------------------------------------\/
    eltwise_mul_bf16_vector = ExternalFunction(
        name="eltwise_mul_bf16_vector", source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/mul.cc", arg_types=[tile_ty, tile_ty, tile_ty], include_dirs=["/scratch/IRONSmithTesting/mlir-aie/aie_kernels", "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"]
    )

    # core_fn here:
    def corefunc_mul(kernel, inputA, inputB, outputC):
            for _ in range_(((65536) // 4096)):
                elem_out = outputC.acquire(1)
                elem_in_a = inputA.acquire(1)
                elem_in_b = inputB.acquire(1)
                kernel(elem_in_a, elem_in_b, elem_out)
                inputA.release(1)
                inputB.release(1)
                outputC.release(1)

    #Workers defined here:
    Workers = []
    worker0 = Worker(core_fn=corefunc_mul, fn_args=[eltwise_mul_bf16_vector, MEM_L2_L1_A1A2A3A4_col0[0].cons(), MEM_L2_L1_B5B6B7B8_col1[0].cons(), MEM_L1_L2_C9C10C11C12_col2[0].prod()], placement=Tile(0, 5))
    worker1 = Worker(core_fn=corefunc_mul, fn_args=[eltwise_mul_bf16_vector, MEM_L2_L1_A1A2A3A4_col0[1].cons(), MEM_L2_L1_B5B6B7B8_col1[1].cons(), MEM_L1_L2_C9C10C11C12_col2[1].prod()], placement=Tile(0, 4))
    worker2 = Worker(core_fn=corefunc_mul, fn_args=[eltwise_mul_bf16_vector, MEM_L2_L1_A1A2A3A4_col0[2].cons(), MEM_L2_L1_B5B6B7B8_col1[2].cons(), MEM_L1_L2_C9C10C11C12_col2[2].prod()], placement=Tile(0, 3))
    worker3 = Worker(core_fn=corefunc_mul, fn_args=[eltwise_mul_bf16_vector, MEM_L2_L1_A1A2A3A4_col0[3].cons(), MEM_L2_L1_B5B6B7B8_col1[3].cons(), MEM_L1_L2_C9C10C11C12_col2[3].prod()], placement=Tile(0, 2))

    Workers = [worker0, worker1, worker2, worker3]

    # Runtime operations to move data to/from the AIE-array
    rt = Runtime()
    with rt.sequence(memtile_ty, memtile_ty, memtile_ty) as (inputa_in, inputb_in, outputc_out):
        rt.start(*Workers)
        rt.fill(of_in_a.prod(), inputa_in, placement=Tile(0, 0))
        rt.fill(of_in_b.prod(), inputb_in, placement=Tile(0, 0))
        rt.drain(of_out_c.cons(), outputc_out, wait=True, placement=Tile(1, 0))

    # Create the program from the current device and runtime
    my_program = Program(iron.get_current_device(), rt)

    # Place components and resolve program (generate MLIR + compile)
    return my_program.resolve_program(SequentialPlacer())


def main():
    N = 65536
    inputA = iron.arange(N, dtype=bfloat16, device="npu")
    inputB = iron.arange(N, dtype=bfloat16, device="npu")
    outputC = iron.zeros(N, dtype=bfloat16, device="npu")
    vector_vector_mul_hlir_example_jit(inputA, inputB, outputC)



if __name__ == "__main__":
    main()