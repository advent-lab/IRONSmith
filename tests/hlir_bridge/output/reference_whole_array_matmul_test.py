# reference_whole_array_matmul_test.py -*- Python -*-
#
# Hand-written reference for a simplified "whole array" matrix-matrix multiply
# (C = A @ B) filling all 16 compute tiles of a 4-column NPU1 device.
#
# This is a SIMPLIFIED teaching version of mlir-aie's real
# programming_examples/basic/matrix_multiplication/whole_array example: same
# 4-row x 4-column compute grid and 3-layer ObjectFifo structure (shim -> L2
# broadcast/join -> compute), but a fixed shape, row-major-only, no
# TaskGroup double-buffered pipelining, and the scalar (non-vectorized)
# matmul kernel so no dims_to_stream tiling is needed.
#
# Not generated through IRONSmith's codegen pipeline - hand-written to have
# something runnable before building the equivalent design in the GUI.

import numpy as np
from ml_dtypes import bfloat16

from aie.iron import Program, Runtime, Worker, ObjectFifo
from aie.iron import ExternalFunction, jit, In, Out, CompileTime
from aie.iron.device import Tile
import aie.iron as iron

from aie.helpers.taplib import TensorAccessPattern
from aie.iron.controlflow import range_


@iron.jit
def whole_array_matmul_test_jit(
    A: In, B: In, C: Out, *,
    M: CompileTime[int], K: CompileTime[int], N: CompileTime[int],
    m: CompileTime[int], k: CompileTime[int], n: CompileTime[int],
):
    # Tensor Types
    A_ty = np.ndarray[(M * K,), np.dtype[np.int16]]
    B_ty = np.ndarray[(K * N,), np.dtype[np.int16]]
    C_ty = np.ndarray[(M * N,), np.dtype[np.int32]]
    a_tile_ty = np.ndarray[(m * k,), np.dtype[np.int16]]
    b_tile_ty = np.ndarray[(k * n,), np.dtype[np.int16]]
    c_tile_ty = np.ndarray[(m * n,), np.dtype[np.int32]]
    c_col_ty = np.ndarray[(M * n,), np.dtype[np.int32]]

    # Data Movement
    # Object Fifos - A/B enter through 4+4 shim tiles, one per row (A) / per
    # column (B), then broadcast from a memtile to the 4 compute tiles that
    # share that row/column. C leaves through 4 shim tiles (one per column),
    # each joining that column's 4 row-tile outputs.
    of_in_a_row0 = ObjectFifo(obj_type=a_tile_ty, depth=2, name="of_in_a_row0")
    of_in_a_row1 = ObjectFifo(obj_type=a_tile_ty, depth=2, name="of_in_a_row1")
    of_in_a_row2 = ObjectFifo(obj_type=a_tile_ty, depth=2, name="of_in_a_row2")
    of_in_a_row3 = ObjectFifo(obj_type=a_tile_ty, depth=2, name="of_in_a_row3")

    of_in_b_col0 = ObjectFifo(obj_type=b_tile_ty, depth=2, name="of_in_b_col0")
    of_in_b_col1 = ObjectFifo(obj_type=b_tile_ty, depth=2, name="of_in_b_col1")
    of_in_b_col2 = ObjectFifo(obj_type=b_tile_ty, depth=2, name="of_in_b_col2")
    of_in_b_col3 = ObjectFifo(obj_type=b_tile_ty, depth=2, name="of_in_b_col3")

    of_out_c_col0 = ObjectFifo(obj_type=c_col_ty, depth=2, name="of_out_c_col0")
    of_out_c_col1 = ObjectFifo(obj_type=c_col_ty, depth=2, name="of_out_c_col1")
    of_out_c_col2 = ObjectFifo(obj_type=c_col_ty, depth=2, name="of_out_c_col2")
    of_out_c_col3 = ObjectFifo(obj_type=c_col_ty, depth=2, name="of_out_c_col3")

    # Broadcasts - one memtile per row (A) / per column (B), each fed to the
    # 4 compute tiles in that row/column.
    a_row0_fwd = of_in_a_row0.cons().forward(tile=Tile(0, 1))
    a_row1_fwd = of_in_a_row1.cons().forward(tile=Tile(1, 1))
    a_row2_fwd = of_in_a_row2.cons().forward(tile=Tile(2, 1))
    a_row3_fwd = of_in_a_row3.cons().forward(tile=Tile(3, 1))

    b_col0_fwd = of_in_b_col0.cons().forward(tile=Tile(0, 1))
    b_col1_fwd = of_in_b_col1.cons().forward(tile=Tile(1, 1))
    b_col2_fwd = of_in_b_col2.cons().forward(tile=Tile(2, 1))
    b_col3_fwd = of_in_b_col3.cons().forward(tile=Tile(3, 1))

    # Joins - each column's 4 row-tile outputs join into one C_L1L2 fifo.
    c_col0_joins = of_out_c_col0.prod().join(
        names=["C_col0_row0", "C_col0_row1", "C_col0_row2", "C_col0_row3"],
        obj_types=[c_tile_ty] * 4, offsets=[0, m * n, 2 * m * n, 3 * m * n],
        tile=Tile(0, 1),
    )
    c_col1_joins = of_out_c_col1.prod().join(
        names=["C_col1_row0", "C_col1_row1", "C_col1_row2", "C_col1_row3"],
        obj_types=[c_tile_ty] * 4, offsets=[0, m * n, 2 * m * n, 3 * m * n],
        tile=Tile(1, 1),
    )
    c_col2_joins = of_out_c_col2.prod().join(
        names=["C_col2_row0", "C_col2_row1", "C_col2_row2", "C_col2_row3"],
        obj_types=[c_tile_ty] * 4, offsets=[0, m * n, 2 * m * n, 3 * m * n],
        tile=Tile(2, 1),
    )
    c_col3_joins = of_out_c_col3.prod().join(
        names=["C_col3_row0", "C_col3_row1", "C_col3_row2", "C_col3_row3"],
        obj_types=[c_tile_ty] * 4, offsets=[0, m * n, 2 * m * n, 3 * m * n],
        tile=Tile(3, 1),
    )

    # Compute Kernels - scalar (non-vectorized) matmul: no dims_to_stream
    # tiling needed, plain row-major (m,k) x (k,n) -> (m,n). zero.cc is
    # pulled in via mm.cc's own #include, so no LUT-style companion source
    # is needed here (unlike bf16_exp's lut_based_ops.cpp).
    matmul_kernel = ExternalFunction(
        name="matmul_scalar_i16_i32",
        source_file="/home/mliraie/mlir-aie/aie_kernels/aie2/mm.cc",
        arg_types=[a_tile_ty, b_tile_ty, c_tile_ty],
        include_dirs=["/home/mliraie/mlir-aie/aie_kernels/aie2"],
        compile_flags=["-DDIM_M=64", "-DDIM_K=64", "-DDIM_N=32", "-Di16_i32_ONLY"],
    )

    # Core Body Function - zero the output tile in Python (mirrors
    # matrix_vector_mul_test's pattern), then accumulate over K // k tiles.
    def core_fn(a_in, b_in, c_out, matmul):
        elem_out = c_out.acquire(1)
        for i in range_(m * n):
            elem_out[i] = 0
        for _ in range_(K // k):
            elem_a = a_in.acquire(1)
            elem_b = b_in.acquire(1)
            matmul(elem_a, elem_b, elem_out)
            a_in.release(1)
            b_in.release(1)
        c_out.release(1)

    # Workers - 16 compute tiles, one per (row, col), filling the full
    # 4-column x 4-compute-row NPU1 grid.
    a_row_fwds = [a_row0_fwd, a_row1_fwd, a_row2_fwd, a_row3_fwd]
    b_col_fwds = [b_col0_fwd, b_col1_fwd, b_col2_fwd, b_col3_fwd]
    c_col_joins = [c_col0_joins, c_col1_joins, c_col2_joins, c_col3_joins]

    Workers = []
    for row in range(4):
        for col in range(4):
            w = Worker(
                core_fn=core_fn,
                fn_args=[
                    a_row_fwds[row].cons(),
                    b_col_fwds[col].cons(),
                    c_col_joins[col][row].prod(),
                    matmul_kernel,
                ],
                tile=Tile(col, 2 + row),
            )
            Workers.append(w)

    # Runtime
    def sequence(A_in, B_in, C_out,
                 a_row0, a_row1, a_row2, a_row3,
                 b_col0, b_col1, b_col2, b_col3,
                 c_col0, c_col1, c_col2, c_col3):
        a_rows = [a_row0, a_row1, a_row2, a_row3]
        b_cols = [b_col0, b_col1, b_col2, b_col3]
        c_cols = [c_col0, c_col1, c_col2, c_col3]

        # Fills - each row of A / column of B streams K // k tiles.
        for row in range(4):
            for kt in range(K // k):
                a_rows[row].fill(
                    A_in,
                    tap=TensorAccessPattern(
                        tensor_dims=[M, K], offset=(row * m * K + kt * k),
                        sizes=[m, k], strides=[K, 1],
                    ),
                )
        for col in range(4):
            for kt in range(K // k):
                b_cols[col].fill(
                    B_in,
                    tap=TensorAccessPattern(
                        tensor_dims=[K, N], offset=(kt * k * N + col * n),
                        sizes=[k, n], strides=[N, 1],
                    ),
                )
        # Drains - each column drains its full (M, n) output strip at once.
        for col in range(4):
            c_cols[col].drain(
                C_out,
                tap=TensorAccessPattern(
                    tensor_dims=[M, N], offset=(col * n),
                    sizes=[M, n], strides=[N, 1],
                ),
                wait=True,
            )

    rt = Runtime(sequence, [
        A_ty, B_ty, C_ty,
        of_in_a_row0.prod(tile=Tile(0, 0)),
        of_in_a_row1.prod(tile=Tile(1, 0)),
        of_in_a_row2.prod(tile=Tile(2, 0)),
        of_in_a_row3.prod(tile=Tile(3, 0)),
        of_in_b_col0.prod(tile=Tile(0, 0)),
        of_in_b_col1.prod(tile=Tile(1, 0)),
        of_in_b_col2.prod(tile=Tile(2, 0)),
        of_in_b_col3.prod(tile=Tile(3, 0)),
        of_out_c_col0.cons(tile=Tile(0, 0)),
        of_out_c_col1.cons(tile=Tile(1, 0)),
        of_out_c_col2.cons(tile=Tile(2, 0)),
        of_out_c_col3.cons(tile=Tile(3, 0)),
    ])

    # Program
    my_program = Program(iron.get_current_device(), rt, workers=Workers)

    return my_program.resolve_program()


def main():
    M, K, N = 256, 128, 128
    m, k, n = 64, 64, 32

    # int16 inputs bounded to keep the K=128-term accumulation well inside
    # int32 (max here: 128 * 7 * 7 = 6272), same lesson learned from
    # matrix_vector_mul_test's earlier int16 arange overflow.
    A = iron.arange(M * K, dtype=np.int16, device="npu")
    B = iron.arange(K * N, dtype=np.int16, device="npu")
    C = iron.zeros(M * N, dtype=np.int32, device="npu")

    A.data[:] = A.data[:] % 8
    A._sync_to_device()
    B.data[:] = B.data[:] % 8
    B._sync_to_device()

    print(f"A[:8] = {A.numpy()[:8]}")
    print(f"B[:8] = {B.numpy()[:8]}")

    whole_array_matmul_test_jit(A, B, C, M=M, K=K, N=N, m=m, k=k, n=n)

    print(f"C[:8] = {C.numpy()[:8]}")

    A_mat = A.numpy().astype(np.int64).reshape(M, K)
    B_mat = B.numpy().astype(np.int64).reshape(K, N)
    expected = (A_mat @ B_mat).astype(np.int32)
    actual = C.numpy().reshape(M, N)

    if np.array_equal(actual, expected):
        print("PASS: C matches A @ B")
    else:
        mismatches = int(np.count_nonzero(actual != expected))
        print(f"FAIL: C does not match A @ B ({mismatches} / {actual.size} elements differ)")
        print(f"actual[:4,:4]   = {actual[:4,:4]}")
        print(f"expected[:4,:4] = {expected[:4,:4]}")


if __name__ == "__main__":
    main()
