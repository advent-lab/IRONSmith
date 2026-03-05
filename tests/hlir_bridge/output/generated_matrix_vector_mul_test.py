# matrix_vector_mul_test.py -*- Python -*-


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

from aie.helpers.taplib import TensorAccessPattern, TensorTiler2D
from aie.iron.controlflow import range_


@iron.jit(is_placed=False)
def matrix_vector_mul_test_jit(inputA, inputB, outputC):
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

    # Define tensor types
    inA_ty = np.ndarray[(m * k,), np.dtype[np.int16]]
    inB_ty = np.ndarray[(k,), np.dtype[np.int16]]
    outC_ty = np.ndarray[(m,), np.dtype[np.int32]]
    A_mem_ty = np.ndarray[(n_cores * m * k,), np.dtype[np.int16]]
    C_mem_ty = np.ndarray[(n_cores * m,), np.dtype[np.int32]]
    A_ty = np.ndarray[(n_fifo_elems, A_elem_size), np.dtype[np.int16]]
    B_ty = np.ndarray[(1, K), np.dtype[np.int16]]
    C_ty = np.ndarray[(1, M), np.dtype[np.int32]]

    # Tensor access patterns
    a_tap = TensorTiler2D.group_tiler((rows_per_core * K_div_k, n_cores * m * k), (1, 512), (rows_per_core * K_div_k, A_elem_size // 512), prune_step=False)[0]
    b_tap = TensorTiler2D.group_tiler((1, 256), (1, 32), (1, K // k), pattern_repeat=M_div_m // n_cores, prune_step=False)[0]
    c_tap = TensorTiler2D.group_tiler((1, 256), (1, n_cores * m), (1, M_div_m // n_cores), prune_step=False)[0]

    # Data movement with ObjectFifos
    inA = ObjectFifo(obj_type=A_mem_ty, depth=2, name="inA")
    inB = ObjectFifo(obj_type=inB_ty, depth=2, name="inB")
    outC = ObjectFifo(obj_type=C_mem_ty, depth=2, name="outC")
    MEM_L2_L1_A1A2A3A4_col0 = inA.cons().split(obj_types=[inA_ty, inA_ty, inA_ty, inA_ty], offsets=[0, 1024, 2048, 3072], names=["MEM_L2_L1_A1_col0", "MEM_L2_L1_A2_col0", "MEM_L2_L1_A3_col0", "MEM_L2_L1_A4_col0"], placement=Tile(0, 1))
    B_fwd = inB.cons().forward(placement=Tile(1, 1))
    MEM_L1_L2_C9C10C11C12_col2 = outC.prod().join(obj_types=[outC_ty, outC_ty, outC_ty, outC_ty], names=["MEM_L1_L2_C9_col2", "MEM_L1_L2_C10_col2", "MEM_L1_L2_C11_col2", "MEM_L1_L2_C12_col2"], placement=Tile(2, 1), offsets=[0, 32, 64, 96])

    #Define kernels here... ------------------------------------------------\/
    matvec_vectorized_i16_i32 = ExternalFunction(
        name="matvec_vectorized_i16_i32", source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/mv.cc", arg_types=[inA_ty, inB_ty, outC_ty], include_dirs=["/scratch/IRONSmithTesting/mlir-aie/aie_kernels", "/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2", "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"]
    )

    # core_fn here:
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

    #Workers defined here:
    Workers = []
    worker0 = Worker(core_fn=core_fn, fn_args=[MEM_L2_L1_A1A2A3A4_col0[0].cons(), B_fwd.cons(), MEM_L1_L2_C9C10C11C12_col2[0].prod(), matvec_vectorized_i16_i32], placement=Tile(0, 2))
    worker1 = Worker(core_fn=core_fn, fn_args=[MEM_L2_L1_A1A2A3A4_col0[1].cons(), B_fwd.cons(), MEM_L1_L2_C9C10C11C12_col2[1].prod(), matvec_vectorized_i16_i32], placement=Tile(0, 3))
    worker2 = Worker(core_fn=core_fn, fn_args=[MEM_L2_L1_A1A2A3A4_col0[2].cons(), B_fwd.cons(), MEM_L1_L2_C9C10C11C12_col2[2].prod(), matvec_vectorized_i16_i32], placement=Tile(0, 4))
    worker3 = Worker(core_fn=core_fn, fn_args=[MEM_L2_L1_A1A2A3A4_col0[3].cons(), B_fwd.cons(), MEM_L1_L2_C9C10C11C12_col2[3].prod(), matvec_vectorized_i16_i32], placement=Tile(0, 5))

    Workers = [worker0, worker1, worker2, worker3]

    # Runtime operations to move data to/from the AIE-array
    rt = Runtime()
    with rt.sequence(A_ty, B_ty, C_ty) as (inputa_in, inputb_in, outputc_out):
        rt.start(*Workers)
        rt.fill(placement=Tile(0, 0), in_fifo=inA.prod(), source=inputa_in, tap=a_tap)
        rt.fill(placement=Tile(1, 0), in_fifo=inB.prod(), source=inputb_in, tap=b_tap)
        rt.drain(placement=Tile(2, 0), out_fifo=outC.cons(), dest=outputc_out, wait=True, tap=c_tap)

    # Create the program from the current device and runtime
    my_program = Program(iron.get_current_device(), rt)

    # Place components and resolve program (generate MLIR + compile)
    return my_program.resolve_program(SequentialPlacer())


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
    inputA = iron.arange(n_fifo_elems, dtype=np.int16, device="npu")
    inputB = iron.arange(n_fifo_elems, dtype=np.int16, device="npu")
    outputC = iron.zeros(n_fifo_elems, dtype=np.int16, device="npu")
    matrix_vector_mul_test_jit(inputA, inputB, outputC)



if __name__ == "__main__":
    main()