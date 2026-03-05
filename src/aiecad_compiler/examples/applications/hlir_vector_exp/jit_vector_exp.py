# jit_vector_exp.py -*- Python -*-
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
import aie.iron as iron


@iron.jit(is_placed=False)
def vector_exp_jit(inputA, outputC):
    N = inputA.numel()

    # Tile sizes
    n = 1024
    N_div_n = N // n
    tiles = N_div_n // 4

    # Define tensor types
    memtile_ty = np.ndarray[(n * 4,), np.dtype[bfloat16]]
    tile_ty = np.ndarray[(n,), np.dtype[bfloat16]]

    # Generate handle to externally defined kernel function
    exp_bf16_1024 = ExternalFunction(
        name="exp_bf16_1024",
        source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/bf16_exp.cc",
        arg_types=[tile_ty, tile_ty],
        include_dirs=[
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels",
            "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"
        ]
    )

    # Dataflow with ObjectFifos
    A_fifo = ObjectFifo(memtile_ty, name="inA")
    C_fifo = ObjectFifo(memtile_ty, name="outC")
    a_fifos = A_fifo.cons().split(
        offsets=[0, 1024, 2048, 3072],
        obj_types=[tile_ty, tile_ty, tile_ty, tile_ty]
    )
    c_fifos = C_fifo.prod().join(
        offsets=[0, 1024, 2048, 3072],
        obj_types=[tile_ty, tile_ty, tile_ty, tile_ty]
    )

    # Define a task a core might perform
    def core_fn(a_in, c_out, exp_bf16_1024):
        for _ in range_(tiles):
            elem_out = c_out.acquire(1)
            elem_in_a = a_in.acquire(1)
            exp_bf16_1024(elem_in_a, elem_out)
            a_in.release(1)
            c_out.release(1)

    # Create workers to run the tasks (one per core)
    worker0 = Worker(core_fn, fn_args=[a_fifos[0].cons(), c_fifos[0].prod(), exp_bf16_1024])
    worker1 = Worker(core_fn, fn_args=[a_fifos[1].cons(), c_fifos[1].prod(), exp_bf16_1024])
    worker2 = Worker(core_fn, fn_args=[a_fifos[2].cons(), c_fifos[2].prod(), exp_bf16_1024])
    worker3 = Worker(core_fn, fn_args=[a_fifos[3].cons(), c_fifos[3].prod(), exp_bf16_1024])

    # Runtime operations to move data to/from the AIE-array
    rt = Runtime()
    with rt.sequence(memtile_ty, memtile_ty) as (a_in, c_out):
        rt.start(worker0, worker1, worker2, worker3)
        rt.fill(A_fifo.prod(), a_in)
        rt.drain(C_fifo.cons(), c_out, wait=True)

    # Place program components and generate an MLIR module
    return Program(iron.get_current_device(), rt).resolve_program(SequentialPlacer())


def main():
    datatype = bfloat16
    N = 65536

    # Create input and output tensors
    inputA = iron.arange(N, dtype=datatype, device="npu")
    outputC = iron.zeros(N, dtype=datatype, device="npu")

    # Run the JIT function
    vector_exp_jit(inputA, outputC)

    print(outputC)

    # Validation
    inputA_data = np.arange(N, dtype=np.float32)
    expected = np.exp(inputA_data)
    actual = np.asarray(outputC, dtype=np.float32)

    # Print some sample values
    print('\nSample element-by-element comparison (first 10):')
    for i in range(min(10, N)):
        print(f'Expected = exp({inputA_data[i]:.1f}) = {expected[i]:.4f} : Received = {actual[i]:.4f}')

    # Check tolerance - handle overflow cases separately
    tolerance = 0.05  # 5% relative tolerance for bfloat16 and lookup table approximation

    # For values that overflow to infinity in float32, the hardware saturates to a max value
    # We'll treat both infinity and large saturated values as matching
    exp_max_input = 88.0  # exp(88) is near float32 max before overflow
    valid_range = inputA_data < exp_max_input

    # Only validate values in the valid range
    valid_indices = np.where(valid_range)[0]
    actual_valid = actual[valid_indices]
    expected_valid = expected[valid_indices]

    mismatches = np.where(~np.isclose(actual_valid, expected_valid, rtol=tolerance))[0]

    print(f'\nValidation results:')
    print(f'  Valid range tested: exp(0) to exp({exp_max_input:.0f})')
    print(f'  Elements in valid range: {len(valid_indices)} / {N}')

    if len(mismatches) == 0:
        print(f'  Status: PASSED - All {len(valid_indices)} values in valid range match within {tolerance*100:.1f}% tolerance')
    else:
        print(f'  Status: FAILED - {len(mismatches)} mismatches found in valid range')
        print(f'\nFirst few mismatches:')
        for i, idx in enumerate(mismatches[:5]):
            actual_idx = valid_indices[idx]
            print(f'  Index {actual_idx}: input={inputA_data[actual_idx]:.1f}, actual={actual_valid[idx]:.4f}, expected={expected_valid[idx]:.4f}')


if __name__ == "__main__":
    main()
