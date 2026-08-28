# matrix_vector_mul_test.py -*- Python -*-


import numpy as np
from ml_dtypes import bfloat16

from aie.iron import Program, Runtime, Worker, ObjectFifo
from aie.iron.device.tile import AnyComputeTile
from aie.iron import ExternalFunction, jit, In, Out, InOut, CompileTime
from aie.iron.dataflow import ObjectFifoLink
from aie.iron.device import Tile
from aie.iron.device import NPU1Col1, NPU2Col1, XCVC1902
import aie.iron as iron

from aie.helpers.taplib import TensorAccessPattern, TensorTiler2D
from aie.iron.controlflow import range_


@iron.jit
def matrix_vector_mul_test_jit(inputA: In, inputB: In, outputC: Out, *, M: CompileTime[int], K: CompileTime[int], m: CompileTime[int], k: CompileTime[int], n_cores: CompileTime[int]):
    # Constants
    M_div_m = M // m
    K_div_k = K // k
    rows_per_core = M_div_m // n_cores
    n_fifo_elems = rows_per_core * K_div_k
    A_elem_size = n_cores * m * k

    # Tensor Types
    inA_ty = np.ndarray[(m * k,), np.dtype[np.int16]]
    inB_ty = np.ndarray[(k,), np.dtype[np.int16]]
    outC_ty = np.ndarray[(m,), np.dtype[np.int32]]
    A_mem_ty = np.ndarray[(n_cores * m * k,), np.dtype[np.int16]]
    C_mem_ty = np.ndarray[(n_cores * m,), np.dtype[np.int32]]
    A_ty = np.ndarray[(n_fifo_elems, A_elem_size), np.dtype[np.int16]]
    B_ty = np.ndarray[(1, K), np.dtype[np.int16]]
    C_ty = np.ndarray[(1, M), np.dtype[np.int32]]

    # Data Movement
    # Object Fifos
    inA = ObjectFifo(obj_type=A_mem_ty, depth=2, name="inA")
    inB = ObjectFifo(obj_type=inB_ty, depth=2, name="inB")
    outC = ObjectFifo(obj_type=C_mem_ty, depth=2, name="outC")
    # Splits
    MEM_L2_L1_A1A2A3A4_col0 = inA.cons().split(names=["MEM_L2_L1_A1_col0", "MEM_L2_L1_A2_col0", "MEM_L2_L1_A3_col0", "MEM_L2_L1_A4_col0"], obj_types=[inA_ty, inA_ty, inA_ty, inA_ty], offsets=[0, 1024, 2048, 3072], tile=Tile(0, 1))
    # Joins
    MEM_L1_L2_C9C10C11C12_col2 = outC.prod().join(names=["MEM_L1_L2_C9_col2", "MEM_L1_L2_C10_col2", "MEM_L1_L2_C11_col2", "MEM_L1_L2_C12_col2"], obj_types=[outC_ty, outC_ty, outC_ty, outC_ty], offsets=[0, 32, 64, 96], tile=Tile(2, 1))
    # Broadcasts
    B_fwd = inB.cons().forward(tile=Tile(1, 1))

    # Compute Kernels
    matvec_vectorized_i16_i32 = ExternalFunction(
        name="matvec_vectorized_i16_i32", source_file="/home/mliraie/mlir-aie/aie_kernels/aie2/mv.cc", arg_types=[inA_ty, inB_ty, outC_ty], include_dirs=["/home/mliraie/mlir-aie/aie_kernels", "/home/mliraie/mlir-aie/aie_kernels/aie2", "/home/mliraie/mlir-aie/aie_runtime_lib/AIE2"]
    )

    # Core Body Functions
    def core_fn(a_in, b_in, c_out, matvec):
        elem_out = c_out.acquire(1)
        for i in range_(32):
            elem_out[i] = 0
        for _ in range_(K // k):
            elem_a = a_in.acquire(1)
            elem_b = b_in.acquire(1)
            matvec(elem_a, elem_b, elem_out)
            a_in.release(1)
            b_in.release(1)
        c_out.release(1)

    # Workers
    Workers = []
    worker0 = Worker(core_fn=core_fn, fn_args=[MEM_L2_L1_A1A2A3A4_col0[0].cons(), B_fwd.cons(), MEM_L1_L2_C9C10C11C12_col2[0].prod(), matvec_vectorized_i16_i32], tile=Tile(0, 2))
    worker1 = Worker(core_fn=core_fn, fn_args=[MEM_L2_L1_A1A2A3A4_col0[1].cons(), B_fwd.cons(), MEM_L1_L2_C9C10C11C12_col2[1].prod(), matvec_vectorized_i16_i32], tile=Tile(0, 3))
    worker2 = Worker(core_fn=core_fn, fn_args=[MEM_L2_L1_A1A2A3A4_col0[2].cons(), B_fwd.cons(), MEM_L1_L2_C9C10C11C12_col2[2].prod(), matvec_vectorized_i16_i32], tile=Tile(0, 4))
    worker3 = Worker(core_fn=core_fn, fn_args=[MEM_L2_L1_A1A2A3A4_col0[3].cons(), B_fwd.cons(), MEM_L1_L2_C9C10C11C12_col2[3].prod(), matvec_vectorized_i16_i32], tile=Tile(0, 5))

    Workers = [worker0, worker1, worker2, worker3]

    # Tensor Access Patterns (TAPs)
    a_tap = TensorTiler2D.group_tiler((rows_per_core * K_div_k, n_cores * m * k), (1, 512), (rows_per_core * K_div_k, A_elem_size // 512), prune_step=False)[0]
    b_tap = TensorTiler2D.group_tiler((1, 256), (1, 32), (1, K // k), pattern_repeat=M_div_m // n_cores, prune_step=False)[0]
    c_tap = TensorTiler2D.group_tiler((1, 256), (1, n_cores * m), (1, M_div_m // n_cores), prune_step=False)[0]

    # Runtime
    def sequence(inputa_in, inputb_in, outputc_out, inA, inB, outC):
        # Fills
        inA.fill(inputa_in, tap=a_tap)
        inB.fill(inputb_in, tap=b_tap)
        # Drains
        outC.drain(outputc_out, wait=True, tap=c_tap)

    rt = Runtime(sequence, [
        A_ty,
        B_ty,
        C_ty,
        inA.prod(tile=Tile(0, 0)),
        inB.prod(tile=Tile(1, 0)),
        outC.cons(tile=Tile(2, 0)),
    ])

    # Program
    my_program = Program(iron.get_current_device(), rt, workers=Workers)

    return my_program.resolve_program()


def main():
    M = 256
    K = 256
    m = 32
    k = 32
    n_cores = 4
    M_div_m = M // m
    K_div_k = K // k
    rows_per_core = M_div_m // n_cores
    n_fifo_elems = rows_per_core * K_div_k
    A_elem_size = n_cores * m * k
    inputA = iron.arange(n_fifo_elems * A_elem_size, dtype=np.int16, device="npu")
    inputB = iron.arange(K, dtype=np.int16, device="npu")
    outputC = iron.zeros(M, dtype=np.int32, device="npu")
    inputA.data[:] = inputA.data[:] % 128
    inputA._sync_to_device()
    inputB.data[:] = inputB.data[:] % 128
    inputB._sync_to_device()
    inputA.data[:] = inputA.data[:] % 8
    inputA._sync_to_device()
    inputB.data[:] = inputB.data[:] % 8
    inputB._sync_to_device()

    print(f"inputA[:8] = {inputA.numpy()[:8]}")
    print(f"inputB[:8] = {inputB.numpy()[:8]}")

    matrix_vector_mul_test_jit(inputA, inputB, outputC, M=M, K=K, m=m, k=k, n_cores=n_cores)

    print(f"outputC[:8] = {outputC.numpy()[:8]}")
    print(f"inputA[:8] = {inputA.numpy()[:8]}")
    print(f"inputB[:8] = {inputB.numpy()[:8]}")
    print(f"outputC[:8] = {outputC.numpy()[:8]}")



if __name__ == "__main__":
    main()