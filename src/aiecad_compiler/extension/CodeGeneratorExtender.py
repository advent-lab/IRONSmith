#!/usr/bin/env python3
"""
CodeGeneratorExtender.py - Extensible code generation for new node types

Add new code generation patterns without modifying CodeGenerator.
Just inherit from CodeGenExtension, implement generate(node_id) -> str
and register the class with register_codegen_extension(kind, cls).

Example:
    Worker nodes will be handled by WorkerCodeGenExtension.
"""

from __future__ import annotations
from typing import Dict, Type, Optional, List
import networkx as nx


# ----------------------------------------------------------------------
# Base extension class
# ----------------------------------------------------------------------
class CodeGenExtension:
    """
    Base class for all code generation extensions.
    
    Sub-classes must:
      * set ``kind`` (node kind to handle)
      * implement ``generate(self, node_id)`` - return generated code string
    """
    
    kind: str = ""  # <-- override in subclass
    
    def __init__(self, generator):
        """
        ``generator`` is the CodeGenerator instance - gives access to
        graph, _emit, _reconstruct_expression, etc.
        """
        self.generator = generator
        self.graph = generator.graph
    
    # Helper shortcuts
    def _get_node_attr(self, node_id: str, attr: str, default=None):
        return self.generator._get_node_attr(node_id, attr, default)
    
    def _get_children(self, node_id: str, edge_type: Optional[str] = None) -> List[str]:
        return self.generator._get_children(node_id, edge_type)
    
    def _reconstruct_expression(self, expr_id: str) -> str:
        return self.generator._reconstruct_expression(expr_id)
    
    def _reconstruct_call(self, call_id: str) -> str:
        return self.generator._reconstruct_call(call_id)
    
    # Sub-classes implement this
    def generate(self, node_id: str) -> str:
        """
        Generate code for this node.
        Returns the code string (without indentation - caller handles that).
        """
        raise NotImplementedError


# ----------------------------------------------------------------------
# Extension: ExternalFunction
# ----------------------------------------------------------------------
class ExternalFunctionCodeGen(CodeGenExtension):
    """Generates ExternalFunction declarations"""
    
    kind = "ExternalFunction"
    
    def generate(self, node_id: str) -> str:
        name = self._get_node_attr(node_id, 'label')
        
        # Get kwargs
        kwarg_nodes = self._get_children(node_id, 'has_kwarg')
        kwargs = []
        
        for kw_id in kwarg_nodes:
            kw_name = self._get_node_attr(kw_id, 'name')
            kw_value = self._get_node_attr(kw_id, 'value')
            
            # Handle special cases
            if kw_name == 'arg_types':
                # This is a list of type references
                value_nodes = self._get_children(kw_id, 'contains')
                if value_nodes:
                    # Reconstruct list
                    types = []
                    for v_id in value_nodes:
                        v_kind = self._get_node_attr(v_id, 'kind')
                        if v_kind == 'List':
                            list_items = self._get_children(v_id, 'contains')
                            for item_id in list_items:
                                item_label = self._get_node_attr(item_id, 'label')
                                types.append(item_label)
                    if types:
                        kwargs.append(f"arg_types=[{', '.join(types)}]")
            elif kw_name == 'include_dirs':
                # This is a list of strings
                value_nodes = self._get_children(kw_id, 'contains')
                if value_nodes:
                    dirs = []
                    for v_id in value_nodes:
                        v_kind = self._get_node_attr(v_id, 'kind')
                        if v_kind == 'List':
                            list_items = self._get_children(v_id, 'contains')
                            for item_id in list_items:
                                item_label = self._get_node_attr(item_id, 'label')
                                # Remove quotes if present
                                item_label = item_label.strip('"\'')
                                dirs.append(f'"{item_label}"')
                    if dirs:
                        kwargs.append(f"include_dirs=[{', '.join(dirs)}]")
            else:
                # Regular kwarg
                if kw_value:
                    kwargs.append(f'{kw_name}="{kw_value}"')
        
        kwargs_str = ", ".join(kwargs) if kwargs else ""
        if kwargs_str:
            return f"{name} = ExternalFunction(\n        {kwargs_str}\n    )"
        return f"{name} = ExternalFunction()"


