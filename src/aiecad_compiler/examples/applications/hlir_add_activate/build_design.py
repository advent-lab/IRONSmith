#!/usr/bin/env python3
"""
HLIR Add-Activate Example - Complete End-to-End Workflow

This example demonstrates the complete HLIR workflow for an add+ReLU pipeline:
1. Build design using HLIR ProgramBuilder (clean Python API)
2. Serialize to GUI XML format using GUIXMLSerializer
3. Process with XMLGenerator to expand to Complete XML
4. Build semantic graph with GraphDriver
5. Generate Python code with CodeGenerator

This shows a more complex multi-column design with:
- 2 input tensors (A, B) split across 4 columns
- Element-wise addition on 8 workers (2 per column)
- ReLU activation on 8 workers (2 per column)
- Output tensor (D) joined across 4 columns
"""

import sys
from pathlib import Path

# Add parent directories to path for imports
hlir_path = Path(__file__).parent.parent.parent.parent / "hlir"
sys.path.insert(0, str(hlir_path.parent))

from hlir import ProgramBuilder, GUIXMLSerializer


def build_add_activate_design():
    """
    Build an add-activate (add + ReLU) design using HLIR.

    This design:
    - Takes two input vectors (A, B) from host
    - Splits each across 4 columns, then further splits to 8 workers
    - Performs element-wise addition on 8 compute tiles
    - Applies ReLU activation on 8 compute tiles
    - Joins results and returns to host

    Architecture:
    - 4 SHIM tiles (columns 0-3) for DMA
    - Input FIFOs for A and B (4 per input, one per column)
    - Split operations to divide work among 2 workers per column
    - 8 intermediate FIFOs between add and ReLU stages
    - Join operations to combine worker outputs
    - Output FIFOs for D (4 total, one per column)
    - 16 total workers (8 for add, 8 for ReLU)
    """
    print("=" * 80)
    print("Building Add-Activate Design with HLIR")
    print("=" * 80)

    # Create program builder
    builder = ProgramBuilder("add_activate_hlir_example")
    print("\n[1] Created ProgramBuilder")

    # Add constants
    builder.add_constant("data_size", 128, "int")
    print("[2] Added constant: data_size=128")

    # Add type definitions
    # Full data type (entire vector)
    builder.add_tensor_type("data_ty", shape=["data_size"], dtype="bfloat16")
    # Chunk per column (data_size / 4)
    builder.add_tensor_type("chunk_ty", shape=["data_size / 4"], dtype="bfloat16")
    # Worker chunk (data_size / 8)
    builder.add_tensor_type("worker_chunk_ty", shape=["data_size / 8"], dtype="bfloat16")
    print("[3] Added type definitions: data_ty, chunk_ty, worker_chunk_ty")

    # Add tiles (4 SHIM tiles for DMA, one per column)
    print("[4] Adding tiles...")
    shim0 = builder.add_tile("shim0", kind="shim", x=0, y=0)
    shim1 = builder.add_tile("shim1", kind="shim", x=1, y=0)
    shim2 = builder.add_tile("shim2", kind="shim", x=2, y=0)
    shim3 = builder.add_tile("shim3", kind="shim", x=3, y=0)
    print(f"    - Added 4 SHIM tiles")

    # Add memory tiles for split/join operations
    mem0 = builder.add_tile("mem0", kind="mem", x=0, y=1)
    mem1 = builder.add_tile("mem1", kind="mem", x=1, y=1)
    mem2 = builder.add_tile("mem2", kind="mem", x=2, y=1)
    mem3 = builder.add_tile("mem3", kind="mem", x=3, y=1)
    print(f"    - Added 4 MEM tiles")

    # Add compute tiles for workers
    print("    - Adding compute tiles for workers...")
    # Column 0 workers
    builder.add_tile("tile_0_5", kind="compute", x=0, y=5)  # add worker 0
    builder.add_tile("tile_0_3", kind="compute", x=0, y=3)  # add worker 1
    builder.add_tile("tile_0_4", kind="compute", x=0, y=4)  # relu worker 0
    builder.add_tile("tile_0_2", kind="compute", x=0, y=2)  # relu worker 1
    # Column 1 workers
    builder.add_tile("tile_1_5", kind="compute", x=1, y=5)
    builder.add_tile("tile_1_3", kind="compute", x=1, y=3)
    builder.add_tile("tile_1_4", kind="compute", x=1, y=4)
    builder.add_tile("tile_1_2", kind="compute", x=1, y=2)
    # Column 2 workers
    builder.add_tile("tile_2_5", kind="compute", x=2, y=5)
    builder.add_tile("tile_2_3", kind="compute", x=2, y=3)
    builder.add_tile("tile_2_4", kind="compute", x=2, y=4)
    builder.add_tile("tile_2_2", kind="compute", x=2, y=2)
    # Column 3 workers
    builder.add_tile("tile_3_5", kind="compute", x=3, y=5)
    builder.add_tile("tile_3_3", kind="compute", x=3, y=3)
    builder.add_tile("tile_3_4", kind="compute", x=3, y=4)
    builder.add_tile("tile_3_2", kind="compute", x=3, y=2)
    print(f"    - Added 16 compute tiles")

    # Add input ObjectFifos for A (4 columns)
    print("[5] Adding ObjectFifos...")
    of_in_a_col0 = builder.add_fifo("of_in_a_col0", obj_type="chunk_ty", depth=2,
                                     context="L3_L2", direction="input", data="A", column=0)
    of_in_a_col1 = builder.add_fifo("of_in_a_col1", obj_type="chunk_ty", depth=2,
                                     context="L3_L2", direction="input", data="A", column=1)
    of_in_a_col2 = builder.add_fifo("of_in_a_col2", obj_type="chunk_ty", depth=2,
                                     context="L3_L2", direction="input", data="A", column=2)
    of_in_a_col3 = builder.add_fifo("of_in_a_col3", obj_type="chunk_ty", depth=2,
                                     context="L3_L2", direction="input", data="A", column=3)
    print(f"    - Added 4 input FIFOs for A")

    # Add input ObjectFifos for B (4 columns)
    of_in_b_col0 = builder.add_fifo("of_in_b_col0", obj_type="chunk_ty", depth=2,
                                     context="L3_L2", direction="input", data="B", column=0)
    of_in_b_col1 = builder.add_fifo("of_in_b_col1", obj_type="chunk_ty", depth=2,
                                     context="L3_L2", direction="input", data="B", column=1)
    of_in_b_col2 = builder.add_fifo("of_in_b_col2", obj_type="chunk_ty", depth=2,
                                     context="L3_L2", direction="input", data="B", column=2)
    of_in_b_col3 = builder.add_fifo("of_in_b_col3", obj_type="chunk_ty", depth=2,
                                     context="L3_L2", direction="input", data="B", column=3)
    print(f"    - Added 4 input FIFOs for B")

    # Add split operations for A (split each column's chunk to 2 workers)
    print("[6] Adding split operations...")
    split_a_col0 = builder.add_fifo_split(
        "split_a_col0", source="of_in_a_col0", num_outputs=2, output_type="worker_chunk_ty",
        output_names=["split_a_col0_0", "split_a_col0_1"], offsets=[0, "data_size / 8"],
        placement="mem0", context="L2_L1", data="A", column=0
    )
    split_a_col1 = builder.add_fifo_split(
        "split_a_col1", source="of_in_a_col1", num_outputs=2, output_type="worker_chunk_ty",
        output_names=["split_a_col1_0", "split_a_col1_1"], offsets=[0, "data_size / 8"],
        placement="mem1", context="L2_L1", data="A", column=1
    )
    split_a_col2 = builder.add_fifo_split(
        "split_a_col2", source="of_in_a_col2", num_outputs=2, output_type="worker_chunk_ty",
        output_names=["split_a_col2_0", "split_a_col2_1"], offsets=[0, "data_size / 8"],
        placement="mem2", context="L2_L1", data="A", column=2
    )
    split_a_col3 = builder.add_fifo_split(
        "split_a_col3", source="of_in_a_col3", num_outputs=2, output_type="worker_chunk_ty",
        output_names=["split_a_col3_0", "split_a_col3_1"], offsets=[0, "data_size / 8"],
        placement="mem3", context="L2_L1", data="A", column=3
    )
    print(f"    - Added 4 split operations for A")

    # Add split operations for B (split each column's chunk to 2 workers)
    split_b_col0 = builder.add_fifo_split(
        "split_b_col0", source="of_in_b_col0", num_outputs=2, output_type="worker_chunk_ty",
        output_names=["split_b_col0_0", "split_b_col0_1"], offsets=[0, "data_size / 8"],
        placement="mem0", context="L2_L1", data="B", column=0
    )
    split_b_col1 = builder.add_fifo_split(
        "split_b_col1", source="of_in_b_col1", num_outputs=2, output_type="worker_chunk_ty",
        output_names=["split_b_col1_0", "split_b_col1_1"], offsets=[0, "data_size / 8"],
        placement="mem1", context="L2_L1", data="B", column=1
    )
    split_b_col2 = builder.add_fifo_split(
        "split_b_col2", source="of_in_b_col2", num_outputs=2, output_type="worker_chunk_ty",
        output_names=["split_b_col2_0", "split_b_col2_1"], offsets=[0, "data_size / 8"],
        placement="mem2", context="L2_L1", data="B", column=2
    )
    split_b_col3 = builder.add_fifo_split(
        "split_b_col3", source="of_in_b_col3", num_outputs=2, output_type="worker_chunk_ty",
        output_names=["split_b_col3_0", "split_b_col3_1"], offsets=[0, "data_size / 8"],
        placement="mem3", context="L2_L1", data="B", column=3
    )
    print(f"    - Added 4 split operations for B")

    # Add intermediate FIFOs between add and relu stages (8 total, 2 per column)
    print("[7] Adding intermediate FIFOs...")
    of_inter_1 = builder.add_fifo("of_inter_1", obj_type="worker_chunk_ty", depth=2,
                                   context="L1_L1", direction="intermediate", stage="add_to_relu", worker=1)
    of_inter_2 = builder.add_fifo("of_inter_2", obj_type="worker_chunk_ty", depth=2,
                                   context="L1_L1", direction="intermediate", stage="add_to_relu", worker=2)
    of_inter_3 = builder.add_fifo("of_inter_3", obj_type="worker_chunk_ty", depth=2,
                                   context="L1_L1", direction="intermediate", stage="add_to_relu", worker=3)
    of_inter_4 = builder.add_fifo("of_inter_4", obj_type="worker_chunk_ty", depth=2,
                                   context="L1_L1", direction="intermediate", stage="add_to_relu", worker=4)
    of_inter_5 = builder.add_fifo("of_inter_5", obj_type="worker_chunk_ty", depth=2,
                                   context="L1_L1", direction="intermediate", stage="add_to_relu", worker=5)
    of_inter_6 = builder.add_fifo("of_inter_6", obj_type="worker_chunk_ty", depth=2,
                                   context="L1_L1", direction="intermediate", stage="add_to_relu", worker=6)
    of_inter_7 = builder.add_fifo("of_inter_7", obj_type="worker_chunk_ty", depth=2,
                                   context="L1_L1", direction="intermediate", stage="add_to_relu", worker=7)
    of_inter_8 = builder.add_fifo("of_inter_8", obj_type="worker_chunk_ty", depth=2,
                                   context="L1_L1", direction="intermediate", stage="add_to_relu", worker=8)
    print(f"    - Added 8 intermediate FIFOs")

    # Add output ObjectFifos for D (4 columns)
    print("[8] Adding output FIFOs...")
    of_out_d_col0 = builder.add_fifo("of_out_d_col0", obj_type="chunk_ty", depth=2,
                                      context="L2_L3", direction="output", data="D", column=0)
    of_out_d_col1 = builder.add_fifo("of_out_d_col1", obj_type="chunk_ty", depth=2,
                                      context="L2_L3", direction="output", data="D", column=1)
    of_out_d_col2 = builder.add_fifo("of_out_d_col2", obj_type="chunk_ty", depth=2,
                                      context="L2_L3", direction="output", data="D", column=2)
    of_out_d_col3 = builder.add_fifo("of_out_d_col3", obj_type="chunk_ty", depth=2,
                                      context="L2_L3", direction="output", data="D", column=3)
    print(f"    - Added 4 output FIFOs for D")

    # Add join operations for D (join 2 worker outputs per column)
    print("[9] Adding join operations...")
    join_d_col0 = builder.add_fifo_join(
        "join_d_col0", dest="of_out_d_col0", num_inputs=2, input_type="worker_chunk_ty",
        input_names=["join_d_col0_0", "join_d_col0_1"], offsets=[0, "data_size / 8"],
        placement="mem0", context="L1_L2", data="D", column=0
    )
    join_d_col1 = builder.add_fifo_join(
        "join_d_col1", dest="of_out_d_col1", num_inputs=2, input_type="worker_chunk_ty",
        input_names=["join_d_col1_0", "join_d_col1_1"], offsets=[0, "data_size / 8"],
        placement="mem1", context="L1_L2", data="D", column=1
    )
    join_d_col2 = builder.add_fifo_join(
        "join_d_col2", dest="of_out_d_col2", num_inputs=2, input_type="worker_chunk_ty",
        input_names=["join_d_col2_0", "join_d_col2_1"], offsets=[0, "data_size / 8"],
        placement="mem2", context="L1_L2", data="D", column=2
    )
    join_d_col3 = builder.add_fifo_join(
        "join_d_col3", dest="of_out_d_col3", num_inputs=2, input_type="worker_chunk_ty",
        input_names=["join_d_col3_0", "join_d_col3_1"], offsets=[0, "data_size / 8"],
        placement="mem3", context="L1_L2", data="D", column=3
    )
    print(f"    - Added 4 join operations for D")

    # Add external kernel functions
    print("[10] Adding external kernels...")
    externalfunc1 = builder.add_external_kernel(
        "externalfunc1", kernel_name="eltwise_add_bf16_scalar",
        source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/add.cc",
        arg_types=["worker_chunk_ty", "worker_chunk_ty", "worker_chunk_ty"],
        operation="element_wise_add",
        include_dirs=[
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels",
            "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"
        ]
    )
    externalfunc2 = builder.add_external_kernel(
        "externalfunc2", kernel_name="bf16_relu",
        source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/relu.cc",
        arg_types=["worker_chunk_ty", "worker_chunk_ty"],
        operation="relu_activation",
        include_dirs=[
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels",
            "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"
        ]
    )
    print(f"    - Added 2 external kernels: add and relu")

    # Add core functions
    print("[11] Adding core functions...")
    corefunc1 = builder.add_core_function(
        "corefunc1",
        parameters=["kernel", "inputA", "inputB", "outputC"],
        acquires=[("inputA", 1, "elementA"), ("inputB", 1, "elementB"), ("outputC", 1, "elementC")],
        kernel_call=("kernel", ["elementA", "elementB", "elementC"]),
        releases=[("inputA", 1), ("inputB", 1), ("outputC", 1)],
        operation="eltwise_add"
    )
    corefunc2 = builder.add_core_function(
        "corefunc2",
        parameters=["kernel", "inputC", "outputD"],
        acquires=[("inputC", 1, "elementC"), ("outputD", 1, "elementD")],
        kernel_call=("kernel", ["elementC", "elementD"]),
        releases=[("inputC", 1), ("outputD", 1)],
        operation="relu"
    )
    print(f"    - Added 2 core functions: add and relu")

    # Add workers for add operation (8 workers, 2 per column)
    print("[12] Adding workers...")
    # Column 0 add workers
    worker_add_col0_w0 = builder.add_worker(
        "worker_add_col0_w0", core_fn="corefunc1",
        fn_args=["externalfunc1", ("split_a_col0", "cons", 0), ("split_b_col0", "cons", 0), ("of_inter_1", "prod", None)],
        placement="tile_0_5", operation="add", column=0, worker_index=0
    )
    worker_add_col0_w1 = builder.add_worker(
        "worker_add_col0_w1", core_fn="corefunc1",
        fn_args=["externalfunc1", ("split_a_col0", "cons", 1), ("split_b_col0", "cons", 1), ("of_inter_2", "prod", None)],
        placement="tile_0_3", operation="add", column=0, worker_index=1
    )
    # Column 1 add workers
    worker_add_col1_w0 = builder.add_worker(
        "worker_add_col1_w0", core_fn="corefunc1",
        fn_args=["externalfunc1", ("split_a_col1", "cons", 0), ("split_b_col1", "cons", 0), ("of_inter_3", "prod", None)],
        placement="tile_1_5", operation="add", column=1, worker_index=0
    )
    worker_add_col1_w1 = builder.add_worker(
        "worker_add_col1_w1", core_fn="corefunc1",
        fn_args=["externalfunc1", ("split_a_col1", "cons", 1), ("split_b_col1", "cons", 1), ("of_inter_4", "prod", None)],
        placement="tile_1_3", operation="add", column=1, worker_index=1
    )
    # Column 2 add workers
    worker_add_col2_w0 = builder.add_worker(
        "worker_add_col2_w0", core_fn="corefunc1",
        fn_args=["externalfunc1", ("split_a_col2", "cons", 0), ("split_b_col2", "cons", 0), ("of_inter_5", "prod", None)],
        placement="tile_2_5", operation="add", column=2, worker_index=0
    )
    worker_add_col2_w1 = builder.add_worker(
        "worker_add_col2_w1", core_fn="corefunc1",
        fn_args=["externalfunc1", ("split_a_col2", "cons", 1), ("split_b_col2", "cons", 1), ("of_inter_6", "prod", None)],
        placement="tile_2_3", operation="add", column=2, worker_index=1
    )
    # Column 3 add workers
    worker_add_col3_w0 = builder.add_worker(
        "worker_add_col3_w0", core_fn="corefunc1",
        fn_args=["externalfunc1", ("split_a_col3", "cons", 0), ("split_b_col3", "cons", 0), ("of_inter_7", "prod", None)],
        placement="tile_3_5", operation="add", column=3, worker_index=0
    )
    worker_add_col3_w1 = builder.add_worker(
        "worker_add_col3_w1", core_fn="corefunc1",
        fn_args=["externalfunc1", ("split_a_col3", "cons", 1), ("split_b_col3", "cons", 1), ("of_inter_8", "prod", None)],
        placement="tile_3_3", operation="add", column=3, worker_index=1
    )
    print(f"    - Added 8 add workers")

    # Add workers for relu operation (8 workers, 2 per column)
    # Column 0 relu workers
    worker_relu_col0_w0 = builder.add_worker(
        "worker_relu_col0_w0", core_fn="corefunc2",
        fn_args=["externalfunc2", ("of_inter_1", "cons", None), ("join_d_col0", "prod", 0)],
        placement="tile_0_4", operation="relu", column=0, worker_index=0
    )
    worker_relu_col0_w1 = builder.add_worker(
        "worker_relu_col0_w1", core_fn="corefunc2",
        fn_args=["externalfunc2", ("of_inter_2", "cons", None), ("join_d_col0", "prod", 1)],
        placement="tile_0_2", operation="relu", column=0, worker_index=1
    )
    # Column 1 relu workers
    worker_relu_col1_w0 = builder.add_worker(
        "worker_relu_col1_w0", core_fn="corefunc2",
        fn_args=["externalfunc2", ("of_inter_3", "cons", None), ("join_d_col1", "prod", 0)],
        placement="tile_1_4", operation="relu", column=1, worker_index=0
    )
    worker_relu_col1_w1 = builder.add_worker(
        "worker_relu_col1_w1", core_fn="corefunc2",
        fn_args=["externalfunc2", ("of_inter_4", "cons", None), ("join_d_col1", "prod", 1)],
        placement="tile_1_2", operation="relu", column=1, worker_index=1
    )
    # Column 2 relu workers
    worker_relu_col2_w0 = builder.add_worker(
        "worker_relu_col2_w0", core_fn="corefunc2",
        fn_args=["externalfunc2", ("of_inter_5", "cons", None), ("join_d_col2", "prod", 0)],
        placement="tile_2_4", operation="relu", column=2, worker_index=0
    )
    worker_relu_col2_w1 = builder.add_worker(
        "worker_relu_col2_w1", core_fn="corefunc2",
        fn_args=["externalfunc2", ("of_inter_6", "cons", None), ("join_d_col2", "prod", 1)],
        placement="tile_2_2", operation="relu", column=2, worker_index=1
    )
    # Column 3 relu workers
    worker_relu_col3_w0 = builder.add_worker(
        "worker_relu_col3_w0", core_fn="corefunc2",
        fn_args=["externalfunc2", ("of_inter_7", "cons", None), ("join_d_col3", "prod", 0)],
        placement="tile_3_4", operation="relu", column=3, worker_index=0
    )
    worker_relu_col3_w1 = builder.add_worker(
        "worker_relu_col3_w1", core_fn="corefunc2",
        fn_args=["externalfunc2", ("of_inter_8", "cons", None), ("join_d_col3", "prod", 1)],
        placement="tile_3_2", operation="relu", column=3, worker_index=1
    )
    print(f"    - Added 8 relu workers")

    # Create runtime sequence
    print("[13] Building runtime sequence...")
    rt = builder.create_runtime("runtime")

    # Add input/output types (use full data_ty, not chunk_ty)
    rt.add_input_type("data_ty")
    rt.add_input_type("data_ty")
    rt.add_output_type("data_ty")
    rt.add_params(["A", "B", "D"])

    # Add all workers to start
    rt.add_worker("worker_add_col0_w0")
    rt.add_worker("worker_add_col0_w1")
    rt.add_worker("worker_add_col1_w0")
    rt.add_worker("worker_add_col1_w1")
    rt.add_worker("worker_add_col2_w0")
    rt.add_worker("worker_add_col2_w1")
    rt.add_worker("worker_add_col3_w0")
    rt.add_worker("worker_add_col3_w1")
    rt.add_worker("worker_relu_col0_w0")
    rt.add_worker("worker_relu_col0_w1")
    rt.add_worker("worker_relu_col1_w0")
    rt.add_worker("worker_relu_col1_w1")
    rt.add_worker("worker_relu_col2_w0")
    rt.add_worker("worker_relu_col2_w1")
    rt.add_worker("worker_relu_col3_w0")
    rt.add_worker("worker_relu_col3_w1")

    # Add fill operations for A
    rt.add_fill("fill_a_col0", "of_in_a_col0", "A", "shim0", column=0, use_tap=True)
    rt.add_fill("fill_a_col1", "of_in_a_col1", "A", "shim1", column=1, use_tap=True)
    rt.add_fill("fill_a_col2", "of_in_a_col2", "A", "shim2", column=2, use_tap=True)
    rt.add_fill("fill_a_col3", "of_in_a_col3", "A", "shim3", column=3, use_tap=True)

    # Add fill operations for B
    rt.add_fill("fill_b_col0", "of_in_b_col0", "B", "shim0", column=0, use_tap=True)
    rt.add_fill("fill_b_col1", "of_in_b_col1", "B", "shim1", column=1, use_tap=True)
    rt.add_fill("fill_b_col2", "of_in_b_col2", "B", "shim2", column=2, use_tap=True)
    rt.add_fill("fill_b_col3", "of_in_b_col3", "B", "shim3", column=3, use_tap=True)

    # Add drain operations for D
    rt.add_drain("drain_d_col0", "of_out_d_col0", "D", "shim0", wait=True, column=0, use_tap=True)
    rt.add_drain("drain_d_col1", "of_out_d_col1", "D", "shim1", wait=True, column=1, use_tap=True)
    rt.add_drain("drain_d_col2", "of_out_d_col2", "D", "shim2", wait=True, column=2, use_tap=True)
    rt.add_drain("drain_d_col3", "of_out_d_col3", "D", "shim3", wait=True, column=3, use_tap=True)

    rt.build()
    print("    - Added input types: data_ty (A, B)")
    print("    - Added output type: data_ty (D)")
    print("    - Added parameters: A, B, D")
    print("    - Added 16 workers to start")
    print("    - Added 8 fill operations (4 for A, 4 for B)")
    print("    - Added 4 drain operations for D")

    # Build program with validation
    print("\n[14] Building and validating program...")
    program = builder.build()
    print(f"    [OK] Program built successfully: {program}")
    print(f"    - Tiles: {len(program.tiles)}")
    print(f"    - FIFOs: {len(program.fifos)}")
    print(f"    - Symbols: {len(program.symbols)}")
    print(f"    - External kernels: {len(program.external_kernels)}")
    print(f"    - Core functions: {len(program.core_functions)}")
    print(f"    - Workers: {len(program.workers)}")
    if program.runtime:
        print(f"    - Runtime operations: {len(program.runtime.operations)}")

    return program


