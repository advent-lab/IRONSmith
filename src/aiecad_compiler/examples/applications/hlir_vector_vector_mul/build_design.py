#!/usr/bin/env python3
"""
HLIR Vector Vector Multiply Example - Complete End-to-End Workflow

This example demonstrates the complete HLIR workflow for element-wise vector multiply:
1. Build design using HLIR ProgramBuilder (clean Python API)
2. Serialize to GUI XML format using GUIXMLSerializer
3. Process with XMLGenerator to expand to Complete XML
4. Build semantic graph with GraphDriver
5. Generate Python code with CodeGenerator

This design:
- Takes two input vectors A and B (65536 bfloat16 elements each) from host
- Splits each across 4 workers at separate memory tiles
- Each worker applies eltwise_mul_bf16_vector kernel (looping 16 times)
- Joins results and returns output vector C to host

Architecture (matches jit_vector_vector_mul.py placements):
- 2 SHIM tiles: (0,0) for fill A+B, (1,0) for drain C
- 3 MEM tiles: (0,1) for split A, (1,1) for split B, (2,1) for join C
- 4 compute tiles in column 0: (0,5), (0,4), (0,3), (0,2) for workers 0-3
- Input FIFO A (memtile_ty) -> split at mem(0,1) -> 4 tile_ty FIFOs
- Input FIFO B (memtile_ty) -> split at mem(1,1) -> 4 tile_ty FIFOs
- 4 workers apply mul kernel (16 iterations each)
- 4 tile_ty FIFOs -> join at mem(2,1) -> Output FIFO C (memtile_ty)

Key parameters:
- N = 65536 (total elements)
- chunk_size = 4096 (memtile_ty streaming size = N / 16)
- tile_ty = 1024 (per-worker processing size = N / 64)
- loop_count = N / chunk_size = 16 iterations per worker
"""

import sys
from pathlib import Path

# Add parent directories to path for imports
hlir_path = Path(__file__).parent.parent.parent.parent / "hlir"
sys.path.insert(0, str(hlir_path.parent))

from hlir import ProgramBuilder, GUIXMLSerializer


