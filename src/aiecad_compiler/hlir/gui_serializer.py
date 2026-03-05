"""
GUI XML serializer for AIECAD HLIR.

Converts HLIR Program objects to GUI XML format (simplified format)
which is then processed by XMLGenerator to create complete XML.

This is the correct serialization path:
  HLIR → GUIXMLSerializer → GUI XML → XMLGenerator → Complete XML → CodeGenerator
"""

from typing import Union, List, Any, Optional
from xml.etree.ElementTree import Element, SubElement, tostring
from xml.dom import minidom
from .core import (
    Program, Tile, ObjectFifo, ExternalKernel, CoreFunction,
    Worker, RuntimeSequence, Symbol, FifoBinding, FifoAccessMode,
    RuntimeFill, RuntimeDrain
)
from .operations import (
    SplitOperation, JoinOperation, ForwardOperation,
    TensorAccessPattern, TensorTiler2DSpec
)
from .core import Assignment, ForLoop, Acquire, Release, KernelCall
from .types import TensorType, DataType


class GUIXMLSerializer:
    """
    Serializes HLIR Program to GUI XML format.

    GUI XML is the simplified format designed for visual editors.
    It uses simplified syntax that is then expanded by XMLGenerator.

    Example:
        serializer = GUIXMLSerializer()
        xml_str = serializer.serialize(program)
    """

    def __init__(self, pretty_print: bool = True):
        """
        Initialize serializer.

        Args:
            pretty_print: Whether to format output with indentation
        """
        self.pretty_print = pretty_print

    def _get_type_name(self, type_ref: Any) -> str:
        """
        Extract the type name from various type representations.

        Handles Symbol objects, TensorType objects, and strings.

        Args:
            type_ref: Type reference (Symbol, TensorType, str, etc.)

        Returns:
            String name of the type
        """
        if isinstance(type_ref, str):
            return type_ref
        elif isinstance(type_ref, Symbol):
            # Symbol wraps a type - return the symbol's name
            return type_ref.name
        elif isinstance(type_ref, TensorType):
            # TensorType directly - this shouldn't happen normally
            # as types are usually wrapped in Symbols
            return str(type_ref)
        elif hasattr(type_ref, 'name'):
            return type_ref.name
        else:
            return str(type_ref)

    def _get_kernel_name(self, kernel_ref: Any) -> str:
        """
        Extract the kernel name from various kernel representations.

        Handles ExternalKernel objects and strings.

        Args:
            kernel_ref: Kernel reference (ExternalKernel, str, etc.)

        Returns:
            String name of the kernel
        """
        if isinstance(kernel_ref, str):
            return kernel_ref
        elif isinstance(kernel_ref, ExternalKernel):
            return kernel_ref.name
        elif hasattr(kernel_ref, 'name'):
            return kernel_ref.name
        else:
            return str(kernel_ref)

    def serialize(self, program: Program) -> str:
        """
        Serialize a Program to GUI XML string.

        Args:
            program: Program to serialize

        Returns:
            GUI XML string
        """
        root = self._build_xml(program)

        if self.pretty_print:
            return self._prettify(root)
        else:
            return tostring(root, encoding='unicode')

    def serialize_to_file(self, program: Program, filepath: str):
        """
        Serialize a Program to GUI XML file.

        Args:
            program: Program to serialize
            filepath: Output file path
        """
        xml_str = self.serialize(program)
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(xml_str)

    def _prettify(self, elem: Element) -> str:
        """Format XML with indentation."""
        rough_string = tostring(elem, encoding='unicode')
        reparsed = minidom.parseString(rough_string)
        # Get pretty XML and remove extra blank lines
        pretty = reparsed.toprettyxml(indent="    ")
        # Remove blank lines
        lines = [line for line in pretty.split('\n') if line.strip()]
        return '\n'.join(lines)

    def _build_xml(self, program: Program) -> Element:
        """Build GUI XML tree from Program."""
        root = Element('Module')
        root.set('name', program.name)
        root.text = '\n'
        root.tail = '\n'

        # Add Symbols section
        if program.symbols or program.fifos:
            symbols_elem = SubElement(root, 'Symbols')
            symbols_elem.text = '\n'
            symbols_elem.tail = '\n'
            self._add_symbols(symbols_elem, program)

        # Add DataFlow section
        if program.fifos or program.workers or program.runtime:
            dataflow_elem = SubElement(root, 'DataFlow')
            dataflow_elem.text = '\n'
            dataflow_elem.tail = '\n'
            self._add_dataflow(dataflow_elem, program)

        # Add Functions section (if we have runtime)
        if program.runtime:
            self._add_functions(root, program)

        # Add EntryPoint section (if we have runtime)
        if program.runtime:
            self._add_entrypoint(root, program)

        return root

    def _add_symbols(self, parent: Element, program: Program):
        """Add symbols section in GUI XML format."""
        # Add constants first
        for symbol in program.symbols.values():
            if symbol.is_constant and not isinstance(symbol.value, TensorType):
                self._add_gui_const(parent, symbol)

        # Add type definitions
        for symbol in program.symbols.values():
            if isinstance(symbol.value, TensorType):
                self._add_gui_type_abstraction(parent, symbol.name, symbol.value)

        # Add TensorTiler2D specs (after types, before runtime)
        for symbol in program.symbols.values():
            if isinstance(symbol.value, TensorTiler2DSpec):
                self._add_gui_tiler2d(parent, symbol.value)

    def _add_gui_const(self, parent: Element, symbol: Symbol):
        """Add constant in GUI XML format."""
        const_elem = SubElement(parent, 'Const')
        const_elem.set('name', symbol.name)
        if symbol.type_hint:
            const_elem.set('type', symbol.type_hint)
        const_elem.text = str(symbol.value)
        const_elem.tail = '\n'

    def _add_gui_type_abstraction(self, parent: Element, name: str, tensor_type: TensorType):
        """Add TensorType in GUI XML format (simplified)."""
        type_elem = SubElement(parent, 'TypeAbstraction')
        type_elem.set('name', name)
        type_elem.text = '\n'
        type_elem.tail = '\n'

        ndarray_elem = SubElement(type_elem, 'ndarray')
        ndarray_elem.text = '\n'
        ndarray_elem.tail = '\n'

        # Shape - simplified format
        shape_elem = SubElement(ndarray_elem, 'shape')
        # Convert shape to simple string expression
        shape_str = self._shape_to_string(tensor_type.shape)
        shape_elem.text = shape_str
        shape_elem.tail = '\n'

        # Dtype - simplified format
        dtype_elem = SubElement(ndarray_elem, 'dtype')
        dtype_elem.text = str(tensor_type.dtype.value)
        dtype_elem.tail = '\n'

    def _shape_to_string(self, shape: tuple) -> str:
        """Convert shape tuple to GUI XML string format."""
        if len(shape) == 1:
            return str(shape[0])
        else:
            # Multi-dimensional: return as comma-separated
            return ', '.join(str(d) for d in shape)

    def _add_dataflow(self, parent: Element, program: Program):
        """Add DataFlow section in GUI XML format."""
        # Add External Kernels
        for kernel in program.external_kernels.values():
            self._add_gui_external_kernel(parent, kernel)

        # Add Core Functions
        for func in program.core_functions.values():
            self._add_gui_core_function(parent, func)

        # Add ObjectFifos
        for fifo in program.fifos.values():
            self._add_gui_object_fifo(parent, fifo)

        # Add split/join/forward operations from symbols
        for symbol in program.symbols.values():
            if isinstance(symbol.value, ForwardOperation):
                self._add_gui_forward_operation(parent, symbol.value)
            elif isinstance(symbol.value, SplitOperation):
                self._add_gui_split_operation(parent, symbol.value)
            elif isinstance(symbol.value, JoinOperation):
                self._add_gui_join_operation(parent, symbol.value)

        # Add Workers
        for worker in program.workers.values():
            self._add_gui_worker(parent, worker)

        # Add Runtime
        if program.runtime:
            self._add_gui_runtime(parent, program.runtime)

        # Add Program definition
        self._add_gui_program(parent, program)

    def _add_gui_external_kernel(self, parent: Element, kernel: ExternalKernel):
        """Add ExternalFunction in GUI XML format."""
        ext_elem = SubElement(parent, 'ExternalFunction')
        ext_elem.set('name', kernel.name)

        # Add metadata as attributes
        for key, value in kernel.metadata.items():
            ext_elem.set(key, str(value))

        ext_elem.text = '\n'
        ext_elem.tail = '\n'

        # Kernel name
        kernel_elem = SubElement(ext_elem, 'kernel')
        kernel_elem.text = kernel.kernel_name
        kernel_elem.tail = '\n'

        # Source file
        source_elem = SubElement(ext_elem, 'source')
        source_elem.text = kernel.source_file
        source_elem.tail = '\n'

        # Arg types
        arg_types_elem = SubElement(ext_elem, 'arg_types')
        arg_types_elem.text = '\n'
        arg_types_elem.tail = '\n'

        for arg_type in kernel.arg_types:
            type_elem = SubElement(arg_types_elem, 'type')
            type_elem.text = self._get_type_name(arg_type)
            type_elem.tail = '\n'

        # Include dirs (if present)
        if kernel.include_dirs:
            inc_dirs_elem = SubElement(ext_elem, 'include_dirs')
            inc_dirs_elem.text = '\n'
            inc_dirs_elem.tail = '\n'
            for inc_dir in kernel.include_dirs:
                dir_elem = SubElement(inc_dirs_elem, 'dir')
                dir_elem.text = inc_dir
                dir_elem.tail = '\n'

    def _add_gui_core_function(self, parent: Element, func: CoreFunction):
        """Add CoreFunction in GUI XML format."""
        func_elem = SubElement(parent, 'CoreFunction')
        func_elem.set('name', func.name)

        # Add metadata as attributes
        for key, value in func.metadata.items():
            func_elem.set(key, str(value))

        func_elem.text = '\n'
        func_elem.tail = '\n'

        # Parameters
        params_elem = SubElement(func_elem, 'parameters')
        params_elem.text = '\n'
        params_elem.tail = '\n'

        # Infer roles from position: first is kernel, rest based on acquires/releases
        for i, param_name in enumerate(func.parameters):
            param_elem = SubElement(params_elem, 'param')
            param_elem.set('name', param_name)

            # Determine role
            if i == 0:
                role = "external_function"
            else:
                # Check if this param is in acquires (consumer) or only in releases (producer)
                is_acquire = any(acq.fifo_param == param_name for acq in func.acquires)
                is_release = any(rel.fifo_param == param_name for rel in func.releases)

                if is_acquire and not is_release:
                    role = "consumer"
                elif is_release and not is_acquire:
                    role = "producer"
                elif is_acquire and is_release:
                    # Both acquire and release - check kernel call to determine
                    # If it's an output parameter, it's producer, otherwise consumer
                    if func.kernel_call and param_name in func.kernel_call.args[2:]:
                        role = "producer"
                    else:
                        role = "consumer"
                else:
                    role = "unknown"

            param_elem.set('role', role)
            param_elem.tail = '\n'

        # Body
        body_elem = SubElement(func_elem, 'body')
        body_elem.text = '\n'
        body_elem.tail = '\n'

        if func.body_stmts is not None:
            # Body mode: serialize explicit statement list
            self._add_gui_body_stmts(body_elem, func.body_stmts)
        else:
            # Flat mode: serialize acquires → call → releases
            for acquire in func.acquires:
                acq_elem = SubElement(body_elem, 'Acquire')
                acq_elem.set('source', acquire.fifo_param)
                acq_elem.set('count', str(acquire.count))
                acq_elem.set('name', acquire.local_var)
                acq_elem.tail = '\n'

            if func.kernel_call:
                call_elem = SubElement(body_elem, 'Call')
                call_elem.set('function', func.kernel_call.kernel_param)
                call_elem.set('args', ', '.join(func.kernel_call.args))
                call_elem.tail = '\n'

            for release in func.releases:
                rel_elem = SubElement(body_elem, 'Release')
                rel_elem.set('source', release.fifo_param)
                rel_elem.set('count', str(release.count))
                rel_elem.tail = '\n'

    def _add_gui_object_fifo(self, parent: Element, fifo: ObjectFifo):
        """Add ObjectFifo in GUI XML format."""
        fifo_elem = SubElement(parent, 'ObjectFifo')
        fifo_elem.set('name', fifo.name)
        fifo_elem.text = '\n'
        fifo_elem.tail = '\n'

        # Type - simple child element
        type_elem = SubElement(fifo_elem, 'type')
        type_elem.text = str(fifo.obj_type)
        type_elem.tail = '\n'

        # Depth - simple child element
        depth_elem = SubElement(fifo_elem, 'depth')
        depth_elem.text = str(fifo.depth)
        depth_elem.tail = '\n'

    def _add_gui_forward_operation(self, parent: Element, forward_op: ForwardOperation):
        """Add forward operation in GUI XML format."""
        forward_elem = SubElement(parent, 'ObjectFifoForward')
        forward_elem.set('name', forward_op.name)
        source_name = forward_op.source if isinstance(forward_op.source, str) else forward_op.source.name
        forward_elem.set('source', source_name)
        if forward_op.placement is not None:
            forward_elem.set('placement', f"Tile({forward_op.placement.x}, {forward_op.placement.y})")
        forward_elem.tail = '\n'

    def _add_gui_split_operation(self, parent: Element, split_op: SplitOperation):
        """Add split operation in GUI XML format."""
        split_elem = SubElement(parent, 'ObjectFifoSplit')
        split_elem.set('name', split_op.name)

        # Add metadata as attributes
        for key, value in split_op.metadata.items():
            split_elem.set(key, str(value))

        split_elem.text = '\n'
        split_elem.tail = '\n'

        # Source as child element
        source_name = split_op.source if isinstance(split_op.source, str) else split_op.source.name
        source_elem = SubElement(split_elem, 'source')
        source_elem.text = source_name
        source_elem.tail = '\n'

        # Number of outputs
        num_elem = SubElement(split_elem, 'num_outputs')
        num_elem.text = str(split_op.num_outputs)
        num_elem.tail = '\n'

        # Output type
        type_elem = SubElement(split_elem, 'output_type')
        type_elem.text = self._get_type_name(split_op.output_type)
        type_elem.tail = '\n'

        # Placement
        place_elem = SubElement(split_elem, 'placement')
        place_elem.text = f"Tile({split_op.placement.x}, {split_op.placement.y})"
        place_elem.tail = '\n'

        # Explicit offsets (if provided and non-trivial)
        if split_op.offsets:
            offsets_elem = SubElement(split_elem, 'offsets')
            offsets_elem.text = ', '.join(str(o) for o in split_op.offsets)
            offsets_elem.tail = '\n'

    def _add_gui_join_operation(self, parent: Element, join_op: JoinOperation):
        """Add join operation in GUI XML format."""
        join_elem = SubElement(parent, 'ObjectFifoJoin')
        join_elem.set('name', join_op.name)

        # Add metadata as attributes
        for key, value in join_op.metadata.items():
            join_elem.set(key, str(value))

        join_elem.text = '\n'
        join_elem.tail = '\n'

        # Dest as child element
        dest_name = join_op.dest if isinstance(join_op.dest, str) else join_op.dest.name
        dest_elem = SubElement(join_elem, 'dest')
        dest_elem.text = dest_name
        dest_elem.tail = '\n'

        # Number of inputs
        num_elem = SubElement(join_elem, 'num_inputs')
        num_elem.text = str(join_op.num_inputs)
        num_elem.tail = '\n'

        # Input type
        type_elem = SubElement(join_elem, 'input_type')
        type_elem.text = self._get_type_name(join_op.input_type)
        type_elem.tail = '\n'

        # Placement
        place_elem = SubElement(join_elem, 'placement')
        place_elem.text = f"Tile({join_op.placement.x}, {join_op.placement.y})"
        place_elem.tail = '\n'

        # Explicit offsets (if provided and non-trivial)
        if join_op.offsets:
            offsets_elem = SubElement(join_elem, 'offsets')
            offsets_elem.text = ', '.join(str(o) for o in join_op.offsets)
            offsets_elem.tail = '\n'

    def _add_gui_worker(self, parent: Element, worker: Worker):
        """Add Worker in GUI XML format."""
        worker_elem = SubElement(parent, 'Worker')
        worker_elem.set('name', worker.name)

        # Add metadata as attributes
        for key, value in worker.metadata.items():
            worker_elem.set(key, str(value))

        worker_elem.text = '\n'
        worker_elem.tail = '\n'

        # Core function reference
        cf_name = worker.core_fn if isinstance(worker.core_fn, str) else worker.core_fn.name
        cf_elem = SubElement(worker_elem, 'core_function')
        cf_elem.text = cf_name
        cf_elem.tail = '\n'

        # Arguments section
        args_elem = SubElement(worker_elem, 'arguments')
        args_elem.text = '\n'
        args_elem.tail = '\n'

        # Add each argument
        for arg in worker.fn_args:
            arg_elem = SubElement(args_elem, 'arg')
            if isinstance(arg, FifoBinding):
                # It's a FIFO binding
                fifo_name = arg.fifo if isinstance(arg.fifo, str) else arg.fifo.name
                arg_elem.set('ref', fifo_name)
                if arg.index is not None:
                    arg_elem.set('index', str(arg.index))
                # Map enum values to full words for GUI XML
                mode_str = 'consumer' if arg.mode.value == 'cons' else 'producer'
                arg_elem.set('mode', mode_str)
            elif isinstance(arg, str):
                # It's a reference (e.g., to external kernel name)
                arg_elem.set('ref', arg)
            elif isinstance(arg, ExternalKernel):
                # It's an external kernel object - extract name
                arg_elem.set('ref', arg.name)
            else:
                # Other types - try to get name attribute
                arg_elem.set('ref', self._get_kernel_name(arg))
            arg_elem.tail = '\n'

        # Placement
        place_elem = SubElement(worker_elem, 'placement')
        place_elem.text = f"Tile({worker.placement.x}, {worker.placement.y})"
        place_elem.tail = '\n'

    def _add_gui_runtime(self, parent: Element, runtime: RuntimeSequence):
        """Add Runtime in GUI XML format."""
        rt_elem = SubElement(parent, 'Runtime')
        rt_elem.set('name', runtime.name)
        rt_elem.text = '\n'
        rt_elem.tail = '\n'

        # Sequence
        seq_elem = SubElement(rt_elem, 'Sequence')

        # Input/output types as comma-separated string
        all_types = runtime.input_types + runtime.output_types
        if all_types:
            types_str = ', '.join(str(t) for t in all_types)
            seq_elem.set('inputs', types_str)

        # Parameter names as comma-separated string
        # Use distinct binding names to avoid shadowing function parameters
        # This allows TAP expressions to still reference the original tensors
        if runtime.param_names:
            # Create binding names that don't shadow function params
            binding_names = []
            for i, name in enumerate(runtime.param_names):
                # Use lowercase with suffix to distinguish from function params
                if i < len(runtime.input_types):
                    binding_names.append(f"{name.lower()}_in")
                else:
                    binding_names.append(f"{name.lower()}_out")
            params_str = ', '.join(binding_names)
            seq_elem.set('as', params_str)

        seq_elem.text = '\n'
        seq_elem.tail = '\n'

        # Start workers (if any)
        if runtime.workers:
            start_elem = SubElement(seq_elem, 'Start')
            worker_names = []
            for w in runtime.workers:
                name = w if isinstance(w, str) else w.name
                worker_names.append(name)
            start_elem.text = ', '.join(worker_names)
            start_elem.tail = '\n'

        # Operations (fill/drain)
        for op in runtime.operations:
            if isinstance(op, RuntimeFill):
                self._add_gui_fill_operation(seq_elem, op)
            elif isinstance(op, RuntimeDrain):
                self._add_gui_drain_operation(seq_elem, op)

    def _add_gui_fill_operation(self, parent: Element, fill_op: RuntimeFill):
        """Add Fill operation in GUI XML format."""
        fill_elem = SubElement(parent, 'Fill')

        # Target FIFO
        fifo_name = fill_op.fifo if isinstance(fill_op.fifo, str) else fill_op.fifo.name
        fill_elem.set('target', fifo_name)

        # Source parameter - use binding name (lowercase with _in suffix)
        # to match the sequence binding names and avoid shadowing
        source_binding = f"{fill_op.source_param.lower()}_in"
        fill_elem.set('source', source_binding)

        # Preserve original data reference for TAP calculations
        # TAP needs to reference the original function parameter (A, B, etc.)
        fill_elem.set('data_ref', fill_op.source_param)

        # Column attribute from metadata
        if fill_op.metadata and 'column' in fill_op.metadata:
            fill_elem.set('column', str(fill_op.metadata['column']))

        # Use TAP - check both metadata and tap field
        use_tap = fill_op.metadata.get('use_tap', fill_op.tap is not None) if fill_op.metadata else (fill_op.tap is not None)
        fill_elem.set('use_tap', "true" if use_tap else "false")

        # Annotate tap type (tiler2d vs standard tap)
        if isinstance(fill_op.tap, TensorTiler2DSpec):
            fill_elem.set('tap_type', 'tiler2d')
            fill_elem.set('tap_var', fill_op.tap.name)
        elif fill_op.tap is not None:
            fill_elem.set('tap_type', 'tap')

        fill_elem.text = '\n'
        fill_elem.tail = '\n'

        # Placement
        place_elem = SubElement(fill_elem, 'placement')
        place_elem.text = f"Tile({fill_op.placement.x}, {fill_op.placement.y})"
        place_elem.tail = '\n'

        # TAP if present and not tiler2d (tiler2d is a variable reference, not inline)
        if fill_op.tap and not isinstance(fill_op.tap, TensorTiler2DSpec):
            self._add_gui_tap(fill_elem, fill_op.tap)

    def _add_gui_drain_operation(self, parent: Element, drain_op: RuntimeDrain):
        """Add Drain operation in GUI XML format."""
        drain_elem = SubElement(parent, 'Drain')

        # Source FIFO
        fifo_name = drain_op.fifo if isinstance(drain_op.fifo, str) else drain_op.fifo.name
        drain_elem.set('source', fifo_name)

        # Target parameter - use binding name (lowercase with _out suffix)
        # to match the sequence binding names and avoid shadowing
        target_binding = f"{drain_op.dest_param.lower()}_out"
        drain_elem.set('target', target_binding)

        # Preserve original data reference for TAP calculations
        # TAP needs to reference the original function parameter (D, etc.)
        drain_elem.set('data_ref', drain_op.dest_param)

        # Column attribute from metadata
        if drain_op.metadata and 'column' in drain_op.metadata:
            drain_elem.set('column', str(drain_op.metadata['column']))

        # Use TAP - check both metadata and tap field
        use_tap = drain_op.metadata.get('use_tap', drain_op.tap is not None) if drain_op.metadata else (drain_op.tap is not None)
        drain_elem.set('use_tap', "true" if use_tap else "false")

        # Annotate tap type (tiler2d vs standard tap)
        if isinstance(drain_op.tap, TensorTiler2DSpec):
            drain_elem.set('tap_type', 'tiler2d')
            drain_elem.set('tap_var', drain_op.tap.name)
        elif drain_op.tap is not None:
            drain_elem.set('tap_type', 'tap')

        drain_elem.text = '\n'
        drain_elem.tail = '\n'

        # Placement
        place_elem = SubElement(drain_elem, 'placement')
        place_elem.text = f"Tile({drain_op.placement.x}, {drain_op.placement.y})"
        place_elem.tail = '\n'

        # Wait
        if drain_op.wait:
            wait_elem = SubElement(drain_elem, 'wait')
            wait_elem.text = "true"
            wait_elem.tail = '\n'

        # TAP if present and not tiler2d (tiler2d is a variable reference, not inline)
        if drain_op.tap and not isinstance(drain_op.tap, TensorTiler2DSpec):
            self._add_gui_tap(drain_elem, drain_op.tap)

    def _add_gui_tap(self, parent: Element, tap: TensorAccessPattern):
        """Add TensorAccessPattern in GUI XML format."""
        tap_elem = SubElement(parent, 'TensorAccessPattern')
        tap_elem.text = '\n'
        tap_elem.tail = '\n'

        # Tensor dims
        dims_elem = SubElement(tap_elem, 'tensor_dims')
        dims_elem.text = ', '.join(str(d) for d in tap.tensor_dims)
        dims_elem.tail = '\n'

        # Offset
        offset_elem = SubElement(tap_elem, 'offset')
        offset_elem.text = str(tap.offset)
        offset_elem.tail = '\n'

        # Sizes
        sizes_elem = SubElement(tap_elem, 'sizes')
        sizes_elem.text = ', '.join(str(s) for s in tap.sizes)
        sizes_elem.tail = '\n'

        # Strides
        strides_elem = SubElement(tap_elem, 'strides')
        strides_elem.text = ', '.join(str(s) for s in tap.strides)
        strides_elem.tail = '\n'

    def _add_gui_tiler2d(self, parent: Element, tiler: TensorTiler2DSpec):
        """Add TensorTiler2DSpec in GUI XML format."""
        tiler_elem = SubElement(parent, 'TensorTiler2D')
        tiler_elem.set('name', tiler.name)
        tiler_elem.set('prune_step', 'true' if tiler.prune_step else 'false')
        tiler_elem.set('index', str(tiler.index))
        if tiler.pattern_repeat is not None:
            tiler_elem.set('pattern_repeat', str(tiler.pattern_repeat))
        tiler_elem.text = '\n'
        tiler_elem.tail = '\n'

        # tensor_dims
        dims_elem = SubElement(tiler_elem, 'tensor_dims')
        dims_elem.text = ', '.join(str(d) for d in tiler.tensor_dims)
        dims_elem.tail = '\n'

        # tile_dims
        tile_dims_elem = SubElement(tiler_elem, 'tile_dims')
        tile_dims_elem.text = ', '.join(str(d) for d in tiler.tile_dims)
        tile_dims_elem.tail = '\n'

        # tile_counts
        tile_counts_elem = SubElement(tiler_elem, 'tile_counts')
        tile_counts_elem.text = ', '.join(str(c) for c in tiler.tile_counts)
        tile_counts_elem.tail = '\n'

    def _add_gui_body_stmts(self, parent: Element, stmts: list):
        """Serialize a list of body statements recursively."""
        for stmt in stmts:
            self._add_gui_body_stmt(parent, stmt)

    def _add_gui_body_stmt(self, parent: Element, stmt):
        """Serialize a single body statement."""
        if isinstance(stmt, Acquire):
            acq_elem = SubElement(parent, 'Acquire')
            acq_elem.set('source', stmt.fifo_param)
            acq_elem.set('count', str(stmt.count))
            acq_elem.set('name', stmt.local_var)
            acq_elem.tail = '\n'

        elif isinstance(stmt, Release):
            rel_elem = SubElement(parent, 'Release')
            rel_elem.set('source', stmt.fifo_param)
            rel_elem.set('count', str(stmt.count))
            rel_elem.tail = '\n'

        elif isinstance(stmt, KernelCall):
            call_elem = SubElement(parent, 'Call')
            call_elem.set('function', stmt.kernel_param)
            call_elem.set('args', ', '.join(stmt.args))
            call_elem.tail = '\n'

        elif isinstance(stmt, ForLoop):
            loop_elem = SubElement(parent, 'ForLoop')
            loop_elem.set('var', stmt.var)
            loop_elem.set('count', str(stmt.count))
            loop_elem.text = '\n'
            loop_elem.tail = '\n'
            self._add_gui_body_stmts(loop_elem, stmt.body)

        elif isinstance(stmt, Assignment):
            assign_elem = SubElement(parent, 'Assignment')
            assign_elem.set('target', stmt.target)
            assign_elem.set('index', str(stmt.index))
            assign_elem.set('value', str(stmt.value))
            assign_elem.tail = '\n'

    def _add_gui_program(self, parent: Element, program: Program):
        """Add Program definition in GUI XML format."""
        prog_elem = SubElement(parent, 'Program')
        prog_elem.set('name', 'program')
        prog_elem.text = '\n'
        prog_elem.tail = '\n'

        # Device
        device_elem = SubElement(prog_elem, 'device')
        device_elem.text = 'current_device'
        device_elem.tail = '\n'

        # Runtime
        if program.runtime:
            rt_elem = SubElement(prog_elem, 'runtime')
            rt_elem.text = program.runtime.name
            rt_elem.tail = '\n'

        # Placer
        placer_elem = SubElement(prog_elem, 'placer')
        placer_elem.text = 'SequentialPlacer'
        placer_elem.tail = '\n'

    def _add_functions(self, parent: Element, program: Program):
        """Add JIT function wrapper."""
        func_elem = SubElement(parent, 'Function')
        func_elem.set('name', 'jit_function')
        func_elem.set('decorator', 'iron.jit')
        func_elem.set('entry', f'{program.name}_jit')
        func_elem.text = '\n'
        func_elem.tail = '\n'

        # Parameters
        params_elem = SubElement(func_elem, 'parameters')
        params_elem.text = '\n'
        params_elem.tail = '\n'

        # Add parameters based on runtime
        if program.runtime and program.runtime.param_names:
            for i, param_name in enumerate(program.runtime.param_names):
                param_elem = SubElement(params_elem, 'param')
                param_elem.set('name', param_name)

                # Determine type
                if i < len(program.runtime.input_types):
                    type_ref = str(program.runtime.input_types[i])
                elif i - len(program.runtime.input_types) < len(program.runtime.output_types):
                    type_ref = str(program.runtime.output_types[i - len(program.runtime.input_types)])
                else:
                    type_ref = 'vector_ty'

                param_elem.set('type', type_ref)
                param_elem.tail = '\n'

        # Body
        body_elem = SubElement(func_elem, 'body')
        body_elem.text = '\n'
        body_elem.tail = '\n'

        use_df_elem = SubElement(body_elem, 'UseDataFlow')
        use_df_elem.tail = '\n'

        ret_elem = SubElement(body_elem, 'Return')
        ret_elem.text = 'program'
        ret_elem.tail = '\n'

    def _add_entrypoint(self, parent: Element, program: Program):
        """Add main function and entry point."""
        # Main function
        main_func = SubElement(parent, 'Function')
        main_func.set('name', 'main_function')
        main_func.set('entry', 'main')
        main_func.text = '\n'
        main_func.tail = '\n'

        body_elem = SubElement(main_func, 'body')
        body_elem.text = '\n'
        body_elem.tail = '\n'

        # Extract tensor size and dtype
        # Use data_ty (full tensor size) if available, otherwise fall back to constant N
        size_expr = None
        dtype_value = None

        # First, look for data_ty which represents the full tensor size
        if "data_ty" in program.symbols:
            data_type = program.symbols["data_ty"].value
            if isinstance(data_type, TensorType):
                if data_type.shape and len(data_type.shape) > 0:
                    size_expr = str(data_type.shape[0])
                if data_type.dtype:
                    dtype_value = str(data_type.dtype.value)

        # If no data_ty, try vector_ty (common in passthrough examples)
        if size_expr is None and "vector_ty" in program.symbols:
            vector_type = program.symbols["vector_ty"].value
            if isinstance(vector_type, TensorType):
                if vector_type.shape and len(vector_type.shape) > 0:
                    size_expr = str(vector_type.shape[0])
                if vector_type.dtype:
                    dtype_value = str(vector_type.dtype.value)

        # If no data_ty or vector_ty, try to find a constant N for size
        if size_expr is None:
            for name, symbol in program.symbols.items():
                if symbol.is_constant and name == "N":
                    size_expr = "N"
                    break

        # Fall back to runtime input type if still no size or dtype found
        if (size_expr is None or dtype_value is None) and program.runtime and len(program.runtime.input_types) > 0:
            first_type_ref = program.runtime.input_types[0]
            if isinstance(first_type_ref, str) and first_type_ref in program.symbols:
                tensor_type = program.symbols[first_type_ref].value
                if isinstance(tensor_type, TensorType):
                    if tensor_type.shape and len(tensor_type.shape) > 0:
                        size_expr = str(tensor_type.shape[0])
                    if dtype_value is None and tensor_type.dtype:
                        dtype_value = str(tensor_type.dtype.value)

        # Add variable assignments for constants used in size expression
        # Find all constant symbols
        constants_to_assign = {}
        for name, symbol in program.symbols.items():
            if symbol.is_constant and not isinstance(symbol.value, TensorType):
                constants_to_assign[name] = symbol.value

        # Don't create a datatype variable - we'll use np.dtype directly in the init expressions

        # Add constant assignments
        for const_name, const_value in constants_to_assign.items():
            assign_elem = SubElement(body_elem, 'Assign')
            assign_elem.set('name', const_name)
            assign_elem.set('value', str(const_value))
            assign_elem.tail = '\n'

        # Add blank line for readability
        body_elem.text = '\n'

        # Add tensor initializations
        if program.runtime and program.runtime.param_names:
            for i, param_name in enumerate(program.runtime.param_names):
                tensor_elem = SubElement(body_elem, 'Tensor')
                tensor_elem.set('name', param_name)
                tensor_elem.text = '\n'
                tensor_elem.tail = '\n'

                init_elem = SubElement(tensor_elem, 'init')
                # Use extracted size and dtype, or defaults
                size_arg = size_expr if size_expr else 'data_size'
                # Use dtype directly - bfloat16 comes from ml_dtypes, not numpy
                # For numpy types like int32, float32, add np. prefix
                if dtype_value:
                    if dtype_value == 'bfloat16':
                        dtype_arg = 'bfloat16'  # From ml_dtypes
                    else:
                        dtype_arg = f'np.{dtype_value}'  # numpy types
                else:
                    dtype_arg = 'bfloat16'

                # Determine if input or output
                if i < len(program.runtime.input_types):
                    init_elem.text = f'iron.arange({size_arg}, dtype={dtype_arg}, device="npu")'
                else:
                    init_elem.text = f'iron.zeros({size_arg}, dtype={dtype_arg}, device="npu")'
                init_elem.tail = '\n'

        # Call JIT function
        call_elem = SubElement(body_elem, 'Call')
        call_elem.set('function', 'jit_function')
        if program.runtime and program.runtime.param_names:
            call_elem.set('args', ', '.join(program.runtime.param_names))
        call_elem.tail = '\n'

        # Entry point
        entry_elem = SubElement(parent, 'EntryPoint')
        entry_elem.text = '\n'
        entry_elem.tail = '\n'

        if_elem = SubElement(entry_elem, 'If')
        if_elem.set('condition', '__name__ == \'__main__\'')
        if_elem.text = '\n'
        if_elem.tail = '\n'

        call_main = SubElement(if_elem, 'Call')
        call_main.set('function', 'main_function')
        call_main.tail = '\n'