def serialize_to_xml(program, output_path):
    """Serialize HLIR program to GUI XML format."""
    print("\n" + "=" * 80)
    print("Serializing HLIR to GUI XML")
    print("=" * 80)

    serializer = GUIXMLSerializer(pretty_print=True)
    serializer.serialize_to_file(program, output_path)

    print(f"\n[OK] Serialized to: {output_path}")

    # Show XML preview
    with open(output_path, 'r') as f:
        xml_content = f.read()

    print(f"\nXML Preview (first 1000 chars):")
    print("-" * 80)
    print(xml_content[:1000])
    print("...")
    print("-" * 80)
    print(f"\nFull XML: {len(xml_content)} characters, {xml_content.count('<')} elements")

    return output_path


def main():
    """Complete end-to-end workflow."""
    # Step 1: Build design using HLIR
    program = build_add_activate_design()

    # Step 2: Serialize to GUI XML
    output_dir = Path(__file__).parent
    xml_path = output_dir / "add_activate_gui.xml"
    serialize_to_xml(program, str(xml_path))

    # Step 3: Instructions for next steps
    print("\n" + "=" * 80)
    print("Next Steps: Code Generation")
    print("=" * 80)
    print("\nThe generated GUI XML can now be processed by the AIECAD compiler:")
    print(f"\n  cd {output_dir.parent.parent}")
    print(f"  python main.py {xml_path.relative_to(output_dir.parent.parent)}")
    print("\nThis will:")
    print("  1. Expand GUI XML to Complete XML via XMLGenerator")
    print("  2. Build semantic graph with GraphDriver")
    print("  3. Generate Python code with CodeGenerator")

    print("\n" + "=" * 80)
    print("HLIR Add-Activate Example Complete!")
    print("=" * 80)

    return xml_path


if __name__ == '__main__':
    try:
        xml_path = main()
        print(f"\n[OK] Success! Generated XML at: {xml_path}")
        sys.exit(0)
    except Exception as e:
        print(f"\n[ERROR] Error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
