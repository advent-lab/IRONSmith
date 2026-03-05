#!/usr/bin/env python3
"""
HLIR Vector Exp Example - Complete End-to-End Workflow

This example demonstrates the complete HLIR workflow for a vector exponential operation:
1. Build design using HLIR ProgramBuilder (clean Python API)
2. Serialize to GUI XML format using GUIXMLSerializer
3. Process with XMLGenerator to expand to Complete XML
4. Build semantic graph with GraphDriver
5. Generate Python code with CodeGenerator

This design:
- Takes input vector A (65536 bfloat16 elements) from host
- Splits across 4 workers at a memory tile
- Each worker applies exp_bf16_1024 kernel (looping 16 times)
- Joins results and returns output vector C to host

Architecture:
- 1 SHIM tile (0, 0) for DMA
- 1 MEM tile (0, 1) for split/join operations
- 4 compute tiles for workers
- Input FIFO (memtile_ty) -> split -> 4 tile_ty FIFOs
- 4 workers apply exp kernel (16 iterations each)
- 4 tile_ty FIFOs -> join -> Output FIFO (memtile_ty)

Key parameters:
- N = 65536 (total elements)
- memtile_ty = N / 16 = 4096 (chunk size for streaming)
- tile_ty = N / 64 = 1024 (per-worker processing size)
- tiles = 16 (iterations per worker)
"""

import sys
from pathlib import Path

# Add parent directories to path for imports
hlir_path = Path(__file__).parent.parent.parent.parent / "hlir"
sys.path.insert(0, str(hlir_path.parent))

from hlir import ProgramBuilder, GUIXMLSerializer


