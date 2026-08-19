"""
XML serializer for AIECAD HLIR.

Converts HLIR Program objects to complete XML format that can be
processed by the existing GraphDriver and CodeGenerator.
"""

from typing import Union, List, Any, Optional
from xml.etree.ElementTree import Element, SubElement, tostring, ElementTree
from xml.dom import minidom
from .core import (
    Program, Tile, ObjectFifo, ExternalKernel, CoreFunction,
    Worker, RuntimeSequence, Symbol, FifoBinding, FifoAccessMode,
    RuntimeFill, RuntimeDrain
)
from .operations import (
    SplitOperation, JoinOperation, ForwardOperation,
    TensorAccessPattern
)
from .types import TensorType, DataType, ScalarType


class XMLSerializer:
    """
    Serializes HLIR Program to complete XML format.

    Example:
        serializer = XMLSerializer()
        xml_str = serializer.serialize(program)
    """

    def __init__(self, pretty_print: bool = True):
        """
        Initialize serializer.

        Args:
            pretty_print: Whether to format output with indentation
        """
        self.pretty_print = pretty_print

    def serialize(self, program: Program) -> str:
        """
        Serialize a Program to XML string.

        Args:
            program: Program to serialize

        Returns:
            XML string
        """
        root = self._build_xml(program)

        if self.pretty_print:
            return self._prettify(root)
        else:
            return tostring(root, encoding='unicode')

    def serialize_to_file(self, program: Program, filepath: str):
        """
        Serialize a Program to XML file.

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
        return reparsed.toprettyxml(indent="  ")

    def _build_xml(self, program: Program) -> Element:
        """Build XML tree from Program."""
        root = Element('Module')
        root.set('name', program.name)

        # Add Symbols section
        if program.symbols or program.fifos or program.external_kernels or program.core_functions:
            symbols_elem = SubElement(root, 'Symbols')
            self._add_symbols(symbols_elem, program)

        # Add DataFlow section
        if program.fifos or program.workers or program.runtime:
            dataflow_elem = SubElement(root, 'DataFlow')
            self._add_dataflow(dataflow_elem, program)

        return root

    def _add_symbols(self, parent: Element, program: Program):
        """Add symbols section."""
        for symbol in program.symbols.values():
            if isinstance(symbol.value, TensorType):
                self._add_type_abstraction(parent, symbol.name, symbol.value)
            elif symbol.is_constant:
                self._add_constant(parent, symbol.name, symbol.value)
            elif isinstance(symbol.value, (SplitOperation, JoinOperation, ForwardOperation)):
                # These are handled in dataflow section
                pass
            else:
                # Generic symbol
                self._add_symbol(parent, symbol.name, symbol.value)

    def _add_constant(self, parent: Element, name: str, value: Any):
        """Add a constant declaration."""
        const_elem = SubElement(parent, 'Const')
        const_elem.set('name', name)
        const_elem.set('type', type(value).__name__)
        const_elem.text = str(value)

    def _add_symbol(self, parent: Element, name: str, value: Any):
        """Add a generic symbol."""
        sym_elem = SubElement(parent, 'Symbol')
        sym_elem.set('name', name)
        sym_elem.text = str(value)

    def _add_type_abstraction(self, parent: Element, name: str, tensor_type: TensorType):
        """Add a TensorType as TypeAbstraction."""
        type_elem = SubElement(parent, 'TypeAbstraction')
        type_elem.set('name', name)

        ndarray_elem = SubElement(type_elem, 'ndarray')

        # Shape
        shape_elem = SubElement(ndarray_elem, 'shape')
        tuple_elem = SubElement(shape_elem, 'tuple')
        for dim in tensor_type.shape:
            expr_elem = SubElement(tuple_elem, 'expr')
            self._add_expression(expr_elem, dim)

        # Dtype
        dtype_elem = SubElement(ndarray_elem, 'dtype')
        dtype_child = SubElement(dtype_elem, 'numpy_dtype')
        dtype_child.text = str(tensor_type.dtype.value)

    def _add_expression(self, parent: Element, expr: Union[int, str, Any]):
        """Add an expression element (can be int, string, or expression tree)."""
        if isinstance(expr, int):
            const_elem = SubElement(parent, 'const')
            const_elem.text = str(expr)
        elif isinstance(expr, str):
            # Parse simple expressions like "N", "N // 4", etc.
            if '//' in expr:
                # Binary operation
                self._add_binary_op(parent, expr, '//')
            elif '/' in expr:
                self._add_binary_op(parent, expr, '/')
            elif '*' in expr:
                self._add_binary_op(parent, expr, '*')
            elif '+' in expr:
                self._add_binary_op(parent, expr, '+')
            elif '-' in expr and expr.count('-') == 1 and '-' in expr[1:]:
                self._add_binary_op(parent, expr, '-')
            else:
                # Simple variable reference
                if '.' in expr and 'numel()' in expr:
                    # Method call like "inputA.numel()"
                    var_name = expr.split('.')[0]
                    mc_elem = SubElement(parent, 'method_chain')
                    base_elem = SubElement(mc_elem, 'base')
                    var_elem = SubElement(base_elem, 'var')
                    var_elem.set('ref', var_name)
                    call_elem = SubElement(mc_elem, 'call')
                    method_elem = SubElement(call_elem, 'method')
                    method_elem.set('name', 'numel')
                else:
                    var_elem = SubElement(parent, 'var')
                    var_elem.set('ref', expr.strip())
        else:
            # Unknown expression type
            const_elem = SubElement(parent, 'const')
            const_elem.text = str(expr)

    def _add_binary_op(self, parent: Element, expr_str: str, op: str):
        """Add a binary operation element."""
        # Simple parser for expressions like "N // 4" or "(N // 4) * 0"
        parts = expr_str.split(op)
        if len(parts) != 2:
            # Fallback to const
            const_elem = SubElement(parent, 'const')
            const_elem.text = expr_str
            return

        binop_elem = SubElement(parent, 'binary_op')
        binop_elem.set('op', op)

        lhs_elem = SubElement(binop_elem, 'lhs')
        self._add_expression(lhs_elem, parts[0].strip().strip('()'))

        rhs_elem = SubElement(binop_elem, 'rhs')
        self._add_expression(rhs_elem, parts[1].strip().strip('()'))

    def _add_dataflow(self, parent: Element, program: Program):
        """Add DataFlow section."""
        # Add external kernels
        for kernel in program.external_kernels.values():
            self._add_external_kernel(parent, kernel)

        # Add core functions
        for func in program.core_functions.values():
            self._add_core_function(parent, func)

        # Add ObjectFifos
        for fifo in program.fifos.values():
            self._add_object_fifo(parent, fifo)

        # Add split/join/forward operations from symbols
        for symbol in program.symbols.values():
            if isinstance(symbol.value, SplitOperation):
                self._add_split_operation(parent, symbol.value)
            elif isinstance(symbol.value, JoinOperation):
                self._add_join_operation(parent, symbol.value)
            elif isinstance(symbol.value, ForwardOperation):
                self._add_forward_operation(parent, symbol.value)

        # Add workers
        for worker in program.workers.values():
            self._add_worker(parent, worker)

        # Add runtime
        if program.runtime:
            self._add_runtime(parent, program.runtime)

    def _add_external_kernel(self, parent: Element, kernel: ExternalKernel):
        """Add ExternalFunction declaration."""
        ext_elem = SubElement(parent, 'external_function')
        ext_elem.set('name', kernel.name)

        kwargs_elem = SubElement(ext_elem, 'kwargs')

        # name kwarg
        name_kwarg = SubElement(kwargs_elem, 'kwarg')
        name_kwarg.set('name', 'name')
        string_elem = SubElement(name_kwarg, 'string')
        string_elem.text = f'"{kernel.kernel_name}"'

        # source_file kwarg
        source_kwarg = SubElement(kwargs_elem, 'kwarg')
        source_kwarg.set('name', 'source_file')
        string_elem = SubElement(source_kwarg, 'string')
        string_elem.text = f'"{kernel.source_file}"'

        # arg_types kwarg
        types_kwarg = SubElement(kwargs_elem, 'kwarg')
        types_kwarg.set('name', 'arg_types')
        list_elem = SubElement(types_kwarg, 'list')
        for arg_type in kernel.arg_types:
            var_elem = SubElement(list_elem, 'var')
            var_elem.set('ref', str(arg_type))

        # include_dirs kwarg (if present)
        if kernel.include_dirs:
            inc_kwarg = SubElement(kwargs_elem, 'kwarg')
            inc_kwarg.set('name', 'include_dirs')
            list_elem = SubElement(inc_kwarg, 'list')
            for inc_dir in kernel.include_dirs:
                string_elem = SubElement(list_elem, 'string')
                string_elem.text = f'"{inc_dir}"'

    def _add_core_function(self, parent: Element, func: CoreFunction):
        """Add CoreFunction definition."""
        cf_elem = SubElement(parent, 'CoreFunction')
        cf_elem.set('name', func.name)

        # Parameters
        params_elem = SubElement(cf_elem, 'parameters')
        for param in func.parameters:
            param_elem = SubElement(params_elem, 'param')
            param_elem.set('name', param)

        # Body
        body_elem = SubElement(cf_elem, 'body')

        # Acquires
        for acq in func.acquires:
            acq_elem = SubElement(body_elem, 'Acquire')
            acq_elem.set('name', acq.local_var)
            call_elem = SubElement(acq_elem, 'call')
            method_elem = SubElement(call_elem, 'method')
            method_elem.set('ref', acq.fifo_param)
            method_elem.set('name', 'acquire')
            arg_elem = SubElement(method_elem, 'arg')
            const_elem = SubElement(arg_elem, 'const')
            const_elem.text = str(acq.count)

        # Kernel call
        if func.kernel_call:
            call_elem = SubElement(body_elem, 'Call')
            func_elem = SubElement(call_elem, 'function')
            func_elem.set('ref', func.kernel_call.kernel_param)
            for arg in func.kernel_call.args:
                arg_elem = SubElement(func_elem, 'arg')
                var_elem = SubElement(arg_elem, 'var')
                var_elem.set('ref', arg)

        # Releases
        for rel in func.releases:
            rel_elem = SubElement(body_elem, 'Release')
            call_elem = SubElement(rel_elem, 'call')
            method_elem = SubElement(call_elem, 'method')
            method_elem.set('ref', rel.fifo_param)
            method_elem.set('name', 'release')
            arg_elem = SubElement(method_elem, 'arg')
            const_elem = SubElement(arg_elem, 'const')
            const_elem.text = str(rel.count)

    def _add_object_fifo(self, parent: Element, fifo: ObjectFifo):
        """Add ObjectFifo declaration."""
        fifo_elem = SubElement(parent, 'object_fifo')
        fifo_elem.set('name', fifo.name)

        # Type
        type_elem = SubElement(fifo_elem, 'type')
        var_elem = SubElement(type_elem, 'var')
        var_elem.set('ref', str(fifo.obj_type))

        # Kwargs
        kwargs_elem = SubElement(fifo_elem, 'kwargs')

        # depth kwarg
        depth_kwarg = SubElement(kwargs_elem, 'kwarg')
        depth_kwarg.set('name', 'depth')
        const_elem = SubElement(depth_kwarg, 'const')
        const_elem.text = str(fifo.depth)

        # name kwarg
        name_kwarg = SubElement(kwargs_elem, 'kwarg')
        name_kwarg.set('name', 'name')
        string_elem = SubElement(name_kwarg, 'string')
        string_elem.text = f'"{fifo.name}"'

    def _add_split_operation(self, parent: Element, split_op: SplitOperation):
        """Add split operation as object_fifo with method_chain."""
        fifo_elem = SubElement(parent, 'object_fifo')
        fifo_elem.set('name', split_op.name)

        mc_elem = SubElement(fifo_elem, 'method_chain')

        # Base
        base_elem = SubElement(mc_elem, 'base')
        var_elem = SubElement(base_elem, 'var')
        source_name = split_op.source if isinstance(split_op.source, str) else split_op.source.name
        var_elem.set('ref', source_name)

        # cons() call
        cons_call = SubElement(mc_elem, 'call')
        cons_method = SubElement(cons_call, 'method')
        cons_method.set('name', 'cons')

        # split() call
        split_call = SubElement(mc_elem, 'call')
        split_method = SubElement(split_call, 'method')
        split_method.set('name', 'split')

        kwargs_elem = SubElement(split_method, 'kwargs')

        # obj_types kwarg
        types_kwarg = SubElement(kwargs_elem, 'kwarg')
        types_kwarg.set('name', 'obj_types')
        list_elem = SubElement(types_kwarg, 'list')
        for _ in range(split_op.num_outputs):
            var_elem = SubElement(list_elem, 'var')
            var_elem.set('ref', str(split_op.output_type))

        # offsets kwarg
        offsets_kwarg = SubElement(kwargs_elem, 'kwarg')
        offsets_kwarg.set('name', 'offsets')
        list_elem = SubElement(offsets_kwarg, 'list')
        for offset in split_op.offsets:
            self._add_expression(list_elem, offset)

        # names kwarg
        names_kwarg = SubElement(kwargs_elem, 'kwarg')
        names_kwarg.set('name', 'names')
        list_elem = SubElement(names_kwarg, 'list')
        for name in split_op.output_names:
            string_elem = SubElement(list_elem, 'string')
            string_elem.text = f'"{name}"'

        # tile kwarg
        place_kwarg = SubElement(kwargs_elem, 'kwarg')
        place_kwarg.set('name', 'tile')
        const_elem = SubElement(place_kwarg, 'constructor')
        const_elem.set('name', 'Tile')
        args_elem = SubElement(const_elem, 'args')
        x_elem = SubElement(args_elem, 'const')
        x_elem.text = str(split_op.placement.x)
        y_elem = SubElement(args_elem, 'const')
        y_elem.text = str(split_op.placement.y)

    def _add_join_operation(self, parent: Element, join_op: JoinOperation):
        """Add join operation as object_fifo with method_chain."""
        fifo_elem = SubElement(parent, 'object_fifo')
        fifo_elem.set('name', join_op.name)

        mc_elem = SubElement(fifo_elem, 'method_chain')

        # Base
        base_elem = SubElement(mc_elem, 'base')
        var_elem = SubElement(base_elem, 'var')
        dest_name = join_op.dest if isinstance(join_op.dest, str) else join_op.dest.name
        var_elem.set('ref', dest_name)

        # prod() call
        prod_call = SubElement(mc_elem, 'call')
        prod_method = SubElement(prod_call, 'method')
        prod_method.set('name', 'prod')

        # join() call
        join_call = SubElement(mc_elem, 'call')
        join_method = SubElement(join_call, 'method')
        join_method.set('name', 'join')

        kwargs_elem = SubElement(join_method, 'kwargs')

        # obj_types kwarg
        types_kwarg = SubElement(kwargs_elem, 'kwarg')
        types_kwarg.set('name', 'obj_types')
        list_elem = SubElement(types_kwarg, 'list')
        for _ in range(join_op.num_inputs):
            var_elem = SubElement(list_elem, 'var')
            var_elem.set('ref', str(join_op.input_type))

        # names kwarg
        names_kwarg = SubElement(kwargs_elem, 'kwarg')
        names_kwarg.set('name', 'names')
        list_elem = SubElement(names_kwarg, 'list')
        for name in join_op.input_names:
            string_elem = SubElement(list_elem, 'string')
            string_elem.text = f'"{name}"'

        # tile kwarg
        place_kwarg = SubElement(kwargs_elem, 'kwarg')
        place_kwarg.set('name', 'tile')
        const_elem = SubElement(place_kwarg, 'constructor')
        const_elem.set('name', 'Tile')
        args_elem = SubElement(const_elem, 'args')
        x_elem = SubElement(args_elem, 'const')
        x_elem.text = str(join_op.placement.x)
        y_elem = SubElement(args_elem, 'const')
        y_elem.text = str(join_op.placement.y)

        # offsets kwarg
        offsets_kwarg = SubElement(kwargs_elem, 'kwarg')
        offsets_kwarg.set('name', 'offsets')
        list_elem = SubElement(offsets_kwarg, 'list')
        for offset in join_op.offsets:
            self._add_expression(list_elem, offset)

    def _add_forward_operation(self, parent: Element, forward_op: ForwardOperation):
        """Add forward operation as object_fifo with method_chain."""
        fifo_elem = SubElement(parent, 'object_fifo')
        fifo_elem.set('name', forward_op.name)

        mc_elem = SubElement(fifo_elem, 'method_chain')

        # Base
        base_elem = SubElement(mc_elem, 'base')
        var_elem = SubElement(base_elem, 'var')
        source_name = forward_op.source if isinstance(forward_op.source, str) else forward_op.source.name
        var_elem.set('ref', source_name)

        # cons() call
        cons_call = SubElement(mc_elem, 'call')
        cons_method = SubElement(cons_call, 'method')
        cons_method.set('name', 'cons')

        # forward() call
        forward_call = SubElement(mc_elem, 'call')
        forward_method = SubElement(forward_call, 'method')
        forward_method.set('name', 'forward')

    def _add_worker(self, parent: Element, worker: Worker):
        """Add Worker declaration."""
        worker_elem = SubElement(parent, 'worker')
        worker_elem.set('name', worker.name)

        kwargs_elem = SubElement(worker_elem, 'kwargs')

        # core_fn kwarg
        cf_kwarg = SubElement(kwargs_elem, 'kwarg')
        cf_kwarg.set('name', 'core_fn')
        var_elem = SubElement(cf_kwarg, 'var')
        cf_name = worker.core_fn if isinstance(worker.core_fn, str) else worker.core_fn.name
        var_elem.set('ref', cf_name)

        # fn_args kwarg
        args_kwarg = SubElement(kwargs_elem, 'kwarg')
        args_kwarg.set('name', 'fn_args')
        list_elem = SubElement(args_kwarg, 'list')

        for arg in worker.fn_args:
            if isinstance(arg, FifoBinding):
                # FIFO binding with cons/prod
                fifo_name = arg.fifo if isinstance(arg.fifo, str) else arg.fifo.name

                # Create method chain
                if arg.index is not None:
                    # Subscript access
                    mc_elem = SubElement(list_elem, 'method_chain')
                    base_elem = SubElement(mc_elem, 'base')
                    sub_elem = SubElement(base_elem, 'subscript')
                    sub_base = SubElement(sub_elem, 'base')
                    var_elem = SubElement(sub_base, 'var')
                    var_elem.set('ref', fifo_name)
                    index_elem = SubElement(sub_elem, 'index')
                    const_elem = SubElement(index_elem, 'const')
                    const_elem.text = str(arg.index)
                    call_elem = SubElement(mc_elem, 'call')
                    method_elem = SubElement(call_elem, 'method')
                    method_elem.set('name', arg.mode.value)
                else:
                    # Direct access
                    mc_elem = SubElement(list_elem, 'method_chain')
                    base_elem = SubElement(mc_elem, 'base')
                    var_elem = SubElement(base_elem, 'var')
                    var_elem.set('ref', fifo_name)
                    call_elem = SubElement(mc_elem, 'call')
                    method_elem = SubElement(call_elem, 'method')
                    method_elem.set('name', arg.mode.value)
            elif isinstance(arg, str):
                # Reference to kernel or other symbol
                var_elem = SubElement(list_elem, 'var')
                var_elem.set('ref', arg)
            else:
                # Unknown type
                var_elem = SubElement(list_elem, 'var')
                var_elem.set('ref', str(arg))

        # placement kwarg
        place_kwarg = SubElement(kwargs_elem, 'kwarg')
        place_kwarg.set('name', 'placement')
        const_elem = SubElement(place_kwarg, 'constructor')
        const_elem.set('name', 'Tile')
        args_elem = SubElement(const_elem, 'args')
        x_elem = SubElement(args_elem, 'const')
        x_elem.text = str(worker.placement.x)
        y_elem = SubElement(args_elem, 'const')
        y_elem.text = str(worker.placement.y)

    def _add_runtime(self, parent: Element, runtime: RuntimeSequence):
        """Add Runtime declaration."""
        rt_elem = SubElement(parent, 'Runtime')
        rt_elem.set('name', runtime.name)

        # Sequence
        seq_elem = SubElement(rt_elem, 'Sequence')

        # Input types
        if runtime.input_types:
            inputs_attr = ', '.join(str(t) for t in runtime.input_types)
            seq_elem.set('inputs', inputs_attr)

        # Parameter bindings
        if runtime.param_names:
            bindings_elem = SubElement(seq_elem, 'bindings')
            bindings_elem.set('as', ', '.join(runtime.param_names))

        # Body
        body_elem = SubElement(seq_elem, 'body')

        # Start workers
        if runtime.workers:
            start_elem = SubElement(body_elem, 'Start')
            workers_elem = SubElement(start_elem, 'workers')
            worker_names = []
            for w in runtime.workers:
                name = w if isinstance(w, str) else w.name
                worker_names.append(name)
            workers_elem.text = ', '.join(worker_names)

        # Operations (fill/drain)
        for op in runtime.operations:
            if isinstance(op, RuntimeFill):
                self._add_fill_operation(body_elem, op)
            elif isinstance(op, RuntimeDrain):
                self._add_drain_operation(body_elem, op)

    def _add_fill_operation(self, parent: Element, fill_op: RuntimeFill):
        """Add fill operation."""
        op_elem = SubElement(parent, 'Operation')
        op_elem.set('name', f"fill_{fill_op.source_param}")
        op_elem.set('type', 'fill')

        kwargs_elem = SubElement(op_elem, 'kwargs')

        # tile kwarg
        place_kwarg = SubElement(kwargs_elem, 'kwarg')
        place_kwarg.set('name', 'tile')
        const_elem = SubElement(place_kwarg, 'constructor')
        const_elem.set('name', 'Tile')
        args_elem = SubElement(const_elem, 'args')
        x_elem = SubElement(args_elem, 'const')
        x_elem.text = str(fill_op.placement.x)
        y_elem = SubElement(args_elem, 'const')
        y_elem.text = str(fill_op.placement.y)

        # in_fifo kwarg
        fifo_kwarg = SubElement(kwargs_elem, 'kwarg')
        fifo_kwarg.set('name', 'in_fifo')
        fifo_name = fill_op.fifo if isinstance(fill_op.fifo, str) else fill_op.fifo.name
        var_elem = SubElement(fifo_kwarg, 'var')
        var_elem.set('ref', fifo_name)
        method_elem = SubElement(fifo_kwarg, 'method')
        method_elem.set('name', 'prod')

        # source kwarg
        source_kwarg = SubElement(kwargs_elem, 'kwarg')
        source_kwarg.set('name', 'source')
        var_elem = SubElement(source_kwarg, 'var')
        var_elem.set('ref', fill_op.source_param)

        # tap kwarg (if present)
        if fill_op.tap:
            self._add_tap_kwarg(kwargs_elem, fill_op.tap)

        # target
        target_elem = SubElement(op_elem, 'target')
        method_elem = SubElement(target_elem, 'method')
        method_elem.set('ref', 'rt')
        method_elem.set('name', 'fill')

    def _add_drain_operation(self, parent: Element, drain_op: RuntimeDrain):
        """Add drain operation."""
        op_elem = SubElement(parent, 'Operation')
        op_elem.set('name', f"drain_{drain_op.dest_param}")
        op_elem.set('type', 'drain')

        kwargs_elem = SubElement(op_elem, 'kwargs')

        # tile kwarg
        place_kwarg = SubElement(kwargs_elem, 'kwarg')
        place_kwarg.set('name', 'tile')
        const_elem = SubElement(place_kwarg, 'constructor')
        const_elem.set('name', 'Tile')
        args_elem = SubElement(const_elem, 'args')
        x_elem = SubElement(args_elem, 'const')
        x_elem.text = str(drain_op.placement.x)
        y_elem = SubElement(args_elem, 'const')
        y_elem.text = str(drain_op.placement.y)

        # out_fifo kwarg
        fifo_kwarg = SubElement(kwargs_elem, 'kwarg')
        fifo_kwarg.set('name', 'out_fifo')
        fifo_name = drain_op.fifo if isinstance(drain_op.fifo, str) else drain_op.fifo.name
        var_elem = SubElement(fifo_kwarg, 'var')
        var_elem.set('ref', fifo_name)
        method_elem = SubElement(fifo_kwarg, 'method')
        method_elem.set('name', 'cons')

        # dest kwarg
        dest_kwarg = SubElement(kwargs_elem, 'kwarg')
        dest_kwarg.set('name', 'dest')
        var_elem = SubElement(dest_kwarg, 'var')
        var_elem.set('ref', drain_op.dest_param)

        # wait kwarg
        wait_kwarg = SubElement(kwargs_elem, 'kwarg')
        wait_kwarg.set('name', 'wait')
        const_elem = SubElement(wait_kwarg, 'const')
        const_elem.text = str(drain_op.wait)

        # tap kwarg (if present)
        if drain_op.tap:
            self._add_tap_kwarg(kwargs_elem, drain_op.tap)

        # target
        target_elem = SubElement(op_elem, 'target')
        method_elem = SubElement(target_elem, 'method')
        method_elem.set('ref', 'rt')
        method_elem.set('name', 'drain')

    def _add_tap_kwarg(self, parent: Element, tap: TensorAccessPattern):
        """Add TensorAccessPattern kwarg."""
        tap_kwarg = SubElement(parent, 'kwarg')
        tap_kwarg.set('name', 'tap')

        const_elem = SubElement(tap_kwarg, 'constructor')
        const_elem.set('name', 'TensorAccessPattern')

        kwargs_elem = SubElement(const_elem, 'kwargs')

        # tensor_dims kwarg
        dims_kwarg = SubElement(kwargs_elem, 'kwarg')
        dims_kwarg.set('name', 'tensor_dims')
        list_elem = SubElement(dims_kwarg, 'list')
        for dim in tap.tensor_dims:
            self._add_expression(list_elem, dim)

        # offset kwarg
        offset_kwarg = SubElement(kwargs_elem, 'kwarg')
        offset_kwarg.set('name', 'offset')
        self._add_expression(offset_kwarg, tap.offset)

        # sizes kwarg
        sizes_kwarg = SubElement(kwargs_elem, 'kwarg')
        sizes_kwarg.set('name', 'sizes')
        list_elem = SubElement(sizes_kwarg, 'list')
        for size in tap.sizes:
            self._add_expression(list_elem, size)

        # strides kwarg
        strides_kwarg = SubElement(kwargs_elem, 'kwarg')
        strides_kwarg.set('name', 'strides')
        list_elem = SubElement(strides_kwarg, 'list')
        for stride in tap.strides:
            self._add_expression(list_elem, stride)
