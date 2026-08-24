# vector_vector_mul_test.py -*- Python -*-


import numpy as np
from ml_dtypes import bfloat16

from aie.iron import Program, Runtime, Worker, ObjectFifo
from aie.iron.device.tile import AnyComputeTile
from aie.iron import ExternalFunction, jit, In, Out, InOut, CompileTime
from aie.iron.dataflow import ObjectFifoLink
from aie.iron.device import Tile
from aie.iron.device import NPU1Col1, NPU2Col1, XCVC1902
import aie.iron as iron

from aie.helpers.taplib import TensorAccessPattern
from aie.iron.controlflow import range_


@iron.jit
def vector_vector_mul_test_jit(inputA: In, inputB: In, outputC: Out, *, N: CompileTime[int]):
    # Tensor Types
    data_ty = np.ndarray[(N,), np.dtype[bfloat16]]
    memtile_ty = np.ndarray[(N // 16,), np.dtype[bfloat16]]
    tile_ty = np.ndarray[(N // 64,), np.dtype[bfloat16]]
    data_a_ty = np.ndarray[(N,), np.dtype[bfloat16]]
    data_b_ty = np.ndarray[(N,), np.dtype[bfloat16]]
    data_c_ty = np.ndarray[(N,), np.dtype[bfloat16]]

    # Data Movement
    # Object Fifos
    of_in_a = ObjectFifo(obj_type=memtile_ty, depth=2, name="of_in_a")
    of_in_b = ObjectFifo(obj_type=memtile_ty, depth=2, name="of_in_b")
    of_out_c = ObjectFifo(obj_type=memtile_ty, depth=2, name="of_out_c")
    # Splits
    MEM_L2_L1_A1A2A3A4_col0 = of_in_a.cons().split(names=["MEM_L2_L1_A1_col0", "MEM_L2_L1_A2_col0", "MEM_L2_L1_A3_col0", "MEM_L2_L1_A4_col0"], obj_types=[tile_ty, tile_ty, tile_ty, tile_ty], offsets=[0, 1024, 2048, 3072], tile=Tile(0, 1))
    MEM_L2_L1_B5B6B7B8_col1 = of_in_b.cons().split(names=["MEM_L2_L1_B5_col1", "MEM_L2_L1_B6_col1", "MEM_L2_L1_B7_col1", "MEM_L2_L1_B8_col1"], obj_types=[tile_ty, tile_ty, tile_ty, tile_ty], offsets=[0, 1024, 2048, 3072], tile=Tile(1, 1))
    # Joins
    MEM_L1_L2_C9C10C11C12_col2 = of_out_c.prod().join(names=["MEM_L1_L2_C9_col2", "MEM_L1_L2_C10_col2", "MEM_L1_L2_C11_col2", "MEM_L1_L2_C12_col2"], obj_types=[tile_ty, tile_ty, tile_ty, tile_ty], offsets=[0, 1024, 2048, 3072], tile=Tile(2, 1))

    # Compute Kernels
    eltwise_mul_bf16_vector = ExternalFunction(
        name="eltwise_mul_bf16_vector", source_file="/home/mliraie/mlir-aie/aie_kernels/aie2/mul.cc", arg_types=[tile_ty, tile_ty, tile_ty], include_dirs=["/home/mliraie/mlir-aie/aie_kernels", "/home/mliraie/mlir-aie/aie_runtime_lib/AIE2", "/home/mliraie/mlir-aie/aie_kernels/aie2"]
    )

    # Core Body Functions
    def corefunc_mul(kernel, inputA, inputB, outputC):
        for _ in range_(((65536) // 4096)):
            elem_out = outputC.acquire(1)
            elem_in_a = inputA.acquire(1)
            elem_in_b = inputB.acquire(1)
            kernel(elem_in_a, elem_in_b, elem_out)
            inputA.release(1)
            inputB.release(1)
            outputC.release(1)

    # Workers
    Workers = []
    worker0 = Worker(core_fn=corefunc_mul, fn_args=[eltwise_mul_bf16_vector, MEM_L2_L1_A1A2A3A4_col0[0].cons(), MEM_L2_L1_B5B6B7B8_col1[0].cons(), MEM_L1_L2_C9C10C11C12_col2[0].prod()], tile=Tile(0, 5))
    worker1 = Worker(core_fn=corefunc_mul, fn_args=[eltwise_mul_bf16_vector, MEM_L2_L1_A1A2A3A4_col0[1].cons(), MEM_L2_L1_B5B6B7B8_col1[1].cons(), MEM_L1_L2_C9C10C11C12_col2[1].prod()], tile=Tile(0, 4))
    worker2 = Worker(core_fn=corefunc_mul, fn_args=[eltwise_mul_bf16_vector, MEM_L2_L1_A1A2A3A4_col0[2].cons(), MEM_L2_L1_B5B6B7B8_col1[2].cons(), MEM_L1_L2_C9C10C11C12_col2[2].prod()], tile=Tile(0, 3))
    worker3 = Worker(core_fn=corefunc_mul, fn_args=[eltwise_mul_bf16_vector, MEM_L2_L1_A1A2A3A4_col0[3].cons(), MEM_L2_L1_B5B6B7B8_col1[3].cons(), MEM_L1_L2_C9C10C11C12_col2[3].prod()], tile=Tile(0, 2))

    Workers = [worker0, worker1, worker2, worker3]

    # Runtime
    def sequence(inputa_in, inputb_in, outputc_out, in_a, in_b, out_c):
        # Fills
        in_a.fill(inputa_in)
        in_b.fill(inputb_in)
        # Drains
        out_c.drain(outputc_out, wait=True)

    rt = Runtime(sequence, [
        data_ty,
        data_ty,
        data_ty,
        of_in_a.prod(tile=Tile(0, 0)),
        of_in_b.prod(tile=Tile(0, 0)),
        of_out_c.cons(tile=Tile(1, 0)),
    ])

    # Program
    my_program = Program(iron.get_current_device(), rt, workers=Workers)

    return my_program.resolve_program()


def main():
    N = 65536
    inputA = iron.arange(N, dtype=bfloat16, device="npu")
    inputB = iron.arange(N, dtype=bfloat16, device="npu")
    outputC = iron.zeros(N, dtype=bfloat16, device="npu")
    inputA.data[:] = inputA.data[:] % 8
    inputA._sync_to_device()
    inputB.data[:] = inputB.data[:] % 8
    inputB._sync_to_device()

    print(f"inputA[:8] = {inputA.numpy()[:8]}")
    print(f"inputB[:8] = {inputB.numpy()[:8]}")

    vector_vector_mul_test_jit(inputA, inputB, outputC, N=N)

    print(f"outputC[:8] = {outputC.numpy()[:8]}")
    print(f"inputA[:8] = {inputA.numpy()[:8]}")
    print(f"inputB[:8] = {inputB.numpy()[:8]}")
    print(f"outputC[:8] = {outputC.numpy()[:8]}")
    expected = inputA.numpy().astype(np.float32) * inputB.numpy().astype(np.float32)
    print(f"expected[:8] = {expected[:8]}")
    print("PASS: outputC matches inputA * inputB" if np.allclose(outputC.numpy().astype(np.float32), expected, atol=1e-1, rtol=1e-1) else "FAIL: outputC does not match inputA * inputB")



if __name__ == "__main__":
    main()