# ----------------------------------------------------------------------
# Extension: CoreFunction
# ----------------------------------------------------------------------
class CoreFunctionCodeGen(CodeGenExtension):
    """Generates CoreFunction (def statements inside jit function)"""


    kind = "CoreFunction"


    def generate(self, node_id: str) -> str:
        name = self._get_node_attr(node_id, 'label')


        # Get parameters
        param_nodes = self._get_children(node_id, 'has_param')
        params = []
        for p_id in param_nodes:
            p_name = self._get_node_attr(p_id, 'label')
            params.append(p_name)


        params_str = ", ".join(params)


        # Generate function signature
        lines = [f"def {name}({params_str}):"]


        # Generate body
        body_nodes = self._get_children(node_id, 'contains')
        if not body_nodes:
            lines.append("    pass")
        else:
            # Process body statements with base indentation (1 level = 4 spaces).
            # The "\n    ".join() below adds another 4 spaces to each continuation
            # line, so indent_level=1 here produces the correct 8-space body indent.
            body_lines = self._process_body_statements(body_nodes, indent_level=1)
            lines.extend(body_lines)

        return "\n    ".join(lines)

    def _process_body_statements(self, node_ids: List[str], indent_level: int = 2) -> List[str]:
        """
        Process body statements recursively, handling For loops with nested content.

        Args:
            node_ids: List of node IDs to process
            indent_level: Number of indentation levels (4 spaces each)

        Returns:
            List of code lines with proper indentation
        """
        lines = []
        indent = "    " * indent_level

        for child_id in node_ids:
            child_kind = self._get_node_attr(child_id, 'kind')

            if child_kind == 'For':
                # Handle For loop
                # Label format: "for _ in range_(...)"
                label = self._get_node_attr(child_id, 'label')
                lines.append(f"{indent}{label}:")

                # Process nested body statements inside the For loop
                nested_nodes = self._get_children(child_id, 'contains')
                if nested_nodes:
                    nested_lines = self._process_body_statements(nested_nodes, indent_level + 1)
                    lines.extend(nested_lines)
                else:
                    lines.append(f"{indent}    pass")

            elif child_kind == 'Acquire':
                var_name = self._get_node_attr(child_id, 'label')
                call_nodes = self._get_children(child_id, 'contains')
                if call_nodes:
                    call_expr = self._reconstruct_core_function_call(call_nodes[0])
                    lines.append(f"{indent}{var_name} = {call_expr}")

            elif child_kind == 'Release':
                call_nodes = self._get_children(child_id, 'contains')
                if call_nodes:
                    call_expr = self._reconstruct_core_function_call(call_nodes[0])
                    lines.append(f"{indent}{call_expr}")

            elif child_kind == 'Call':
                # This is a function call statement
                call_expr = self._reconstruct_function_call_statement(child_id)
                if call_expr:
                    lines.append(f"{indent}{call_expr}")

            elif child_kind == 'Assignment':
                # Indexed assignment: target[index] = value
                target = self._get_node_attr(child_id, 'target')
                index = self._get_node_attr(child_id, 'index')
                value = self._get_node_attr(child_id, 'value')
                if target is not None and index is not None and value is not None:
                    lines.append(f"{indent}{target}[{index}] = {value}")

        return lines
    
    def _reconstruct_core_function_call(self, call_id: str) -> str:
        """Reconstruct method calls in CoreFunction body (acquire/release)"""
        # The call_id is a Call node that contains a MethodCall
        # Get the method call
        method_nodes = self._get_children(call_id, 'calls')
        if not method_nodes:
            return ""
        
        method_id = method_nodes[0]
        method_kind = self._get_node_attr(method_id, 'kind')
        
        if method_kind == 'MethodCall':
            method_name = self._get_node_attr(method_id, 'label')
            
            # Get the object this method is called on
            obj_nodes = self._get_children(method_id, 'object')
            if obj_nodes:
                obj_name = self._get_node_attr(obj_nodes[0], 'label')
                
                # Get arguments from the Call node (not the MethodCall)
                arg_nodes = self._get_children(call_id, 'has_arg')
                if arg_nodes:
                    args = []
                    for arg_id in arg_nodes:
                        arg_expr = self._reconstruct_expression(arg_id)
                        if arg_expr:
                            args.append(arg_expr)
                    args_str = ", ".join(args)
                    return f"{obj_name}.{method_name}({args_str})"
                else:
                    return f"{obj_name}.{method_name}()"
            else:
                # Try to extract from the method call label itself (e.g., "inputA.acquire")
                label = self._get_node_attr(method_id, 'label')
                if '.' in label:
                    # It's already formatted as obj.method
                    arg_nodes = self._get_children(call_id, 'has_arg')
                    if arg_nodes:
                        args = []
                        for arg_id in arg_nodes:
                            arg_expr = self._reconstruct_expression(arg_id)
                            if arg_expr:
                                args.append(arg_expr)
                        args_str = ", ".join(args)
                        return f"{label}({args_str})"
                    else:
                        return f"{label}()"
        
        return ""
    
    def _reconstruct_function_call_statement(self, call_id: str) -> str:
        """Reconstruct function call statements in CoreFunction body"""
        # Get the function being called
        func_nodes = self._get_children(call_id, 'calls')
        if not func_nodes:
            return ""
        
        func_id = func_nodes[0]
        func_name = self._get_node_attr(func_id, 'label')
        
        # Get arguments
        arg_nodes = self._get_children(call_id, 'has_arg')
        args = []
        for arg_id in arg_nodes:
            arg_name = self._get_node_attr(arg_id, 'label')
            if arg_name:
                args.append(arg_name)
        
        args_str = ", ".join(args)
        return f"{func_name}({args_str})"


