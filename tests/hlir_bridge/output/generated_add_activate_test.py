# add_activate_test.py -*- Python -*-


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
def add_activate_test_jit(A: In, B: In, D: Out, *, data_size: CompileTime[int]):
    # Tensor Types
    data_ty = np.ndarray[(data_size,), np.dtype[bfloat16]]
    chunk_ty = np.ndarray[(data_size // 4,), np.dtype[bfloat16]]
    worker_chunk_ty = np.ndarray[(data_size // 8,), np.dtype[bfloat16]]
    data_a_ty = np.ndarray[(data_size,), np.dtype[bfloat16]]
    chunk_a = np.ndarray[(data_size // 4,), np.dtype[bfloat16]]
    chunk_a_worker = np.ndarray[(data_size // 8,), np.dtype[bfloat16]]
    data_b_ty = np.ndarray[(data_size,), np.dtype[bfloat16]]
    chunk_b = np.ndarray[(data_size // 4,), np.dtype[bfloat16]]
    chunk_b_worker = np.ndarray[(data_size // 8,), np.dtype[bfloat16]]
    data_d_ty = np.ndarray[(data_size,), np.dtype[bfloat16]]
    chunk_d = np.ndarray[(data_size // 4,), np.dtype[bfloat16]]
    chunk_d_worker = np.ndarray[(data_size // 8,), np.dtype[bfloat16]]

    # Data Movement
    # Object Fifos
    of_in_a_col0 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_in_a_col0")
    of_in_a_col1 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_in_a_col1")
    of_in_a_col2 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_in_a_col2")
    of_in_a_col3 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_in_a_col3")
    of_in_b_col0 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_in_b_col0")
    of_in_b_col1 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_in_b_col1")
    of_in_b_col2 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_in_b_col2")
    of_in_b_col3 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_in_b_col3")
    of_inter_1 = ObjectFifo(obj_type=worker_chunk_ty, depth=2, name="of_inter_1")
    of_inter_2 = ObjectFifo(obj_type=worker_chunk_ty, depth=2, name="of_inter_2")
    of_inter_3 = ObjectFifo(obj_type=worker_chunk_ty, depth=2, name="of_inter_3")
    of_inter_4 = ObjectFifo(obj_type=worker_chunk_ty, depth=2, name="of_inter_4")
    of_inter_5 = ObjectFifo(obj_type=worker_chunk_ty, depth=2, name="of_inter_5")
    of_inter_6 = ObjectFifo(obj_type=worker_chunk_ty, depth=2, name="of_inter_6")
    of_inter_7 = ObjectFifo(obj_type=worker_chunk_ty, depth=2, name="of_inter_7")
    of_inter_8 = ObjectFifo(obj_type=worker_chunk_ty, depth=2, name="of_inter_8")
    of_out_d_col0 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_out_d_col0")
    of_out_d_col1 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_out_d_col1")
    of_out_d_col2 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_out_d_col2")
    of_out_d_col3 = ObjectFifo(obj_type=chunk_ty, depth=2, name="of_out_d_col3")
    # Splits
    MEM_L2_L1_A1A2_col0 = of_in_a_col0.cons().split(names=["MEM_L2_L1_A1_col0", "MEM_L2_L1_A2_col0"], obj_types=[chunk_a_worker, chunk_a_worker], offsets=[0, 16], tile=Tile(0, 1))
    MEM_L2_L1_A3A4_col1 = of_in_a_col1.cons().split(names=["MEM_L2_L1_A3_col1", "MEM_L2_L1_A4_col1"], obj_types=[chunk_a_worker, chunk_a_worker], offsets=[0, 16], tile=Tile(1, 1))
    MEM_L2_L1_A5A6_col2 = of_in_a_col2.cons().split(names=["MEM_L2_L1_A5_col2", "MEM_L2_L1_A6_col2"], obj_types=[chunk_a_worker, chunk_a_worker], offsets=[0, 16], tile=Tile(2, 1))
    MEM_L2_L1_A7A8_col3 = of_in_a_col3.cons().split(names=["MEM_L2_L1_A7_col3", "MEM_L2_L1_A8_col3"], obj_types=[chunk_a_worker, chunk_a_worker], offsets=[0, 16], tile=Tile(3, 1))
    MEM_L2_L1_B1B2_col0 = of_in_b_col0.cons().split(names=["MEM_L2_L1_B1_col0", "MEM_L2_L1_B2_col0"], obj_types=[chunk_b_worker, chunk_b_worker], offsets=[0, 16], tile=Tile(0, 1))
    MEM_L2_L1_B3B4_col1 = of_in_b_col1.cons().split(names=["MEM_L2_L1_B3_col1", "MEM_L2_L1_B4_col1"], obj_types=[chunk_b_worker, chunk_b_worker], offsets=[0, 16], tile=Tile(1, 1))
    MEM_L2_L1_B5B6_col2 = of_in_b_col2.cons().split(names=["MEM_L2_L1_B5_col2", "MEM_L2_L1_B6_col2"], obj_types=[chunk_b_worker, chunk_b_worker], offsets=[0, 16], tile=Tile(2, 1))
    MEM_L2_L1_B7B8_col3 = of_in_b_col3.cons().split(names=["MEM_L2_L1_B7_col3", "MEM_L2_L1_B8_col3"], obj_types=[chunk_b_worker, chunk_b_worker], offsets=[0, 16], tile=Tile(3, 1))
    # Joins
    MEM_L1_L2_D1D2_col0 = of_out_d_col0.prod().join(names=["MEM_L1_L2_D1_col0", "MEM_L1_L2_D2_col0"], obj_types=[chunk_d_worker, chunk_d_worker], offsets=[0, 16], tile=Tile(0, 1))
    MEM_L1_L2_D3D4_col1 = of_out_d_col1.prod().join(names=["MEM_L1_L2_D3_col1", "MEM_L1_L2_D4_col1"], obj_types=[chunk_d_worker, chunk_d_worker], offsets=[0, 16], tile=Tile(1, 1))
    MEM_L1_L2_D5D6_col2 = of_out_d_col2.prod().join(names=["MEM_L1_L2_D5_col2", "MEM_L1_L2_D6_col2"], obj_types=[chunk_d_worker, chunk_d_worker], offsets=[0, 16], tile=Tile(2, 1))
    MEM_L1_L2_D7D8_col3 = of_out_d_col3.prod().join(names=["MEM_L1_L2_D7_col3", "MEM_L1_L2_D8_col3"], obj_types=[chunk_d_worker, chunk_d_worker], offsets=[0, 16], tile=Tile(3, 1))

    # Compute Kernels
    externalfunc1 = ExternalFunction(
        name="eltwise_add_bf16_scalar", source_file="/home/mliraie/mlir-aie/aie_kernels/aie2/add.cc", arg_types=[worker_chunk_ty, worker_chunk_ty, worker_chunk_ty], include_dirs=["/home/mliraie/mlir-aie/aie_kernels/aie2"]
    )

    externalfunc2 = ExternalFunction(
        name="bf16_relu", source_file="/home/mliraie/mlir-aie/aie_kernels/aie2/relu.cc", arg_types=[worker_chunk_ty, worker_chunk_ty], include_dirs=["/home/mliraie/mlir-aie/aie_kernels/aie2"]
    )

    # Core Body Functions
    def corefunc1(kernel, inputA, inputB, outputC):
        elementA = inputA.acquire(1)
        elementB = inputB.acquire(1)
        elementC = outputC.acquire(1)
        kernel(elementA, elementB, elementC)
        inputA.release(1)
        inputB.release(1)
        outputC.release(1)

    def corefunc2(kernel, inputC, outputD):
        elementC = inputC.acquire(1)
        elementD = outputD.acquire(1)
        kernel(elementC, elementD)
        inputC.release(1)
        outputD.release(1)

    # Workers
    Workers = []
    worker_add_col0_w0 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A1A2_col0[0].cons(), MEM_L2_L1_B1B2_col0[0].cons(), of_inter_1.prod()], tile=Tile(0, 5))
    worker_add_col0_w1 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A1A2_col0[1].cons(), MEM_L2_L1_B1B2_col0[1].cons(), of_inter_2.prod()], tile=Tile(0, 3))
    worker_add_col1_w0 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A3A4_col1[0].cons(), MEM_L2_L1_B3B4_col1[0].cons(), of_inter_3.prod()], tile=Tile(1, 5))
    worker_add_col1_w1 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A3A4_col1[1].cons(), MEM_L2_L1_B3B4_col1[1].cons(), of_inter_4.prod()], tile=Tile(1, 3))
    worker_add_col2_w0 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A5A6_col2[0].cons(), MEM_L2_L1_B5B6_col2[0].cons(), of_inter_5.prod()], tile=Tile(2, 5))
    worker_add_col2_w1 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A5A6_col2[1].cons(), MEM_L2_L1_B5B6_col2[1].cons(), of_inter_6.prod()], tile=Tile(2, 3))
    worker_add_col3_w0 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A7A8_col3[0].cons(), MEM_L2_L1_B7B8_col3[0].cons(), of_inter_7.prod()], tile=Tile(3, 5))
    worker_add_col3_w1 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A7A8_col3[1].cons(), MEM_L2_L1_B7B8_col3[1].cons(), of_inter_8.prod()], tile=Tile(3, 3))
    worker_relu_col0_w0 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_1.cons(), MEM_L1_L2_D1D2_col0[0].prod()], tile=Tile(0, 4))
    worker_relu_col0_w1 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_2.cons(), MEM_L1_L2_D1D2_col0[1].prod()], tile=Tile(0, 2))
    worker_relu_col1_w0 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_3.cons(), MEM_L1_L2_D3D4_col1[0].prod()], tile=Tile(1, 4))
    worker_relu_col1_w1 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_4.cons(), MEM_L1_L2_D3D4_col1[1].prod()], tile=Tile(1, 2))
    worker_relu_col2_w0 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_5.cons(), MEM_L1_L2_D5D6_col2[0].prod()], tile=Tile(2, 4))
    worker_relu_col2_w1 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_6.cons(), MEM_L1_L2_D5D6_col2[1].prod()], tile=Tile(2, 2))
    worker_relu_col3_w0 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_7.cons(), MEM_L1_L2_D7D8_col3[0].prod()], tile=Tile(3, 4))
    worker_relu_col3_w1 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_8.cons(), MEM_L1_L2_D7D8_col3[1].prod()], tile=Tile(3, 2))

    Workers = [worker_add_col0_w0, worker_add_col0_w1, worker_add_col1_w0, worker_add_col1_w1, worker_add_col2_w0, worker_add_col2_w1, worker_add_col3_w0, worker_add_col3_w1, worker_relu_col0_w0, worker_relu_col0_w1, worker_relu_col1_w0, worker_relu_col1_w1, worker_relu_col2_w0, worker_relu_col2_w1, worker_relu_col3_w0, worker_relu_col3_w1]

    # Runtime
    def sequence(a_in, b_in, d_out, in_a_col0, in_a_col1, in_a_col2, in_a_col3, in_b_col0, in_b_col1, in_b_col2, in_b_col3, out_d_col0, out_d_col1, out_d_col2, out_d_col3):
        # Fills
        in_a_col0.fill(a_in, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 0), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        in_a_col1.fill(a_in, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 1), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        in_a_col2.fill(a_in, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 2), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        in_a_col3.fill(a_in, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 3), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        in_b_col0.fill(b_in, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 0), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        in_b_col1.fill(b_in, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 1), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        in_b_col2.fill(b_in, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 2), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        in_b_col3.fill(b_in, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 3), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        # Drains
        out_d_col0.drain(d_out, wait=True, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 0), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        out_d_col1.drain(d_out, wait=True, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 1), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        out_d_col2.drain(d_out, wait=True, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 2), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))
        out_d_col3.drain(d_out, wait=True, tap=TensorAccessPattern(tensor_dims=[data_size], offset=((data_size // 4) * 3), sizes=[((data_size // 4) // (data_size // 8)), (data_size // 8)], strides=[(data_size // 8), 1]))

    rt = Runtime(sequence, [
        data_ty,
        data_ty,
        data_ty,
        of_in_a_col0.prod(tile=Tile(0, 0)),
        of_in_a_col1.prod(tile=Tile(1, 0)),
        of_in_a_col2.prod(tile=Tile(2, 0)),
        of_in_a_col3.prod(tile=Tile(3, 0)),
        of_in_b_col0.prod(tile=Tile(0, 0)),
        of_in_b_col1.prod(tile=Tile(1, 0)),
        of_in_b_col2.prod(tile=Tile(2, 0)),
        of_in_b_col3.prod(tile=Tile(3, 0)),
        of_out_d_col0.cons(tile=Tile(0, 0)),
        of_out_d_col1.cons(tile=Tile(1, 0)),
        of_out_d_col2.cons(tile=Tile(2, 0)),
        of_out_d_col3.cons(tile=Tile(3, 0)),
    ])

    # Program
    my_program = Program(iron.get_current_device(), rt, workers=Workers)

    return my_program.resolve_program()


def main():
    data_size = 128
    A = iron.arange(data_size, dtype=bfloat16, device="npu")
    B = iron.arange(data_size, dtype=bfloat16, device="npu")
    D = iron.zeros(data_size, dtype=bfloat16, device="npu")
    A.data[:] = A.data[:] % 8
    A._sync_to_device()
    B.data[:] = B.data[:] % 8
    B._sync_to_device()

    print(f"A[:8] = {A.numpy()[:8]}")
    print(f"B[:8] = {B.numpy()[:8]}")

    add_activate_test_jit(A, B, D, data_size=data_size)

    print(f"D[:8] = {D.numpy()[:8]}")
    print(f"A[:8] = {A.numpy()[:8]}")
    print(f"B[:8] = {B.numpy()[:8]}")
    print(f"D[:8] = {D.numpy()[:8]}")
    expected = np.maximum(A.numpy().astype(np.float32) + B.numpy().astype(np.float32), 0)
    print(f"expected[:8] = {expected[:8]}")
    print("PASS: D matches relu(A + B)" if np.allclose(D.numpy().astype(np.float32), expected, atol=1e-2) else "FAIL: D does not match relu(A + B)")



if __name__ == "__main__":
    main()