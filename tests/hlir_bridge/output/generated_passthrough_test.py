# passthrough_test.py -*- Python -*-


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


@iron.jit(is_placed=False)
def passthrough_test_jit(inputA, outputC):
    # Constants
    N = 4096

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
    rt = Runtime()
    with rt.sequence(vector_ty, vector_ty) as (inputa_in, outputc_out):
        # Fills
        rt.fill(of_in.prod(), inputa_in, placement=Tile(0, 0))
        # Drains
        rt.drain(of_out.cons(), outputc_out, wait=True, placement=Tile(0, 0))

    # Program
    my_program = Program(iron.get_current_device(), rt)

    # Placement
    return my_program.resolve_program(SequentialPlacer())


def main():
    N = 4096
    inputA = iron.arange(N, dtype=np.int32, device="npu")
    outputC = iron.zeros(N, dtype=np.int32, device="npu")
    passthrough_test_jit(inputA, outputC)



if __name__ == "__main__":
    main()