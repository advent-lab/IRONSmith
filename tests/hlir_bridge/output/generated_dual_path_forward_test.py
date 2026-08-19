# dual_path_forward_test.py -*- Python -*-


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
def dual_path_forward_test_jit(inputA: In, inputB: In, outputA: Out, outputB: Out, *, N: CompileTime[int]):
    # Tensor Types
    data_ty = np.ndarray[(N,), np.dtype[np.int32]]
    data_a_ty = np.ndarray[(N,), np.dtype[bfloat16]]
    data_b_ty = np.ndarray[(N,), np.dtype[bfloat16]]

    # Data Movement
    # Object Fifos
    in1 = ObjectFifo(obj_type=data_ty, depth=2, name="in1")
    in2 = ObjectFifo(obj_type=data_ty, depth=2, name="in2")
    out1 = ObjectFifo(obj_type=data_ty, depth=2, name="out1")
    out2 = ObjectFifo(obj_type=data_ty, depth=2, name="out2")
    out3 = ObjectFifo(obj_type=data_ty, depth=2, name="out3")
    out4 = ObjectFifo(obj_type=data_ty, depth=2, name="out4")
    # Broadcasts
    out5 = out3.cons().forward(tile=Tile(0, 1))
    out6 = out4.cons().forward(tile=Tile(0, 1))

    # Compute Kernels
    copy2_kernel = ExternalFunction(
        name="copy_two_fifos", source_file="copy2.cc", arg_types=[data_ty, data_ty, data_ty, data_ty]
    )

    # Core Body Functions
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

    # Workers
    Workers = []
    worker1 = Worker(core_fn=corefunc_copy2, fn_args=[copy2_kernel, in1.cons(), in2.cons(), out1.prod(), out2.prod()], tile=Tile(0, 3))
    worker2 = Worker(core_fn=corefunc_copy2, fn_args=[copy2_kernel, out1.cons(), out2.cons(), out3.prod(), out4.prod()], tile=Tile(0, 2))

    Workers = [worker1, worker2]

    # Runtime
    def sequence(inputa_in, inputb_in, outputa_out, outputb_out, in1, in2, out5, out6):
        # Fills
        in1.fill(inputa_in)
        in2.fill(inputb_in)
        # Drains
        out5.drain(outputa_out, wait=True)
        out6.drain(outputb_out, wait=True)

    rt = Runtime(sequence, [
        data_ty,
        data_ty,
        data_ty,
        data_ty,
        in1.prod(tile=Tile(0, 0)),
        in2.prod(tile=Tile(0, 0)),
        out5.cons(tile=Tile(0, 0)),
        out6.cons(tile=Tile(0, 0)),
    ])

    # Program
    my_program = Program(iron.get_current_device(), rt, workers=Workers)

    return my_program.resolve_program()


def main():
    N = 64
    inputA = iron.arange(N, dtype=np.int32, device="npu")
    inputB = iron.arange(N, dtype=np.int32, device="npu")
    outputA = iron.zeros(N, dtype=np.int32, device="npu")
    outputB = iron.zeros(N, dtype=np.int32, device="npu")
    dual_path_forward_test_jit(inputA, inputB, outputA, outputB, N=N)
    print(f"inputA[:8] = {inputA.numpy()[:8]}")
    print(f"inputB[:8] = {inputB.numpy()[:8]}")
    print(f"outputA[:8] = {outputA.numpy()[:8]}")
    print(f"outputB[:8] = {outputB.numpy()[:8]}")
    okA = np.array_equal(outputA.numpy(), inputA.numpy())
    okB = np.array_equal(outputB.numpy(), inputB.numpy())
    print("PASS: outputA matches inputA and outputB matches inputB" if (okA and okB) else f"FAIL: outputA matches inputA = {okA}, outputB matches inputB = {okB}")



if __name__ == "__main__":
    main()