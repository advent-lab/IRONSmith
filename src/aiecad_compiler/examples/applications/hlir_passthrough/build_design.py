#!/usr/bin/env python3
"""
HLIR Passthrough Example - Complete End-to-End Workflow

This example demonstrates the complete HLIR workflow:
1. Build design using HLIR ProgramBuilder (clean Python API)
2. Serialize to GUI XML format using GUIXMLSerializer
3. Process with XMLGenerator to expand to Complete XML
4. Build semantic graph with GraphDriver
5. Generate Python code with CodeGenerator

This shows how the HLIR provides a clean interface between
GUI/frontend tools and the AIECAD compiler pipeline.
"""

import sys
from pathlib import Path

# Add parent directories to path for imports
hlir_path = Path(__file__).parent.parent.parent.parent / "hlir"
sys.path.insert(0, str(hlir_path.parent))

from hlir import ProgramBuilder, GUIXMLSerializer


def build_passthrough_design():
    """
    Build a simple passthrough design using HLIR.

    This design:
    - Takes input vector from host
    - Passes it through the AIE array without processing
    - Returns it back to host

    Architecture:
    - 1 SHIM tile (0, 0) for DMA
    - 2 ObjectFifos: of_in and of_out (forward connection)
    - Simple fill/drain runtime operations
    """
    print("=" * 80)
    print("Building Passthrough Design with HLIR")
    print("=" * 80)

    # Create program builder
    builder = ProgramBuilder("passthrough_hlir_example")
    print("\n[1] Created ProgramBuilder")

    # Add constants
    builder.add_constant("N", 4096, "int")
    print("[2] Added constant: N=4096")

    # Add type definitions
    builder.add_tensor_type("vector_ty", shape=["N"], dtype="int32")
    builder.add_tensor_type("line_ty", shape=["N / 4"], dtype="int32")
    print("[3] Added type definitions: vector_ty, line_ty (with N / 4 shape)")

    # Add tiles
    shim = builder.add_tile("shim0", kind="shim", x=0, y=0)
    print(f"[4] Added tile: {shim}")

    # Add ObjectFifos
    fifo_in = builder.add_fifo("of_in", obj_type="line_ty", depth=2)
    print(f"[5] Added input FIFO: {fifo_in.component.name}")

    # Forward operation (passthrough)
    fifo_out = builder.add_fifo_forward("of_out", source="of_in")
    print(f"[6] Added forward operation: {fifo_out.component.name} <- of_in")

    # Create runtime sequence
    print("[7] Building runtime sequence...")
    rt = builder.create_runtime("runtime")
    rt.add_input_type("vector_ty")
    rt.add_output_type("vector_ty")
    rt.add_params(["inputA", "outputC"])
    rt.add_fill("fill_0", "of_in", "inputA", "shim0")
    rt.add_drain("drain_0", "of_out", "outputC", "shim0", wait=True)
    rt.build()
    print("    - Added input type: vector_ty")
    print("    - Added output type: vector_ty")
    print("    - Added parameters: inputA, outputC")
    print("    - Added fill operation: inputA -> of_in")
    print("    - Added drain operation: of_out -> outputC")

    # Build program with validation
    print("\n[8] Building and validating program...")
    program = builder.build()
    print(f"    [OK] Program built successfully: {program}")
    print(f"    - Tiles: {len(program.tiles)}")
    print(f"    - FIFOs: {len(program.fifos)}")
    print(f"    - Symbols: {len(program.symbols)}")
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
    program = build_passthrough_design()

    # Step 2: Serialize to GUI XML
    output_dir = Path(__file__).parent
    xml_path = output_dir / "passthrough_gui.xml"
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
    print("HLIR Example Complete!")
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
