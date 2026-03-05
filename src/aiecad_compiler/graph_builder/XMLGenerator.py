#!/usr/bin/env python3
"""
XMLGenerator.py — GUI XML to Complete XML Generator

This module takes a GUI-friendly XML and expands it into a complete
XML structure that can be parsed by the GraphDriver. It acts as an interface
layer between the GUI and the GraphDriver.

Key Responsibilities:
- Template-based name generation using context attributes
- Expression expansion (data_size / 4 → ((inputA.numel()) // 4))
- Method chain generation (.cons().split(), .prod().join())
- Library function resolution
- Offset inference based on splits/joins
- Type reference expansion

Architecture:
- NamingConventions: Template-based naming for generated elements
- ExpressionExpander: Expands GUI expressions to full Python expressions
- MethodChainBuilder: Generates method chains for ObjectFifo operations
- XMLTransformer: Main orchestrator for transformation
"""

from lxml import etree
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import re


class NamingConventions:
    """
    Template-based naming system for generating semantic names from context.
    Uses context attributes (memory level, data direction, column) to generate
    names that match the expected GraphDriver format.
    """

    # Naming templates for different memory hierarchy levels
    OBJECTFIFO_TEMPLATES = {
        "L3_L2": "SHIM_L3_L2_{data}{workers}_col{column}",
        "L2_L3": "SHIM_L2_L3_{data}{workers}_col{column}",
        "L2_L1": "MEM_L2_L1_{data}{workers}_col{column}",
        "L1_L2": "MEM_L1_L2_{data}{workers}_col{column}",
        "L1_L1": "L1_L1_{stage}_{worker}",
    }

    @classmethod
    def generate_objectfifo_name(cls, attrs: Dict[str, str], num_workers: int = 2) -> str:
        """
        Generate ObjectFifo name from context attributes.

        Args:
            attrs: Dictionary of attributes from XML element
            num_workers: Number of workers per column (for worker suffix)

        Returns:
            Generated ObjectFifo name

        Examples:
            {context="L3_L2", data="A", column="0"} → "SHIM_L3_L2_A1A2_col0"
            {context="L1_L1", stage="add_to_relu", worker="1"} → "L1_L1_add_to_relu_1"
        """
        context = attrs.get("context", "")
        data = attrs.get("data", "")
        column = attrs.get("column", "")
        worker = attrs.get("worker", "")
        stage = attrs.get("stage", "")

        template = cls.OBJECTFIFO_TEMPLATES.get(context, "{data}_col{column}")

        # For L3_L2/L2_L3: generate worker pair notation based on column
        if context in ["L3_L2", "L2_L3", "L2_L1", "L1_L2"] and column.isdigit():
            col_num = int(column)
            worker_idx = col_num * num_workers + 1
            # Format as A1A2 not A12 (each worker number prefixed with data letter)
            # e.g., data=A, workers 1,2 → A1A2
            worker_suffix = "".join(f"{data}{worker_idx + i}" for i in range(num_workers))
            return template.format(data="", workers=worker_suffix, column=column)

        # For L1_L1: use stage and worker attributes
        if context == "L1_L1":
            return template.format(stage=stage, worker=worker)

        return template.format(data=data, workers="", column=column)

    @classmethod
    def generate_split_output_names(cls, attrs: Dict[str, str], num_outputs: int) -> List[str]:
        """
        Generate individual split output names.

        Args:
            attrs: Attributes from ObjectFifoSplit element
            num_outputs: Number of split outputs

        Returns:
            List of generated output names
        """
        data = attrs.get("data", "")
        column = attrs.get("column", "")

        if not column.isdigit():
            name = attrs.get("name", "split")
            return [f"{name}_out{i + 1}" for i in range(num_outputs)]

        col_num = int(column)
        base_idx = col_num * num_outputs + 1
        return [f"MEM_L2_L1_{data}{base_idx + i}_col{column}"
                for i in range(num_outputs)]

    @classmethod
    def generate_join_input_names(cls, attrs: Dict[str, str], num_inputs: int) -> List[str]:
        """
        Generate individual join input names.

        Args:
            attrs: Attributes from ObjectFifoJoin element
            num_inputs: Number of join inputs

        Returns:
            List of generated input names
        """
        data = attrs.get("data", "")
        column = attrs.get("column", "")

        if not column.isdigit():
            name = attrs.get("name", "join")
            return [f"{name}_in{i + 1}" for i in range(num_inputs)]

        col_num = int(column)
        base_idx = col_num * num_inputs + 1
        return [f"MEM_L1_L2_{data}{base_idx + i}_col{column}"
                for i in range(num_inputs)]


class ExpressionExpander:
    """
    Expands simplified expressions to full Python expressions with proper references.

    Handles:
    - Constant references (data_size → actual value or variable)
    - Type size expressions (data_size / 4 → ((inputA.numel()) // 4))
    - Variable substitution based on context
    - Function parameter mapping
    """

    # Standard library function mapping
    LIBRARY_MAP = {
        "iron.arange": "iron.arange",
        "iron.zeros": "iron.zeros",
        "iron.get_current_device": "iron.get_current_device",
        "SequentialPlacer": "SequentialPlacer",
    }

    def __init__(self, symbols: Dict[str, str], function_params: Dict[str, List[str]]):
        """
        Initialize with symbol table and function parameters.

        Args:
            symbols: Dictionary of constant/variable names to values
            function_params: Dictionary mapping function names to their parameter lists
        """
        self.symbols = symbols
        self.function_params = function_params
        self.tensor_refs = {}  # Will be populated from function parameters

    def set_tensor_refs(self, param_map: Dict[str, str]):
        """
        Set mapping from generic data references to actual parameter names.

        Args:
            param_map: e.g., {"A": "inputA", "B": "inputB", "D": "outputD"}
        """
        self.tensor_refs = param_map

    def get_tensor_ref(self, data_key: str) -> str:
        """Get actual tensor reference for a data key."""
        return self.tensor_refs.get(data_key, data_key.lower())

    def expand_shape_expression(self, expr: str, tensor_ref: Optional[str] = None) -> str:
        """
        Expand shape expression to full form.

        Examples:
            "data_size" → "(inputA.numel())" or "128"
            "data_size / 4" → "((inputA.numel()) // 4)"
            "data_size / 8" → "((inputA.numel()) // 8)"
        """
        # If it's just a constant reference
        if expr in self.symbols:
            value = self.symbols[expr]
            # If we have a tensor reference, use .numel()
            if tensor_ref:
                return f"({tensor_ref}.numel())"
            # Otherwise return the literal value
            return value

        # Parse division expressions
        if "/" in expr:
            parts = expr.split("/")
            if len(parts) == 2:
                numerator = parts[0].strip()
                denominator = parts[1].strip()

                # Expand numerator
                if numerator in self.symbols:
                    if tensor_ref:
                        numerator_expanded = f"{tensor_ref}.numel()"
                    else:
                        numerator_expanded = self.symbols.get(numerator, numerator)
                else:
                    numerator_expanded = numerator

                # Use integer division
                return f"(({numerator_expanded}) // {denominator})"

        return expr

    def resolve_library_function(self, func: str) -> str:
        """Resolve library function reference."""
        return self.LIBRARY_MAP.get(func, func)


