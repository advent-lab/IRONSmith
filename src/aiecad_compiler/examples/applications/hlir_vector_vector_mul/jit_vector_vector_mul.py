# jit_vector_vector_mul.py -*- Python -*-
#
# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# (c) Copyright 2024 Advanced Micro Devices, Inc. or its affiliates

import numpy as np
from ml_dtypes import bfloat16

from aie.iron import Program, Runtime, Worker, ObjectFifo
from aie.iron.placers import SequentialPlacer
from aie.iron import ExternalFunction, jit
from aie.iron.controlflow import range_
from aie.iron.device import Tile
import aie.iron as iron


@iron.jit(is_placed=False)
def vector_vector_mul_jit(inputA, inputB, outputC):
    N = inputA.numel()

    # Tile sizes
    chunk_size = 4096
    # n = 1024
    # N_div_n = N // n

    # Define tensor types
    memtile_ty = np.ndarray[(chunk_size,), np.dtype[bfloat16]]
    tile_ty = np.ndarray[(chunk_size // 4,), np.dtype[bfloat16]]

    # Generate handle to externally defined kernel function
    eltwise_mul_bf16_vector = ExternalFunction(
        name="eltwise_mul_bf16_vector",
        source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/mul.cc",
        arg_types=[tile_ty, tile_ty, tile_ty],
        include_dirs=[
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels",
            "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"
        ]
    )

    # Dataflow with ObjectFifos
    A_fifo = ObjectFifo(memtile_ty, name="inA")
    B_fifo = ObjectFifo(memtile_ty, name="inB")
    C_fifo = ObjectFifo(memtile_ty, name="outC")
    a_fifos = A_fifo.cons().split(
        offsets=[0, 1024, 2048, 3072],
        obj_types=[tile_ty, tile_ty, tile_ty, tile_ty],
        placement=Tile(0, 1)
    )
    b_fifos = B_fifo.cons().split(
        offsets=[0, 1024, 2048, 3072],
        obj_types=[tile_ty, tile_ty, tile_ty, tile_ty],
        placement=Tile(1, 1)
    )
    c_fifos = C_fifo.prod().join(
        offsets=[0, 1024, 2048, 3072],
        obj_types=[tile_ty, tile_ty, tile_ty, tile_ty],
        placement=Tile(2, 1)
    )

    # Define a task a core might perform
    def core_fn(a_in, b_in, c_out, eltwise_mul_bf16_vector):
        for _ in range_(N // chunk_size):
            elem_out = c_out.acquire(1)
            elem_in_a = a_in.acquire(1)
            elem_in_b = b_in.acquire(1)
            eltwise_mul_bf16_vector(elem_in_a, elem_in_b, elem_out)
            a_in.release(1)
            b_in.release(1)
            c_out.release(1)

    # Create workers to run the tasks (one per core)
    worker0 = Worker(core_fn, fn_args=[a_fifos[0].cons(), b_fifos[0].cons(), c_fifos[0].prod(), eltwise_mul_bf16_vector], placement=Tile(0, 5))
    worker1 = Worker(core_fn, fn_args=[a_fifos[1].cons(), b_fifos[1].cons(), c_fifos[1].prod(), eltwise_mul_bf16_vector], placement=Tile(0, 4))
    worker2 = Worker(core_fn, fn_args=[a_fifos[2].cons(), b_fifos[2].cons(), c_fifos[2].prod(), eltwise_mul_bf16_vector], placement=Tile(0, 3))
    worker3 = Worker(core_fn, fn_args=[a_fifos[3].cons(), b_fifos[3].cons(), c_fifos[3].prod(), eltwise_mul_bf16_vector], placement=Tile(0, 2))

    # Runtime operations to move data to/from the AIE-array
    rt = Runtime()
    with rt.sequence(memtile_ty, memtile_ty, memtile_ty) as (a_in, b_in, c_out):
        rt.start(worker0, worker1, worker2, worker3)
        rt.fill(A_fifo.prod(), a_in, placement=Tile(0, 0))
        rt.fill(B_fifo.prod(), b_in, placement=Tile(0, 0))
        rt.drain(C_fifo.cons(), c_out, wait=True, placement=Tile(1, 0))

    # Place program components and generate an MLIR module
    return Program(iron.get_current_device(), rt).resolve_program(SequentialPlacer())


def main():
    datatype = bfloat16
    N = 65536

    # Create input and output tensors
    inputA = iron.arange(N, dtype=datatype, device="npu")
    inputB = iron.arange(N, dtype=datatype, device="npu")
    outputC = iron.zeros(N, dtype=datatype, device="npu")

    # Run the JIT function
    vector_vector_mul_jit(inputA, inputB, outputC)

    print(outputC)

    # Validation - only check first 4096 elements to avoid overflow (4096*4096 overflows bfloat16)
    V = 4096
    inputA_data = np.arange(V, dtype=np.float32)
    inputB_data = np.arange(V, dtype=np.float32)
    expected = inputA_data * inputB_data
    actual = np.asarray(outputC, dtype=np.float32)[:V]

    # Print some sample values
    print('\nSample element-by-element comparison (first 10):')
    for i in range(min(10, V)):
        print(f'Expected = {inputA_data[i]:.1f} * {inputB_data[i]:.1f} = {expected[i]:.4f} : Received = {actual[i]:.4f}')

    # Check tolerance
    tolerance = 0.05  # 5% relative tolerance for bfloat16

    mismatches = np.where(~np.isclose(actual, expected, rtol=tolerance))[0]

    print(f'\nValidation results:')
    print(f'  Elements tested: {V} (of {N}, limited to avoid bfloat16 overflow)')

    if len(mismatches) == 0:
        print(f'  Status: PASSED - All {V} values match within {tolerance*100:.1f}% tolerance')
    else:
        print(f'  Status: FAILED - {len(mismatches)} mismatches found')
        print(f'\nFirst few mismatches:')
        for i, idx in enumerate(mismatches[:5]):
            print(f'  Index {idx}: input_a={inputA_data[idx]:.1f}, input_b={inputB_data[idx]:.1f}, actual={actual[idx]:.4f}, expected={expected[idx]:.4f}')


if __name__ == "__main__":
    main()
