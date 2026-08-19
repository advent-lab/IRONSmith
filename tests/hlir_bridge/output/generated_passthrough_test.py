# passthrough_test.py -*- Python -*-


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


@iron.jit
def passthrough_test_jit(inputA: In, outputC: Out, *, N: CompileTime[int]):
    # Tensor Types
    vector_ty = np.ndarray[(N,), np.dtype[np.int32]]
    line_ty = np.ndarray[(N // 4,), np.dtype[np.int32]]

    # Data Movement
    # Object Fifos
    of_in = ObjectFifo(obj_type=line_ty, depth=2, name="of_in")
    # Broadcasts
    of_out = of_in.cons().forward()

    Workers = []

    # Runtime
    def sequence(inputa_in, outputc_out, in_h, out):
        # Fills
        in_h.fill(inputa_in)
        # Drains
        out.drain(outputc_out, wait=True)

    rt = Runtime(sequence, [
        vector_ty,
        vector_ty,
        of_in.prod(tile=Tile(0, 0)),
        of_out.cons(tile=Tile(0, 0)),
    ])

    # Program
    my_program = Program(iron.get_current_device(), rt, workers=Workers)

    return my_program.resolve_program()


def main():
    N = 4096
    inputA = iron.arange(N, dtype=np.int32, device="npu")
    outputC = iron.zeros(N, dtype=np.int32, device="npu")
    passthrough_test_jit(inputA, outputC, N=N)
    print(f"inputA[:8] = {inputA.numpy()[:8]}")
    print(f"outputC[:8] = {outputC.numpy()[:8]}")
    print("PASS: outputC matches inputA" if np.array_equal(outputC.numpy(), inputA.numpy()) else "FAIL: outputC does not match inputA")



if __name__ == "__main__":
    main()