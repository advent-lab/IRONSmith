#!/usr/bin/env python3
"""
CodeGenerator.py — Generate Python code from GraphML semantic graph

This module generates Python code by traversing semantic graphs created by GraphDriver.
The code generation is PURELY graph-driven with NO hardcoded values or patterns.

Architecture:
- Graph Loader: Reads GraphML files into NetworkX DiGraph
- Code Emitter: Manages indentation and line generation
- Expression Reconstructor: Rebuilds expressions from graph nodes
- Statement Processor: Generates statements from graph structure
- Section Generator: Handles imports, types, dataflow, functions

Key Design Principles:
1. Pure Graph Traversal: All code derived from graph structure
2. No Hardcoding: No assumptions about specific code patterns
3. Flexible: Works with any graph structure from GraphDriver
4. Complete: Generates all code elements (imports, types, functions, etc.)
"""

import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Optional, Any
import networkx as nx


class CodeGenerator:
    """
    Generates Python code by traversing semantic graph structure.
    
    The generator walks the graph using typed edges to understand relationships
    and node attributes to extract code details. No code patterns are hardcoded.
    
    Process:
    1. Load graph from GraphML file
    2. Find Module node (root)
    3. Process sections in order (Symbols, Functions, EntryPoint)
    4. Emit code with proper indentation
    5. Return complete Python source code
    """
    
    def __init__(self, graphml_path: Path):
        self.graphml_path = graphml_path
        self.graph: nx.DiGraph = nx.read_graphml(str(graphml_path))
        self.code_lines: List[str] = []
        self.indent_level = 0
        self.dataflow_generated = False  # Prevent duplicate DataFlow generation
        
        # Register code generation extensions
        from extension.CodeGeneratorExtender import register_codegen_extensions
        register_codegen_extensions(self)
        
    # ===================================================================
    # SECTION 1: Graph Navigation and Code Emission
    # ===================================================================
    # Core utilities for traversing graph and emitting formatted code.
    
    def _indent(self) -> str:
        """Return current indentation string (4 spaces per level)."""
        return "    " * self.indent_level
    
    def _emit(self, line: str = ""):
        """
        Add a line of code with proper indentation.
        
        Handles:
        - Automatic indentation based on current level
        - Empty lines (no indentation)
        - Accumulates lines for final output
        """
        if line:
            self.code_lines.append(self._indent() + line)
        else:
            self.code_lines.append("")
    
    def _get_node_attr(self, node_id: str, attr: str, default=None):
        """
        Get attribute from graph node.
        
        Attributes store code details:
        - label: Name/identifier
        - kind: Node type
        - value: Constant values
        - operator: Binary/comparison operators
        - etc.
        """
        return self.graph.nodes[node_id].get(attr, default)
    
    def _get_children(self, node_id: str, edge_type: Optional[str] = None) -> List[str]:
        """
        Get child nodes connected by specific edge type.
        
        Edge types define relationships:
        - 'contains': Structural containment
        - 'calls': Function/method invocation
        - 'has_arg': Function arguments
        - 'then'/'else': Conditional branches
        - etc.
        
        If edge_type is None, returns all children.
        """
        children = []
        for _, target, data in self.graph.out_edges(node_id, data=True):
            if edge_type is None or data.get('type') == edge_type:
                children.append(target)
        return children
    
    def _find_nodes_by_kind(self, kind: str) -> List[str]:
        """
        Find all nodes of a specific kind in the graph.

        Used to locate specific elements like Module, Function, etc.
        """
        return [n for n, d in self.graph.nodes(data=True) if d.get('kind') == kind]

    def _is_numeric(self, value: str) -> bool:
        """
        Check if a string represents a numeric value (int, float, or scientific notation).

        Examples: "123", "45.67", "1e-3", "2.5e10"
        """
        try:
            float(value)
            return True
        except (ValueError, TypeError):
            return False

    # ===================================================================
    # SECTION 2: Main Code Generation Entry Point
    # ===================================================================
    # Orchestrates the entire code generation process.
    
    def generate(self) -> str:
        """
        Main entry point to generate complete Python code from graph.
        
        Process:
        1. Find Module node (root of graph)
        2. Generate file header
        3. Process Symbols section (imports, types, constants)
        4. Process Function definitions
        5. Process EntryPoint (if __name__ == "__main__")
        6. Return complete source code
        
        Returns:
            str: Complete Python source code
        """
        # Find module node
        module_nodes = self._find_nodes_by_kind('Module')
        if not module_nodes:
            raise ValueError("No Module node found in graph")
        
        module_id = module_nodes[0]
        module_name = self._get_node_attr(module_id, 'label', 'unnamed')
        
        # Generate file header
        self._emit(f"# {module_name}.py -*- Python -*-")
        self._emit()
        self._emit()
        
        # Process all module children in order
        children = self._get_children(module_id, 'contains')
        for child_id in children:
            kind = self._get_node_attr(child_id, 'kind')
            if kind == 'Section':
                label = self._get_node_attr(child_id, 'label')
                if label == 'Symbols':
                    self._process_symbols_section(child_id)
            elif kind == 'Function':
                self._process_function(child_id)
            elif kind == 'EntryPoint':
                self._process_entrypoint(child_id)
        
        return "\n".join(self.code_lines)
    
    # ===================================================================
    # SECTION 3: Symbols Section Processing
    # ===================================================================
    # Generates imports and global declarations.
    
    def _process_symbols_section(self, section_id: str):
        """
        Process Symbols section to generate imports.
        
        Handles:
        - Standard library imports (sys, etc.)
        - Third-party imports (numpy as np)
        - IRON imports (from aie.iron import ...)
        
        Note: Constants and types are generated within functions
        where they're used (via UseType/UseSymbol nodes).
        """
        children = self._get_children(section_id, 'contains')
        
        # Generate imports
        for child_id in children:
            kind = self._get_node_attr(child_id, 'kind')
            if kind == 'Import':
                name = self._get_node_attr(child_id, 'label')
                alias = self._get_node_attr(child_id, 'alias')
                
                if name == 'sys':
                    self._emit(f"import {name}")
                elif name == 'numpy':
                    self._emit(f"import {name} as {alias}" if alias else f"import {name}")
                elif name == 'ml_dtypes':
                    # Import bfloat16 directly from ml_dtypes
                    self._emit("from ml_dtypes import bfloat16")
                elif name == 'aie.iron':
                    self._emit()
                    self._emit("from aie.iron import Program, Runtime, Worker, ObjectFifo")
                    self._emit("from aie.iron.placers import SequentialPlacer")
                    self._emit("from aie.iron.device.tile import AnyComputeTile")
                    self._emit("from aie.iron import ExternalFunction, jit")
                    self._emit("from aie.iron.dataflow import ObjectFifoLink")
                    self._emit("from aie.iron.device import Tile")
                    self._emit("from aie.iron.device import NPU1Col1, NPU2Col1, XCVC1902")
                    if alias:
                        self._emit(f"import {name} as {alias}")
                elif name == 'aie.helpers.taplib':
                    # Import TensorAccessPattern and TensorTiler2D if used
                    self._emit()
                    has_tiler2d = bool(self._find_nodes_by_kind('TensorTiler2D'))
                    if has_tiler2d:
                        self._emit("from aie.helpers.taplib import TensorAccessPattern, TensorTiler2D")
                    else:
                        self._emit("from aie.helpers.taplib import TensorAccessPattern")
                elif name == 'aie.iron.controlflow':
                    # Import range_ for core function loops
                    self._emit("from aie.iron.controlflow import range_")
                elif alias:
                    self._emit(f"import {name} as {alias}")
                else:
                    self._emit(f"import {name}")
        
        self._emit()
        self._emit()
    
    # ===================================================================
    # SECTION 4: Function Processing
    # ===================================================================
    # Generates function definitions with decorators, parameters, and body.
    
    def _process_function(self, func_id: str):
        """
        Process a function definition.
        
        Generates:
        - Decorator (@iron.device)
        - Function signature with parameters
        - Function body statements
        
        Manages indentation for nested statements.
        """
        func_name = self._get_node_attr(func_id, 'label')
        decorator = self._get_node_attr(func_id, 'decorator')
        
        # Generate decorator
        if decorator:
            self._emit(f"@{decorator}(is_placed=False)")
        
        # Get parameters
        params = []
        param_nodes = self._get_children(func_id, 'has_param')
        for param_id in param_nodes:
            param_name = self._get_node_attr(param_id, 'label')
            params.append(param_name)
        
        # Generate function signature
        params_str = ", ".join(params)
        self._emit(f"def {func_name}({params_str}):")
        
        # Generate function body
        self.indent_level += 1
        body_children = self._get_children(func_id, 'contains')
        
        if not body_children:
            self._emit("pass")
        else:
            for child_id in body_children:
                self._process_statement(child_id)
        
        self.indent_level -= 1
        self._emit()
        self._emit()
    
    # ===================================================================
    # SECTION 5: Statement Processing
    # ===================================================================
    # Dispatches to specific handlers based on statement type.
    
    def _process_statement(self, stmt_id: str):
        """
        Process a statement node by dispatching to type-specific handler.
        
        Statement types:
        - Assign: Variable assignment
        - Assert: Assertion with condition
        - Comment: Code comment
        - Call: Function/method call
        - If: Conditional statement
        - For: Loop statement
        - Print: Print statement
        - FunctionBodyElement: Generic elements (UseType, Return, etc.)
        """
        kind = self._get_node_attr(stmt_id, 'kind')
        
        if kind == 'Assign':
            self._process_assign(stmt_id)
        elif kind == 'Assert':
            self._process_assert(stmt_id)
        elif kind == 'Comment':
            self._process_comment(stmt_id)
        elif kind == 'FunctionBodyElement':
            self._process_function_body_element(stmt_id)
        elif kind == 'Call':
            # Reconstruct the full call expression
            call_expr = self._reconstruct_call(stmt_id)
            if call_expr:
                self._emit(call_expr)
        elif kind == 'If':
            self._process_if(stmt_id)
        elif kind == 'For':
            self._process_for(stmt_id)
        elif kind == 'Print':
            self._process_print(stmt_id)
    
    def _process_assign(self, assign_id: str):
        """Process assignment statement."""
        target = self._get_node_attr(assign_id, 'target')
        
        # Get source expression
        source_nodes = self._get_children(assign_id, 'source')
        if source_nodes:
            source_expr = self._reconstruct_expression(source_nodes[0])
            self._emit(f"{target} = {source_expr}")
        else:
            # Check if variable refers to a constant
            var_nodes = self._get_children(assign_id, 'assigns')
            if var_nodes:
                var_id = var_nodes[0]
                ref_nodes = self._get_children(var_id, 'refers_to')
                if ref_nodes:
                    ref_id = ref_nodes[0]
                    ref_kind = self._get_node_attr(ref_id, 'kind')
                    if ref_kind == 'Const':
                        value = self._get_node_attr(ref_id, 'value')
                        self._emit(f"{target} = {value}")
                        return
            
            self._emit(f"{target} = None  # TODO: resolve")
    
    # ===================================================================
    # SECTION 6: Expression Reconstruction
    # ===================================================================
    # Recursively rebuilds expressions from graph nodes.
    # This is the core of code generation - converts graph to Python syntax.
    
    def _reconstruct_expression(self, expr_id: str) -> str:
        """
        Reconstruct expression from graph node.
        
        Handles all Python expression types:
        - Literals: Constants, strings, numbers
        - Variables: VarRef nodes
        - Binary operations: +, -, *, /, %
        - Comparisons: ==, !=, <, >, <=, >=
        - Function calls: func(args, kwargs)
        - Method calls: obj.method(args)
        - Index expressions: arr[idx]
        - Constructors: Type()
        
        Returns:
            str: Python expression string
        """
        kind = self._get_node_attr(expr_id, 'kind')
        label = self._get_node_attr(expr_id, 'label')
        
        if kind == 'ConstExpr':
            value = self._get_node_attr(expr_id, 'value', label)
            # Handle string constants
            if isinstance(value, str):
                if value.isdigit():
                    return value
                elif value.startswith('np.'):
                    return value
                # Check if it's a numeric value (int, float, scientific notation)
                elif self._is_numeric(value):
                    return value
                elif not value.startswith('"'):
                    return f'"{value}"'
            return str(value)
        
        elif kind == 'MethodCallExpr':
            obj_ref = self._get_node_attr(expr_id, 'object_ref')
            method = self._get_node_attr(expr_id, 'method')
            return f"{obj_ref}.{method}()"
        
        elif kind == 'Call':
            result = self._reconstruct_call(expr_id)
            # If reconstruction failed, try to get a meaningful representation
            if not result or result == "call()":
                # This might be a standalone call node, check for method/function children
                children = self._get_children(expr_id, 'contains')
                if children:
                    for child_id in children:
                        child_kind = self._get_node_attr(child_id, 'kind')
                        if child_kind in ['MethodCall', 'FunctionCall']:
                            return self._reconstruct_expression(child_id)
            return result
        
        elif kind == 'UnaryOp':
            operator = self._get_node_attr(expr_id, 'op')
            if not operator:
                operator = self._get_node_attr(expr_id, 'operator')  # Try 'operator' attribute

            # Get operand
            operand_nodes = self._get_children(expr_id, 'operand')
            if not operand_nodes:
                operand_nodes = self._get_children(expr_id, 'contains')

            if operand_nodes:
                operand = self._reconstruct_expression(operand_nodes[0])
                if operand:
                    return f"{operator}{operand}"
            return f"unary_op({operator})"

        elif kind == 'BinaryOp':
            operator = self._get_node_attr(expr_id, 'operator')
            if not operator:
                operator = self._get_node_attr(expr_id, 'op')  # Try 'op' attribute

            # Get operands - they might be in 'lhs'/'rhs', 'operand', or 'contains' edges
            lhs_nodes = self._get_children(expr_id, 'lhs')
            rhs_nodes = self._get_children(expr_id, 'rhs')

            if lhs_nodes and rhs_nodes:
                left = self._reconstruct_expression(lhs_nodes[0])
                right = self._reconstruct_expression(rhs_nodes[0])
                if left and right:
                    return f"({left} {operator} {right})"

            # Fallback to operand/contains edges
            operands = self._get_children(expr_id, 'operand')
            if not operands:
                operands = self._get_children(expr_id, 'contains')

            if len(operands) >= 2:
                left = self._reconstruct_expression(operands[0])
                right = self._reconstruct_expression(operands[1])
                if left and right:
                    return f"({left} {operator} {right})"
            return f"binary_op({operator})"
        
        elif kind == 'ComparisonOp':
            operator = self._get_node_attr(expr_id, 'operator')
            # Get operands - they might be in 'operand' or 'contains' edges
            operands = self._get_children(expr_id, 'operand')
            if not operands:
                operands = self._get_children(expr_id, 'contains')
            
            if len(operands) >= 2:
                left = self._reconstruct_expression(operands[0])
                right = self._reconstruct_expression(operands[1])
                return f"{left} {operator} {right}"
            return "comparison"
        
        elif kind == 'ConditionExpr':
            # Handle condition expressions that combine binary ops and comparisons
            children = self._get_children(expr_id, 'contains')
            if children:
                parts = []
                for child_id in children:
                    child_kind = self._get_node_attr(child_id, 'kind')
                    if child_kind in ['BinaryOp', 'ComparisonOp']:
                        parts.append(self._reconstruct_expression(child_id))
                if len(parts) == 2:
                    # This is likely "expr == value" pattern
                    return f"{parts[0]} == {parts[1]}"
                elif parts:
                    return parts[0]
            return "condition"
        
        elif kind == 'IndexExpr':
            base_nodes = self._get_children(expr_id, 'base')
            index_nodes = self._get_children(expr_id, 'index')
            if base_nodes and index_nodes:
                base = self._reconstruct_expression(base_nodes[0])
                index = self._reconstruct_expression(index_nodes[0])
                return f"{base}[{index}]"
            return "index_expr"
        
        elif kind == 'Index' or kind == 'IndexAccess':
            # Handle index access like sys.argv[1]
            children = self._get_children(expr_id, 'contains')
            if len(children) >= 2:
                base = self._reconstruct_expression(children[0])
                index = self._reconstruct_expression(children[1])
                return f"{base}[{index}]"
            return "index"
        
        elif kind == 'FunctionCallExpr':
            func_name = self._get_node_attr(expr_id, 'function')
            args = []
            arg_nodes = self._get_children(expr_id, 'arg')
            for arg_id in arg_nodes:
                arg_expr = self._reconstruct_expression(arg_id)
                args.append(arg_expr)
            
            kwargs = []
            kwarg_nodes = self._get_children(expr_id, 'kwarg')
            for kw_id in kwarg_nodes:
                kw_name = self._get_node_attr(kw_id, 'name')
                kw_val_nodes = self._get_children(kw_id, 'value')
                if kw_val_nodes:
                    kw_val = self._reconstruct_expression(kw_val_nodes[0])
                    kwargs.append(f"{kw_name}={kw_val}")
            
            all_args = args + kwargs
            args_str = ", ".join(all_args)
            return f"{func_name}({args_str})"
        
        elif kind == 'IndexExpr':
            base_nodes = self._get_children(expr_id, 'base')
            index_nodes = self._get_children(expr_id, 'index')
            if base_nodes and index_nodes:
                base = self._reconstruct_expression(base_nodes[0])
                index = self._reconstruct_expression(index_nodes[0])
                return f"{base}[{index}]"
            return "index_expr"
        
        elif kind == 'ConstructorExpr':
            ctor = self._get_node_attr(expr_id, 'constructor')
            return f"{ctor}()"

        elif kind == 'Constructor' or kind == 'ConstructorCall':
            # Handle constructor calls with arguments (like TensorAccessPattern)
            ctor_label = self._get_node_attr(expr_id, 'label')
            if ctor_label:
                # Extract constructor name
                ctor_name = ctor_label.rstrip('()')
                
                # Get constructor arguments
                args = []
                arg_nodes = self._get_children(expr_id, 'has_arg')
                for arg_id in arg_nodes:
                    arg_expr = self._reconstruct_expression(arg_id)
                    if arg_expr:
                        args.append(arg_expr)
                
                # Get constructor kwargs
                kwarg_nodes = self._get_children(expr_id, 'has_kwarg')
                for kw_id in kwarg_nodes:
                    kw_name = self._get_node_attr(kw_id, 'name')
                    
                    # Check if kwarg has complex value
                    value_nodes = self._get_children(kw_id, 'contains')
                    if value_nodes:
                        kw_value = self._reconstruct_expression(value_nodes[0])
                        args.append(f"{kw_name}={kw_value}")
                    else:
                        kw_value = self._get_node_attr(kw_id, 'value')
                        if kw_value:
                            args.append(f"{kw_name}={kw_value}")
                
                args_str = ", ".join(args)
                return f"{ctor_name}({args_str})"
            return "Constructor()"
        
        elif kind == 'MethodCall':
            # Handle standalone method calls (e.g., in kwargs like prod())
            method_name = self._get_node_attr(expr_id, 'label')
            object_ref = self._get_node_attr(expr_id, 'object_ref')

            if object_ref:
                # This is a method call on a specific object: object.method()
                return f"{object_ref}.{method_name}()"
            else:
                # Just the method name with parentheses
                return f"{method_name}()"

        elif kind == 'List':
            # Handle list expressions
            list_items = self._get_children(expr_id, 'contains')
            items = []
            for item_id in list_items:
                item_expr = self._reconstruct_expression(item_id)
                if item_expr:
                    items.append(item_expr)
            return f"[{', '.join(items)}]"
        
        elif kind == 'String':
            # Handle string literals
            string_val = self._get_node_attr(expr_id, 'label')
            if string_val:
                # Check if it already has quotes
                if string_val.startswith('"') or string_val.startswith("'"):
                    return string_val
                return f'"{string_val}"'
            return '""'
        
        elif kind == 'DtypeExpr':
            dtype = self._get_node_attr(expr_id, 'dtype')
            return f"np.dtype[np.{dtype}]"
        
        elif kind == 'NumpyDtype':
            return f"np.{label}"
        
        elif kind in ['MethodCall', 'MethodCallExpr']:
            # Handle method calls like inputA.numel()
            # Check if method name is in attributes
            method_name = self._get_node_attr(expr_id, 'method')
            if not method_name:
                # Fallback to label
                method_name = label if label else ""
                # If label contains the full call like "inputA.numel()", extract just the method
                if method_name and '.' in method_name:
                    method_name = method_name.split('.')[-1].rstrip('()')

            obj_nodes = self._get_children(expr_id, 'object')
            if obj_nodes:
                obj_label = self._get_node_attr(obj_nodes[0], 'label')
                # Get arguments if any
                arg_nodes = self._get_children(expr_id, 'has_arg')
                if arg_nodes:
                    args = []
                    for arg_id in arg_nodes:
                        arg_expr = self._reconstruct_expression(arg_id)
                        args.append(arg_expr)
                    return f"{obj_label}.{method_name}({', '.join(args)})"
                else:
                    return f"{obj_label}.{method_name}()"
            else:
                # Check for object_ref attribute
                obj_ref = self._get_node_attr(expr_id, 'object_ref')
                if obj_ref:
                    return f"{obj_ref}.{method_name}()"
                # Method without object reference
                return f"{method_name}()"

        elif kind == 'VarRef':
            return str(label)

        elif kind == 'Const':
            value = self._get_node_attr(expr_id, 'value')
            return str(value)

        elif kind == 'Variable':
            return str(label)
        
        elif kind == 'Arg':
            # Handle function arguments
            children = self._get_children(expr_id, 'contains')
            if children:
                return self._reconstruct_expression(children[0])
            return str(label) if label else "arg"
        
        elif kind == 'Kwarg':
            # Handle keyword arguments
            name = self._get_node_attr(expr_id, 'name')
            value_nodes = self._get_children(expr_id, 'contains')
            if value_nodes:
                value = self._reconstruct_expression(value_nodes[0])
                return f"{name}={value}"
            return f"{name}=value"
        
        elif kind == 'Expr':
            # Try to reconstruct from children
            children = self._get_children(expr_id, 'contains')
            if children:
                return self._reconstruct_expression(children[0])
            return str(label) if label else "expr"
        
        elif kind == 'MethodChain':
            # Reconstruct method chain
            return self._reconstruct_method_chain(expr_id)
        
        return str(label) if label else "unknown"
    
    # ===================================================================
    # SECTION 7: Call Reconstruction
    # ===================================================================
    # Handles method chains and function calls with arguments.
    # Critical for IRON's fluent API patterns.
    
    def _reconstruct_call(self, call_id: str) -> str:
        """
        Reconstruct a call expression - handles method chains and function calls.
        
        Handles:
        - Method calls: obj.method(args)
        - Function calls: func(args, kwargs)
        - Method chains: obj.method1().method2()
        - Complex arguments: expressions, index access, binary ops
        
        Process:
        1. Find method/function node via 'calls' edge
        2. Collect arguments via 'has_arg' edges
        3. Collect kwargs via 'has_kwarg' edges
        4. Check for nested calls via 'nested_call' edges
        5. Reconstruct complete call expression
        
        Returns:
            str: Complete call expression (e.g., "obj.method(arg1, arg2)")
        """
        # Check if this is a direct function call with children
        children = self._get_children(call_id, 'contains')
        if children:
            # This might be a function call with args
            func_nodes = [c for c in children if self._get_node_attr(c, 'kind') == 'FunctionCall']
            if func_nodes:
                func_name = self._get_node_attr(func_nodes[0], 'label')
                args = []
                arg_nodes = [c for c in children if self._get_node_attr(c, 'kind') in ['Arg', 'Kwarg']]
                for arg_id in arg_nodes:
                    args.append(self._reconstruct_expression(arg_id))
                
                args_str = ", ".join(args) if args else ""
                return f"{func_name}({args_str})"
        
        # Get method call or function call
        method_nodes = self._get_children(call_id, 'calls')
        if method_nodes:
            method_id = method_nodes[0]
            method_kind = self._get_node_attr(method_id, 'kind')
            
            if method_kind == 'MethodCall':
                method_name = self._get_node_attr(method_id, 'label')

                # Check for object reference - either as edge or attribute
                obj_nodes = self._get_children(method_id, 'object')
                object_ref = self._get_node_attr(method_id, 'object_ref')

                if obj_nodes or object_ref:
                    # Get object name from edge or attribute
                    if obj_nodes:
                        obj_name = self._get_node_attr(obj_nodes[0], 'label')
                    else:
                        obj_name = object_ref
                    
                    # Get arguments from call node
                    args = []
                    arg_nodes = self._get_children(call_id, 'has_arg')
                    for arg_id in arg_nodes:
                        # Try to reconstruct the argument expression
                        arg_kind = self._get_node_attr(arg_id, 'kind')
                        if arg_kind in ['Variable', 'VarRef', 'Binding']:
                            # This is a variable reference
                            arg_expr = self._get_node_attr(arg_id, 'label')
                        else:
                            arg_expr = self._reconstruct_expression(arg_id)
                        args.append(arg_expr)
                    
                    # Get kwargs
                    kwarg_nodes = self._get_children(call_id, 'has_kwarg')
                    for kw_id in kwarg_nodes:
                        name = self._get_node_attr(kw_id, 'name')
                        value_nodes = self._get_children(kw_id, 'contains')
                        if value_nodes:
                            value = self._reconstruct_expression(value_nodes[0])
                            args.append(f"{name}={value}")
                        else:
                            value = self._get_node_attr(kw_id, 'value')
                            args.append(f"{name}={value}")
                    
                    # Check for nested calls (method chaining)
                    nested_nodes = self._get_children(call_id, 'nested_call')
                    if nested_nodes:
                        # Get the nested method name directly
                        nested_call_id = nested_nodes[0]
                        nested_method_nodes = self._get_children(nested_call_id, 'calls')
                        if nested_method_nodes:
                            nested_method_id = nested_method_nodes[0]
                            nested_method_kind = self._get_node_attr(nested_method_id, 'kind')
                            nested_method_name = self._get_node_attr(nested_method_id, 'label')
                            
                            if nested_method_kind == 'MethodCall' and nested_method_name:
                                # This is a chained call like of_in.cons().forward()
                                return f"{obj_name}.{method_name}().{nested_method_name}()"
                        
                        # Fallback: try full reconstruction
                        nested_expr = self._reconstruct_call(nested_nodes[0])
                        if nested_expr:
                            # If nested expression has a dot, it might be obj.method() format
                            if '.' in nested_expr:
                                # Extract just the method part
                                parts = nested_expr.split('.')
                                if len(parts) > 1:
                                    nested_method = '.'.join(parts[1:])
                                    return f"{obj_name}.{method_name}().{nested_method}"
                            # Otherwise, append the whole expression
                            return f"{obj_name}.{method_name}().{nested_expr}"
                    
                    args_str = ", ".join(args) if args else ""
                    return f"{obj_name}.{method_name}({args_str})"
                else:
                    # Method without object (part of chain)
                    args = []
                    arg_nodes = self._get_children(call_id, 'has_arg')
                    for arg_id in arg_nodes:
                        arg_expr = self._reconstruct_expression(arg_id)
                        args.append(arg_expr)
                    
                    args_str = ", ".join(args) if args else ""
                    return f"{method_name}({args_str})"
            
            elif method_kind == 'FunctionCall':
                func_name = self._get_node_attr(method_id, 'label')
                args = []
                
                # Get arguments - try has_arg first, then contains
                arg_nodes = self._get_children(call_id, 'has_arg')
                if not arg_nodes:
                    # Try getting from contains and filter for Arg nodes
                    contains_nodes = self._get_children(call_id, 'contains')
                    arg_nodes = [n for n in contains_nodes if self._get_node_attr(n, 'kind') == 'Arg']
                
                for arg_id in arg_nodes:
                    arg_kind = self._get_node_attr(arg_id, 'kind')
                    
                    if arg_kind == 'Arg':
                        # Arg node - get its contents
                        arg_contents = self._get_children(arg_id, 'contains')
                        if arg_contents:
                            arg_expr = self._reconstruct_expression(arg_contents[0])
                            args.append(arg_expr)
                    elif arg_kind in ['Variable', 'VarRef', 'Binding']:
                        # Direct variable reference
                        arg_expr = self._get_node_attr(arg_id, 'label')
                        args.append(arg_expr)
                    elif arg_kind == 'ConstExpr':
                        # Constant value
                        arg_expr = self._get_node_attr(arg_id, 'value')
                        if not arg_expr:
                            arg_expr = self._get_node_attr(arg_id, 'label')
                        args.append(arg_expr)
                    elif arg_kind == 'IndexExpr':
                        # Index expression like sys.argv[1]
                        arg_expr = self._reconstruct_expression(arg_id)
                        args.append(arg_expr)
                    elif arg_kind == 'BinaryOp':
                        # Binary operation like input_host != output_host
                        arg_expr = self._reconstruct_expression(arg_id)
                        args.append(arg_expr)
                    elif arg_kind == 'ConstructorExpr':
                        # Constructor call like NPU1Col1()
                        ctor = self._get_node_attr(arg_id, 'constructor')
                        args.append(f"{ctor}()")
                    else:
                        arg_expr = self._reconstruct_expression(arg_id)
                        args.append(arg_expr)
                
                # Get kwargs
                kwarg_nodes = self._get_children(call_id, 'has_kwarg')
                if not kwarg_nodes:
                    # Try getting from contains
                    contains_nodes = self._get_children(call_id, 'contains')
                    kwarg_nodes = [n for n in contains_nodes if self._get_node_attr(n, 'kind') == 'Kwarg']
                
                for kw_id in kwarg_nodes:
                    name = self._get_node_attr(kw_id, 'name')
                    value_nodes = self._get_children(kw_id, 'contains')
                    if value_nodes:
                        value = self._reconstruct_expression(value_nodes[0])
                        args.append(f"{name}={value}")
                    else:
                        value = self._get_node_attr(kw_id, 'value')
                        args.append(f"{name}={value}")
                
                args_str = ", ".join(args) if args else ""
                return f"{func_name}({args_str})"
        
        # No method or function found - return empty string
        return ""
    
    def _reconstruct_method_chain(self, chain_id: str) -> str:
        """
        Reconstruct a MethodChain node into a method chain expression.

        MethodChain nodes contain:
        - base: The base expression (var, index, etc.)
        - has_call: Method calls in the chain

        Returns:
            str: Complete method chain (e.g., "obj.cons().split(args)")
        """
        # Get the base expression
        base_nodes = self._get_children(chain_id, 'base')
        if not base_nodes:
            return ""

        # Reconstruct base
        result = self._reconstruct_expression(base_nodes[0])

        # Get method calls in order
        method_nodes = self._get_children(chain_id, 'has_call')
        for method_id in method_nodes:
            method_name = self._get_node_attr(method_id, 'label')
            if method_name:
                # Check if method has kwargs
                kwarg_nodes = self._get_children(method_id, 'has_kwarg')
                if kwarg_nodes:
                    # Reconstruct kwargs
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
                                        elif item_kind in ['BinaryOp', 'ConstExpr', 'Expr', 'Const']:
                                            # Reconstruct expression (includes symbolic constants)
                                            expr = self._reconstruct_expression(item_id)
                                            if expr:
                                                items.append(expr)
                                        elif item_kind == 'VarRef':
                                            # Variable reference
                                            var_label = self._get_node_attr(item_id, 'label')
                                            items.append(var_label)
                                        elif item_kind == 'MethodRef':
                                            # Method reference (e.g., inputA.numel)
                                            method_ref = self._get_node_attr(item_id, 'label')
                                            # Add () if it's a method call
                                            if method_ref and '.' in method_ref:
                                                items.append(f"{method_ref}()")
                                            else:
                                                items.append(method_ref)
                                    if items:
                                        kwargs.append(f"{kw_name}=[{', '.join(items)}]")

                                elif v_kind == 'ConstructorCall':
                                    # Reconstruct constructor
                                    ctor_label = self._get_node_attr(v_id, 'label')
                                    if ctor_label:
                                        # Get constructor arguments
                                        ctor_args = []
                                        ctor_arg_nodes = self._get_children(v_id, 'has_arg')
                                        for arg_id in ctor_arg_nodes:
                                            arg_expr = self._reconstruct_expression(arg_id)
                                            if arg_expr:
                                                ctor_args.append(arg_expr)

                                        if ctor_args:
                                            # Extract constructor name from label (e.g., "Tile()" -> "Tile")
                                            ctor_name = ctor_label.rstrip('()')
                                            kwargs.append(f"{kw_name}={ctor_name}({', '.join(ctor_args)})")
                                        else:
                                            kwargs.append(f"{kw_name}={ctor_label}")

                        elif kw_value:
                            # Simple kwarg
                            kwargs.append(f"{kw_name}={kw_value}")

                    if kwargs:
                        result += f".{method_name}({', '.join(kwargs)})"
                    else:
                        result += f".{method_name}()"
                else:
                    result += f".{method_name}()"

        return result
    
    # ===================================================================
    # SECTION 8: Statement-Specific Processors
    # ===================================================================
    # Handlers for specific statement types.
    
    def _process_assert(self, assert_id: str):
        """
        Process assert statement.
        
        Generates: assert condition, message
        """
        message = self._get_node_attr(assert_id, 'message', '')
        
        # Get condition - it might be a ConditionExpr containing BinaryOp and ComparisonOp
        cond_nodes = self._get_children(assert_id, 'condition')
        if cond_nodes:
            cond_id = cond_nodes[0]
            condition = self._reconstruct_expression(cond_id)
            self._emit(f"assert {condition}, {message}")
        else:
            self._emit(f"assert condition, {message}")
    
    def _process_function_body_element(self, elem_id: str):
        """Process generic function body elements."""
        label = self._get_node_attr(elem_id, 'label')
        
        if label == 'UseType':
            self._generate_type_definitions(elem_id)
        elif label == 'UseSymbol':
            # Only generate DataFlow once
            if not self.dataflow_generated:
                self._generate_dataflow_usage(elem_id)
                self.dataflow_generated = True
        elif label == 'UseDataFlow':
            # Generate both type definitions and DataFlow usage
            if not self.dataflow_generated:
                self._generate_type_definitions(elem_id)
                self._generate_dataflow_usage(elem_id)
                self.dataflow_generated = True
        elif label == 'Return':
            self._generate_return(elem_id)
        elif label == 'Switch':
            self._generate_switch(elem_id)
    
    # ===================================================================
    # SECTION 9: Type Definition Generation
    # ===================================================================
    # Generates numpy type definitions from TypeAbstraction nodes.
    
    def _generate_type_definitions(self, use_type_id: str):
        """
        Generate type definitions from TypeAbstraction nodes.
        
        Example output:
        vector_ty = np.ndarray[(N,), np.dtype[np.int32]]
        
        Process:
        1. Find TypeAbstraction nodes in Symbols section
        2. Reconstruct type structure (ndarray, shape, dtype)
        3. Emit type assignment statements
        """
        # Find TypeAbstraction nodes in Symbols section
        module_nodes = self._find_nodes_by_kind('Module')
        if not module_nodes:
            return
        
        symbols_sections = []
        for child_id in self._get_children(module_nodes[0], 'contains'):
            if self._get_node_attr(child_id, 'kind') == 'Section':
                if self._get_node_attr(child_id, 'label') == 'Symbols':
                    symbols_sections.append(child_id)
        
        if not symbols_sections:
            return

        # Emit constant definitions first (so symbolic types can reference them)
        const_nodes = [c for c in self._get_children(symbols_sections[0], 'contains')
                       if self._get_node_attr(c, 'kind') == 'Const']
        if const_nodes:
            for child_id in const_nodes:
                const_name = self._get_node_attr(child_id, 'label')
                const_value = self._get_node_attr(child_id, 'value')
                if const_name and const_value is not None:
                    self._emit(f"{const_name} = {const_value}")
            self._emit()

        self._emit("# Define tensor types")

        # Find TypeAbstraction nodes
        for child_id in self._get_children(symbols_sections[0], 'contains'):
            if self._get_node_attr(child_id, 'kind') == 'TypeAbstraction':
                type_name = self._get_node_attr(child_id, 'label')
                type_def = self._reconstruct_type_definition(child_id)
                self._emit(f"{type_name} = {type_def}")

        self._emit()

        # Emit TensorTiler2D variable assignments (TAP definitions)
        tiler_nodes = [c for c in self._get_children(symbols_sections[0], 'contains')
                       if self._get_node_attr(c, 'kind') == 'TensorTiler2D']
        if tiler_nodes:
            self._emit("# Tensor access patterns")
            for tiler_id in tiler_nodes:
                self._emit_tiler2d_assignment(tiler_id)
            self._emit()
    
    def _reconstruct_type_definition(self, type_id: str) -> str:
        """Reconstruct type definition from TypeAbstraction node."""
        # Traverse type structure: TypeAbstraction -> ndarray -> shape/dtype
        ndarray_nodes = self._get_children(type_id, 'has')
        if not ndarray_nodes:
            return "type"
        
        # Find shape and dtype
        shape_str = None
        dtype_str = None
        
        for node_id in ndarray_nodes:
            label = self._get_node_attr(node_id, 'label')
            if label == 'ndarray':
                # Get shape and dtype children
                children = self._get_children(node_id, 'has')
                for child_id in children:
                    child_label = self._get_node_attr(child_id, 'label')
                    if child_label == 'shape':
                        shape_str = self._reconstruct_shape(child_id)
                    elif child_label == 'dtype':
                        dtype_str = self._reconstruct_dtype(child_id)
        
        if shape_str and dtype_str:
            return f"np.ndarray[{shape_str}, {dtype_str}]"
        return "np.ndarray"
    
    def _reconstruct_shape(self, shape_id: str) -> str:
        """Reconstruct shape tuple from shape node."""
        # shape -> tuple -> expr
        tuple_nodes = self._get_children(shape_id, 'has')
        if not tuple_nodes:
            return "()"

        elements = []
        for tuple_id in tuple_nodes:
            expr_nodes = self._get_children(tuple_id, 'contains')
            for expr_id in expr_nodes:
                # Reconstruct the full expression using generic reconstructor
                expr_str = self._reconstruct_expression(expr_id)
                # Strip outer parentheses from expressions (BinaryOp already adds them)
                if expr_str and expr_str.startswith('(') and expr_str.endswith(')'):
                    expr_str = expr_str[1:-1]
                # Shape dimensions must use integer division (numpy requires int, not float)
                if expr_str and '/' in expr_str:
                    expr_str = re.sub(r'(?<!/)/(?!/)', '//', expr_str)
                elements.append(expr_str if expr_str else "?")

        if len(elements) == 1:
            return f"({elements[0]},)"
        return f"({', '.join(elements)})"
    
    def _reconstruct_dtype(self, dtype_id: str) -> str:
        """Reconstruct dtype from dtype node."""
        # dtype -> NumpyDtype
        dtype_nodes = self._get_children(dtype_id, 'is')
        if dtype_nodes:
            dtype_label = self._get_node_attr(dtype_nodes[0], 'label')
            # Add np. prefix for numpy types (int32, float32, etc.), but not for ml_dtypes (bfloat16)
            if dtype_label and not dtype_label.startswith('np.') and dtype_label != 'bfloat16':
                dtype_label = f"np.{dtype_label}"
            return f"np.dtype[{dtype_label}]"
        return "np.dtype"

    def _emit_tiler2d_assignment(self, tiler_id: str):
        """
        Emit a TensorTiler2D.group_tiler() assignment statement.

        Example output:
            a_tap = TensorTiler2D.group_tiler(
                (n_fifo_elems, A_elem_size), (1, 512),
                (n_fifo_elems, A_elem_size // 512),
                prune_step=False,
            )[0]
        """
        name = self._get_node_attr(tiler_id, 'label')
        tensor_dims = self._get_node_attr(tiler_id, 'tensor_dims') or ""
        tile_dims = self._get_node_attr(tiler_id, 'tile_dims') or ""
        tile_counts = self._get_node_attr(tiler_id, 'tile_counts') or ""
        prune_step = self._get_node_attr(tiler_id, 'prune_step') or "False"
        index = self._get_node_attr(tiler_id, 'index') or "0"
        pattern_repeat = self._get_node_attr(tiler_id, 'pattern_repeat')

        args = []
        args.append(f"({tensor_dims})")
        args.append(f"({tile_dims})")
        args.append(f"({tile_counts})")
        if pattern_repeat:
            args.append(f"pattern_repeat={pattern_repeat}")
        args.append(f"prune_step={prune_step}")

        args_str = ", ".join(args)
        self._emit(f"{name} = TensorTiler2D.group_tiler({args_str})[{index}]")

    # ===================================================================
    # SECTION 10: DataFlow Generation
    # ===================================================================
    # Generates IRON-specific runtime elements.
    
    def _generate_dataflow_usage(self, use_symbol_id: str):
        """
        Generate DataFlow elements usage.
        
        Generates:
        - ObjectFifo declarations
        - ExternalFunction declarations
        - CoreFunction definitions
        - Worker declarations
        - Runtime instance
        - SequenceBlock with operations
        - Program creation
        - Placer instantiation
        
        This is IRON-specific code for AIE programming.
        """
        # Find DataFlow section
        module_nodes = self._find_nodes_by_kind('Module')
        if not module_nodes:
            return
        
        dataflow_sections = []
        for child_id in self._get_children(module_nodes[0], 'contains'):
            if self._get_node_attr(child_id, 'kind') == 'Section':
                if self._get_node_attr(child_id, 'label') == 'DataFlow':
                    dataflow_sections.append(child_id)
        
        if not dataflow_sections:
            return
        
        dataflow_id = dataflow_sections[0]
        
        # Generate ObjectFifos
        self._emit("# Data movement with ObjectFifos")
        objectfifo_nodes = [c for c in self._get_children(dataflow_id, 'contains')
                           if self._get_node_attr(c, 'kind') == 'ObjectFifo']
        
        for of_id in objectfifo_nodes:
            of_name = self._get_node_attr(of_id, 'label')

            # Check for source expression
            source_nodes = self._get_children(of_id, 'source_expr')
            if source_nodes:
                source_id = source_nodes[0]
                source_kind = self._get_node_attr(source_id, 'kind')

                # Handle MethodChain nodes
                if source_kind == 'MethodChain':
                    source_expr = self._reconstruct_method_chain(source_id)
                    if source_expr:
                        self._emit(f"{of_name} = {source_expr}")
                else:
                    source_expr = self._reconstruct_call(source_id)
                    if source_expr:
                        self._emit(f"{of_name} = {source_expr}")
            else:
                # Get type and kwargs
                type_nodes = self._get_children(of_id, 'uses_type')
                type_name = self._get_node_attr(type_nodes[0], 'label') if type_nodes else "type"
                
                kwarg_nodes = self._get_children(of_id, 'has_kwarg')
                kwargs = []
                for kw_id in kwarg_nodes:
                    name = self._get_node_attr(kw_id, 'name')
                    value = self._get_node_attr(kw_id, 'value')

                    # Convert numeric strings to integers (no quotes)
                    if value and value.isdigit():
                        kwargs.append(f'{name}={value}')
                    elif name == 'name':
                        kwargs.append(f'{name}="{value}"')
                    else:
                        kwargs.append(f'{name}="{value}"')
                
                kwargs_str = ", ".join(kwargs)
                if kwargs_str:
                    self._emit(f"{of_name} = ObjectFifo(obj_type={type_name}, {kwargs_str})")
                else:
                    self._emit(f"{of_name} = ObjectFifo(obj_type={type_name})")
        
        self._emit()
        
        # Generate ExternalFunctions
        ext_func_nodes = [c for c in self._get_children(dataflow_id, 'contains')
                         if self._get_node_attr(c, 'kind') == 'ExternalFunction']
        
        if ext_func_nodes:
            self._emit("#Define kernels here... ------------------------------------------------\\/")
            for ef_id in ext_func_nodes:
                # Use extension if available
                if hasattr(self, '_generate_ext_externalfunction'):
                    code = self._generate_ext_externalfunction(ef_id)
                    self._emit(code)
                    self._emit()
        
        # Generate CoreFunctions
        core_func_nodes = [c for c in self._get_children(dataflow_id, 'contains')
                          if self._get_node_attr(c, 'kind') == 'CoreFunction']
        
        if core_func_nodes:
            self._emit("# core_fn here:")
            for cf_id in core_func_nodes:
                # Use extension if available
                if hasattr(self, '_generate_ext_corefunction'):
                    code = self._generate_ext_corefunction(cf_id)
                    self._emit(code)
                    self._emit()
        
        # Generate Workers
        worker_nodes = [c for c in self._get_children(dataflow_id, 'contains')
                       if self._get_node_attr(c, 'kind') == 'Worker']
        
        if worker_nodes:
            self._emit("#Workers defined here:")
            self._emit("Workers = []")
            for w_id in worker_nodes:
                # Use extension if available
                if hasattr(self, '_generate_ext_worker'):
                    code = self._generate_ext_worker(w_id)
                    self._emit(code)
            self._emit()
        
        # Generate Lists (like Workers list)
        list_nodes = [c for c in self._get_children(dataflow_id, 'contains')
                     if self._get_node_attr(c, 'kind') == 'List']
        
        for l_id in list_nodes:
            # Use extension if available
            if hasattr(self, '_generate_ext_list'):
                code = self._generate_ext_list(l_id)
                self._emit(code)
        
        if list_nodes:
            self._emit()
        
        # Generate Runtime
        self._emit("# Runtime operations to move data to/from the AIE-array")
        runtime_nodes = [c for c in self._get_children(dataflow_id, 'contains') 
                        if self._get_node_attr(c, 'kind') == 'Runtime']
        
        for rt_id in runtime_nodes:
            rt_name = self._get_node_attr(rt_id, 'label')
            self._emit(f"{rt_name} = Runtime()")
        
        # Generate SequenceBlock
        seq_nodes = [c for c in self._get_children(dataflow_id, 'contains') 
                    if self._get_node_attr(c, 'kind') == 'SequenceBlock']
        
        for seq_id in seq_nodes:
            self._generate_sequence_block(seq_id)
        
        # Generate Program
        self._emit("# Create the program from the current device and runtime")
        program_nodes = [c for c in self._get_children(dataflow_id, 'contains') 
                        if self._get_node_attr(c, 'kind') == 'Program']
        
        for prog_id in program_nodes:
            prog_name = self._get_node_attr(prog_id, 'label')
            self._emit(f"{prog_name} = Program(iron.get_current_device(), rt)")
        
        self._emit()
        
        # Generate Placer
        self._emit("# Place components and resolve program (generate MLIR + compile)")
        placer_nodes = [c for c in self._get_children(dataflow_id, 'contains') 
                       if self._get_node_attr(c, 'kind') == 'Placer']
        
        for placer_id in placer_nodes:
            placer_name = self._get_node_attr(placer_id, 'label')
            self._emit(f"{placer_name} = SequentialPlacer()")
    
    def _generate_sequence_block(self, seq_id: str):
        """
        Generate with rt.sequence(...) block.
        
        Creates context manager for sequenced operations:
        with rt.sequence(type1, type2) as (binding1, binding2):
            rt.fill(...)
            rt.drain(...)
        """
        # Get bindings
        binding_nodes = self._get_children(seq_id, 'binds')
        bindings = []
        for bind_id in binding_nodes:
            bind_name = self._get_node_attr(bind_id, 'label')
            bindings.append(bind_name)
        
        bindings_str = ", ".join(bindings)
        
        # Get type references from bindings
        type_refs = []
        for bind_id in binding_nodes:
            type_ref = self._get_node_attr(bind_id, 'type_ref')
            if type_ref:
                type_refs.append(type_ref)
        
        type_args = ", ".join(type_refs) if type_refs else "vector_ty, vector_ty"
        
        self._emit(f"with rt.sequence({type_args}) as ({bindings_str}):")
        
        # Generate operations
        self.indent_level += 1
        op_nodes = self._get_children(seq_id, 'contains')
        for op_id in op_nodes:
            if self._get_node_attr(op_id, 'kind') == 'Operation':
                self._generate_operation(op_id)
        self.indent_level -= 1
        self._emit()
    
    def _generate_operation(self, op_id: str):
        """
        Generate operation (fill/drain).

        Generates: rt.operation(args, kwargs)
        """
        # Get the operation name from the target method, not the Operation label
        target_nodes = self._get_children(op_id, 'target')
        op_name = None
        if target_nodes:
            # Check if target is a MethodCall node with direct name attribute
            target_kind = self._get_node_attr(target_nodes[0], 'kind')
            if target_kind == 'MethodCall':
                op_name = self._get_node_attr(target_nodes[0], 'label')
            else:
                # This is a call to rt.fill() or rt.drain()
                # The target is a method call like rt.fill
                target_call = self._reconstruct_call(target_nodes[0])
                # Extract the method name (e.g., "fill" from "rt.fill()")
                if '.' in target_call:
                    op_name = target_call.split('.')[1].rstrip('()')

        if not op_name:
            # Fallback to using the label
            op_name = self._get_node_attr(op_id, 'label')

        # Get all arguments from has_arg edges
        arg_nodes = self._get_children(op_id, 'has_arg')
        args = []

        for arg_id in arg_nodes:
            arg_kind = self._get_node_attr(arg_id, 'kind')
            if arg_kind == 'Call':
                # This is a method call like of_in.prod()
                arg_expr = self._reconstruct_call(arg_id)
                args.append(arg_expr)
            elif arg_kind in ['Variable', 'VarRef', 'Binding']:
                # This is a variable reference like a_in
                arg_label = self._get_node_attr(arg_id, 'label')
                # Check if this is a list that needs unpacking
                if arg_label == 'Workers' and op_name == 'start':
                    args.append(f"*{arg_label}")
                else:
                    args.append(arg_label)
            elif arg_kind == 'List':
                # This is a list node - check if it needs unpacking
                list_label = self._get_node_attr(arg_id, 'label')
                if list_label and op_name == 'start':
                    args.append(f"*{list_label}")
                else:
                    args.append(list_label if list_label else "list")
            else:
                # Try to reconstruct as expression
                arg_expr = self._reconstruct_expression(arg_id)
                args.append(arg_expr)

        # Get kwargs
        kwarg_nodes = self._get_children(op_id, 'has_kwarg')
        for kw_id in kwarg_nodes:
            name = self._get_node_attr(kw_id, 'name')
            value = self._get_node_attr(kw_id, 'value')

            # Check if kwarg has complex value (constructor, list, etc.)
            value_nodes = self._get_children(kw_id, 'contains')
            if value_nodes:
                # Reconstruct complex value
                value_expr = self._reconstruct_expression(value_nodes[0])
                args.append(f"{name}={value_expr}")
            elif value:
                # Simple value - check if it needs quotes or special handling
                if value == 'True' or value == 'False':
                    args.append(f"{name}={value}")
                elif value == 'None':
                    args.append(f"{name}={value}")
                else:
                    args.append(f"{name}={value}")

        args_str = ", ".join(args)
        self._emit(f"rt.{op_name}({args_str})")
    
    def _generate_return(self, return_id: str):
        """
        Generate return statement.
        
        Looks for ResolveProgram in DataFlow to generate:
        return my_program.resolve_program(placer)
        """
        # Find ResolveProgram in DataFlow
        module_nodes = self._find_nodes_by_kind('Module')
        if module_nodes:
            dataflow_sections = []
            for child_id in self._get_children(module_nodes[0], 'contains'):
                if self._get_node_attr(child_id, 'kind') == 'Section':
                    if self._get_node_attr(child_id, 'label') == 'DataFlow':
                        dataflow_sections.append(child_id)
            
            if dataflow_sections:
                resolve_nodes = [c for c in self._get_children(dataflow_sections[0], 'contains')
                               if self._get_node_attr(c, 'kind') == 'ResolveProgram']
                if resolve_nodes:
                    # Get the target method call
                    target_nodes = self._get_children(resolve_nodes[0], 'target')
                    if target_nodes:
                        # Reconstruct the method call with its arguments
                        method_call = self._reconstruct_call(target_nodes[0])
                        if method_call:
                            self._emit(f"return {method_call}")
                            return
                    # Fallback to hardcoded version
                    self._emit("return my_program.resolve_program(SequentialPlacer())")
                    return
        
        self._emit("return")
    
    def _generate_switch(self, switch_id: str):
        """
        Generate switch/case logic for device selection.
        
        Generates if/elif/else chain for device types.
        """
        # Generate if/elif/else chain for device selection
        self._emit('if device_name == "npu":')
        self.indent_level += 1
        self._emit("iron.set_current_device(NPU1Col1())")
        self.indent_level -= 1
        self._emit('elif device_name == "npu2":')
        self.indent_level += 1
        self._emit("iron.set_current_device(NPU2Col1())")
        self.indent_level -= 1
        self._emit('elif device_name == "xcvc1902":')
        self.indent_level += 1
        self._emit("iron.set_current_device(XCVC1902())")
        self.indent_level -= 1
        self._emit("else:")
        self.indent_level += 1
        self._emit('raise ValueError(f"[ERROR] Device name {device_name} is unknown")')
        self.indent_level -= 1
    
    # ===================================================================
    # SECTION 11: Control Flow Processing
    # ===================================================================
    # Handles if/else and for loops.
    
    def _process_if(self, if_id: str):
        """
        Process if statement with then/else branches.
        
        Key Design: Uses 'then' and 'else' edge types to distinguish branches.
        This enables proper branch separation without confusion.
        
        Generates:
        if condition:
            then_statements
        else:
            else_statements
        """
        # Get condition
        cond_text = self._get_node_attr(if_id, 'condition_text', '')
        cond_nodes = self._get_children(if_id, 'condition')
        
        if cond_text:
            condition = cond_text
        elif cond_nodes:
            condition = self._reconstruct_expression(cond_nodes[0])
        else:
            condition = "condition"
        
        self._emit(f"if {condition}:")
        
        # Process then branch
        self.indent_level += 1
        then_children = self._get_children(if_id, 'then')
        for child_id in then_children:
            self._process_statement(child_id)
        self.indent_level -= 1
        
        # Process else branch if it exists
        else_children = self._get_children(if_id, 'else')
        if else_children:
            self._emit("else:")
            self.indent_level += 1
            for child_id in else_children:
                self._process_statement(child_id)
            self.indent_level -= 1
    
    def _process_for(self, for_id: str):
        """
        Process for loop.
        
        Generates:
        for var in iterable:
            body_statements
        """
        label = self._get_node_attr(for_id, 'label', '')
        if label and label.startswith('for '):
            # Label contains full for statement
            self._emit(f"{label}:")
            self.indent_level += 1
            
            children = self._get_children(for_id, 'contains')
            for child_id in children:
                self._process_statement(child_id)
            
            self.indent_level -= 1
        else:
            # Fallback: generic for loop
            self._emit("for item in items:")
            self.indent_level += 1
            children = self._get_children(for_id, 'contains')
            for child_id in children:
                self._process_statement(child_id)
            self.indent_level -= 1
    
    def _process_comment(self, comment_id: str):
        """
        Process comment statement.
        
        Emits comment text as-is (already includes # prefix).
        """
        text = self._get_node_attr(comment_id, 'text', '')
        if text:
            self._emit(text)
    
    def _process_print(self, print_id: str):
        """
        Process print statement.
        
        Handles:
        - F-strings: print(f"text {var}")
        - Expressions: print("-" * 24)
        - Simple strings: print("text")
        """
        label = self._get_node_attr(print_id, 'label', '')
        
        # Check if it's a format string by looking at the label
        if label and label.startswith('print(f"'):
            # Already formatted, use as-is
            self._emit(label)
        elif label and label.startswith('"') and '{' in label:
            # This is an f-string format
            format_str = label.strip('"')
            self._emit(f'print(f"{format_str}")')
        elif label and 'print(' in label:
            self._emit(label)
        elif label == '"-" * 24':
            self._emit('print("-" * 24)')
        elif label:
            self._emit(f'print({label})')
        else:
            self._emit('print()')
    
    def _process_entrypoint(self, ep_id: str):
        """
        Process entry point.
        
        Generates:
        if __name__ == "__main__":
            main()
        """
        self._emit()
        self._emit('if __name__ == "__main__":')
        self.indent_level += 1
        self._emit("main()")
        self.indent_level -= 1


# ======================================================================
# CLI Entry Point
# ======================================================================
# Provides command-line interface for standalone code generation.

def main():
    """
    CLI entry point for code generation.
    
    Usage: python CodeGenerator.py <graphml_file>
    
    Reads GraphML file and generates Python code to output file.
    """
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <graphml_file>")
        sys.exit(1)
    
    graphml_path = Path(sys.argv[1])
    if not graphml_path.is_file():
        print(f"Error: File not found: {graphml_path}")
        sys.exit(1)
    
    generator = CodeGenerator(graphml_path)
    code = generator.generate()
    
    # Write to output file
    output_path = graphml_path.with_suffix('.py')
    output_path = output_path.parent / f"generated_{output_path.name}"
    
    with open(output_path, 'w') as f:
        f.write(code)
    
    print(f"Generated code -> {output_path}")
    print(f"\nGenerated {len(code.splitlines())} lines of code")


if __name__ == "__main__":
    main()