class MethodChainBuilder:
    """
    Generates method chains for ObjectFifo split/join operations.

    Handles:
    - .cons().split() chains for ObjectFifoSplit
    - .prod().join() chains for ObjectFifoJoin
    - Offset calculation based on num_outputs/num_inputs and type info
    """

    def __init__(self, expander: ExpressionExpander):
        self.expander = expander

    def build_split_chain(self, source_name: str, num_outputs: int,
                         output_type: str, output_names: List[str],
                         placement: str, attrs: Dict[str, str],
                         source_type_divisor: int = 1,
                         explicit_offsets: Optional[List[str]] = None) -> etree.Element:
        """
        Build <ObjectFifo> element with .cons().split() method chain.

        Args:
            source_type_divisor: Divisor from source FIFO's type (e.g., 16 for memtile_ty = N/16)

        Args:
            source_type_divisor: Divisor from source FIFO's type (e.g., 16 for memtile_ty = N/16)

        Returns XML structure for split operation.
        """
        # Determine tensor reference from data attribute
        data = attrs.get("data", "")
        tensor_ref = self.expander.get_tensor_ref(data)

        obj_fifo = etree.Element("ObjectFifo")
        source_elem = etree.SubElement(obj_fifo, "source")
        method_chain = etree.SubElement(source_elem, "method_chain")

        # Base: reference to source ObjectFifo
        base = etree.SubElement(method_chain, "base")
        var = etree.SubElement(base, "var", ref=source_name)

        # Call: .cons()
        call_cons = etree.SubElement(method_chain, "call")
        method_cons = etree.SubElement(call_cons, "method", name="cons")

        # Call: .split(...)
        call_split = etree.SubElement(method_chain, "call")
        method_split = etree.SubElement(call_split, "method", name="split")

        # kwarg: obj_types
        kwarg_types = etree.SubElement(method_split, "kwarg", name="obj_types")
        type_list = etree.SubElement(kwarg_types, "list")
        for _ in range(num_outputs):
            type_ref_elem = etree.SubElement(type_list, "type_ref")
            type_ref_elem.text = output_type

        # kwarg: offsets
        kwarg_offsets = etree.SubElement(method_split, "kwarg", name="offsets")
        offset_list = etree.SubElement(kwarg_offsets, "list")

        if explicit_offsets:
            # Use explicit symbolic offsets from the GUI XML (as raw const text)
            for off in explicit_offsets:
                off_elem = etree.SubElement(offset_list, "const")
                off_elem.text = str(off).strip()
        else:
            # Calculate offsets as: (tensor.numel() // total_divisor) * i
            total_divisor = source_type_divisor * num_outputs
            for i in range(num_outputs):
                offset_expr = etree.SubElement(offset_list, "binary_op", op="*")
                div_expr = etree.SubElement(offset_expr, "binary_op", op="//")
                method_elem = etree.SubElement(div_expr, "method", ref=tensor_ref, name="numel")
                const_divisor = etree.SubElement(div_expr, "const")
                const_divisor.text = str(total_divisor)
                const_i = etree.SubElement(offset_expr, "const")
                const_i.text = str(i)

        # kwarg: names
        kwarg_names = etree.SubElement(method_split, "kwarg", name="names")
        name_list = etree.SubElement(kwarg_names, "list")
        for name in output_names:
            string_elem = etree.SubElement(name_list, "string")
            string_elem.text = name

        # kwarg: placement
        self._add_placement_kwarg(method_split, placement)

        return obj_fifo

    def build_join_chain(self, dest_name: str, num_inputs: int,
                        input_type: str, input_names: List[str],
                        placement: str, attrs: Dict[str, str],
                        dest_type_divisor: int = 1,
                        explicit_offsets: Optional[List[str]] = None) -> etree.Element:
        """
        Build <ObjectFifo> element with .prod().join() method chain.

        Args:
            dest_type_divisor: Divisor from dest FIFO's type (e.g., 16 for memtile_ty = N/16)

        Args:
            dest_type_divisor: Divisor from dest FIFO's type (e.g., 16 for memtile_ty = N/16)
        """
        data = attrs.get("data", "")
        tensor_ref = self.expander.get_tensor_ref(data)

        obj_fifo = etree.Element("ObjectFifo")
        source_elem = etree.SubElement(obj_fifo, "source")
        method_chain = etree.SubElement(source_elem, "method_chain")

        # Base: reference to dest ObjectFifo
        base = etree.SubElement(method_chain, "base")
        var = etree.SubElement(base, "var", ref=dest_name)

        # Call: .prod()
        call_prod = etree.SubElement(method_chain, "call")
        method_prod = etree.SubElement(call_prod, "method", name="prod")

        # Call: .join(...)
        call_join = etree.SubElement(method_chain, "call")
        method_join = etree.SubElement(call_join, "method", name="join")

        # kwarg: obj_types
        kwarg_types = etree.SubElement(method_join, "kwarg", name="obj_types")
        type_list = etree.SubElement(kwarg_types, "list")
        for _ in range(num_inputs):
            type_ref_elem = etree.SubElement(type_list, "type_ref")
            type_ref_elem.text = input_type

        # kwarg: names
        kwarg_names = etree.SubElement(method_join, "kwarg", name="names")
        name_list = etree.SubElement(kwarg_names, "list")
        for name in input_names:
            string_elem = etree.SubElement(name_list, "string")
            string_elem.text = name

        # kwarg: placement
        self._add_placement_kwarg(method_join, placement)

        # kwarg: offsets
        kwarg_offsets = etree.SubElement(method_join, "kwarg", name="offsets")
        offset_list = etree.SubElement(kwarg_offsets, "list")

        if explicit_offsets:
            # Use explicit symbolic offsets from the GUI XML (as raw const text)
            for off in explicit_offsets:
                off_elem = etree.SubElement(offset_list, "const")
                off_elem.text = str(off).strip()
        else:
            # Calculate offsets as: (tensor.numel() // total_divisor) * i
            total_divisor = dest_type_divisor * num_inputs
            for i in range(num_inputs):
                offset_expr = etree.SubElement(offset_list, "binary_op", op="*")
                div_expr = etree.SubElement(offset_expr, "binary_op", op="//")
                method_elem = etree.SubElement(div_expr, "method", ref=tensor_ref, name="numel")
                const_divisor = etree.SubElement(div_expr, "const")
                const_divisor.text = str(total_divisor)
                const_i = etree.SubElement(offset_expr, "const")
                const_i.text = str(i)

        return obj_fifo

    def _add_placement_kwarg(self, parent: etree.Element, placement: str):
        """Add placement kwarg with Tile constructor."""
        kwarg_placement = etree.SubElement(parent, "kwarg", name="placement")
        constructor = etree.SubElement(kwarg_placement, "constructor", ref="Tile")

        # Parse Tile(x, y)
        match = re.match(r'Tile\((\d+),\s*(\d+)\)', placement)
        if match:
            arg_x = etree.SubElement(constructor, "arg")
            const_x = etree.SubElement(arg_x, "const")
            const_x.text = match.group(1)
            arg_y = etree.SubElement(constructor, "arg")
            const_y = etree.SubElement(arg_y, "const")
            const_y.text = match.group(2)