# ----------------------------------------------------------------------
# Extension: Worker
# ----------------------------------------------------------------------
class WorkerCodeGen(CodeGenExtension):
    """Generates Worker declarations"""
    
    kind = "Worker"
    
    def generate(self, node_id: str) -> str:
        name = self._get_node_attr(node_id, 'label')
        
        # Get core_fn reference
        core_fn_nodes = self._get_children(node_id, 'core_fn')
        core_fn_name = None
        if core_fn_nodes:
            core_fn_name = self._get_node_attr(core_fn_nodes[0], 'label')
        
        # Get fn_args
        arg_nodes = self._get_children(node_id, 'has_arg')
        args = []
        for arg_id in arg_nodes:
            arg_expr = self._reconstruct_worker_arg(arg_id)
            if arg_expr:  # Only add non-empty expressions
                args.append(arg_expr)
        
        # Get placement
        placement_nodes = self._get_children(node_id, 'placed_by')
        placement_str = None
        if placement_nodes:
            placement_str = self._reconstruct_placement(placement_nodes[0])
        
        # Build Worker call
        args_str = ", ".join(args) if args else ""
        parts = [f"core_fn={core_fn_name}" if core_fn_name else ""]
        if args_str:
            parts.append(f"fn_args=[{args_str}]")
        if placement_str:
            parts.append(f"placement={placement_str}")
        
        kwargs_str = ", ".join([p for p in parts if p])
        return f"{name} = Worker({kwargs_str})"
    
    def _reconstruct_worker_arg(self, arg_id: str) -> str:
        """
        Reconstruct worker argument with proper handling of method chains.
        
        Handles MethodChain nodes which encapsulate the entire chain structure.
        """
        kind = self._get_node_attr(arg_id, 'kind')
        
        # Check if this is a MethodChain node
        if kind == 'MethodChain':
            # Get the base expression
            base_nodes = self._get_children(arg_id, 'base')
            if not base_nodes:
                return ""

            # Reconstruct base
            result = self._reconstruct_expression(base_nodes[0])

            # If the base FIFO name was suppressed because it sourced from a
            # join-target FIFO, substitute the original join-target FIFO name so
            # the worker calls .cons() on it directly instead of on the deleted
            # broadcast alias.
            join_broadcast_map = getattr(self.generator, '_join_broadcast_map', {})
            if result in join_broadcast_map:
                result = join_broadcast_map[result]

            # Get method calls in order
            method_nodes = self._get_children(arg_id, 'has_call')
            for method_id in method_nodes:
                method_name = self._get_node_attr(method_id, 'label')
                if method_name:
                    # Check if method has kwargs
                    kwarg_nodes = self._get_children(method_id, 'has_kwarg')
                    if kwarg_nodes:
                        # Reconstruct kwargs
                        kwargs = self._reconstruct_method_kwargs(kwarg_nodes)
                        result += f".{method_name}({kwargs})"
                    else:
                        result += f".{method_name}()"

            return result
        
        # For non-MethodChain nodes, use standard reconstruction
        return self._reconstruct_expression(arg_id)
    
    def _reconstruct_method_kwargs(self, kwarg_nodes: List[str]) -> str:
        """Reconstruct method kwargs from kwarg nodes"""
        kwargs = []
        
        for kw_id in kwarg_nodes:
            kw_name = self._get_node_attr(kw_id, 'name')
            kw_value = self._get_node_attr(kw_id, 'value')
            
            # Check if kwarg has complex value (list, constructor, etc.)
            value_nodes = self._get_children(kw_id, 'contains')
            if value_nodes:
                # Reconstruct complex value
                for v_id in value_nodes:
                    v_kind = self._get_node_attr(v_id, 'kind')
                    
                    if v_kind == 'List':
                        # Reconstruct list
                        list_items = self._get_children(v_id, 'contains')
                        items = []
                        for item_id in list_items:
                            item_kind = self._get_node_attr(item_id, 'kind')
                            if item_kind == 'TypeRef':
                                items.append(self._get_node_attr(item_id, 'label'))
                            elif item_kind == 'String':
                                item_val = self._get_node_attr(item_id, 'label')
                                items.append(f'"{item_val}"')
                            elif item_kind in ['BinaryOp', 'ConstExpr', 'Const']:
                                # Reconstruct expression (includes symbolic constants)
                                expr = self._reconstruct_expression(item_id)
                                if expr:
                                    items.append(expr)
                        if items:
                            kwargs.append(f"{kw_name}=[{', '.join(items)}]")
                    
                    elif v_kind == 'ConstructorCall':
                        # Reconstruct constructor
                        ctor_expr = self._reconstruct_placement(v_id)
                        kwargs.append(f"{kw_name}={ctor_expr}")
            
            elif kw_value:
                # Simple kwarg
                kwargs.append(f"{kw_name}={kw_value}")
        
        return ", ".join(kwargs)
    
    def _reconstruct_method_chain(self, node_id: str) -> str:
        """Reconstruct a method chain by walking backwards through calls edges"""
        # Build the chain from end to start
        chain_parts = []
        current_id = node_id
        visited = set()
        
        while current_id and current_id not in visited:
            visited.add(current_id)
            kind = self._get_node_attr(current_id, 'kind')
            
            if kind == 'MethodCall':
                method_name = self._get_node_attr(current_id, 'label')
                chain_parts.insert(0, f".{method_name}()")
                
                # Find what this method is called on
                # Look for incoming 'calls' edges
                predecessors = [n for n in self.graph.predecessors(current_id)
                               if self.graph[n][current_id].get('edge_type') == 'calls']
                if predecessors:
                    current_id = predecessors[0]
                else:
                    break
            elif kind == 'IndexExpr':
                # Reconstruct index expression
                base_nodes = self._get_children(current_id, 'base')
                index_nodes = self._get_children(current_id, 'index')
                
                if base_nodes and index_nodes:
                    # Get base - could be a VarRef or another node
                    base_node = base_nodes[0]
                    base_kind = self._get_node_attr(base_node, 'kind')
                    
                    if base_kind == 'VarRef':
                        base_label = self._get_node_attr(base_node, 'label')
                    else:
                        # Try to get label directly
                        base_label = self._get_node_attr(base_node, 'label')
                        if not base_label:
                            # Look up the symbol
                            try:
                                base_label = base_node  # Use node id as fallback
                            except:
                                base_label = "unknown"
                    
                    index_kind = self._get_node_attr(index_nodes[0], 'kind')
                    
                    if index_kind == 'ConstExpr':
                        index_val = self._get_node_attr(index_nodes[0], 'value', '0')
                    else:
                        index_val = self._get_node_attr(index_nodes[0], 'label', '0')
                    
                    chain_parts.insert(0, f"{base_label}[{index_val}]")
                break
            else:
                # Try to get label for any other node type
                label = self._get_node_attr(current_id, 'label')
                if label:
                    chain_parts.insert(0, label)
                break
        
        return ''.join(chain_parts)
    
    def _reconstruct_placement(self, placement_id: str) -> str:
        """Reconstruct Tile(x, y) constructor"""
        kind = self._get_node_attr(placement_id, 'kind')
        
        if kind == 'ConstructorCall':
            # Get constructor arguments
            arg_nodes = self._get_children(placement_id, 'has_arg')
            args = []
            for arg_id in arg_nodes:
                # Use the base _reconstruct_expression to handle any expression type
                arg_expr = self._reconstruct_expression(arg_id)
                if arg_expr:
                    args.append(arg_expr)
            
            if args:
                args_str = ", ".join(args)
                return f"Tile({args_str})"
        
        return "Tile()"