def build_vector_exp_design():
    """
    Build a vector exponential design using HLIR.

    This design:
    - Takes input vector A from host (N=65536 bfloat16 elements)
    - Streams through FIFO in memtile_ty chunks (N/16 = 4096)
    - Splits data across 4 compute tiles via memory tile
    - Each tile applies exp_bf16_1024 kernel (N/64 = 1024 elements at a time)
    - Each worker loops 16 times to process all data
    - Joins results back and returns to host

    Type hierarchy (following add_activate pattern):
    - data_ty = N (full tensor, 65536 elements)
    - memtile_ty = N / 16 (FIFO streaming, 4096 elements)
    - tile_ty = N / 64 (worker processing, 1024 elements)
    """
    print("=" * 80)
    print("Building Vector Exp Design with HLIR")
    print("=" * 80)

    # Create program builder
    builder = ProgramBuilder("vector_exp_hlir_example")
    print("\n[1] Created ProgramBuilder")

    # Add constants - only the base size N is needed
    # Other sizes are expressed as fractions of N for proper code generation
    builder.add_constant("N", 65536, "int")
    print("[2] Added constant: N=65536")

    # Add type definitions - express as fractions of N (like add_activate pattern)
    # data_ty: full tensor (N elements)
    builder.add_tensor_type("data_ty", shape=["N"], dtype="bfloat16")
    # memtile_ty: FIFO chunk size (N / 16 = 4096 elements)
    builder.add_tensor_type("memtile_ty", shape=["N / 16"], dtype="bfloat16")
    # tile_ty: worker processing size (N / 64 = 1024 elements)
    builder.add_tensor_type("tile_ty", shape=["N / 64"], dtype="bfloat16")
    print("[3] Added type definitions: data_ty (N), memtile_ty (N/16), tile_ty (N/64)")

    # Add tiles
    print("[4] Adding tiles...")
    shim0 = builder.add_tile("shim0", kind="shim", x=0, y=0)
    print(f"    - Added SHIM tile: {shim0}")

    mem0 = builder.add_tile("mem0", kind="mem", x=0, y=1)
    print(f"    - Added MEM tile: {mem0}")

    # Add compute tiles for 4 workers
    builder.add_tile("tile_0_2", kind="compute", x=0, y=2)  # worker 0
    builder.add_tile("tile_0_3", kind="compute", x=0, y=3)  # worker 1
    builder.add_tile("tile_0_4", kind="compute", x=0, y=4)  # worker 2
    builder.add_tile("tile_0_5", kind="compute", x=0, y=5)  # worker 3
    print("    - Added 4 compute tiles for workers")

    # Add input ObjectFifo (from SHIM to MEM)
    print("[5] Adding ObjectFifos...")
    of_in_a = builder.add_fifo("of_in_a", obj_type="memtile_ty", depth=2,
                                context="L3_L2", direction="input", data="A", column=0)
    print(f"    - Added input FIFO: {of_in_a.component.name} (memtile_ty)")

    # Add split operation for input A (split memtile_ty into 4 tile_ty FIFOs)
    # Offsets expressed as fractions: 0, N/64, 2*N/64, 3*N/64
    print("[6] Adding split operation...")
    split_a = builder.add_fifo_split(
        "split_a", source="of_in_a", num_outputs=4, output_type="tile_ty",
        output_names=["split_a_0", "split_a_1", "split_a_2", "split_a_3"],
        offsets=[0, "N / 64", "(N / 64) * 2", "(N / 64) * 3"],
        placement="mem0", context="L2_L1", data="A", column=0
    )
    print(f"    - Added split operation: {split_a} -> 4 tile_ty FIFOs")

    # Add output ObjectFifo (from MEM to SHIM)
    of_out_c = builder.add_fifo("of_out_c", obj_type="memtile_ty", depth=2,
                                 context="L2_L3", direction="output", data="C", column=0)
    print(f"    - Added output FIFO: {of_out_c.component.name} (memtile_ty)")

    # Add join operation for output C (join 4 tile_ty FIFOs into memtile_ty)
    print("[7] Adding join operation...")
    join_c = builder.add_fifo_join(
        "join_c", dest="of_out_c", num_inputs=4, input_type="tile_ty",
        input_names=["join_c_0", "join_c_1", "join_c_2", "join_c_3"],
        offsets=[0, "N / 64", "(N / 64) * 2", "(N / 64) * 3"],
        placement="mem0", context="L1_L2", data="C", column=0
    )
    print(f"    - Added join operation: 4 tile_ty FIFOs -> {join_c}")

    # Add external kernel function
    print("[8] Adding external kernel...")
    exp_kernel = builder.add_external_kernel(
        "exp_bf16_1024", kernel_name="exp_bf16_1024",
        source_file="/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/bf16_exp.cc",
        arg_types=["tile_ty", "tile_ty"],
        operation="exp",
        include_dirs=[
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels",
            "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"
        ]
    )
    print(f"    - Added external kernel: exp_bf16_1024")

    # Add core function for exp operation
    # Note: loop_count is expressed as a formula for code generation
    print("[9] Adding core function...")
    corefunc_exp = builder.add_core_function(
        "corefunc_exp",
        parameters=["kernel", "inputA", "outputC"],
        acquires=[("outputC", 1, "elem_out"), ("inputA", 1, "elem_in")],
        kernel_call=("kernel", ["elem_in", "elem_out"]),
        releases=[("inputA", 1), ("outputC", 1)],
        operation="exp",
        loop_count="N / 4096"  # 65536 / 4096 = 16 iterations per worker
    )
    print(f"    - Added core function: corefunc_exp (with loop_count=N/4096)")

    # Add workers (4 workers, one per split/join output)
    print("[10] Adding workers...")
    worker0 = builder.add_worker(
        "worker0", core_fn="corefunc_exp",
        fn_args=["exp_bf16_1024", ("split_a", "cons", 0), ("join_c", "prod", 0)],
        placement="tile_0_2", operation="exp", column=0, worker_index=0
    )
    worker1 = builder.add_worker(
        "worker1", core_fn="corefunc_exp",
        fn_args=["exp_bf16_1024", ("split_a", "cons", 1), ("join_c", "prod", 1)],
        placement="tile_0_3", operation="exp", column=0, worker_index=1
    )
    worker2 = builder.add_worker(
        "worker2", core_fn="corefunc_exp",
        fn_args=["exp_bf16_1024", ("split_a", "cons", 2), ("join_c", "prod", 2)],
        placement="tile_0_4", operation="exp", column=0, worker_index=2
    )
    worker3 = builder.add_worker(
        "worker3", core_fn="corefunc_exp",
        fn_args=["exp_bf16_1024", ("split_a", "cons", 3), ("join_c", "prod", 3)],
        placement="tile_0_5", operation="exp", column=0, worker_index=3
    )
    print(f"    - Added 4 workers")

    # Create runtime sequence
    # Build runtime sequence with memtile_ty for streaming (matches reference)
    print("[11] Building runtime sequence...")
    rt = builder.create_runtime("runtime")

    # Use memtile_ty for sequence types (matches reference rt.sequence(memtile_ty, memtile_ty))
    rt.add_input_type("memtile_ty")
    rt.add_output_type("memtile_ty")
    rt.add_params(["inputA", "outputC"])

    # Add all workers to start
    rt.add_worker("worker0")
    rt.add_worker("worker1")
    rt.add_worker("worker2")
    rt.add_worker("worker3")

    # Add fill operation for input A (simple, no TAP - matches reference)
    rt.add_fill("fill_a", "of_in_a", "inputA", "shim0", column=0, use_tap=False)

    # Add drain operation for output C (simple, no TAP - matches reference)
    rt.add_drain("drain_c", "of_out_c", "outputC", "shim0", wait=True, column=0, use_tap=False)

    rt.build()
    print("    - Added input type: data_ty (N elements)")
    print("    - Added output type: data_ty (N elements)")
    print("    - Added parameters: inputA, outputC")
    print("    - Added 4 workers to start")
    print("    - Added fill operation: inputA -> of_in_a")
    print("    - Added drain operation: of_out_c -> outputC")

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
    program = build_vector_exp_design()

    # Step 2: Serialize to GUI XML
    output_dir = Path(__file__).parent
    xml_path = output_dir / "vector_exp_gui.xml"
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
    print("HLIR Vector Exp Example Complete!")
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