def build_vector_vector_mul_design():
    """
    Build a vector-vector element-wise multiply design using HLIR.

    Matches jit_vector_vector_mul.py with explicit placements:
    - Split A at mem(0,1), Split B at mem(1,1), Join C at mem(2,1)
    - Fill A+B from shim(0,0), Drain C from shim(1,0)
    - Workers at compute tiles (0,5), (0,4), (0,3), (0,2)
    """
    print("=" * 80)
    print("Building Vector Vector Multiply Design with HLIR")
    print("=" * 80)

    # Create program builder
    builder = ProgramBuilder("vector_vector_mul_hlir_example")
    print("\n[1] Created ProgramBuilder")

    # Add constants
    builder.add_constant("N", 65536, "int")
    print("[2] Added constant: N=65536")

    # Add type definitions - express as fractions of N
    # data_ty: full tensor (N elements)
    builder.add_tensor_type("data_ty", shape=["N"], dtype="bfloat16")
    # memtile_ty: FIFO chunk size (N / 16 = 4096 elements)
    builder.add_tensor_type("memtile_ty", shape=["N / 16"], dtype="bfloat16")
    # tile_ty: worker processing size (N / 64 = 1024 elements)
    builder.add_tensor_type("tile_ty", shape=["N / 64"], dtype="bfloat16")
    print("[3] Added type definitions: data_ty (N), memtile_ty (N/16), tile_ty (N/64)")

    # Add tiles - matching JIT placements
    print("[4] Adding tiles...")

    # SHIM tiles: (0,0) for fill A+B, (1,0) for drain C
    shim0 = builder.add_tile("shim0", kind="shim", x=0, y=0)
    shim1 = builder.add_tile("shim1", kind="shim", x=1, y=0)
    print(f"    - Added 2 SHIM tiles: (0,0) and (1,0)")

    # MEM tiles: (0,1) for split A, (1,1) for split B, (2,1) for join C
    mem0 = builder.add_tile("mem0", kind="mem", x=0, y=1)
    mem1 = builder.add_tile("mem1", kind="mem", x=1, y=1)
    mem2 = builder.add_tile("mem2", kind="mem", x=2, y=1)
    print(f"    - Added 3 MEM tiles: (0,1), (1,1), (2,1)")

    # Compute tiles for 4 workers - all in column 0
    builder.add_tile("tile_0_5", kind="compute", x=0, y=5)  # worker 0
    builder.add_tile("tile_0_4", kind="compute", x=0, y=4)  # worker 1
    builder.add_tile("tile_0_3", kind="compute", x=0, y=3)  # worker 2
    builder.add_tile("tile_0_2", kind="compute", x=0, y=2)  # worker 3
    print("    - Added 4 compute tiles: (0,5), (0,4), (0,3), (0,2)")

    # Add input ObjectFifo for A (from SHIM to MEM)
    print("[5] Adding ObjectFifos...")
    of_in_a = builder.add_fifo("of_in_a", obj_type="memtile_ty", depth=2,
                                context="L3_L2", direction="input", data="A", column=0)
    print(f"    - Added input FIFO: of_in_a (memtile_ty)")

    # Add input ObjectFifo for B (from SHIM to MEM)
    of_in_b = builder.add_fifo("of_in_b", obj_type="memtile_ty", depth=2,
                                context="L3_L2", direction="input", data="B", column=0)
    print(f"    - Added input FIFO: of_in_b (memtile_ty)")

    # Add split operation for input A at mem(0,1)
    print("[6] Adding split operations...")
    split_a = builder.add_fifo_split(
        "split_a", source="of_in_a", num_outputs=4, output_type="tile_ty",
        output_names=["split_a_0", "split_a_1", "split_a_2", "split_a_3"],
        offsets=[0, "N / 64", "(N / 64) * 2", "(N / 64) * 3"],
        placement="mem0", context="L2_L1", data="A", column=0
    )
    print(f"    - Added split A at mem(0,1) -> 4 tile_ty FIFOs")

    # Add split operation for input B at mem(1,1)
    split_b = builder.add_fifo_split(
        "split_b", source="of_in_b", num_outputs=4, output_type="tile_ty",
        output_names=["split_b_0", "split_b_1", "split_b_2", "split_b_3"],
        offsets=[0, "N / 64", "(N / 64) * 2", "(N / 64) * 3"],
        placement="mem1", context="L2_L1", data="B", column=1
    )
    print(f"    - Added split B at mem(1,1) -> 4 tile_ty FIFOs")

    # Add output ObjectFifo for C (from MEM to SHIM)
    of_out_c = builder.add_fifo("of_out_c", obj_type="memtile_ty", depth=2,
                                 context="L2_L3", direction="output", data="C", column=1)
    print(f"    - Added output FIFO: of_out_c (memtile_ty)")

    # Add join operation for output C at mem(2,1)
    print("[7] Adding join operation...")
    join_c = builder.add_fifo_join(
        "join_c", dest="of_out_c", num_inputs=4, input_type="tile_ty",
        input_names=["join_c_0", "join_c_1", "join_c_2", "join_c_3"],
        offsets=[0, "N / 64", "(N / 64) * 2", "(N / 64) * 3"],
        placement="mem2", context="L1_L2", data="C", column=2
    )
    print(f"    - Added join C at mem(2,1) <- 4 tile_ty FIFOs")

    # Add external kernel function
    print("[8] Adding external kernel...")
    mul_kernel = builder.add_external_kernel(
        "eltwise_mul_bf16_vector", kernel_name="eltwise_mul_bf16_vector",
        source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/mul.cc",
        arg_types=["tile_ty", "tile_ty", "tile_ty"],
        operation="mul",
        include_dirs=[
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels",
            "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"
        ]
    )
    print(f"    - Added external kernel: eltwise_mul_bf16_vector (3 args)")

    # Add core function for mul operation
    # Acquire order matches JIT: output first, then inputA, then inputB
    print("[9] Adding core function...")
    corefunc_mul = builder.add_core_function(
        "corefunc_mul",
        parameters=["kernel", "inputA", "inputB", "outputC"],
        acquires=[("outputC", 1, "elem_out"), ("inputA", 1, "elem_in_a"), ("inputB", 1, "elem_in_b")],
        kernel_call=("kernel", ["elem_in_a", "elem_in_b", "elem_out"]),
        releases=[("inputA", 1), ("inputB", 1), ("outputC", 1)],
        operation="mul",
        loop_count="N / 4096"  # 65536 / 4096 = 16 iterations per worker
    )
    print(f"    - Added core function: corefunc_mul (loop_count=N/4096)")

    # Add workers (4 workers, matching JIT placements)
    print("[10] Adding workers...")
    worker0 = builder.add_worker(
        "worker0", core_fn="corefunc_mul",
        fn_args=["eltwise_mul_bf16_vector", ("split_a", "cons", 0), ("split_b", "cons", 0), ("join_c", "prod", 0)],
        placement="tile_0_5", operation="mul", column=0, worker_index=0
    )
    worker1 = builder.add_worker(
        "worker1", core_fn="corefunc_mul",
        fn_args=["eltwise_mul_bf16_vector", ("split_a", "cons", 1), ("split_b", "cons", 1), ("join_c", "prod", 1)],
        placement="tile_0_4", operation="mul", column=0, worker_index=1
    )
    worker2 = builder.add_worker(
        "worker2", core_fn="corefunc_mul",
        fn_args=["eltwise_mul_bf16_vector", ("split_a", "cons", 2), ("split_b", "cons", 2), ("join_c", "prod", 2)],
        placement="tile_0_3", operation="mul", column=0, worker_index=2
    )
    worker3 = builder.add_worker(
        "worker3", core_fn="corefunc_mul",
        fn_args=["eltwise_mul_bf16_vector", ("split_a", "cons", 3), ("split_b", "cons", 3), ("join_c", "prod", 3)],
        placement="tile_0_2", operation="mul", column=0, worker_index=3
    )
    print(f"    - Added 4 workers at tiles (0,5), (0,4), (0,3), (0,2)")

    # Create runtime sequence
    print("[11] Building runtime sequence...")
    rt = builder.create_runtime("runtime")

    # Sequence types: memtile_ty for A, memtile_ty for B, memtile_ty for C
    rt.add_input_type("memtile_ty")
    rt.add_input_type("memtile_ty")
    rt.add_output_type("memtile_ty")
    rt.add_params(["inputA", "inputB", "outputC"])

    # Add all workers to start
    rt.add_worker("worker0")
    rt.add_worker("worker1")
    rt.add_worker("worker2")
    rt.add_worker("worker3")

    # Fill A from shim(0,0), Fill B from shim(0,0)
    rt.add_fill("fill_a", "of_in_a", "inputA", "shim0", column=0, use_tap=False)
    rt.add_fill("fill_b", "of_in_b", "inputB", "shim0", column=0, use_tap=False)

    # Drain C from shim(1,0)
    rt.add_drain("drain_c", "of_out_c", "outputC", "shim1", wait=True, column=1, use_tap=False)

    rt.build()
    print("    - Added 2 input types: memtile_ty (A, B)")
    print("    - Added 1 output type: memtile_ty (C)")
    print("    - Added parameters: inputA, inputB, outputC")
    print("    - Added 4 workers to start")
    print("    - Added fill A from shim(0,0)")
    print("    - Added fill B from shim(0,0)")
    print("    - Added drain C from shim(1,0)")

    # Build program with validation
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
    program = build_vector_vector_mul_design()

    # Step 2: Serialize to GUI XML
    output_dir = Path(__file__).parent
    xml_path = output_dir / "vector_vector_mul_gui.xml"
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
    print("HLIR Vector Vector Multiply Example Complete!")
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
