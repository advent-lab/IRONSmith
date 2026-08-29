# gui_design.py -*- Python -*-


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
def gui_design_jit(A: In, B: In, C: Out, *, M: CompileTime[int], K: CompileTime[int], N: CompileTime[int], m: CompileTime[int], k: CompileTime[int], n: CompileTime[int]):
    # Tensor Types
    A_ty = np.ndarray[(M*K,), np.dtype[np.int16]]
    B_ty = np.ndarray[(K*N,), np.dtype[np.int16]]
    C_ty = np.ndarray[(M*N,), np.dtype[np.int32]]
    a_tile_ty = np.ndarray[(m*k,), np.dtype[np.int16]]
    b_tile_ty = np.ndarray[(k*n,), np.dtype[np.int16]]
    c_tile_ty = np.ndarray[(m*n,), np.dtype[np.int32]]
    c_col_ty = np.ndarray[(M*n,), np.dtype[np.int32]]

    # Data Movement
    # Object Fifos
    of_in_a_row0 = ObjectFifo(obj_type=a_tile_ty, depth=2, name="of_in_a_row0")
    of_in_b_col0 = ObjectFifo(obj_type=b_tile_ty, depth=2, name="of_in_b_col0")
    of_out_c_col0 = ObjectFifo(obj_type=c_col_ty, depth=2, name="of_out_c_col0")
    of_in_a_row1 = ObjectFifo(obj_type=a_tile_ty, depth=2, name="of_in_a_row1")
    of_in_a_row2 = ObjectFifo(obj_type=a_tile_ty, depth=2, name="of_in_a_row2")
    of_in_a_row3 = ObjectFifo(obj_type=a_tile_ty, depth=2, name="of_in_a_row3")
    # Joins
    c_col0_joins = of_out_c_col0.prod().join(names=["c_col0_joins_in1", "c_col0_joins_in2", "c_col0_joins_in3", "c_col0_joins_in4"], obj_types=[c_tile_ty, c_tile_ty, c_tile_ty, c_tile_ty], offsets=[0, 2048, 4096, 6144], tile=Tile(0, 1))
    # Broadcasts
    b_col0_fwd = of_in_b_col0.cons().forward(tile=Tile(0, 1))

    # Compute Kernels
    kernel_matmul_scalar_i16_i32 = ExternalFunction(
        name="matmul_scalar_i16_i32", source_file="/home/mliraie/mlir-aie/aie_kernels/aie2/mm.cc", arg_types=[a_tile_ty, b_tile_ty, c_tile_ty], include_dirs=["/home/mliraie/mlir-aie/aie_kernels/aie2"], compile_flags=["-DDIM_M=64", "-DDIM_K=64", "-DDIM_N=32", "-Di16_i32_ONLY"]
    )

    # Core Body Functions
    def core_shared_core_shared_core_fn_matmul(matmul, a_in, b_in, c_out):
        elem_out = c_out.acquire(1)
        for i in range_(m*n):
            elem_out[i] = 0
        for _ in range_(K//k):
            elem_a = a_in.acquire(1)
            elem_b = b_in.acquire(1)
            matmul(elem_a, elem_b, elem_out)
            a_in.release(1)
            b_in.release(1)
        c_out.release(1)

    # Workers
    Workers = []
    worker_aie0_2 = Worker(core_fn=core_shared_core_shared_core_fn_matmul, fn_args=[kernel_matmul_scalar_i16_i32, of_in_a_row0.cons(), b_col0_fwd.cons(), c_col0_joins[0].prod()], tile=Tile(0, 2))
    worker_aie0_3 = Worker(core_fn=core_shared_core_shared_core_fn_matmul, fn_args=[kernel_matmul_scalar_i16_i32, of_in_a_row1.cons(), b_col0_fwd.cons(), c_col0_joins[1].prod()], tile=Tile(0, 3))
    worker_aie0_4 = Worker(core_fn=core_shared_core_shared_core_fn_matmul, fn_args=[kernel_matmul_scalar_i16_i32, of_in_a_row2.cons(), b_col0_fwd.cons(), c_col0_joins[2].prod()], tile=Tile(0, 4))
    worker_aie0_5 = Worker(core_fn=core_shared_core_shared_core_fn_matmul, fn_args=[kernel_matmul_scalar_i16_i32, of_in_a_row3.cons(), b_col0_fwd.cons(), c_col0_joins[3].prod()], tile=Tile(0, 5))

    Workers = [worker_aie0_2, worker_aie0_3, worker_aie0_4, worker_aie0_5]

    # Tensor Access Patterns (TAPs)
    of_in_a_row0_tap = TensorAccessPattern(tensor_dims=[256, 128], offset=0, sizes=[2, 64, 64], strides=[64, 128, 1])
    of_in_a_row1_tap = TensorAccessPattern(tensor_dims=[256, 128], offset=8192, sizes=[2, 64, 64], strides=[64, 128, 1])
    of_in_a_row3_tap = TensorAccessPattern(tensor_dims=[256, 128], offset=24576, sizes=[2, 64, 64], strides=[64, 128, 1])
    of_in_b_col0_tap = TensorAccessPattern(tensor_dims=[128, 32], offset=0, sizes=[128, 32], strides=[32, 1])
    of_out_c_col0_tap = TensorAccessPattern(tensor_dims=[256, 32], offset=0, sizes=[256, 32], strides=[32, 1])
    of_in_a_row2_tap = TensorAccessPattern(tensor_dims=[256, 128], offset=16384, sizes=[2, 64, 64], strides=[64, 128, 1])

    # Runtime
    def sequence(a_in, b_in, c_out, in_a_row0, in_a_row1, in_a_row2, in_a_row3, in_b_col0, out_c_col0):
        # Fills
        in_a_row0.fill(a_in, tap=of_in_a_row0_tap)
        in_a_row1.fill(a_in, tap=of_in_a_row1_tap)
        in_a_row2.fill(a_in, tap=of_in_a_row2_tap)
        in_a_row3.fill(a_in, tap=of_in_a_row3_tap)
        in_b_col0.fill(b_in, tap=of_in_b_col0_tap)
        # Drains
        out_c_col0.drain(c_out, wait=True, tap=of_out_c_col0_tap)

    rt = Runtime(sequence, [
        A_ty,
        B_ty,
        C_ty,
        of_in_a_row0.prod(tile=Tile(0, 0)),
        of_in_a_row1.prod(tile=Tile(1, 0)),
        of_in_a_row2.prod(tile=Tile(2, 0)),
        of_in_a_row3.prod(tile=Tile(3, 0)),
        of_in_b_col0.prod(tile=Tile(0, 0)),
        of_out_c_col0.cons(tile=Tile(0, 0)),
    ])

    # Program
    my_program = Program(iron.get_current_device(), rt, workers=Workers)

    return my_program.resolve_program()


def main():
    M = 256
    K = 128
    N = 32
    m = 64
    k = 64
    n = 32
    A = iron.arange(M*K, dtype=np.int16, device="npu")
    B = iron.arange(K*N, dtype=np.int16, device="npu")
    C = iron.zeros(M*N, dtype=np.int32, device="npu")
    A.data[:] = A.data[:] % 8
    A._sync_to_device()
    B.data[:] = B.data[:] % 8
    B._sync_to_device()

    print(f"A[:8] = {A.numpy()[:8]}")
    print(f"B[:8] = {B.numpy()[:8]}")

    gui_design_jit(A, B, C, M=M, K=K, N=N, m=m, k=k, n=n)

    print(f"C[:8] = {C.numpy()[:8]}")



if __name__ == "__main__":
    main()