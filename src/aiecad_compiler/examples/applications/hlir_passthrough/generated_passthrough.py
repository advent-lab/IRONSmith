# passthrough_hlir_example.py -*- Python -*-


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
def passthrough_hlir_example_jit(inputA, outputC):
    # Define tensor types
    vector_ty = np.ndarray[(inputA.numel(),), np.dtype[np.int32]]
    line_ty = np.ndarray[(inputA.numel() // 4,), np.dtype[np.int32]]

    # Data movement with ObjectFifos
    of_in = ObjectFifo(obj_type=line_ty, depth=2, name="of_in")
    of_out = of_in.cons().forward()

    Workers = []

    # Runtime operations to move data to/from the AIE-array
    rt = Runtime()
    with rt.sequence(vector_ty, vector_ty) as (inputa_in, outputc_out):
        rt.fill(of_in.prod(), inputa_in)
        rt.drain(of_out.cons(), outputc_out, wait=True)

    # Create the program from the current device and runtime
    my_program = Program(iron.get_current_device(), rt)

    # Place components and resolve program (generate MLIR + compile)
    return my_program.resolve_program(SequentialPlacer())


def main():
    N = 4096
    inputA = iron.arange(N, dtype=np.int32, device="npu")
    outputC = iron.zeros(N, dtype=np.int32, device="npu")
    passthrough_hlir_example_jit(inputA, outputC)



if __name__ == "__main__":
    main()