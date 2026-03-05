# dual_path_forward_test.py -*- Python -*-


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
def dual_path_forward_test_jit(inputA, inputB, outputA, outputB):
    N = 64

    # Define tensor types
    data_ty = np.ndarray[(N,), np.dtype[np.int32]]
    data_a_ty = np.ndarray[(outputA.numel(),), np.dtype[bfloat16]]
    data_b_ty = np.ndarray[(outputB.numel(),), np.dtype[bfloat16]]

    # Data movement with ObjectFifos
    in1 = ObjectFifo(obj_type=data_ty, depth=2, name="in1")
    in2 = ObjectFifo(obj_type=data_ty, depth=2, name="in2")
    out1 = ObjectFifo(obj_type=data_ty, depth=2, name="out1")
    out2 = ObjectFifo(obj_type=data_ty, depth=2, name="out2")
    out3 = ObjectFifo(obj_type=data_ty, depth=2, name="out3")
    out4 = ObjectFifo(obj_type=data_ty, depth=2, name="out4")
    out5 = out3.cons().forward(placement=Tile(0, 1))
    out6 = out4.cons().forward(placement=Tile(0, 1))

    #Define kernels here... ------------------------------------------------\/
    copy2_kernel = ExternalFunction(
        name="copy_two_fifos", source_file="copy2.cc", arg_types=[data_ty, data_ty, data_ty, data_ty]
    )

    # core_fn here:
    def corefunc_copy2(kernel, fifo_a, fifo_b, fifo_c, fifo_d):
        elem_a = fifo_a.acquire(1)
        elem_b = fifo_b.acquire(1)
        elem_c = fifo_c.acquire(1)
        elem_d = fifo_d.acquire(1)
        kernel(elem_a, elem_b, elem_c, elem_d)
        fifo_a.release(1)
        fifo_b.release(1)
        fifo_c.release(1)
        fifo_d.release(1)

    #Workers defined here:
    Workers = []
    worker1 = Worker(core_fn=corefunc_copy2, fn_args=[copy2_kernel, in1.cons(), in2.cons(), out1.prod(), out2.prod()], placement=Tile(0, 3))
    worker2 = Worker(core_fn=corefunc_copy2, fn_args=[copy2_kernel, out1.cons(), out2.cons(), out3.prod(), out4.prod()], placement=Tile(0, 2))

    Workers = [worker1, worker2]

    # Runtime operations to move data to/from the AIE-array
    rt = Runtime()
    with rt.sequence(data_ty, data_ty, data_ty, data_ty) as (inputa_in, inputb_in, outputa_out, outputb_out):
        rt.start(*Workers)
        rt.fill(in1.prod(), inputa_in, placement=Tile(0, 0))
        rt.fill(in2.prod(), inputb_in, placement=Tile(0, 0))
        rt.drain(out5.cons(), outputa_out, wait=True, placement=Tile(0, 0))
        rt.drain(out6.cons(), outputb_out, wait=True, placement=Tile(0, 0))

    # Create the program from the current device and runtime
    my_program = Program(iron.get_current_device(), rt)

    # Place components and resolve program (generate MLIR + compile)
    return my_program.resolve_program(SequentialPlacer())


def main():
    N = 64
    inputA = iron.arange(N, dtype=np.int32, device="npu")
    inputB = iron.arange(N, dtype=np.int32, device="npu")
    outputA = iron.zeros(N, dtype=np.int32, device="npu")
    outputB = iron.zeros(N, dtype=np.int32, device="npu")
    dual_path_forward_test_jit(inputA, inputB, outputA, outputB)



if __name__ == "__main__":
    main()