# ----------------------------------------------------------------------
# Extension: List
# ----------------------------------------------------------------------
class ListCodeGen(CodeGenExtension):
    """Generates List declarations"""
    
    kind = "List"
    
    def generate(self, node_id: str) -> str:
        name = self._get_node_attr(node_id, 'label')
        
        # Get list items
        item_nodes = self._get_children(node_id, 'contains')
        items = []
        for item_id in item_nodes:
            item_label = self._get_node_attr(item_id, 'label')
            items.append(item_label)
        
        items_str = ", ".join(items)
        return f"{name} = [{items_str}]"


# ----------------------------------------------------------------------
# Registry & auto-wiring into CodeGenerator
# ----------------------------------------------------------------------
_CODEGEN_EXTENSION_REGISTRY: Dict[str, Type[CodeGenExtension]] = {}


def register_codegen_extension(cls: Type[CodeGenExtension]):
    """Register a CodeGenExtension subclass by its kind."""
    kind = cls.kind
    if not kind:
        raise ValueError(f"{cls.__name__}.kind must be set")
    _CODEGEN_EXTENSION_REGISTRY[kind] = cls
    return cls


def register_codegen_extensions(generator) -> None:
    """
    Call this from CodeGenerator to inject extension handlers.
    
    Creates a method _generate_ext_<kind> for each registered extension.
    """
    for kind, ext_cls in _CODEGEN_EXTENSION_REGISTRY.items():
        method_name = f"_generate_ext_{kind.lower()}"
        
        def make_handler(ext_cls=ext_cls):
            def handler(self, node_id: str) -> str:
                ext = ext_cls(self)
                return ext.generate(node_id)
            return handler
        
        # Bind the generated handler into the generator instance
        setattr(generator, method_name, make_handler().__get__(generator, type(generator)))


# ----------------------------------------------------------------------
# Register built-in extensions
# ----------------------------------------------------------------------
register_codegen_extension(ExternalFunctionCodeGen)
register_codegen_extension(CoreFunctionCodeGen)
register_codegen_extension(WorkerCodeGen)
register_codegen_extension(ListCodeGen)