class XMLTransformer:
    """
    Main orchestrator for transforming simplified XML to complete XML.

    Process:
    1. Parse simplified XML
    2. Extract symbol table and function signatures
    3. Build tensor reference mapping from function parameters
    4. Transform each section (Symbols, DataFlow, Functions)
    5. Generate complete XML with all expanded details
    """

    def __init__(self, simple_xml_path: Path):
        self.simple_xml_path = simple_xml_path
        self.tree = etree.parse(str(simple_xml_path))
        self.root = self.tree.getroot()

        # Extract symbols and function info
        self.symbols = self._extract_symbols()
        self.function_params = self._extract_function_params()

        # Build expander and method builder
        self.expander = ExpressionExpander(self.symbols, self.function_params)
        self.method_builder = MethodChainBuilder(self.expander)

        # Set up tensor reference mapping from JIT function parameters
        self._setup_tensor_refs()

        # Naming lookup tables
        self.objectfifo_names = {}  # simple_name → expanded_name
        self.objectfifo_types = {}  # simple_name → type_name (e.g., "of_in_a" → "memtile_ty")
        self.type_divisors = self._extract_type_divisors()  # type_name → divisor (e.g., "memtile_ty" → 16)
        self.split_outputs = {}  # split_name → list of output names
        self.join_inputs = {}  # join_name → list of input names
        self.function_entry_names = self._build_function_mapping()  # function_name → entry_name

        # Track if we need controlflow import (for range_ loops)
        self.needs_controlflow_import = False
        # Track if we need TensorTiler2D import
        self.needs_tiler2d_import = False

    def _build_function_mapping(self) -> Dict[str, str]:
        """Build mapping of function names to entry names."""
        mapping = {}
        for func in self.root.findall("Function"):
            name = func.get("name")
            entry = func.get("entry")
            if entry:
                mapping[name] = entry
        return mapping

    def _extract_symbols(self) -> Dict[str, str]:
        """Extract constants and type definitions from Symbols section."""
        symbols = {}
        symbols_section = self.root.find("Symbols")
        if symbols_section is not None:
            for const in symbols_section.findall("Const"):
                name = const.get("name")
                value = const.text.strip() if const.text else ""
                symbols[name] = value
        return symbols

    def _extract_type_divisors(self) -> Dict[str, int]:
        """
        Extract divisor values from type shape expressions.

        For types like 'memtile_ty' with shape 'N / 16', extracts divisor 16.
        For types like 'tile_ty' with shape 'N / 64', extracts divisor 64.
        Returns dict mapping type name to its divisor (1 if no division).
        """
        type_divisors = {}
        symbols_section = self.root.find("Symbols")
        if symbols_section is not None:
            for type_abs in symbols_section.findall("TypeAbstraction"):
                name = type_abs.get("name")
                ndarray = type_abs.find("ndarray")
                if ndarray is not None:
                    shape_elem = ndarray.find("shape")
                    if shape_elem is not None and shape_elem.text:
                        shape = shape_elem.text.strip()
                        # Parse shape like "N / 16" to extract divisor
                        if "/" in shape:
                            parts = shape.split("/")
                            if len(parts) == 2:
                                try:
                                    divisor = int(parts[1].strip())
                                    type_divisors[name] = divisor
                                except ValueError:
                                    type_divisors[name] = 1
                        else:
                            # No division means full size (divisor = 1)
                            type_divisors[name] = 1
        return type_divisors

    def _extract_function_params(self) -> Dict[str, List[str]]:
        """Extract function parameter lists."""
        func_params = {}
        for func in self.root.findall("Function"):
            name = func.get("name")
            params = func.find("parameters")
            if params is not None:
                param_list = [p.get("name") for p in params.findall("param")]
                func_params[name] = param_list
        return func_params

    def _setup_tensor_refs(self):
        """
        Set up tensor reference mapping by analyzing JIT function parameters.
        Looks for function with decorator="iron.jit" and extracts parameter names.
        """
        tensor_map = {}

        # Find JIT function
        for func in self.root.findall("Function"):
            if func.get("decorator") == "iron.jit":
                params = func.find("parameters")
                if params is not None:
                    param_list = params.findall("param")
                    # Map based on parameter position/naming convention
                    # Common patterns: inputA, inputB, outputC, outputD, etc.
                    for param in param_list:
                        param_name = param.get("name")
                        if param_name:
                            # Extract letter (A, B, C, D) from parameter name
                            match = re.search(r'([A-Z])$', param_name)
                            if match:
                                letter = match.group(1)
                                tensor_map[letter] = param_name

        self.expander.set_tensor_refs(tensor_map)

    def transform(self) -> etree.Element:
        """
        Transform simplified XML to complete XML.

        Returns:
            Complete XML root element ready for GraphDriver
        """
        # Create new complete XML structure
        complete_root = etree.Element("Module", name=self.root.get("name"))

        # Pre-scan for features that require imports
        self._prescan_for_imports()

        # Transform each section
        self._transform_symbols(complete_root)
        self._transform_dataflow(complete_root)
        self._transform_functions(complete_root)
        self._transform_entrypoint(complete_root)

        return complete_root

    def _prescan_for_imports(self):
        """Pre-scan the XML to determine which imports are needed."""
        # Check for CoreFunctions with loop_count or nested ForLoop elements
        dataflow = self.root.find("DataFlow")
        if dataflow is not None:
            for core_func in dataflow.findall("CoreFunction"):
                if core_func.get("loop_count"):
                    self.needs_controlflow_import = True
                    break
                # Check for nested ForLoop elements in body
                body = core_func.find("body")
                if body is not None:
                    if body.find(".//ForLoop") is not None:
                        self.needs_controlflow_import = True
                        break

        # Check for TensorTiler2D specs in Symbols
        simple_symbols = self.root.find("Symbols")
        if simple_symbols is not None:
            for child in simple_symbols:
                if child.tag == "TensorTiler2D":
                    self.needs_tiler2d_import = True
                    break

    def _transform_symbols(self, parent: etree.Element):
        """Transform Symbols section with expanded type expressions."""
        simple_symbols = self.root.find("Symbols")
        if simple_symbols is None:
            return

        symbols_section = etree.SubElement(parent, "Symbols")

        # Add standard imports first
        self._add_imports(symbols_section)

        # Copy constants
        for child in simple_symbols:
            if child.tag == "Const":
                const = etree.SubElement(symbols_section, "Const",
                                       name=child.get("name"),
                                       type=child.get("type", "int"))
                const.text = child.text

            elif child.tag == "TypeDef":
                # TypeDefs are typically just inline in Python, skip separate definition
                pass

            elif child.tag == "TypeAbstraction":
                self._transform_type_abstraction(child, symbols_section)

            elif child.tag == "TensorTiler2D":
                self._transform_tiler2d(child, symbols_section)

        # Auto-generate tensor-specific type variations if only generic types were provided
        self._auto_generate_type_variations(simple_symbols, symbols_section)

    def _add_imports(self, parent: etree.Element):
        """Add standard imports required for the code."""
        imports = [
            ("numpy", "np"),
            ("ml_dtypes", "ml_dtypes"),
        ]

        for module, alias in imports:
            import_elem = etree.SubElement(parent, "Import", name=module)
            if alias:
                import_elem.set("alias", alias)

        # Add aie.iron with submodules
        iron_import = etree.SubElement(parent, "Import", name="aie.iron", alias="iron")
        submodules = ["Program", "Runtime", "Worker", "ObjectFifo",
                     "SequentialPlacer", "AnyComputeTile", "Tile",
                     "ExternalFunction", "jit", "dataflow"]
        for sub in submodules:
            sub_elem = etree.SubElement(iron_import, "Submodule", name=sub)

        functions = ["get_current_device", "arange", "zeros"]
        for func in functions:
            func_elem = etree.SubElement(iron_import, "Function", name=func)

        # Add taplib imports
        taplib_import = etree.SubElement(parent, "Import", name="aie.helpers.taplib")
        sub_elem = etree.SubElement(taplib_import, "Submodule", name="TensorAccessPattern")
        if self.needs_tiler2d_import:
            sub_elem2 = etree.SubElement(taplib_import, "Submodule", name="TensorTiler2D")

        # Add controlflow import if needed (for range_ loops)
        if self.needs_controlflow_import:
            controlflow_import = etree.SubElement(parent, "Import", name="aie.iron.controlflow")
            sub_elem = etree.SubElement(controlflow_import, "Submodule", name="range_")

    def _transform_type_abstraction(self, simple_type: etree.Element, parent: etree.Element):
        """Transform TypeAbstraction with expanded expressions.

        Handles:
        - Symbolic shapes (m * k, n_cores * m, etc.) — emitted as raw expr text
        - Data-size shapes (data_size, data_size / N) — emitted as numel() calls
        - Multi-dimensional shapes (dim1, dim2) — one tuple per dimension
        """
        name = simple_type.get("name")

        type_abs = etree.SubElement(parent, "TypeAbstraction", name=name)

        # Process ndarray structure
        ndarray = simple_type.find("ndarray")
        if ndarray is not None:
            ndarray_elem = etree.SubElement(type_abs, "ndarray")

            # Transform shape
            shape = ndarray.find("shape")
            if shape is not None:
                shape_elem = etree.SubElement(ndarray_elem, "shape")
                shape_text = shape.text.strip() if shape.text else ""

                # Split by comma for multi-dimensional shapes
                dims = [d.strip() for d in shape_text.split(",") if d.strip()]

                tensor_refs = self.expander.tensor_refs
                tensor_ref = next(iter(tensor_refs.values())) if tensor_refs else "input"

                for dim in dims:
                    tuple_elem = etree.SubElement(shape_elem, "tuple")
                    expr_elem = etree.SubElement(tuple_elem, "expr")

                    # Determine if this is a data_size pattern (legacy) or symbolic
                    is_data_size = "data_size" in dim
                    if is_data_size and tensor_ref:
                        # data_size / N → numel() // N
                        if "/" in dim:
                            parts = dim.split("/")
                            denominator = parts[1].strip()
                            binary_op = etree.SubElement(expr_elem, "binary_op", op="//")
                            etree.SubElement(binary_op, "method", ref=tensor_ref, name="numel")
                            const_elem = etree.SubElement(binary_op, "const")
                            const_elem.text = denominator
                        else:
                            # data_size → numel()
                            etree.SubElement(expr_elem, "method", ref=tensor_ref, name="numel")
                    else:
                        # Symbolic expression — keep as raw text.
                        # Constants will be defined as local variables in the jit function.
                        # Use integer division (//) so numpy ndarray shapes stay int (not float).
                        expr_elem.text = re.sub(r'(?<!/)/(?!/)', '//', dim)

            # Transform dtype
            dtype = ndarray.find("dtype")
            if dtype is not None:
                dtype_elem = etree.SubElement(ndarray_elem, "dtype")
                numpy_dtype = etree.SubElement(dtype_elem, "numpy_dtype")
                numpy_dtype.text = dtype.text.strip() if dtype.text else "bfloat16"

    def _auto_generate_type_variations(self, simple_symbols: etree.Element, symbols_section: etree.Element):
        """
        Auto-generate tensor-specific type variations from generic types.

        If simple XML has generic types like 'data_ty', 'chunk_ty', 'worker_chunk_ty',
        this will generate specific variations like 'data_a_ty', 'chunk_a', 'chunk_a_worker'
        for each tensor parameter (A, B, D, etc.).
        """
        # Check if we have generic types that need expansion
        existing_types = {ta.get("name") for ta in symbols_section.findall("TypeAbstraction")}

        # Detect if we only have generic types (data_ty, chunk_ty, worker_chunk_ty)
        has_generic_data = "data_ty" in existing_types
        has_generic_chunk = "chunk_ty" in existing_types
        has_generic_worker_chunk = "worker_chunk_ty" in existing_types

        # Check if specific types already exist (if so, don't auto-generate)
        has_specific_types = any(name.endswith("_a_ty") or name.endswith("_b_ty")
                                for name in existing_types)

        if not has_specific_types and (has_generic_data or has_generic_chunk or has_generic_worker_chunk):
            # Get tensor references from function parameters
            tensor_refs = self.expander.tensor_refs

            if not tensor_refs:
                return

            # For each tensor, create specific type variations
            for letter, param_name in sorted(tensor_refs.items()):
                letter_lower = letter.lower()

                # Generate data_X_ty (full tensor type)
                if has_generic_data:
                    self._generate_type_variation(
                        symbols_section,
                        f"data_{letter_lower}_ty",
                        param_name,
                        "1",
                        "bfloat16"
                    )

                # Generate chunk_X (column chunk type)
                if has_generic_chunk:
                    self._generate_type_variation(
                        symbols_section,
                        f"chunk_{letter_lower}",
                        param_name,
                        "4",
                        "bfloat16"
                    )

                # Generate chunk_X_worker (worker chunk type)
                if has_generic_worker_chunk:
                    self._generate_type_variation(
                        symbols_section,
                        f"chunk_{letter_lower}_worker",
                        param_name,
                        "8",
                        "bfloat16"
                    )

    def _generate_type_variation(self, parent: etree.Element, type_name: str,
                                 tensor_ref: str, divisor: str, dtype_str: str):
        """Generate a single type variation."""
        type_abs = etree.SubElement(parent, "TypeAbstraction", name=type_name)
        ndarray_elem = etree.SubElement(type_abs, "ndarray")

        # Shape
        shape_elem = etree.SubElement(ndarray_elem, "shape")
        tuple_elem = etree.SubElement(shape_elem, "tuple")
        expr_elem = etree.SubElement(tuple_elem, "expr")

        if divisor == "1":
            # Just tensor.numel()
            method_elem = etree.SubElement(expr_elem, "method", ref=tensor_ref, name="numel")
        else:
            # tensor.numel() // divisor
            binary_op = etree.SubElement(expr_elem, "binary_op", op="//")
            method_elem = etree.SubElement(binary_op, "method", ref=tensor_ref, name="numel")
            const_elem = etree.SubElement(binary_op, "const")
            const_elem.text = divisor

        # Dtype
        dtype_elem = etree.SubElement(ndarray_elem, "dtype")
        numpy_dtype = etree.SubElement(dtype_elem, "numpy_dtype")
        numpy_dtype.text = dtype_str

    def _transform_dataflow(self, parent: etree.Element):
        """Transform DataFlow section with expanded ObjectFifos, Workers, Runtime."""
        simple_dataflow = self.root.find("DataFlow")
        if simple_dataflow is None:
            return

        dataflow_section = etree.SubElement(parent, "DataFlow")

        # Process elements in order
        for child in simple_dataflow:
            tag = child.tag

            if tag == "ExternalFunction":
                self._transform_external_function(child, dataflow_section)
            elif tag == "CoreFunction":
                self._transform_core_function(child, dataflow_section)
            elif tag == "ObjectFifo":
                self._transform_objectfifo(child, dataflow_section)
            elif tag == "ObjectFifoSplit":
                self._transform_split(child, dataflow_section)
            elif tag == "ObjectFifoJoin":
                self._transform_join(child, dataflow_section)
            elif tag == "ObjectFifoForward":
                self._transform_forward(child, dataflow_section)
            elif tag == "Worker":
                self._transform_worker(child, dataflow_section)
            elif tag == "Runtime":
                self._transform_runtime(child, dataflow_section)
            elif tag == "Program":
                self._transform_program(child, dataflow_section)

    def _transform_external_function(self, simple_func: etree.Element, parent: etree.Element):
        """Transform ExternalFunction with proper structure."""
        name = simple_func.get("name")

        ext_func = etree.SubElement(parent, "ExternalFunction", name=name)
        attributes = etree.SubElement(ext_func, "attributes")

        # kernel name
        kernel = simple_func.find("kernel")
        if kernel is not None:
            kwarg = etree.SubElement(attributes, "kwarg", name="name", value=kernel.text.strip())

        # source file
        source = simple_func.find("source")
        if source is not None:
            kwarg = etree.SubElement(attributes, "kwarg", name="source_file", value=source.text.strip())

        # arg_types
        arg_types = simple_func.find("arg_types")
        if arg_types is not None:
            kwarg = etree.SubElement(attributes, "kwarg", name="arg_types")
            type_list = etree.SubElement(kwarg, "list")
            for type_elem in arg_types.findall("type"):
                type_ref = etree.SubElement(type_list, "type_ref")
                type_ref.text = type_elem.text.strip()

        # include_dirs (read from GUI XML if present)
        include_dirs_elem = simple_func.find("include_dirs")
        if include_dirs_elem is not None:
            kwarg_include = etree.SubElement(attributes, "kwarg", name="include_dirs")
            include_list = etree.SubElement(kwarg_include, "list")
            for dir_elem in include_dirs_elem.findall("dir"):
                string_elem = etree.SubElement(include_list, "string")
                string_elem.text = dir_elem.text.strip() if dir_elem.text else ""

    def _transform_core_function(self, simple_func: etree.Element, parent: etree.Element):
        """Transform CoreFunction with full body, including optional loop wrapper."""
        name = simple_func.get("name")
        loop_count = simple_func.get("loop_count")

        core_func = etree.SubElement(parent, "CoreFunction", name=name)

        # Parameters
        params_section = etree.SubElement(core_func, "parameters")
        params = simple_func.find("parameters")
        if params is not None:
            for param in params.findall("param"):
                param_elem = etree.SubElement(params_section, "param", name=param.get("name"))

        # Body
        body_section = etree.SubElement(core_func, "body")

        # If loop_count is specified, wrap body in a For loop
        if loop_count:
            self.needs_controlflow_import = True
            # Expand the loop_count expression
            expanded_loop_count = self.expander.expand_shape_expression(loop_count)
            # Create For element with range_(loop_count)
            for_elem = etree.SubElement(body_section, "For", var="_")
            for_elem.set("range", f"range_({expanded_loop_count})")
            # Statements go inside the For element
            stmt_parent = for_elem
        else:
            # Statements go directly in body
            stmt_parent = body_section

        body = simple_func.find("body")
        if body is not None:
            self._transform_core_body_stmts(body, stmt_parent)

    def _transform_core_body_stmts(self, stmts_elem: etree.Element, parent: etree.Element):
        """
        Transform a sequence of body statement elements into complete XML nodes.

        Handles: Acquire, Release, Call, ForLoop, Assignment
        Supports nested ForLoop recursively for patterns like matrix_vector_mul.
        """
        for stmt in stmts_elem:
            if stmt.tag == "Acquire":
                acquire = etree.SubElement(parent, "Acquire", name=stmt.get("name"))
                call = etree.SubElement(acquire, "call")
                method = etree.SubElement(call, "method", ref=stmt.get("source"), name="acquire")
                arg = etree.SubElement(method, "arg")
                const = etree.SubElement(arg, "const")
                const.text = stmt.get("count", "1")

            elif stmt.tag == "Call":
                call = etree.SubElement(parent, "Call")
                func = etree.SubElement(call, "function", ref=stmt.get("function"))
                args_text = stmt.get("args", "")
                for arg_name in args_text.split(","):
                    arg_name = arg_name.strip()
                    if arg_name:
                        arg = etree.SubElement(func, "arg")
                        var = etree.SubElement(arg, "var", ref=arg_name)

            elif stmt.tag == "Release":
                release = etree.SubElement(parent, "Release")
                call = etree.SubElement(release, "call")
                method = etree.SubElement(call, "method", ref=stmt.get("source"), name="release")
                arg = etree.SubElement(method, "arg")
                const = etree.SubElement(arg, "const")
                const.text = stmt.get("count", "1")

            elif stmt.tag == "ForLoop":
                # Nested for loop: for <var> in range_(<count>)
                self.needs_controlflow_import = True
                var_name = stmt.get("var", "_")
                count_expr = stmt.get("count", "1")
                expanded_count = self.expander.expand_shape_expression(count_expr)
                for_elem = etree.SubElement(parent, "For", var=var_name)
                for_elem.set("range", f"range_({expanded_count})")
                # Recursively process the nested body
                self._transform_core_body_stmts(stmt, for_elem)

            elif stmt.tag == "Assignment":
                # Indexed assignment: target[index] = value
                target = stmt.get("target", "")
                index = stmt.get("index", "")
                value = stmt.get("value", "0")
                assign_elem = etree.SubElement(parent, "Assignment")
                assign_elem.set("target", target)
                assign_elem.set("index", index)
                assign_elem.set("value", value)

    def _transform_tiler2d(self, simple_tiler: etree.Element, parent: etree.Element):
        """
        Transform a TensorTiler2D element into an assignment variable node.

        Generates: name = TensorTiler2D.group_tiler(tensor_dims, tile_dims, tile_counts,
                             [pattern_repeat=value,] prune_step=bool)[index]
        """
        name = simple_tiler.get("name")
        prune_step = simple_tiler.get("prune_step", "false").lower() == "true"
        index = int(simple_tiler.get("index", "0"))
        pattern_repeat = simple_tiler.get("pattern_repeat")

        # Parse dimension expressions (comma-separated)
        tensor_dims_text = (simple_tiler.findtext("tensor_dims") or "").strip()
        tile_dims_text = (simple_tiler.findtext("tile_dims") or "").strip()
        tile_counts_text = (simple_tiler.findtext("tile_counts") or "").strip()

        # Expand expressions
        def expand_dim_list(text):
            parts = [p.strip() for p in text.split(",")]
            return [self.expander.expand_shape_expression(p) for p in parts if p]

        tensor_dims = expand_dim_list(tensor_dims_text)
        tile_dims = expand_dim_list(tile_dims_text)
        tile_counts = expand_dim_list(tile_counts_text)

        # Emit as a TensorTiler2D node: name = TensorTiler2D.group_tiler(...)[index]
        tiler_node = etree.SubElement(parent, "TensorTiler2D")
        tiler_node.set("name", name)

        # Store expanded parameters as attributes for CodeGenerator to use
        tiler_node.set("tensor_dims", ", ".join(tensor_dims))
        tiler_node.set("tile_dims", ", ".join(tile_dims))
        tiler_node.set("tile_counts", ", ".join(tile_counts))
        tiler_node.set("prune_step", "True" if prune_step else "False")
        tiler_node.set("index", str(index))
        if pattern_repeat is not None:
            expanded_repeat = self.expander.expand_shape_expression(pattern_repeat)
            tiler_node.set("pattern_repeat", expanded_repeat)

    def _transform_objectfifo(self, simple_of: etree.Element, parent: etree.Element):
        """Transform ObjectFifo with generated name based on context."""
        simple_name = simple_of.get("name")
        attrs = dict(simple_of.attrib)

        # Generate full name based on context attributes
        # If no context, use the simple name as-is
        if "context" in attrs:
            expanded_name = NamingConventions.generate_objectfifo_name(attrs)
        else:
            expanded_name = simple_name
        self.objectfifo_names[simple_name] = expanded_name

        # Store the FIFO's type for offset calculations in split/join
        fifo_type = simple_of.find("type").text.strip() if simple_of.find("type") is not None else "data_ty"
        self.objectfifo_types[simple_name] = fifo_type

        # Create ObjectFifo element
        obj_fifo = etree.SubElement(parent, "ObjectFifo", name=expanded_name)
        obj_type = etree.SubElement(obj_fifo, "obj_type")
        type_ref = etree.SubElement(obj_type, "type_ref")

        # Map generic type to tensor-specific type
        generic_type = simple_of.find("type").text.strip()
        specific_type = self._map_to_specific_type(generic_type, attrs.get("data", ""), attrs.get("context", ""))
        type_ref.text = specific_type

        # Add attributes
        attributes = etree.SubElement(obj_fifo, "attributes")

        depth = simple_of.find("depth")
        if depth is not None:
            kwarg = etree.SubElement(attributes, "kwarg", name="depth", value=depth.text.strip())

        kwarg_name = etree.SubElement(attributes, "kwarg", name="name", value=expanded_name)

    def _map_to_specific_type(self, generic_type: str, data: str, context: str) -> str:
        """
        Map generic type name to tensor-specific type name.

        Args:
            generic_type: Generic type like 'chunk_ty' or 'worker_chunk_ty'
            data: Data identifier like 'A', 'B', 'D'
            context: Context like 'L3_L2', 'L2_L1', etc.

        Returns:
            Specific type name like 'chunk_a', 'chunk_b_worker', etc.
        """
        if not data:
            return generic_type

        data_lower = data.lower()

        # Map generic types to specific tensor types
        type_map = {
            "data_ty": f"data_{data_lower}_ty",
            "chunk_ty": f"chunk_{data_lower}",
            "worker_chunk_ty": f"chunk_{data_lower}_worker",
        }

        return type_map.get(generic_type, generic_type)

    def _transform_split(self, simple_split: etree.Element, parent: etree.Element):
        """Transform ObjectFifoSplit to method chain form."""
        simple_name = simple_split.get("name")
        source_name = simple_split.find("source").text.strip()
        num_outputs = int(simple_split.find("num_outputs").text.strip())
        generic_output_type = simple_split.find("output_type").text.strip()
        placement = simple_split.find("placement").text.strip()
        attrs = dict(simple_split.attrib)

        # Get expanded source name
        expanded_source = self.objectfifo_names.get(source_name, source_name)

        # Get the source FIFO's type divisor for offset calculation
        # e.g., if source is memtile_ty (N/16), divisor is 16
        source_fifo_type = self.objectfifo_types.get(source_name, "data_ty")
        source_type_divisor = self.type_divisors.get(source_fifo_type, 1)

        # Map generic type to specific type
        specific_output_type = self._map_to_specific_type(
            generic_output_type, attrs.get("data", ""), attrs.get("context", "")
        )

        # Generate output names
        output_names = NamingConventions.generate_split_output_names(attrs, num_outputs)
        self.split_outputs[simple_name] = output_names

        # Generate split name from context; fall back to the HLIR operation name
        # when there is no context metadata (e.g. GUI-generated designs).
        split_name = NamingConventions.generate_objectfifo_name(attrs, num_outputs)
        if not attrs.get("context"):
            split_name = simple_name

        # Check for explicit offsets in the GUI XML
        explicit_offsets = None
        offsets_elem = simple_split.find("offsets")
        if offsets_elem is not None and offsets_elem.text:
            explicit_offsets = [o.strip() for o in offsets_elem.text.split(",") if o.strip()]

        # Build method chain with source type divisor for offset calculation
        obj_fifo = self.method_builder.build_split_chain(
            expanded_source, num_outputs, specific_output_type, output_names, placement, attrs,
            source_type_divisor=source_type_divisor,
            explicit_offsets=explicit_offsets
        )
        obj_fifo.set("name", split_name)

        parent.append(obj_fifo)
        self.objectfifo_names[simple_name] = split_name

    def _transform_join(self, simple_join: etree.Element, parent: etree.Element):
        """Transform ObjectFifoJoin to method chain form."""
        simple_name = simple_join.get("name")
        dest_name = simple_join.find("dest").text.strip()
        num_inputs = int(simple_join.find("num_inputs").text.strip())
        generic_input_type = simple_join.find("input_type").text.strip()
        placement = simple_join.find("placement").text.strip()
        attrs = dict(simple_join.attrib)

        # Get expanded dest name
        expanded_dest = self.objectfifo_names.get(dest_name, dest_name)

        # Get the dest FIFO's type divisor for offset calculation
        # e.g., if dest is memtile_ty (N/16), divisor is 16
        dest_fifo_type = self.objectfifo_types.get(dest_name, "data_ty")
        dest_type_divisor = self.type_divisors.get(dest_fifo_type, 1)

        # Map generic type to specific type
        specific_input_type = self._map_to_specific_type(
            generic_input_type, attrs.get("data", ""), attrs.get("context", "")
        )

        # Generate input names
        input_names = NamingConventions.generate_join_input_names(attrs, num_inputs)
        self.join_inputs[simple_name] = input_names

        # Generate join name from context; fall back to the HLIR operation name
        # when there is no context metadata (e.g. GUI-generated designs).
        join_name = NamingConventions.generate_objectfifo_name(attrs, num_inputs)
        if not attrs.get("context"):
            join_name = simple_name

        # Check for explicit offsets in the GUI XML
        explicit_offsets = None
        offsets_elem = simple_join.find("offsets")
        if offsets_elem is not None and offsets_elem.text:
            explicit_offsets = [o.strip() for o in offsets_elem.text.split(",") if o.strip()]

        # Build method chain with dest type divisor for offset calculation
        obj_fifo = self.method_builder.build_join_chain(
            expanded_dest, num_inputs, specific_input_type, input_names, placement, attrs,
            dest_type_divisor=dest_type_divisor,
            explicit_offsets=explicit_offsets
        )
        obj_fifo.set("name", join_name)

        parent.append(obj_fifo)
        self.objectfifo_names[simple_name] = join_name

    def _transform_forward(self, simple_forward: etree.Element, parent: etree.Element):
        """Transform ObjectFifoForward to method chain form: of_out = of_in.cons().forward()"""
        simple_name = simple_forward.get("name")
        source_name = simple_forward.get("source")

        # Get expanded source name
        expanded_source = self.objectfifo_names.get(source_name, source_name)

        # Build method chain: source.cons().forward()
        obj_fifo = etree.Element("ObjectFifo")
        source_elem = etree.SubElement(obj_fifo, "source")
        method_chain = etree.SubElement(source_elem, "method_chain")

        # Base: reference to source ObjectFifo
        base = etree.SubElement(method_chain, "base")
        var = etree.SubElement(base, "var", ref=expanded_source)

        # Call: .cons()
        call_cons = etree.SubElement(method_chain, "call")
        method_cons = etree.SubElement(call_cons, "method", name="cons")

        # Call: .forward(placement=Tile(x, y))
        call_forward = etree.SubElement(method_chain, "call")
        method_forward = etree.SubElement(call_forward, "method", name="forward")

        # Add placement kwarg if present
        placement_str = simple_forward.get("placement")
        if placement_str:
            match = re.match(r'Tile\((\d+),\s*(\d+)\)', placement_str)
            if match:
                kwarg_placement = etree.SubElement(call_forward, "kwarg", name="placement")
                constructor = etree.SubElement(kwarg_placement, "constructor", ref="Tile")
                arg_x = etree.SubElement(constructor, "arg")
                const_x = etree.SubElement(arg_x, "const")
                const_x.text = match.group(1)
                arg_y = etree.SubElement(constructor, "arg")
                const_y = etree.SubElement(arg_y, "const")
                const_y.text = match.group(2)

        # Set the name to match simple name
        obj_fifo.set("name", simple_name)

        parent.append(obj_fifo)
        self.objectfifo_names[simple_name] = simple_name

    def _transform_worker(self, simple_worker: etree.Element, parent: etree.Element):
        """Transform Worker with full argument paths."""
        name = simple_worker.get("name")
        attrs = dict(simple_worker.attrib)

        # Use provided name or generate from context
        # The simple XML should already have meaningful worker names
        worker = etree.SubElement(parent, "Worker", name=name)

        # core_fn
        core_fn = simple_worker.find("core_function").text.strip()
        core_fn_elem = etree.SubElement(worker, "core_fn", ref=core_fn)

        # fn_args
        fn_args = etree.SubElement(worker, "fn_args")
        arguments = simple_worker.find("arguments")

        for arg in arguments.findall("arg"):
            arg_ref = arg.get("ref")
            index = arg.get("index")
            mode = arg.get("mode")

            arg_elem = etree.SubElement(fn_args, "arg")

            # If it references something simple (external function or direct ObjectFifo)
            if index is None and mode is None:
                # Direct reference (likely external function)
                var = etree.SubElement(arg_elem, "var", ref=arg_ref)

            # If it's a split/join with index
            elif index is not None:
                # Build indexing with method chain
                method_chain = etree.SubElement(arg_elem, "method_chain")

                # Base: index into split/join array
                base = etree.SubElement(method_chain, "base")
                index_elem = etree.SubElement(base, "index")
                index_base = etree.SubElement(index_elem, "base")

                # Get the expanded name
                expanded_ref = self.objectfifo_names.get(arg_ref, arg_ref)
                var_ref = etree.SubElement(index_base, "var", ref=expanded_ref)

                index_value = etree.SubElement(index_elem, "index_value")
                const = etree.SubElement(index_value, "const")
                const.text = index

                # Call: .cons() or .prod()
                call = etree.SubElement(method_chain, "call")
                method = etree.SubElement(call, "method", name="cons" if mode == "consumer" else "prod")

            # If it's a simple ObjectFifo reference with mode
            elif mode is not None:
                method_chain = etree.SubElement(arg_elem, "method_chain")
                base = etree.SubElement(method_chain, "base")
                expanded_ref = self.objectfifo_names.get(arg_ref, arg_ref)
                var = etree.SubElement(base, "var", ref=expanded_ref)
                call = etree.SubElement(method_chain, "call")
                method = etree.SubElement(call, "method", name="cons" if mode == "consumer" else "prod")

        # placement
        placement = simple_worker.find("placement").text.strip()
        placement_elem = etree.SubElement(worker, "placement")
        match = re.match(r'Tile\((\d+),\s*(\d+)\)', placement)
        if match:
            constructor = etree.SubElement(placement_elem, "constructor", ref="Tile")
            arg_x = etree.SubElement(constructor, "arg")
            const_x = etree.SubElement(arg_x, "const")
            const_x.text = match.group(1)
            arg_y = etree.SubElement(constructor, "arg")
            const_y = etree.SubElement(arg_y, "const")
            const_y.text = match.group(2)

    def _transform_runtime(self, simple_runtime: etree.Element, parent: etree.Element):
        """Transform Runtime with complete sequence block."""
        runtime = etree.SubElement(parent, "Runtime", name="rt")
        instance = etree.SubElement(runtime, "instance")
        constructor = etree.SubElement(instance, "constructor", ref="Runtime")

        # Add Workers List (collect all workers from DataFlow)
        workers_list = etree.SubElement(parent, "List", name="Workers")
        items = etree.SubElement(workers_list, "items")

        # Find all workers in simple dataflow
        simple_dataflow = self.root.find("DataFlow")
        if simple_dataflow is not None:
            for worker in simple_dataflow.findall("Worker"):
                worker_name = worker.get("name")
                var = etree.SubElement(items, "var", ref=worker_name)

        # Process sequence
        sequence = simple_runtime.find("Sequence")
        if sequence is not None:
            self._transform_sequence(sequence, parent)

    def _transform_sequence(self, simple_seq: etree.Element, parent: etree.Element):
        """Transform Sequence block with fills and drains."""
        # Create SequenceBlock
        seq_block = etree.SubElement(parent, "SequenceBlock")

        # Context
        context = etree.SubElement(seq_block, "context")
        call = etree.SubElement(context, "call")
        method = etree.SubElement(call, "method", ref="rt", name="sequence")

        # Add type args
        inputs = simple_seq.get("inputs", "")
        for type_name in inputs.split(","):
            type_name = type_name.strip()
            if type_name:
                arg = etree.SubElement(method, "arg")
                arg.set("type_ref", type_name)

        # Bindings
        bindings = etree.SubElement(seq_block, "bindings")
        binding_names = simple_seq.get("as", "").split(",")
        type_names = inputs.split(",")
        for bind_name, type_name in zip(binding_names, type_names):
            bind_name = bind_name.strip()
            type_name = type_name.strip()
            if bind_name and type_name:
                bind = etree.SubElement(bindings, "bind", name=bind_name)
                bind.set("type_ref", type_name)

        # Body
        body = etree.SubElement(seq_block, "body")

        # Start operation
        start = simple_seq.find("Start")
        if start is not None:
            self._transform_start(start, body)

        # Fill operations
        for fill in simple_seq.findall("Fill"):
            self._transform_fill(fill, body)

        # Drain operations
        for drain in simple_seq.findall("Drain"):
            self._transform_drain(drain, body)

    def _transform_start(self, simple_start: etree.Element, parent: etree.Element):
        """Transform Start operation."""
        operation = etree.SubElement(parent, "Operation", name="start")
        target = etree.SubElement(operation, "target")
        method = etree.SubElement(target, "method", ref="rt", name="start")

        args = etree.SubElement(operation, "args")

        # Add reference to Workers list
        arg = etree.SubElement(args, "arg")
        var = etree.SubElement(arg, "var", ref="Workers")

    def _transform_fill(self, simple_fill: etree.Element, parent: etree.Element):
        """Transform Fill operation with or without TensorAccessPattern."""
        target = simple_fill.get("target")
        source = simple_fill.get("source")
        column = simple_fill.get("column", "0")
        use_tap = simple_fill.get("use_tap", "false").lower() == "true"
        # data_ref preserves original parameter name (A, B, etc.) for TAP calculations
        data_ref = simple_fill.get("data_ref", source)

        # Get expanded target name
        expanded_target = self.objectfifo_names.get(target, target)

        operation_name = f"fill_{source}_col{column}"
        operation = etree.SubElement(parent, "Operation", name=operation_name)

        # target
        target_elem = etree.SubElement(operation, "target")
        method = etree.SubElement(target_elem, "method", ref="rt", name="fill")

        # args
        args = etree.SubElement(operation, "args")

        tap_type = simple_fill.get("tap_type", "tap")
        tap_var = simple_fill.get("tap_var")

        if use_tap:
            # Complex form with kwargs and TAP/TensorTiler2D
            # placement
            placement = simple_fill.find("placement").text.strip()
            kwarg_placement = etree.SubElement(args, "kwarg", name="placement")
            match = re.match(r'Tile\((\d+),\s*(\d+)\)', placement)
            if match:
                constructor = etree.SubElement(kwarg_placement, "constructor", ref="Tile")
                arg_x = etree.SubElement(constructor, "arg")
                const_x = etree.SubElement(arg_x, "const")
                const_x.text = match.group(1)
                arg_y = etree.SubElement(constructor, "arg")
                const_y = etree.SubElement(arg_y, "const")
                const_y.text = match.group(2)

            # in_fifo
            kwarg_fifo = etree.SubElement(args, "kwarg", name="in_fifo")
            var = etree.SubElement(kwarg_fifo, "var", ref=expanded_target)
            dot = etree.SubElement(kwarg_fifo, "method", name="prod")

            # source
            kwarg_source = etree.SubElement(args, "kwarg", name="source")
            var = etree.SubElement(kwarg_source, "var", ref=source)

            # tap — either a TensorTiler2D variable reference or inline TAP constructor
            kwarg_tap = etree.SubElement(args, "kwarg", name="tap")
            if tap_type == "tiler2d" and tap_var:
                var_ref = etree.SubElement(kwarg_tap, "var", ref=tap_var)
            else:
                self._build_tensor_access_pattern(kwarg_tap, data_ref, column)
        else:
            # Simple form with positional args only
            # arg1: ObjectFifo.prod()
            arg1 = etree.SubElement(args, "arg")
            call = etree.SubElement(arg1, "call")
            method = etree.SubElement(call, "method", ref=expanded_target, name="prod")

            # arg2: source tensor
            arg2 = etree.SubElement(args, "arg")
            var = etree.SubElement(arg2, "var", ref=source)

            # placement kwarg (if present)
            placement_elem = simple_fill.find("placement")
            if placement_elem is not None:
                placement = placement_elem.text.strip()
                kwarg_placement = etree.SubElement(args, "kwarg", name="placement")
                match = re.match(r'Tile\((\d+),\s*(\d+)\)', placement)
                if match:
                    constructor = etree.SubElement(kwarg_placement, "constructor", ref="Tile")
                    arg_x = etree.SubElement(constructor, "arg")
                    const_x = etree.SubElement(arg_x, "const")
                    const_x.text = match.group(1)
                    arg_y = etree.SubElement(constructor, "arg")
                    const_y = etree.SubElement(arg_y, "const")
                    const_y.text = match.group(2)

    def _transform_drain(self, simple_drain: etree.Element, parent: etree.Element):
        """Transform Drain operation with or without TensorAccessPattern."""
        source = simple_drain.get("source")
        target = simple_drain.get("target")
        column = simple_drain.get("column", "0")
        use_tap = simple_drain.get("use_tap", "false").lower() == "true"
        # data_ref preserves original parameter name (D, etc.) for TAP calculations
        data_ref = simple_drain.get("data_ref", target)

        # Get expanded source name
        expanded_source = self.objectfifo_names.get(source, source)

        operation_name = f"drain_{target}_col{column}"
        operation = etree.SubElement(parent, "Operation", name=operation_name)

        # target
        target_elem = etree.SubElement(operation, "target")
        method = etree.SubElement(target_elem, "method", ref="rt", name="drain")

        # args
        args = etree.SubElement(operation, "args")

        tap_type = simple_drain.get("tap_type", "tap")
        tap_var = simple_drain.get("tap_var")

        if use_tap:
            # Complex form with kwargs and TAP/TensorTiler2D
            # placement
            placement = simple_drain.find("placement").text.strip()
            kwarg_placement = etree.SubElement(args, "kwarg", name="placement")
            match = re.match(r'Tile\((\d+),\s*(\d+)\)', placement)
            if match:
                constructor = etree.SubElement(kwarg_placement, "constructor", ref="Tile")
                arg_x = etree.SubElement(constructor, "arg")
                const_x = etree.SubElement(arg_x, "const")
                const_x.text = match.group(1)
                arg_y = etree.SubElement(constructor, "arg")
                const_y = etree.SubElement(arg_y, "const")
                const_y.text = match.group(2)

            # out_fifo
            kwarg_fifo = etree.SubElement(args, "kwarg", name="out_fifo")
            var = etree.SubElement(kwarg_fifo, "var", ref=expanded_source)
            dot = etree.SubElement(kwarg_fifo, "method", name="cons")

            # dest
            kwarg_dest = etree.SubElement(args, "kwarg", name="dest")
            var = etree.SubElement(kwarg_dest, "var", ref=target)

            # wait
            wait = simple_drain.find("wait")
            if wait is not None:
                kwarg_wait = etree.SubElement(args, "kwarg", name="wait", value=wait.text.strip().capitalize())

            # tap — either a TensorTiler2D variable reference or inline TAP constructor
            kwarg_tap = etree.SubElement(args, "kwarg", name="tap")
            if tap_type == "tiler2d" and tap_var:
                var_ref = etree.SubElement(kwarg_tap, "var", ref=tap_var)
            else:
                self._build_tensor_access_pattern(kwarg_tap, data_ref, column)
        else:
            # Simple form with positional args only
            # arg1: ObjectFifo.cons()
            arg1 = etree.SubElement(args, "arg")
            call = etree.SubElement(arg1, "call")
            method = etree.SubElement(call, "method", ref=expanded_source, name="cons")

            # arg2: target tensor
            arg2 = etree.SubElement(args, "arg")
            var = etree.SubElement(arg2, "var", ref=target)

            # kwarg: wait (if present)
            wait = simple_drain.find("wait")
            if wait is not None:
                kwarg_wait = etree.SubElement(args, "kwarg", name="wait", value=wait.text.strip().capitalize())

            # placement kwarg (if present)
            placement_elem = simple_drain.find("placement")
            if placement_elem is not None:
                placement = placement_elem.text.strip()
                kwarg_placement = etree.SubElement(args, "kwarg", name="placement")
                match = re.match(r'Tile\((\d+),\s*(\d+)\)', placement)
                if match:
                    constructor = etree.SubElement(kwarg_placement, "constructor", ref="Tile")
                    arg_x = etree.SubElement(constructor, "arg")
                    const_x = etree.SubElement(arg_x, "const")
                    const_x.text = match.group(1)
                    arg_y = etree.SubElement(constructor, "arg")
                    const_y = etree.SubElement(arg_y, "const")
                    const_y.text = match.group(2)

    def _build_tensor_access_pattern(self, parent: etree.Element, tensor: str, column: str):
        """Build TensorAccessPattern constructor."""
        tensor_ref = self.expander.get_tensor_ref(tensor)
        col_num = int(column)

        constructor = etree.SubElement(parent, "constructor", ref="TensorAccessPattern")

        # tensor_dims
        kwarg_dims = etree.SubElement(constructor, "kwarg", name="tensor_dims")
        list_elem = etree.SubElement(kwarg_dims, "list")
        method = etree.SubElement(list_elem, "method", ref=tensor_ref, name="numel")

        # offset
        kwarg_offset = etree.SubElement(constructor, "kwarg", name="offset")
        offset_expr = etree.SubElement(kwarg_offset, "binary_op", op="*")
        div_expr = etree.SubElement(offset_expr, "binary_op", op="//")
        method1 = etree.SubElement(div_expr, "method", ref=tensor_ref, name="numel")
        const_4 = etree.SubElement(div_expr, "const")
        const_4.text = "4"
        const_col = etree.SubElement(offset_expr, "const")
        const_col.text = str(col_num)

        # sizes
        kwarg_sizes = etree.SubElement(constructor, "kwarg", name="sizes")
        list_sizes = etree.SubElement(kwarg_sizes, "list")

        # First size: ((tensor.numel() // 4) // (tensor.numel() // 8))
        size1 = etree.SubElement(list_sizes, "binary_op", op="//")
        numerator = etree.SubElement(size1, "binary_op", op="//")
        method2 = etree.SubElement(numerator, "method", ref=tensor_ref, name="numel")
        const_4_2 = etree.SubElement(numerator, "const")
        const_4_2.text = "4"
        denominator = etree.SubElement(size1, "binary_op", op="//")
        method3 = etree.SubElement(denominator, "method", ref=tensor_ref, name="numel")
        const_8 = etree.SubElement(denominator, "const")
        const_8.text = "8"

        # Second size: (tensor.numel() // 8)
        size2 = etree.SubElement(list_sizes, "binary_op", op="//")
        method4 = etree.SubElement(size2, "method", ref=tensor_ref, name="numel")
        const_8_2 = etree.SubElement(size2, "const")
        const_8_2.text = "8"

        # strides
        kwarg_strides = etree.SubElement(constructor, "kwarg", name="strides")
        list_strides = etree.SubElement(kwarg_strides, "list")

        # First stride: (tensor.numel() // 8)
        stride1 = etree.SubElement(list_strides, "binary_op", op="//")
        method5 = etree.SubElement(stride1, "method", ref=tensor_ref, name="numel")
        const_8_3 = etree.SubElement(stride1, "const")
        const_8_3.text = "8"

        # Second stride: 1
        const_1 = etree.SubElement(list_strides, "const")
        const_1.text = "1"

    def _transform_program(self, simple_program: etree.Element, parent: etree.Element):
        """Transform Program construction."""
        program = etree.SubElement(parent, "Program", name="my_program")
        constructor = etree.SubElement(program, "constructor")
        call = etree.SubElement(constructor, "call")
        prog_constructor = etree.SubElement(call, "constructor", ref="Program")

        # Device arg
        arg1 = etree.SubElement(prog_constructor, "arg")
        call_device = etree.SubElement(arg1, "call")
        func_device = etree.SubElement(call_device, "function", ref="iron.get_current_device")

        # Runtime arg
        arg2 = etree.SubElement(prog_constructor, "arg")
        var_rt = etree.SubElement(arg2, "var", ref="rt")

        # Resolve program
        resolve = etree.SubElement(parent, "ResolveProgram")
        target = etree.SubElement(resolve, "target")
        method = etree.SubElement(target, "method", ref="my_program", name="resolve_program")
        arg_placer = etree.SubElement(method, "arg")
        constructor_placer = etree.SubElement(arg_placer, "constructor", ref="SequentialPlacer")

        returns = etree.SubElement(resolve, "returns")
        symbol = etree.SubElement(returns, "symbol", ref="my_program_resolved")

    def _transform_functions(self, parent: etree.Element):
        """Transform Function definitions."""
        for func in self.root.findall("Function"):
            self._transform_function(func, parent)

    def _transform_function(self, simple_func: etree.Element, parent: etree.Element):
        """Transform individual Function."""
        name = simple_func.get("name")
        decorator = simple_func.get("decorator")
        entry = simple_func.get("entry")

        # Use entry name if provided, otherwise use name
        expanded_name = entry if entry else name

        func = etree.SubElement(parent, "Function", name=expanded_name)
        if decorator:
            func.set("decorator", decorator)

        # Parameters
        params = simple_func.find("parameters")
        if params is not None:
            params_section = etree.SubElement(func, "parameters")
            for param in params.findall("param"):
                param_elem = etree.SubElement(params_section, "param", name=param.get("name"))
                param_type = param.get("type")
                if param_type:
                    param_elem.set("type_ref", param_type)

        # Attributes (for JIT functions)
        if decorator and "jit" in decorator:
            attributes = etree.SubElement(func, "attributes")
            kwarg = etree.SubElement(attributes, "kwarg", name="is_placed", value="False")

        # Body
        body = simple_func.find("body")
        if body is not None:
            body_section = etree.SubElement(func, "body")

            for stmt in body:
                if stmt.tag == "UseDataFlow":
                    use_df = etree.SubElement(body_section, "UseDataFlow")
                    # Need to collect actual type names from Symbols section
                    symbols_section = self.root.find("Symbols")
                    if symbols_section is not None:
                        for type_abs in symbols_section.findall("TypeAbstraction"):
                            type_name = type_abs.get("name")
                            use_type = etree.SubElement(use_df, "UseType", name=type_name)

                elif stmt.tag == "Return":
                    return_elem = etree.SubElement(body_section, "Return")
                    var = etree.SubElement(return_elem, "var", ref=stmt.text.strip())

                elif stmt.tag == "Assign":
                    self._transform_assign(stmt, body_section)

                elif stmt.tag == "Tensor":
                    self._transform_tensor_assign(stmt, body_section)

                elif stmt.tag == "Call":
                    self._transform_call(stmt, body_section)

    def _transform_assign(self, simple_assign: etree.Element, parent: etree.Element):
        """Transform Assign statement."""
        assign = etree.SubElement(parent, "Assign")
        target = etree.SubElement(assign, "target")
        target.text = simple_assign.get("name")
        source = etree.SubElement(assign, "source")

        value = simple_assign.get("value")
        if value:
            if value.isdigit():
                const = etree.SubElement(source, "const")
                const.text = value
            else:
                var = etree.SubElement(source, "var", ref=value)

    def _transform_tensor_assign(self, simple_tensor: etree.Element, parent: etree.Element):
        """Transform Tensor initialization."""
        assign = etree.SubElement(parent, "Assign")
        target = etree.SubElement(assign, "target")
        target.text = simple_tensor.get("name")
        source = etree.SubElement(assign, "source")

        init = simple_tensor.find("init")
        if init is not None:
            self._parse_function_call(init.text.strip(), source)

    def _transform_call(self, simple_call: etree.Element, parent: etree.Element):
        """Transform Call statement."""
        call = etree.SubElement(parent, "Call")
        func_name = simple_call.get("function")

        # Resolve function name to entry name if it has one
        resolved_name = self.function_entry_names.get(func_name, func_name)

        func_elem = etree.SubElement(call, "function", ref=resolved_name)

        args_text = simple_call.get("args", "")
        for arg_name in args_text.split(","):
            arg_name = arg_name.strip()
            if arg_name:
                arg = etree.SubElement(func_elem, "arg")
                var = etree.SubElement(arg, "var")
                var.text = arg_name

    def _parse_function_call(self, call_text: str, parent: etree.Element):
        """Parse function call text and create XML structure."""
        # Match function_name(args)
        match = re.match(r'([a-zA-Z_.]+)\((.*)\)', call_text)
        if match:
            func_name = match.group(1)
            args_text = match.group(2)

            call = etree.SubElement(parent, "call")
            func = etree.SubElement(call, "function", ref=func_name)

            # Parse arguments
            if args_text:
                for arg in args_text.split(","):
                    arg = arg.strip()
                    if "=" in arg:
                        # Keyword argument
                        key, value = arg.split("=", 1)
                        key = key.strip()
                        value = value.strip()
                        kwarg = etree.SubElement(func, "kwarg", name=key)
                        if value.startswith('"') or value.startswith("'"):
                            string = etree.SubElement(kwarg, "string")
                            string.text = value.strip('"').strip("'")
                        else:
                            var = etree.SubElement(kwarg, "var")
                            var.text = value
                    else:
                        # Positional argument
                        arg_elem = etree.SubElement(func, "arg")
                        var = etree.SubElement(arg_elem, "var")
                        var.text = arg

    def _transform_entrypoint(self, parent: etree.Element):
        """Transform EntryPoint."""
        simple_ep = self.root.find("EntryPoint")
        if simple_ep is None:
            return

        ep = etree.SubElement(parent, "EntryPoint")

        if_stmt = simple_ep.find("If")
        if if_stmt is not None:
            if_elem = etree.SubElement(ep, "If", condition=if_stmt.get("condition"))
            call = if_stmt.find("Call")
            if call is not None:
                call_elem = etree.SubElement(if_elem, "Call")
                func_name = call.get("function")
                # Resolve function name to entry name if it has one
                resolved_name = self.function_entry_names.get(func_name, func_name)
                func = etree.SubElement(call_elem, "function", ref=resolved_name)

    def save(self, output_path: Path):
        """Save complete XML to file."""
        complete_root = self.transform()
        tree = etree.ElementTree(complete_root)
        tree.write(str(output_path),
                  pretty_print=True,
                  xml_declaration=True,
                  encoding='UTF-8')


def main():
    """CLI entry point for XMLGenerator."""
    import sys

    if len(sys.argv) < 2:
        print("Usage: python XMLGenerator.py <simple_xml_path> [output_xml_path]")
        sys.exit(1)

    simple_xml_path = Path(sys.argv[1])
    output_xml_path = Path(sys.argv[2]) if len(sys.argv) > 2 else simple_xml_path.parent / "generated_complete.xml"

    print(f"Transforming {simple_xml_path} to {output_xml_path}...")

    transformer = XMLTransformer(simple_xml_path)
    transformer.save(output_xml_path)

    print(f"Complete XML generated at: {output_xml_path}")


if __name__ == "__main__":
    main()
