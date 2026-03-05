#!/usr/bin/env python3
"""
HLIR Matrix Vector Multiply Example - Complete End-to-End Workflow

This example demonstrates the complete HLIR workflow for matrix-vector multiplication:
1. Build design using HLIR ProgramBuilder (clean Python API)
2. Serialize to GUI XML format using GUIXMLSerializer
3. Process with XMLGenerator to expand to Complete XML
4. Build semantic graph with GraphDriver
5. Generate Python code with CodeGenerator

This design:
- Takes matrix A (M x K, int16) and vector B (K, int16) from host
- Outputs vector C (M, int32) — result of A @ B
- Distributes M rows across 4 compute tiles (rows_per_core rows each)
- Uses TensorTiler2D access patterns for efficient DMA

Architecture (matches jit_matrix_vector_mul.py placements):
- SHIM tiles: (0,0) for fill A, (1,0) for fill B, (2,0) for drain C
- MEM tiles: (0,1) for split A, (1,1) for forward B, (2,1) for join C
- 4 compute tiles in column 0: (0,2)-(0,5)

Key parameters:
- M = 256, K = 256 (matrix/vector dimensions)
- m = 32, k = 32 (tile dimensions per compute tile)
- n_cores = 4
- rows_per_core = (M // m) // n_cores = 2
- A_mem_ty: n_cores * m * k = 4096 int16 elements per FIFO object
- C_mem_ty: n_cores * m = 128 int32 elements per FIFO object
"""

import sys
from pathlib import Path

# Add parent directories to path for imports
hlir_path = Path(__file__).parent.parent.parent.parent / "hlir"
sys.path.insert(0, str(hlir_path.parent))

from hlir import ProgramBuilder, GUIXMLSerializer
from hlir.core import Acquire, Release, KernelCall, Assignment, ForLoop


