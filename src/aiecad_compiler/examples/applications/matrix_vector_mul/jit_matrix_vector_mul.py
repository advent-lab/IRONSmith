# jit_matrix_vector_mul.py -*- Python -*-
#
# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# (c) Copyright 2025 Advanced Micro Devices, Inc. or its affiliates

import numpy as np

from aie.iron import Program, Runtime, Worker, ObjectFifo
from aie.iron.placers import SequentialPlacer
from aie.iron import ExternalFunction, jit
from aie.iron.controlflow import range_
from aie.iron.device import Tile
from aie.helpers.taplib import TensorTiler2D
import aie.iron as iron


@iron.jit(is_placed=False)
def matrix_vector_mul_jit(inputA, inputB, outputC):
    # Matrix dimensions
    M = 256
    K = 256
    m = 32  # tile rows (matches DIM_M default in mv.cc)
    k = 32  # tile cols (matches DIM_K default in mv.cc)
    n_cores = 4
    M_div_m = M // m                    # 8 row blocks total
    K_div_k = K // k                    # 8 col blocks total
    rows_per_core = M_div_m // n_cores  # 2 row blocks per compute tile

    # Define tile types
    inA_ty = np.ndarray[(m, k), np.dtype[np.int16]]
    inB_ty = np.ndarray[(k,), np.dtype[np.int16]]
    outC_ty = np.ndarray[(m,), np.dtype[np.int32]]

    # Memtile buffer types (n_cores sub-buffers interleaved, one per compute tile)
    A_mem_ty = np.ndarray[(n_cores * m * k,), np.dtype[np.int16]]   # 4096 int16
    C_mem_ty = np.ndarray[(n_cores * m,), np.dtype[np.int32]]       # 128 int32

    # Fifo element counts
    n_fifo_elems = rows_per_core * K_div_k  # 16
    A_elem_size = n_cores * m * k           # 4096 int16 per fifo element

    # Host buffer types (2D for TensorTiler2D compatibility)
    A_ty = np.ndarray[(n_fifo_elems, A_elem_size), np.dtype[np.int16]]
    B_ty = np.ndarray[(1, K), np.dtype[np.int16]]
    C_ty = np.ndarray[(1, M), np.dtype[np.int32]]

    # External kernel function
    matvec = ExternalFunction(
        name="matvec_vectorized_i16_i32",
        source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/mv.cc",
        arg_types=[inA_ty, inB_ty, outC_ty],
        include_dirs=[
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels",
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2",
            "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2",
        ],
    )

    # --- ObjectFifos ---

    # A: shim → mem tile (col 0) → split to 4 compute tiles
    # DMA channels on A mem tile: 1 input + 4 outputs = 5 (≤6)
    A_fifo = ObjectFifo(A_mem_ty, name="inA")
    a_fifos = A_fifo.cons().split(
        offsets=[0, m * k, 2 * m * k, 3 * m * k],
        obj_types=[inA_ty, inA_ty, inA_ty, inA_ty],
        placement=Tile(0, 1),
    )

    # B: shim → mem tile (col 1) → forward (broadcast) to 4 compute tiles
    B_fifo = ObjectFifo(inB_ty, name="inB")
    B_fwd = B_fifo.cons().forward(placement=Tile(1, 1))

    # C: 4 compute tiles → join → mem tile (col 2) → shim
    # DMA channels on C mem tile: 4 inputs + 1 output = 5 (≤6)
    C_fifo = ObjectFifo(C_mem_ty, name="outC")
    c_fifos = C_fifo.prod().join(
        offsets=[0, m, 2 * m, 3 * m],
        obj_types=[outC_ty, outC_ty, outC_ty, outC_ty],
        placement=Tile(2, 1),
    )

    # --- Core function ---
    # Each compute tile: 1 A input + 1 B input = 2 input DMA channels (≤2)
    #                     1 C output = 1 output DMA channel (≤2)
    def core_fn(a_in, b_in, c_out, matvec):
        elem_out = c_out.acquire(1)
        for i in range_(m):
            elem_out[i] = 0
        for _ in range_(K_div_k):
            elem_a = a_in.acquire(1)
            elem_b = b_in.acquire(1)
            matvec(elem_a, elem_b, elem_out)
            a_in.release(1)
            b_in.release(1)
        c_out.release(1)

    # --- Workers (4 compute tiles on col 0, rows 2-5) ---
    worker0 = Worker(core_fn, fn_args=[a_fifos[0].cons(), B_fwd.cons(), c_fifos[0].prod(), matvec], placement=Tile(0, 2))
    worker1 = Worker(core_fn, fn_args=[a_fifos[1].cons(), B_fwd.cons(), c_fifos[1].prod(), matvec], placement=Tile(0, 3))
    worker2 = Worker(core_fn, fn_args=[a_fifos[2].cons(), B_fwd.cons(), c_fifos[2].prod(), matvec], placement=Tile(0, 4))
    worker3 = Worker(core_fn, fn_args=[a_fifos[3].cons(), B_fwd.cons(), c_fifos[3].prod(), matvec], placement=Tile(0, 5))

    # --- Tensor access patterns ---
    a_tap = TensorTiler2D.group_tiler(
        (n_fifo_elems, A_elem_size), (1, 512),
        (n_fifo_elems, A_elem_size // 512),
        prune_step=False,
    )[0]
    b_tap = TensorTiler2D.group_tiler(
        (1, K), (1, k), (1, K_div_k),
        pattern_repeat=rows_per_core,
        prune_step=False,
    )[0]
    c_tap = TensorTiler2D.group_tiler(
        (1, M), (1, n_cores * m), (1, rows_per_core),
        prune_step=False,
    )[0]

    # --- Runtime ---
    rt = Runtime()
    with rt.sequence(A_ty, B_ty, C_ty) as (a_in, b_in, c_out):
        rt.start(worker0, worker1, worker2, worker3)
        rt.fill(in_fifo=A_fifo.prod(), source=a_in, tap=a_tap, placement=Tile(0, 0))
        rt.fill(in_fifo=B_fifo.prod(), source=b_in, tap=b_tap, placement=Tile(1, 0))
        rt.drain(out_fifo=C_fifo.cons(), dest=c_out, tap=c_tap, wait=True, placement=Tile(2, 0))

    return Program(iron.get_current_device(), rt).resolve_program(SequentialPlacer())


def main():
    M = 256
    K = 256
    m = 32
    k = 32
    n_cores = 4
    M_div_m = M // m         # 8
    K_div_k = K // k          # 8
    rows_per_core = M_div_m // n_cores  # 2

    # Create input data
    np.random.seed(42)
    A_data = np.random.randint(-16, 16, size=(M, K)).astype(np.int16)
    b_data = np.random.randint(-16, 16, size=(K,)).astype(np.int16)

    # --- Prepare A host buffer ---
    # Layout: n_fifo_elems fifo elements, each = n_cores interleaved (m,k) tiles
    # with 32-bit word transposition for matvec_vectorized.
    #
    # Consumption order: for each row block iteration (rows_per_core=2),
    # for each col block (K_div_k=8), each compute tile gets one (m,k) tile.
    # Fifo element i contains tiles for all 4 compute tiles at the same col block.

    def prepare_A(A_data):
        buf = []
        for rb_group in range(rows_per_core):
            for cb in range(K_div_k):
                for core in range(n_cores):
                    rb = core + rb_group * n_cores
                    tile = A_data[rb * m:(rb + 1) * m, cb * k:(cb + 1) * k]
                    # 32-bit word transposition for matvec_vectorized
                    tile_t = tile.reshape(m, k // 2, 2).transpose(1, 0, 2).reshape(-1)
                    buf.append(tile_t)
        return np.concatenate(buf)

    A_buf = prepare_A(A_data)

    # --- Create device tensors ---
    inputA = iron.zeros(len(A_buf), dtype=np.int16, device="npu")
    inputA.data[:] = A_buf
    inputA._sync_to_device()

    # B uses pattern_repeat in the TAP to re-read the same vector for each
    # row block iteration, so no host-side replication is needed.
    inputB = iron.zeros(K, dtype=np.int16, device="npu")
    inputB.data[:] = b_data
    inputB._sync_to_device()

    outputC = iron.zeros(M, dtype=np.int32, device="npu")

    # Run the JIT function
    matrix_vector_mul_jit(inputA, inputB, outputC)

    print(outputC)

    # Validation
    expected = A_data.astype(np.int32) @ b_data.astype(np.int32)
    actual = np.asarray(outputC, dtype=np.int32)

    # Output join order: [compute0, compute1, compute2, compute3] per round
    #   Round 0: C[0:31], C[32:63], C[64:95], C[96:127]   (row blocks 0-3)
    #   Round 1: C[128:159], C[160:191], C[192:223], C[224:255]  (row blocks 4-7)

    print("\nSample element-by-element comparison (first 10):")
    for i in range(min(10, M)):
        print(f"  Row {i}: Expected = {expected[i]} : Received = {actual[i]}")

    mismatches = np.where(actual != expected)[0]

    print(f"\nValidation results:")
    print(f"  Elements tested: {M}")

    if len(mismatches) == 0:
        print(f"  Status: PASSED - All {M} values match exactly")
    else:
        print(f"  Status: FAILED - {len(mismatches)} mismatches found")
        print(f"\nFirst few mismatches:")
        for i, idx in enumerate(mismatches[:5]):
            print(f"  Row {idx}: actual={actual[idx]}, expected={expected[idx]}")


if __name__ == "__main__":
    main()
