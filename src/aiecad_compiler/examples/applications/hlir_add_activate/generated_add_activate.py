# add_activate_hlir_example.py -*- Python -*-


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
def add_activate_hlir_example_jit(A, B, D):
    # Define tensor types
    data_ty = np.ndarray[(A.numel(),), np.dtype[bfloat16]]
    chunk_ty = np.ndarray[(A.numel() // 4,), np.dtype[bfloat16]]
    worker_chunk_ty = np.ndarray[(A.numel() // 8,), np.dtype[bfloat16]]
    data_a_ty = np.ndarray[(A.numel(),), np.dtype[bfloat16]]
    chunk_a = np.ndarray[(A.numel() // 4,), np.dtype[bfloat16]]
    chunk_a_worker = np.ndarray[(A.numel() // 8,), np.dtype[bfloat16]]
    data_b_ty = np.ndarray[(B.numel(),), np.dtype[bfloat16]]
    chunk_b = np.ndarray[(B.numel() // 4,), np.dtype[bfloat16]]
    chunk_b_worker = np.ndarray[(B.numel() // 8,), np.dtype[bfloat16]]
    data_d_ty = np.ndarray[(D.numel(),), np.dtype[bfloat16]]
    chunk_d = np.ndarray[(D.numel() // 4,), np.dtype[bfloat16]]
    chunk_d_worker = np.ndarray[(D.numel() // 8,), np.dtype[bfloat16]]

    # Data movement with ObjectFifos
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
    MEM_L2_L1_A1A2_col0 = of_in_a_col0.cons().split(obj_types=[chunk_a_worker, chunk_a_worker], offsets=[((A.numel() // 8) * 0), ((A.numel() // 8) * 1)], names=["MEM_L2_L1_A1_col0", "MEM_L2_L1_A2_col0"], placement=Tile(0, 1))
    MEM_L2_L1_A3A4_col1 = of_in_a_col1.cons().split(obj_types=[chunk_a_worker, chunk_a_worker], offsets=[((A.numel() // 8) * 0), ((A.numel() // 8) * 1)], names=["MEM_L2_L1_A3_col1", "MEM_L2_L1_A4_col1"], placement=Tile(1, 1))
    MEM_L2_L1_A5A6_col2 = of_in_a_col2.cons().split(obj_types=[chunk_a_worker, chunk_a_worker], offsets=[((A.numel() // 8) * 0), ((A.numel() // 8) * 1)], names=["MEM_L2_L1_A5_col2", "MEM_L2_L1_A6_col2"], placement=Tile(2, 1))
    MEM_L2_L1_A7A8_col3 = of_in_a_col3.cons().split(obj_types=[chunk_a_worker, chunk_a_worker], offsets=[((A.numel() // 8) * 0), ((A.numel() // 8) * 1)], names=["MEM_L2_L1_A7_col3", "MEM_L2_L1_A8_col3"], placement=Tile(3, 1))
    MEM_L2_L1_B1B2_col0 = of_in_b_col0.cons().split(obj_types=[chunk_b_worker, chunk_b_worker], offsets=[((B.numel() // 8) * 0), ((B.numel() // 8) * 1)], names=["MEM_L2_L1_B1_col0", "MEM_L2_L1_B2_col0"], placement=Tile(0, 1))
    MEM_L2_L1_B3B4_col1 = of_in_b_col1.cons().split(obj_types=[chunk_b_worker, chunk_b_worker], offsets=[((B.numel() // 8) * 0), ((B.numel() // 8) * 1)], names=["MEM_L2_L1_B3_col1", "MEM_L2_L1_B4_col1"], placement=Tile(1, 1))
    MEM_L2_L1_B5B6_col2 = of_in_b_col2.cons().split(obj_types=[chunk_b_worker, chunk_b_worker], offsets=[((B.numel() // 8) * 0), ((B.numel() // 8) * 1)], names=["MEM_L2_L1_B5_col2", "MEM_L2_L1_B6_col2"], placement=Tile(2, 1))
    MEM_L2_L1_B7B8_col3 = of_in_b_col3.cons().split(obj_types=[chunk_b_worker, chunk_b_worker], offsets=[((B.numel() // 8) * 0), ((B.numel() // 8) * 1)], names=["MEM_L2_L1_B7_col3", "MEM_L2_L1_B8_col3"], placement=Tile(3, 1))
    MEM_L1_L2_D1D2_col0 = of_out_d_col0.prod().join(obj_types=[chunk_d_worker, chunk_d_worker], names=["MEM_L1_L2_D1_col0", "MEM_L1_L2_D2_col0"], placement=Tile(0, 1), offsets=[((D.numel() // 8) * 0), ((D.numel() // 8) * 1)])
    MEM_L1_L2_D3D4_col1 = of_out_d_col1.prod().join(obj_types=[chunk_d_worker, chunk_d_worker], names=["MEM_L1_L2_D3_col1", "MEM_L1_L2_D4_col1"], placement=Tile(1, 1), offsets=[((D.numel() // 8) * 0), ((D.numel() // 8) * 1)])
    MEM_L1_L2_D5D6_col2 = of_out_d_col2.prod().join(obj_types=[chunk_d_worker, chunk_d_worker], names=["MEM_L1_L2_D5_col2", "MEM_L1_L2_D6_col2"], placement=Tile(2, 1), offsets=[((D.numel() // 8) * 0), ((D.numel() // 8) * 1)])
    MEM_L1_L2_D7D8_col3 = of_out_d_col3.prod().join(obj_types=[chunk_d_worker, chunk_d_worker], names=["MEM_L1_L2_D7_col3", "MEM_L1_L2_D8_col3"], placement=Tile(3, 1), offsets=[((D.numel() // 8) * 0), ((D.numel() // 8) * 1)])

    #Define kernels here... ------------------------------------------------\/
    externalfunc1 = ExternalFunction(
        name="eltwise_add_bf16_scalar", source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/add.cc", arg_types=[worker_chunk_ty, worker_chunk_ty, worker_chunk_ty], include_dirs=["/scratch/IRONSmithTesting/mlir-aie/aie_kernels", "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"]
    )

    externalfunc2 = ExternalFunction(
        name="bf16_relu", source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/relu.cc", arg_types=[worker_chunk_ty, worker_chunk_ty], include_dirs=["/scratch/IRONSmithTesting/mlir-aie/aie_kernels", "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"]
    )

    # core_fn here:
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

    #Workers defined here:
    Workers = []
    worker_add_col0_w0 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A1A2_col0[0].cons(), MEM_L2_L1_B1B2_col0[0].cons(), of_inter_1.prod()], placement=Tile(0, 5))
    worker_add_col0_w1 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A1A2_col0[1].cons(), MEM_L2_L1_B1B2_col0[1].cons(), of_inter_2.prod()], placement=Tile(0, 3))
    worker_add_col1_w0 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A3A4_col1[0].cons(), MEM_L2_L1_B3B4_col1[0].cons(), of_inter_3.prod()], placement=Tile(1, 5))
    worker_add_col1_w1 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A3A4_col1[1].cons(), MEM_L2_L1_B3B4_col1[1].cons(), of_inter_4.prod()], placement=Tile(1, 3))
    worker_add_col2_w0 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A5A6_col2[0].cons(), MEM_L2_L1_B5B6_col2[0].cons(), of_inter_5.prod()], placement=Tile(2, 5))
    worker_add_col2_w1 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A5A6_col2[1].cons(), MEM_L2_L1_B5B6_col2[1].cons(), of_inter_6.prod()], placement=Tile(2, 3))
    worker_add_col3_w0 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A7A8_col3[0].cons(), MEM_L2_L1_B7B8_col3[0].cons(), of_inter_7.prod()], placement=Tile(3, 5))
    worker_add_col3_w1 = Worker(core_fn=corefunc1, fn_args=[externalfunc1, MEM_L2_L1_A7A8_col3[1].cons(), MEM_L2_L1_B7B8_col3[1].cons(), of_inter_8.prod()], placement=Tile(3, 3))
    worker_relu_col0_w0 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_1.cons(), MEM_L1_L2_D1D2_col0[0].prod()], placement=Tile(0, 4))
    worker_relu_col0_w1 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_2.cons(), MEM_L1_L2_D1D2_col0[1].prod()], placement=Tile(0, 2))
    worker_relu_col1_w0 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_3.cons(), MEM_L1_L2_D3D4_col1[0].prod()], placement=Tile(1, 4))
    worker_relu_col1_w1 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_4.cons(), MEM_L1_L2_D3D4_col1[1].prod()], placement=Tile(1, 2))
    worker_relu_col2_w0 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_5.cons(), MEM_L1_L2_D5D6_col2[0].prod()], placement=Tile(2, 4))
    worker_relu_col2_w1 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_6.cons(), MEM_L1_L2_D5D6_col2[1].prod()], placement=Tile(2, 2))
    worker_relu_col3_w0 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_7.cons(), MEM_L1_L2_D7D8_col3[0].prod()], placement=Tile(3, 4))
    worker_relu_col3_w1 = Worker(core_fn=corefunc2, fn_args=[externalfunc2, of_inter_8.cons(), MEM_L1_L2_D7D8_col3[1].prod()], placement=Tile(3, 2))

    Workers = [worker_add_col0_w0, worker_add_col0_w1, worker_add_col1_w0, worker_add_col1_w1, worker_add_col2_w0, worker_add_col2_w1, worker_add_col3_w0, worker_add_col3_w1, worker_relu_col0_w0, worker_relu_col0_w1, worker_relu_col1_w0, worker_relu_col1_w1, worker_relu_col2_w0, worker_relu_col2_w1, worker_relu_col3_w0, worker_relu_col3_w1]

    # Runtime operations to move data to/from the AIE-array
    rt = Runtime()
    with rt.sequence(data_ty, data_ty, data_ty) as (a_in, b_in, d_out):
        rt.start(*Workers)
        rt.fill(placement=Tile(0, 0), in_fifo=of_in_a_col0.prod(), source=a_in, tap=TensorAccessPattern(tensor_dims=[A.numel()], offset=((A.numel() // 4) * 0), sizes=[((A.numel() // 4) // (A.numel() // 8)), (A.numel() // 8)], strides=[(A.numel() // 8), 1]))
        rt.fill(placement=Tile(1, 0), in_fifo=of_in_a_col1.prod(), source=a_in, tap=TensorAccessPattern(tensor_dims=[A.numel()], offset=((A.numel() // 4) * 1), sizes=[((A.numel() // 4) // (A.numel() // 8)), (A.numel() // 8)], strides=[(A.numel() // 8), 1]))
        rt.fill(placement=Tile(2, 0), in_fifo=of_in_a_col2.prod(), source=a_in, tap=TensorAccessPattern(tensor_dims=[A.numel()], offset=((A.numel() // 4) * 2), sizes=[((A.numel() // 4) // (A.numel() // 8)), (A.numel() // 8)], strides=[(A.numel() // 8), 1]))
        rt.fill(placement=Tile(3, 0), in_fifo=of_in_a_col3.prod(), source=a_in, tap=TensorAccessPattern(tensor_dims=[A.numel()], offset=((A.numel() // 4) * 3), sizes=[((A.numel() // 4) // (A.numel() // 8)), (A.numel() // 8)], strides=[(A.numel() // 8), 1]))
        rt.fill(placement=Tile(0, 0), in_fifo=of_in_b_col0.prod(), source=b_in, tap=TensorAccessPattern(tensor_dims=[B.numel()], offset=((B.numel() // 4) * 0), sizes=[((B.numel() // 4) // (B.numel() // 8)), (B.numel() // 8)], strides=[(B.numel() // 8), 1]))
        rt.fill(placement=Tile(1, 0), in_fifo=of_in_b_col1.prod(), source=b_in, tap=TensorAccessPattern(tensor_dims=[B.numel()], offset=((B.numel() // 4) * 1), sizes=[((B.numel() // 4) // (B.numel() // 8)), (B.numel() // 8)], strides=[(B.numel() // 8), 1]))
        rt.fill(placement=Tile(2, 0), in_fifo=of_in_b_col2.prod(), source=b_in, tap=TensorAccessPattern(tensor_dims=[B.numel()], offset=((B.numel() // 4) * 2), sizes=[((B.numel() // 4) // (B.numel() // 8)), (B.numel() // 8)], strides=[(B.numel() // 8), 1]))
        rt.fill(placement=Tile(3, 0), in_fifo=of_in_b_col3.prod(), source=b_in, tap=TensorAccessPattern(tensor_dims=[B.numel()], offset=((B.numel() // 4) * 3), sizes=[((B.numel() // 4) // (B.numel() // 8)), (B.numel() // 8)], strides=[(B.numel() // 8), 1]))
        rt.drain(placement=Tile(0, 0), out_fifo=of_out_d_col0.cons(), dest=d_out, wait=True, tap=TensorAccessPattern(tensor_dims=[D.numel()], offset=((D.numel() // 4) * 0), sizes=[((D.numel() // 4) // (D.numel() // 8)), (D.numel() // 8)], strides=[(D.numel() // 8), 1]))
        rt.drain(placement=Tile(1, 0), out_fifo=of_out_d_col1.cons(), dest=d_out, wait=True, tap=TensorAccessPattern(tensor_dims=[D.numel()], offset=((D.numel() // 4) * 1), sizes=[((D.numel() // 4) // (D.numel() // 8)), (D.numel() // 8)], strides=[(D.numel() // 8), 1]))
        rt.drain(placement=Tile(2, 0), out_fifo=of_out_d_col2.cons(), dest=d_out, wait=True, tap=TensorAccessPattern(tensor_dims=[D.numel()], offset=((D.numel() // 4) * 2), sizes=[((D.numel() // 4) // (D.numel() // 8)), (D.numel() // 8)], strides=[(D.numel() // 8), 1]))
        rt.drain(placement=Tile(3, 0), out_fifo=of_out_d_col3.cons(), dest=d_out, wait=True, tap=TensorAccessPattern(tensor_dims=[D.numel()], offset=((D.numel() // 4) * 3), sizes=[((D.numel() // 4) // (D.numel() // 8)), (D.numel() // 8)], strides=[(D.numel() // 8), 1]))

    # Create the program from the current device and runtime
    my_program = Program(iron.get_current_device(), rt)

    # Place components and resolve program (generate MLIR + compile)
    return my_program.resolve_program(SequentialPlacer())


def main():
    data_size = 128
    A = iron.arange(data_size, dtype=bfloat16, device="npu")
    B = iron.arange(data_size, dtype=bfloat16, device="npu")
    D = iron.zeros(data_size, dtype=bfloat16, device="npu")
    add_activate_hlir_example_jit(A, B, D)



if __name__ == "__main__":
    main()