def build_matrix_vector_mul_design():
    """
    Build a matrix-vector multiply design using HLIR.

    Matches jit_matrix_vector_mul.py with explicit placements:
    - Split A at mem(0,1), Forward B at mem(1,1), Join C at mem(2,1)
    - Fill A from shim(0,0), Fill B from shim(1,0), Drain C from shim(2,0)
    - 4 workers at compute tiles (0,2), (0,3), (0,4), (0,5)
    """
    print("=" * 80)
    print("Building Matrix Vector Multiply Design with HLIR")
    print("=" * 80)

    # Create program builder
    builder = ProgramBuilder("matrix_vector_mul_hlir_example")
    print("\n[1] Created ProgramBuilder")

    # --- Constants ---
    builder.add_constant("M", 256, "int")
    builder.add_constant("K", 256, "int")
    builder.add_constant("m", 32, "int")
    builder.add_constant("k", 32, "int")
    builder.add_constant("n_cores", 4, "int")
    builder.add_constant("M_div_m", "M // m", "int")
    builder.add_constant("K_div_k", "K // k", "int")
    builder.add_constant("rows_per_core", "M_div_m // n_cores", "int")
    builder.add_constant("n_fifo_elems", "rows_per_core * K_div_k", "int")
    builder.add_constant("A_elem_size", "n_cores * m * k", "int")
    print("[2] Added constants: M, K, m, k, n_cores, M_div_m, K_div_k, rows_per_core, n_fifo_elems, A_elem_size")

    # --- Type definitions ---
    # Per-tile types
    builder.add_tensor_type("inA_ty", shape=["m * k"], dtype="int16")
    builder.add_tensor_type("inB_ty", shape=["k"], dtype="int16")
    builder.add_tensor_type("outC_ty", shape=["m"], dtype="int32")
    # Memtile buffer types (n_cores sub-buffers interleaved)
    builder.add_tensor_type("A_mem_ty", shape=["n_cores * m * k"], dtype="int16")
    builder.add_tensor_type("C_mem_ty", shape=["n_cores * m"], dtype="int32")
    # Host buffer types (2D for TensorTiler2D)
    builder.add_tensor_type("A_ty", shape=["n_fifo_elems", "A_elem_size"], dtype="int16")
    builder.add_tensor_type("B_ty", shape=["1", "K"], dtype="int16")
    builder.add_tensor_type("C_ty", shape=["1", "M"], dtype="int32")
    print("[3] Added type definitions: inA_ty, inB_ty, outC_ty, A_mem_ty, C_mem_ty, A_ty, B_ty, C_ty")

    # --- Tiles ---
    print("[4] Adding tiles...")
    shim0 = builder.add_tile("shim0", kind="shim", x=0, y=0)   # fill A
    shim1 = builder.add_tile("shim1", kind="shim", x=1, y=0)   # fill B
    shim2 = builder.add_tile("shim2", kind="shim", x=2, y=0)   # drain C
    mem0 = builder.add_tile("mem0", kind="mem", x=0, y=1)      # split A
    mem1 = builder.add_tile("mem1", kind="mem", x=1, y=1)      # forward B
    mem2 = builder.add_tile("mem2", kind="mem", x=2, y=1)      # join C
    builder.add_tile("tile_0_2", kind="compute", x=0, y=2)
    builder.add_tile("tile_0_3", kind="compute", x=0, y=3)
    builder.add_tile("tile_0_4", kind="compute", x=0, y=4)
    builder.add_tile("tile_0_5", kind="compute", x=0, y=5)
    print("    - Added 3 SHIM, 3 MEM, 4 compute tiles")

    # --- ObjectFifos ---
    print("[5] Adding ObjectFifos...")
    # A: shim(0,0) -> mem(0,1) -> split to 4 compute tiles
    of_in_a = builder.add_fifo("inA", obj_type="A_mem_ty", depth=2,
                                context="L3_L2", direction="input", data="A", column=0)
    # B: shim(1,0) -> mem(1,1) -> forward (broadcast) to 4 compute tiles
    of_in_b = builder.add_fifo("inB", obj_type="inB_ty", depth=2,
                                context="L3_L2", direction="input", data="B", column=1)
    # C: 4 compute tiles -> join -> mem(2,1) -> shim(2,0)
    of_out_c = builder.add_fifo("outC", obj_type="C_mem_ty", depth=2,
                                 context="L2_L3", direction="output", data="C", column=2)
    print("    - Added input FIFOs: inA (A_mem_ty), inB (inB_ty)")
    print("    - Added output FIFO: outC (C_mem_ty)")

    # --- Split A at mem(0,1) into 4 inA_ty FIFOs ---
    print("[6] Adding split/forward/join operations...")
    split_a = builder.add_fifo_split(
        "a_fifos", source="inA", num_outputs=4, output_type="inA_ty",
        output_names=["a_fifos_0", "a_fifos_1", "a_fifos_2", "a_fifos_3"],
        offsets=[0, "m * k", "2 * m * k", "3 * m * k"],
        placement="mem0", context="L2_L1", data="A", column=0
    )
    print("    - Added split A at mem(0,1) -> 4 inA_ty FIFOs")

    # --- Forward B at mem(1,1) (broadcast to all 4 compute tiles) ---
    b_fwd = builder.add_fifo_forward(
        "B_fwd", source="inB", placement="mem1"
    )
    print("    - Added forward B at mem(1,1)")

    # --- Join C at mem(2,1) from 4 outC_ty FIFOs ---
    join_c = builder.add_fifo_join(
        "c_fifos", dest="outC", num_inputs=4, input_type="outC_ty",
        input_names=["c_fifos_0", "c_fifos_1", "c_fifos_2", "c_fifos_3"],
        offsets=[0, "m", "2 * m", "3 * m"],
        placement="mem2", context="L1_L2", data="C", column=2
    )
    print("    - Added join C at mem(2,1) <- 4 outC_ty FIFOs")

    # --- External kernel ---
    print("[7] Adding external kernel...")
    matvec = builder.add_external_kernel(
        "matvec_vectorized_i16_i32",
        kernel_name="matvec_vectorized_i16_i32",
        source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/mv.cc",
        arg_types=["inA_ty", "inB_ty", "outC_ty"],
        operation="matvec",
        include_dirs=[
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels",
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2",
            "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2",
        ]
    )
    print("    - Added external kernel: matvec_vectorized_i16_i32")

    # --- Core function (nested body) ---
    # Each compute tile:
    #   elem_out = c_out.acquire(1)
    #   for i in range_(m): elem_out[i] = 0       # zero-init output row
    #   for _ in range_(K_div_k):                 # iterate over column blocks
    #       elem_a = a_in.acquire(1)
    #       elem_b = b_in.acquire(1)
    #       matvec(elem_a, elem_b, elem_out)
    #       a_in.release(1)
    #       b_in.release(1)
    #   c_out.release(1)
    print("[8] Adding core function...")
    corefunc_matvec = builder.add_core_function(
        "core_fn",
        parameters=["a_in", "b_in", "c_out", "matvec"],
        body_stmts=[
            Acquire("c_out", 1, "elem_out"),
            ForLoop("i", "m", [
                Assignment("elem_out", "i", 0),
            ]),
            ForLoop("_", "K_div_k", [
                Acquire("a_in", 1, "elem_a"),
                Acquire("b_in", 1, "elem_b"),
                KernelCall("matvec", ["elem_a", "elem_b", "elem_out"]),
                Release("a_in", 1),
                Release("b_in", 1),
            ]),
            Release("c_out", 1),
        ],
        operation="matvec"
    )
    print("    - Added core function: core_fn (nested body with zero-init + K_div_k loop)")

    # --- Workers (4 compute tiles, col 0 rows 2-5) ---
    print("[9] Adding workers...")
    worker0 = builder.add_worker(
        "worker0", core_fn="core_fn",
        fn_args=[("a_fifos", "cons", 0), ("B_fwd", "cons", None), ("c_fifos", "prod", 0), "matvec_vectorized_i16_i32"],
        placement="tile_0_2", operation="matvec", column=0, worker_index=0
    )
    worker1 = builder.add_worker(
        "worker1", core_fn="core_fn",
        fn_args=[("a_fifos", "cons", 1), ("B_fwd", "cons", None), ("c_fifos", "prod", 1), "matvec_vectorized_i16_i32"],
        placement="tile_0_3", operation="matvec", column=0, worker_index=1
    )
    worker2 = builder.add_worker(
        "worker2", core_fn="core_fn",
        fn_args=[("a_fifos", "cons", 2), ("B_fwd", "cons", None), ("c_fifos", "prod", 2), "matvec_vectorized_i16_i32"],
        placement="tile_0_4", operation="matvec", column=0, worker_index=2
    )
    worker3 = builder.add_worker(
        "worker3", core_fn="core_fn",
        fn_args=[("a_fifos", "cons", 3), ("B_fwd", "cons", None), ("c_fifos", "prod", 3), "matvec_vectorized_i16_i32"],
        placement="tile_0_5", operation="matvec", column=0, worker_index=3
    )
    print("    - Added 4 workers at tiles (0,2), (0,3), (0,4), (0,5)")

    # --- TensorTiler2D access patterns ---
    print("[10] Adding TensorTiler2D access patterns...")
    # a_tap: read A as (n_fifo_elems x A_elem_size), stride 512 along K
    a_tap_result = builder.add_tiler2d(
        "a_tap",
        tensor_dims=["n_fifo_elems", "A_elem_size"],
        tile_dims=[1, 512],
        tile_counts=["n_fifo_elems", "A_elem_size // 512"],
        prune_step=False,
        index=0
    )
    # b_tap: repeat B vector rows_per_core times (once per row block group)
    b_tap_result = builder.add_tiler2d(
        "b_tap",
        tensor_dims=[1, "K"],
        tile_dims=[1, "k"],
        tile_counts=[1, "K_div_k"],
        pattern_repeat="rows_per_core",
        prune_step=False,
        index=0
    )
    # c_tap: output C as (1 x M), tiled by (1 x n_cores*m)
    c_tap_result = builder.add_tiler2d(
        "c_tap",
        tensor_dims=[1, "M"],
        tile_dims=[1, "n_cores * m"],
        tile_counts=[1, "rows_per_core"],
        prune_step=False,
        index=0
    )
    print("    - Added a_tap, b_tap, c_tap")

    # --- Runtime ---
    print("[11] Building runtime sequence...")
    rt = builder.create_runtime("runtime")

    # Sequence types: A_ty (2D int16), B_ty (2D int16), C_ty (2D int32)
    rt.add_input_type("A_ty")
    rt.add_input_type("B_ty")
    rt.add_output_type("C_ty")
    rt.add_params(["inputA", "inputB", "outputC"])

    rt.add_worker("worker0")
    rt.add_worker("worker1")
    rt.add_worker("worker2")
    rt.add_worker("worker3")

    # Get TensorTiler2DSpec objects for fill/drain
    a_tap = a_tap_result.component
    b_tap = b_tap_result.component
    c_tap = c_tap_result.component

    # Fill A from shim(0,0) with a_tap
    rt.add_fill("fill_a", "inA", "inputA", "shim0", tap=a_tap, column=0)
    # Fill B from shim(1,0) with b_tap
    rt.add_fill("fill_b", "inB", "inputB", "shim1", tap=b_tap, column=1)
    # Drain C from shim(2,0) with c_tap
    rt.add_drain("drain_c", "outC", "outputC", "shim2", wait=True, tap=c_tap, column=2)

    rt.build()
    print("    - Added 2 input types: A_ty, B_ty")
    print("    - Added 1 output type: C_ty")
    print("    - Added fill A (shim0, a_tap), fill B (shim1, b_tap)")
    print("    - Added drain C (shim2, c_tap)")

    # --- Build program ---
    print("\n[12] Building and validating program...")
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

    with open(output_path, 'r') as f:
        xml_content = f.read()

    print(f"\nXML Preview (first 1500 chars):")
    print("-" * 80)
    print(xml_content[:1500])
    print("...")
    print("-" * 80)
    print(f"\nFull XML: {len(xml_content)} characters, {xml_content.count('<')} elements")

    return output_path


def main():
    """Complete end-to-end workflow."""
    # Step 1: Build design using HLIR
    program = build_matrix_vector_mul_design()

    # Step 2: Serialize to GUI XML
    output_dir = Path(__file__).parent
    xml_path = output_dir / "matrix_vector_mul_gui.xml"
    serialize_to_xml(program, str(xml_path))

    print("\n" + "=" * 80)
    print("Next Steps: Code Generation")
    print("=" * 80)
    print("\nThe generated GUI XML can now be processed by the AIECAD compiler:")
    print(f"\n  cd {output_dir.parent.parent}")
    print(f"  python main.py {xml_path.relative_to(output_dir.parent.parent)}")

    print("\n" + "=" * 80)
    print("HLIR Matrix Vector Multiply Example Complete!")
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
