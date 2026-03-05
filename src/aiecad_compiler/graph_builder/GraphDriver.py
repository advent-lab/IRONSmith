#!/usr/bin/env python3
"""
GraphDriver.py — COMPLETE & CORRECT GRAPH GENERATION

This module converts XML representations of IRON code into semantic graphs.
The graph structure captures all code elements, relationships, and control flow
in a format that enables pure graph-driven code generation without hardcoded values.

Architecture:
- XML Parser: Reads and validates XML structure
- Graph Builder: Creates NetworkX DiGraph with typed nodes and edges
- Symbol Table: Tracks variable/function scopes for reference resolution
- Expression Walker: Recursively processes nested expressions
- Type System: Captures type definitions and relationships
"""

import sys
from pathlib import Path
from typing import Dict, List
import networkx as nx
from lxml import etree
from extension.GraphExtender import register_extensions

# ----------------------------------------------------------------------
# Type Definitions
# ----------------------------------------------------------------------
NodeId = str  # Unique identifier for graph nodes
Graph = nx.DiGraph  # Directed graph structure

# ----------------------------------------------------------------------
# GraphBuilder Class - Main XML to Graph Converter
# ----------------------------------------------------------------------
class GraphBuilder:
    """
    Converts XML representation of IRON code into a semantic graph.
    
    The graph uses:
    - Nodes: Represent code elements (functions, variables, expressions, etc.)
    - Edges: Represent relationships (contains, depends_on, calls, etc.)
    - Attributes: Store metadata (labels, types, values, etc.)
    
    Key Design Principles:
    1. Pure Graph Structure: No hardcoded code generation logic
    2. Complete Information: All code details captured in graph
    3. Flexible Traversal: Graph can be walked in any order
    4. Type Safety: Explicit node kinds and edge types
    """
    
    def __init__(self, xml_path: Path):
        self.xml_path = xml_path
        self.graph: Graph = nx.DiGraph()
        self._counter = 0  # Unique ID generator
        self._symbol_table: Dict[str, NodeId] = {}  # Global scope
        self._scope_stack: List[Dict[str, NodeId]] = [self._symbol_table]  # Nested scopes

        #Bring in GraphExtender extensions 
        register_extensions(self)

    # ===================================================================
    # SECTION 1: Node and Edge Management
    # ===================================================================
    # These methods handle the creation and linking of graph nodes.
    # Each node has a unique ID, label, kind, and optional attributes.
    # Edges connect nodes with typed relationships (contains, calls, etc.)
    
    def _new_id(self, prefix: str = "node") -> NodeId:
        """Generate unique node identifier with semantic prefix."""
        self._counter += 1
        return f"{prefix}_{self._counter}"

    def _add_node(self, label: str, kind: str, **attrs) -> NodeId:
        """
        Create a new graph node with metadata.
        
        Args:
            label: Human-readable name (e.g., variable name, function name)
            kind: Node type (e.g., 'Function', 'Variable', 'Call')
            **attrs: Additional attributes (value, type_ref, etc.)
        
        Returns:
            NodeId: Unique identifier for the created node
        """
        clean_attrs = {k: v for k, v in attrs.items() if v is not None}
        nid = self._new_id(kind)
        self.graph.add_node(nid, label=label, kind=kind, **clean_attrs)
        return nid

    def _link(self, src: NodeId, dst: NodeId, edge_type: str = "depends_on"):
        """
        Create directed edge between nodes with typed relationship.
        
        Edge types define semantic relationships:
        - 'contains': Parent-child structural relationship
        - 'calls': Function/method invocation
        - 'depends_on': Data dependency
        - 'uses_type': Type reference
        - 'source': Assignment source
        - 'condition': Conditional expression
        - 'then'/'else': Branch statements
        """
        if src and dst and edge_type:
            self.graph.add_edge(src, dst, type=edge_type)

    # ===================================================================
    # SECTION 2: Symbol Table and Scope Management
    # ===================================================================
    # Manages variable/function scopes for reference resolution.
    # Supports nested scopes (functions, loops, blocks) with proper shadowing.
    # Critical for linking variable references to their declarations.
    
    def _declare_symbol(self, name: str, nid: NodeId):
        """
        Register a symbol (variable, function, type) in current scope.
        
        Handles:
        - New declarations in current scope
        - Shadowing of outer scope symbols
        - Preventing duplicate declarations in same scope
        """
        if name is None:
            return
        # Check if symbol exists in outer scopes (shadowing)
        for scope in reversed(self._scope_stack[:-1]):
            if name in scope:
                self._scope_stack[-1][name] = nid
                return
        # Prevent duplicate in current scope
        if name in self._scope_stack[-1]:
            return
        # Register new symbol
        self._scope_stack[-1][name] = nid

    def _lookup(self, name: str) -> NodeId:
        """
        Resolve symbol name to its node ID.
        
        Searches scopes from innermost to outermost (lexical scoping).
        Raises NameError if symbol not found.
        """
        if not name:
            raise NameError("Empty name in lookup")
        for scope in reversed(self._scope_stack):
            if name in scope:
                return scope[name]
        raise NameError(f"Undefined symbol: {name}")

    # ===================================================================
    # SECTION 3: Main Build Process
    # ===================================================================
    # Entry point for XML to graph conversion.
    # Validates XML structure and dispatches to section-specific handlers.
    
    def build(self) -> Graph:
        """
        Parse XML and build complete semantic graph.
        
        Process:
        1. Parse and validate XML structure
        2. Create root Module node
        3. Dispatch to section handlers (Symbols, DataFlow, Function, etc.)
        4. Return complete graph
        
        Returns:
            Graph: NetworkX DiGraph with all code elements and relationships
        """
        tree = etree.parse(str(self.xml_path))
        root = tree.getroot()
        if root.tag != "Module":
            raise ValueError("Root must be <Module>")

        module_name = root.get("name", "unnamed")
        module_nid = self._add_node(module_name, "Module")
        self._declare_symbol(module_name, module_nid)

        # Process each top-level section
        for section in root:
            if section.tag is etree.Comment:
                continue
            tag = str(section.tag)
            # Dynamic dispatch to section handler
            handler = getattr(self, f"_process_{tag.lower()}", None)
            if handler:
                handler(section, module_nid)
        return self.graph

    # ===================================================================
    # SECTION 4: Symbols Section Processing
    # ===================================================================
    # Handles imports, constants, and type definitions.
    # These are global declarations used throughout the code.
    
    def _process_symbols(self, elem: etree.Element, parent_nid: NodeId):
        """
        Process Symbols section containing imports, constants, and types.
        
        Creates:
        - Import nodes for module imports
        - Const nodes for constant values
        - TypeAbstraction nodes for type definitions
        """
        sec = self._add_node("Symbols", "Section")
        self._link(parent_nid, sec, "contains")
        for child in elem:
            if child.tag is etree.Comment:
                continue
            tag = str(child.tag)
            # Dispatch to symbol-specific handler
            h = getattr(self, f"_symbol_{tag.lower()}", None)
            if h:
                h(child, sec)

    def _symbol_const(self, e: etree.Element, p: NodeId):
        """Create Const node for constant declarations (e.g., N = 4096)."""
        name = e.get("name")
        value = e.text.strip() if e.text else "?"
        n = self._add_node(name, "Const", value=value)
        self._link(p, n, "contains")
        self._declare_symbol(name, n)

    def _symbol_typeabstraction(self, e: etree.Element, p: NodeId):
        """
        Create TypeAbstraction node for type definitions.
        
        Example: vector_ty = np.ndarray[(N,), np.dtype[np.int32]]
        Creates hierarchical structure: TypeAbstraction -> ndarray -> shape/dtype
        """
        name = e.get("name")
        n = self._add_node(name, "TypeAbstraction")
        self._link(p, n, "contains")
        self._declare_symbol(name, n)
        self._walk_type_def(e, n)

    def _walk_type_def(self, e: etree.Element, p: NodeId):
        """
        Recursively walk type definition structure.
        
        Handles:
        - ndarray: Array type container
        - shape: Tuple of dimensions
        - dtype: Data type specification
        - expr: Dimension expressions (e.g., N, N*2)
        """
        for c in e:
            if c.tag is etree.Comment:
                continue
            t = str(c.tag)
            if t in ("ndarray", "shape", "tuple", "dtype"):
                nd = self._add_node(t, "TypeNode")
                self._link(p, nd, "has")
                self._walk_type_def(c, nd)
            elif t == "expr":
                # Check if expr has child elements (method, var, binary_op, etc.)
                expr_children = [child for child in c if child.tag is not etree.Comment]
                if expr_children:
                    # Create Expr node and process children
                    nd = self._add_node("", "Expr")
                    self._link(p, nd, "contains")
                    # Process the first child as the expression content
                    child_node = self._walk_expression(expr_children[0], nd)
                    if child_node:
                        self._link(nd, child_node, "contains")
                else:
                    # Fallback to text content
                    txt = c.findtext("var") or c.text or "?"
                    nd = self._add_node(txt.strip(), "Expr")
                    self._link(p, nd, "contains")
            elif t == "numpy_dtype":
                txt = c.text.strip() if c.text else "unknown"
                nd = self._add_node(txt, "NumpyDtype")
                self._link(p, nd, "is")

    def _symbol_import(self, e: etree.Element, p: NodeId):
        """Create Import node for module imports (e.g., import numpy as np)."""
        name = e.get("name")
        alias = e.get("alias")
        n = self._add_node(name, "Import", alias=alias)
        self._link(p, n, "contains")
        self._declare_symbol(name, n)

    def _symbol_tensortiler2d(self, e: etree.Element, p: NodeId):
        """
        Create TensorTiler2D node for tiler variable declarations.

        Example: a_tap = TensorTiler2D.group_tiler(tensor_dims, tile_dims, tile_counts,
                             prune_step=False)[0]
        """
        name = e.get("name")
        n = self._add_node(name, "TensorTiler2D",
                           tensor_dims=e.get("tensor_dims", ""),
                           tile_dims=e.get("tile_dims", ""),
                           tile_counts=e.get("tile_counts", ""),
                           prune_step=e.get("prune_step", "False"),
                           index=e.get("index", "0"),
                           pattern_repeat=e.get("pattern_repeat"))
        self._link(p, n, "contains")
        self._declare_symbol(name, n)

    # ===================================================================
    # SECTION 5: DataFlow Section Processing
    # ===================================================================
    # Handles IRON-specific data movement and runtime constructs.
    # Includes ObjectFifos, Runtime, SequenceBlocks, and Programs.
    
    def _process_dataflow(self, elem: etree.Element, parent_nid: NodeId):
        """
        Process DataFlow section containing IRON runtime elements.
        
        DataFlow elements:
        - ObjectFifo: Data buffers for AIE communication
        - Runtime: Runtime operations (fill/drain)
        - SequenceBlock: Sequenced operations with bindings
        - Program: Compiled program representation
        - Placer: Component placement strategy
        - ExternalFunction, CoreFunction, Worker, List: Extension-handled nodes
        """
        sec = self._add_node("DataFlow", "Section")
        self._link(parent_nid, sec, "contains")
        for c in elem:
            if c.tag is etree.Comment:
                continue
            t = str(c.tag)
            # First try dataflow-specific handler
            h = getattr(self, f"_df_{t.lower()}", None)
            if h:
                h(c, sec)
            else:
                # Try extension handler
                ext_h = getattr(self, f"_process_ext_{t.lower()}", None)
                if ext_h:
                    ext_h(c, sec)

    def _df_objectfifo(self, e: etree.Element, p: NodeId):
        """
        Create ObjectFifo node for data buffers.
        
        ObjectFifos manage data movement between AIE tiles.
        Captures:
        - Type reference (links to TypeAbstraction)
        - Attributes (depth, name, etc.)
        - Source expression (for derived fifos like of_in.cons().forward())
        """
        name = e.get("name")
        n = self._add_node(name, "ObjectFifo")
        self._link(p, n, "contains")
        self._declare_symbol(name, n)
        
        for sub in e:
            if sub.tag == "obj_type":
                # Link to type definition
                type_ref_elem = sub.find("type_ref")
                if type_ref_elem is not None and type_ref_elem.text:
                    ref = type_ref_elem.text.strip()
                    try:
                        type_node = self._lookup(ref)
                        self._link(n, type_node, "uses_type")
                    except NameError:
                        pass
                else:
                    # Fallback to old symbol format
                    ref = sub.findtext("symbol")
                    if ref:
                        try:
                            type_node = self._lookup(ref)
                            self._link(n, type_node, "uses_type")
                        except NameError:
                            pass
            elif sub.tag == "attributes":
                # Capture keyword arguments
                for kw in sub:
                    if kw.tag == "kwarg":
                        kw_n = self._add_node(f"{kw.get('name')}={kw.get('value')}", "Kwarg",
                                            name=kw.get("name"), value=kw.get("value"))
                        self._link(n, kw_n, "has_kwarg")
            elif sub.tag == "source":
                # Handle method chains for derived fifos
                method_chain = sub.find("method_chain")
                if method_chain is not None:
                    # Try to use extension handler if available (for kwargs support)
                    ext_handler = getattr(self, '_process_ext_worker', None)
                    if ext_handler:
                        # Use WorkerExtension's _walk_method_chain which handles kwargs
                        from extension.GraphExtender import WorkerExtension
                        worker_ext = WorkerExtension(self)
                        chain_expr = worker_ext._walk_method_chain(method_chain, n)
                    else:
                        chain_expr = self._walk_method_chain(method_chain, n)
                    if chain_expr:
                        self._link(n, chain_expr, "source_expr")
                else:
                    # Fallback to old call format
                    source_call = sub.find("call")
                    if source_call is not None:
                        source_expr = self._walk_call_chain(source_call, n)
                        self._link(n, source_expr, "source_expr")

    def _df_runtime(self, e: etree.Element, p: NodeId):
        name = e.get("name")
        n = self._add_node(name, "Runtime")
        self._link(p, n, "contains")
        self._declare_symbol(name, n)

    def _df_sequenceblock(self, e: etree.Element, p: NodeId):
        n = self._add_node("SequenceBlock", "SequenceBlock")
        self._link(p, n, "contains")

        # Context: rt.sequence(...)
        ctx_call = e.find("context/call")
        if ctx_call is not None:
            ctx_n = self._walk_call_chain(ctx_call, n)
            self._link(n, ctx_n, "context")

        # Bindings
        binds = e.find("bindings")
        if binds is not None:
            for b in binds:
                if b.tag == "bind":
                    bn = self._add_node(b.get("name"), "Binding", type_ref=b.get("type_ref"))
                    self._link(n, bn, "binds")
                    self._declare_symbol(b.get("name"), bn)

        # Body: fill/drain
        body = e.find("body")
        if body is not None:
            for op in body:
                if op.tag == "Operation":
                    self._df_operation(op, n)

    def _df_operation(self, e: etree.Element, p: NodeId):
        name = e.get("name")
        op = self._add_node(name, "Operation")
        self._link(p, op, "contains")

        # Target call chain or method
        target_call = e.find("target/call")
        target_method = e.find("target/method")

        if target_call is not None:
            tgt_call = self._walk_call_chain(target_call, op)
            self._link(op, tgt_call, "target")
        elif target_method is not None:
            # Handle <target><method ref="rt" name="fill"/></target>
            method_name = target_method.get("name")
            method_ref = target_method.get("ref")
            if method_name:
                method_node = self._add_node(method_name, "MethodCall", object_ref=method_ref)
                self._link(op, method_node, "target")

        # Args - handle new structure with <arg> elements
        args = e.find("args")
        if args is not None:
            for arg in args:
                arg_tag = str(arg.tag)
                if arg_tag == "arg":
                    # Process the argument content
                    call_elem = arg.find("call")
                    var_elem = arg.find("var")
                    if call_elem is not None:
                        arg_n = self._walk_call_chain(call_elem, op)
                        self._link(op, arg_n, "has_arg")
                    elif var_elem is not None:
                        var_ref = var_elem.get("ref")
                        if var_ref:
                            try:
                                var_node = self._lookup(var_ref)
                                self._link(op, var_node, "has_arg")
                            except NameError:
                                var_node = self._add_node(var_ref, "VarRef")
                                self._link(op, var_node, "has_arg")
                elif arg_tag in ("producer", "consumer", "input", "output"):
                    chain = arg.find("call")
                    if chain is not None:
                        chain_n = self._walk_call_chain(chain, op)
                        self._link(op, chain_n, arg_tag)
                elif arg_tag == "kwarg":
                    kw_name = arg.get("name")
                    kw_value = arg.get("value")

                    # Check for child elements (constructor, list, var, method, etc.)
                    kw_children = [c for c in arg if c.tag is not etree.Comment]

                    if kw_children:
                        # Check for var + method pattern (e.g., <var ref="obj"/>.<method name="prod"/>)
                        if (len(kw_children) == 2 and
                            kw_children[0].tag == "var" and
                            kw_children[1].tag == "method"):
                            # This is a method call pattern: obj.method()
                            var_ref = kw_children[0].get("ref")
                            method_name = kw_children[1].get("name")

                            kw_n = self._add_node(f"{kw_name}=...", "Kwarg", name=kw_name)
                            self._link(op, kw_n, "has_kwarg")

                            # Create a Call node that represents var.method()
                            call_node = self._add_node(f"{var_ref}.{method_name}", "Call")
                            self._link(kw_n, call_node, "contains")

                            # Add the method as a child
                            method_node = self._add_node(method_name, "MethodCall", object_ref=var_ref)
                            self._link(call_node, method_node, "calls")
                        else:
                            # Complex kwarg with child elements
                            kw_n = self._add_node(f"{kw_name}=...", "Kwarg", name=kw_name)
                            self._link(op, kw_n, "has_kwarg")

                            # Process child elements
                            for child in kw_children:
                                child_node = self._process_kwarg_child(child, kw_n)
                                if child_node:
                                    self._link(kw_n, child_node, "contains")
                    else:
                        # Simple kwarg with value attribute
                        kw_n = self._add_node(f"{kw_name}={kw_value}", "Kwarg", name=kw_name, value=kw_value)
                        self._link(op, kw_n, "has_kwarg")

    def _process_kwarg_child(self, elem: etree.Element, parent_nid: NodeId) -> NodeId | None:
        """Process child elements within kwargs (constructor, list, var, method, etc.)"""
        tag = elem.tag

        if tag == "constructor":
            # Handle <constructor ref="Tile">...</constructor>
            ref = elem.get("ref")
            ctor_nid = self._add_node(ref, "Constructor")

            # Process constructor arguments
            for arg in elem.findall("arg"):
                arg_children = [c for c in arg if c.tag is not etree.Comment]
                for child in arg_children:
                    if child.tag == "const":
                        const_val = child.text
                        const_nid = self._add_node(const_val, "ConstExpr", value=const_val)
                        self._link(ctor_nid, const_nid, "has_arg")
                    elif child.tag == "var":
                        var_ref = child.get("ref")
                        try:
                            var_node = self._lookup(var_ref)
                            self._link(ctor_nid, var_node, "has_arg")
                        except NameError:
                            var_node = self._add_node(var_ref, "VarRef")
                            self._link(ctor_nid, var_node, "has_arg")

            # Process constructor kwargs
            for kwarg in elem.findall("kwarg"):
                kw_name = kwarg.get("name")
                kw_children = [c for c in kwarg if c.tag is not etree.Comment]
                if kw_children:
                    kw_nid = self._add_node(f"{kw_name}=...", "Kwarg", name=kw_name)
                    self._link(ctor_nid, kw_nid, "has_kwarg")
                    for child in kw_children:
                        child_node = self._process_kwarg_child(child, kw_nid)
                        if child_node:
                            self._link(kw_nid, child_node, "contains")

            return ctor_nid

        elif tag == "list":
            # Handle <list>...</list>
            list_nid = self._add_node("list", "List")
            for item in elem:
                if item.tag == "const":
                    const_val = item.text
                    const_nid = self._add_node(const_val, "ConstExpr", value=const_val)
                    self._link(list_nid, const_nid, "contains")
                elif item.tag == "method":
                    method_name = item.get("name")
                    method_ref = item.get("ref")
                    method_nid = self._add_node(method_name, "MethodCall", object_ref=method_ref)
                    self._link(list_nid, method_nid, "contains")
                elif item.tag == "binary_op":
                    binop_nid = self._process_binary_op(item, list_nid)
                    if binop_nid:
                        self._link(list_nid, binop_nid, "contains")
            return list_nid

        elif tag == "const":
            # Handle <const>value</const>
            const_val = elem.text
            return self._add_node(const_val, "ConstExpr", value=const_val)

        elif tag == "string":
            # Handle <string>"value"</string> or <string>value</string>
            string_val = elem.text
            return self._add_node(string_val, "String", value=string_val)

        elif tag == "var":
            # Handle <var ref="..."/>
            var_ref = elem.get("ref")
            try:
                return self._lookup(var_ref)
            except NameError:
                return self._add_node(var_ref, "VarRef")

        elif tag == "method":
            # Handle <method ref="..." name="..."/>
            method_name = elem.get("name")
            method_ref = elem.get("ref")
            return self._add_node(method_name, "MethodCall", object_ref=method_ref)

        elif tag == "binary_op":
            return self._process_binary_op(elem, parent_nid)

        return None

    def _process_binary_op(self, elem: etree.Element, parent_nid: NodeId) -> NodeId | None:
        """Process binary operation elements"""
        op = elem.get("op", "+")
        binop_nid = self._add_node(f"{op}", "BinaryOp", op=op)

        # Process operands
        children = [c for c in elem if c.tag is not etree.Comment]
        if len(children) >= 1:
            left_nid = self._process_kwarg_child(children[0], binop_nid)
            if left_nid:
                self._link(binop_nid, left_nid, "lhs")
        if len(children) >= 2:
            right_nid = self._process_kwarg_child(children[1], binop_nid)
            if right_nid:
                self._link(binop_nid, right_nid, "rhs")

        return binop_nid

    def _df_program(self, e: etree.Element, p: NodeId):
        name = e.get("name")
        n = self._add_node(name, "Program")
        self._link(p, n, "contains")
        self._declare_symbol(name, n)

        ctor = e.find("constructor/call")
        if ctor is not None:
            ctor_call = self._walk_call_chain(ctor, n)
            self._link(n, ctor_call, "constructed_by")

    def _df_placer(self, e: etree.Element, p: NodeId):
        name = e.get("name")
        n = self._add_node(name, "Placer")
        self._link(p, n, "contains")
        self._declare_symbol(name, n)

    def _df_resolveprogram(self, e: etree.Element, p: NodeId):
        n = self._add_node("resolve_program", "ResolveProgram")
        self._link(p, n, "contains")
        target = e.find("target/method")
        if target is not None:
            tgt_call = self._walk_call_chain(target, n)
            self._link(n, tgt_call, "target")

    # ===================================================================
    # SECTION 6: Method Chain and Call Processing
    # ===================================================================
    # Handles method chains (obj.method1().method2()) and function calls.
    # Critical for capturing IRON's fluent API patterns.
    
    def _walk_method_chain(self, e: etree.Element, p: NodeId) -> NodeId | None:
        """
        Process method chains like of_in.cons().forward().
        
        Creates linked Call nodes with 'nested_call' edges to preserve order.
        Uses temporary parent to avoid polluting graph structure.
        
        Returns:
            NodeId: First call in the chain (entry point for traversal)
        """
        calls = e.findall("call")
        if not calls:
            return None
        
        # Create temporary parent to avoid linking to wrong parent
        chain_parent = self._add_node("method_chain_temp", "TempNode")
        
        # Process first call
        first_call = self._walk_call_chain(calls[0], chain_parent)
        prev_call = first_call
        
        # Chain subsequent calls with nested_call edges
        for call_elem in calls[1:]:
            next_call = self._walk_call_chain(call_elem, chain_parent)
            self._link(prev_call, next_call, "nested_call")
            prev_call = next_call
        
        # Clean up temporary node
        self.graph.remove_node(chain_parent)
        
        return first_call
    
    def _walk_call_chain(self, e: etree.Element, p: NodeId) -> NodeId:
        """
        Process a single call (method or function) with arguments.
        
        Handles:
        - Method calls: obj.method(args)
        - Function calls: func(args)
        - Arguments: positional and keyword
        - Nested expressions in arguments
        
        Returns:
            NodeId: Call node with all arguments and kwargs linked
        """
        call = self._add_node("call", "Call")
        self._link(p, call, "contains")

        # Find method
        method = e.find("method")
        if method is not None:
            obj_ref = method.get("ref")
            method_name = method.get("name")
            if obj_ref and method_name:
                try:
                    obj_n = self._lookup(obj_ref)
                    mn = self._add_node(method_name, "MethodCall")
                    self._link(call, mn, "calls")
                    self._link(mn, obj_n, "object")
                except NameError:
                    pass
            elif method_name:
                # Method without object reference (chained call)
                mn = self._add_node(method_name, "MethodCall")
                self._link(call, mn, "calls")

        # Find function
        function = e.find("function")
        if function is not None:
            func_name = function.get("name") or function.get("ref")
            if func_name:
                fn = self._add_node(func_name, "FunctionCall")
                self._link(call, fn, "calls")
                
                # Process arguments that are children of the function element
                for func_child in function:
                    if func_child.tag is etree.Comment:
                        continue
                    func_child_tag = str(func_child.tag)
                    if func_child_tag == "arg":
                        # Process argument content
                        arg_content = None
                        for child in func_child:
                            if child.tag is not etree.Comment:
                                arg_content = self._walk_expression(child, call)
                                break
                        if arg_content:
                            self._link(call, arg_content, "has_arg")
                    elif func_child_tag == "kwarg":
                        kw_name = func_child.get("name")
                        kw_value = func_child.get("value")
                        kw_children = [c for c in func_child if c.tag is not etree.Comment]
                        if kw_children:
                            kw_expr = self._walk_expression(kw_children[0], call)
                            kw_n = self._add_node(f"{kw_name}=...", "Kwarg", name=kw_name)
                            if kw_expr:
                                self._link(kw_n, kw_expr, "contains")
                            self._link(call, kw_n, "has_kwarg")
                        else:
                            kw_n = self._add_node(f"{kw_name}={kw_value}", "Kwarg", name=kw_name, value=kw_value)
                            self._link(call, kw_n, "has_kwarg")

        # Args
        for arg in e:
            arg_tag = str(arg.tag)
            if arg_tag == "arg":
                # Process argument content
                arg_content = None
                for child in arg:
                    if child.tag is not etree.Comment:
                        arg_content = self._walk_expression(child, call)
                        break
                if arg_content:
                    self._link(call, arg_content, "has_arg")
                else:
                    arg_n = self._add_node("arg", "Arg")
                    self._link(call, arg_n, "has_arg")
            elif arg_tag == "kwarg":
                kw_name = arg.get("name")
                kw_value = arg.get("value")
                # Check if kwarg has child elements
                kw_children = [c for c in arg if c.tag is not etree.Comment]
                if kw_children:
                    kw_expr = self._walk_expression(kw_children[0], call)
                    kw_n = self._add_node(f"{kw_name}=...", "Kwarg", name=kw_name)
                    if kw_expr:
                        self._link(kw_n, kw_expr, "contains")
                    self._link(call, kw_n, "has_kwarg")
                else:
                    kw_n = self._add_node(f"{kw_name}={kw_value}", "Kwarg", name=kw_name, value=kw_value)
                    self._link(call, kw_n, "has_kwarg")
            # Note: We don't process nested call/method here because method_chain handles that

        return call

    # ===================================================================
    # SECTION 7: Function Processing
    # ===================================================================
    # Handles function definitions, parameters, and body statements.
    # Manages function scope for local variables.
    
    def _process_function(self, e: etree.Element, p: NodeId):
        """
        Process function definition with decorator, parameters, and body.
        
        Creates new scope for function-local symbols.
        Handles:
        - Decorators (@iron.device)
        - Parameters with type annotations
        - Function body statements
        """
        name = e.get("name")
        nid = self._add_node(name, "Function", decorator=e.get("decorator"))
        self._link(p, nid, "contains")
        self._scope_stack.append({})
        self._declare_symbol(name, nid)

        params = e.find("parameters")
        if params is not None:
            for param in params:
                if param.tag == "param":
                    p_name = param.get("name")
                    p_n = self._add_node(p_name, "Parameter", type_ref=param.get("type_ref"))
                    self._link(nid, p_n, "has_param")
                    self._declare_symbol(p_name, p_n)

        body = e.find("body")
        if body is not None:
            self._process_function_body(body, nid)
        self._scope_stack.pop()

    def _process_function_body(self, e: etree.Element, p: NodeId):
        for c in e:
            if c.tag is etree.Comment:
                continue
            t = str(c.tag)
            h = getattr(self, f"_func_{t.lower()}", None)
            if h:
                h(c, p)
            else:
                self._func_generic(c, p)

    # ===================================================================
    # SECTION 8: Statement Processing
    # ===================================================================
    # Handles various statement types within function bodies.
    # Each statement type has specific graph representation.
    
    def _func_assign(self, e: etree.Element, p: NodeId):
        """
        Process assignment statement (target = source).
        
        Creates:
        - Assign node with target name
        - Variable node for target
        - Links to source expression
        - Symbol table entry for new variables
        """
        target = e.findtext("target")
        src_elem = e.find("source")
        src = self._walk_expression(src_elem, p) if src_elem is not None else None
        a = self._add_node(f"{target}=…", "Assign", target=target)
        self._link(p, a, "contains")
        if src:
            self._link(a, src, "source")

        v = self._add_node(target, "Variable")
        self._link(a, v, "assigns")

        try:
            existing = self._lookup(target)
            self._link(v, existing, "refers_to")
        except NameError:
            self._link(v, v, "defines")
            self._declare_symbol(target, v)

    def _func_assert(self, e: etree.Element, p: NodeId):
        cond = self._walk_expression(e.find("condition"), p)
        message = e.findtext("message", "")
        a = self._add_node("assert ...", "Assert", message=message)
        self._link(p, a, "contains")
        if cond:
            self._link(a, cond, "condition")

    # ===================================================================
    # SECTION 9: Expression Processing
    # ===================================================================
    # Recursively processes nested expressions into graph nodes.
    # Handles all Python expression types used in IRON code.
    
    def _walk_expression(self, e: etree.Element | None, p: NodeId) -> NodeId | None:
        """
        Recursively process expression into graph nodes.
        
        Handles:
        - Variables: Create VarRef nodes (not Const lookups)
        - Constants: Literal values
        - Binary operations: +, -, *, /, %
        - Comparisons: ==, !=, <, >, <=, >=
        - Function calls: func(args, kwargs)
        - Method calls: obj.method(args)
        - Index expressions: arr[idx]
        - Constructors: Type()
        
        Key Design: Always creates VarRef for variables in expressions
        to preserve variable names (not constant values) in generated code.
        """
        if e is None:
            return None
        tag = str(e.tag)
        
        if tag == "var":
            name = e.text.strip() if e.text else ""
            ref = e.get("ref")
            if ref:
                name = ref
            if name:
                # Always create a VarRef node for variables in expressions
                # This ensures we use the variable name, not the constant value
                var_node = self._add_node(name, "VarRef")
                return var_node
        
        if tag == "const":
            txt = e.text.strip() if e.text else "?"
            return self._add_node(txt, "ConstExpr", value=txt)
        
        if tag == "call":
            return self._walk_call_chain(e, p)
        
        if tag == "attribute":
            obj_ref = e.get("ref")
            attr_name = e.get("name")
            if obj_ref and attr_name:
                try:
                    obj_n = self._lookup(obj_ref)
                    attr = self._add_node(f"{obj_ref}.{attr_name}()", "MethodCallExpr",
                                        object_ref=obj_ref, method=attr_name)
                    self._link(attr, obj_n, "object")
                    return attr
                except NameError:
                    # Create placeholder
                    attr = self._add_node(f"{obj_ref}.{attr_name}()", "MethodCallExpr",
                                        object_ref=obj_ref, method=attr_name)
                    return attr
        
        if tag == "comparison":
            # New comparison format with op attribute
            op = e.get("op", "==")
            comp_node = self._add_node(f"comparison({op})", "ComparisonOp", operator=op)
            # Process left and right
            left_elem = e.find("left")
            right_elem = e.find("right")
            if left_elem is not None:
                for child in left_elem:
                    if child.tag is not etree.Comment:
                        left_expr = self._walk_expression(child, comp_node)
                        if left_expr:
                            self._link(comp_node, left_expr, "operand")
            if right_elem is not None:
                for child in right_elem:
                    if child.tag is not etree.Comment:
                        right_expr = self._walk_expression(child, comp_node)
                        if right_expr:
                            self._link(comp_node, right_expr, "operand")
            return comp_node
        
        if tag == "binary_op":
            op = e.get("op")
            bin_node = self._add_node(f"binary_op({op})", "BinaryOp", operator=op)
            # Process left and right if they exist
            left_elem = e.find("left")
            right_elem = e.find("right")
            if left_elem is not None:
                for child in left_elem:
                    if child.tag is not etree.Comment:
                        left_expr = self._walk_expression(child, bin_node)
                        if left_expr:
                            self._link(bin_node, left_expr, "operand")
            if right_elem is not None:
                for child in right_elem:
                    if child.tag is not etree.Comment:
                        right_expr = self._walk_expression(child, bin_node)
                        if right_expr:
                            self._link(bin_node, right_expr, "operand")
            
            # Fallback: process all children
            if left_elem is None and right_elem is None:
                for child in e:
                    if child.tag is not etree.Comment:
                        operand = self._walk_expression(child, bin_node)
                        if operand:
                            self._link(bin_node, operand, "operand")
            return bin_node
        
        if tag == "equals":
            eq_node = self._add_node("==", "ComparisonOp", operator="==")
            for child in e:
                if child.tag is not etree.Comment:
                    operand = self._walk_expression(child, eq_node)
                    if operand:
                        self._link(eq_node, operand, "operand")
            return eq_node
        
        if tag == "index":
            # Handle array indexing like sys.argv[1]
            idx_node = self._add_node("[]", "IndexExpr")
            # Check for base/index_value structure
            base_elem = e.find("base")
            index_val_elem = e.find("index_value")
            if base_elem is not None:
                for child in base_elem:
                    if child.tag is not etree.Comment:
                        base_expr = self._walk_expression(child, idx_node)
                        if base_expr:
                            self._link(idx_node, base_expr, "base")
            if index_val_elem is not None:
                for child in index_val_elem:
                    if child.tag is not etree.Comment:
                        index_expr = self._walk_expression(child, idx_node)
                        if index_expr:
                            self._link(idx_node, index_expr, "index")
            
            # Fallback to old format
            if base_elem is None and index_val_elem is None:
                for child in e:
                    if child.tag is not etree.Comment:
                        part = self._walk_expression(child, idx_node)
                        if part:
                            child_tag = str(child.tag)
                            if child_tag == "var":
                                self._link(idx_node, part, "base")
                            else:
                                self._link(idx_node, part, "index")
            return idx_node
        
        if tag == "method":
            # Method call in expression
            obj_ref = e.get("ref")
            method_name = e.get("name")
            if obj_ref and method_name:
                try:
                    obj_n = self._lookup(obj_ref)
                    method_node = self._add_node(f"{obj_ref}.{method_name}()", "MethodCallExpr",
                                                object_ref=obj_ref, method=method_name)
                    self._link(method_node, obj_n, "object")
                    return method_node
                except NameError:
                    method_node = self._add_node(f"{obj_ref}.{method_name}()", "MethodCallExpr",
                                                object_ref=obj_ref, method=method_name)
                    return method_node
        
        if tag == "function":
            # Function call in expression
            func_name = e.get("name") or e.get("ref")
            func_node = self._add_node(func_name or "function", "FunctionCallExpr", function=func_name)
            # Process arguments
            for child in e:
                if child.tag is not etree.Comment:
                    child_tag = str(child.tag)
                    if child_tag == "arg":
                        arg_expr = self._walk_expression(child[0] if len(child) > 0 else child, func_node)
                        if arg_expr:
                            self._link(func_node, arg_expr, "arg")
                    elif child_tag == "kwarg":
                        kw_name = child.get("name")
                        kw_val_elem = child[0] if len(child) > 0 else None
                        if kw_val_elem is not None:
                            kw_val = self._walk_expression(kw_val_elem, func_node)
                            kw_node = self._add_node(f"{kw_name}=...", "KwargExpr", name=kw_name)
                            if kw_val:
                                self._link(kw_node, kw_val, "value")
                            self._link(func_node, kw_node, "kwarg")
            return func_node
        
        if tag == "constructor":
            # Constructor call
            ctor_ref = e.get("ref")
            ctor_node = self._add_node(f"{ctor_ref}()", "ConstructorExpr", constructor=ctor_ref)
            return ctor_node
        
        if tag == "numpy_dtype":
            dtype_val = e.text.strip() if e.text else "unknown"
            dtype_node = self._add_node(f"np.{dtype_val}", "DtypeExpr", dtype=dtype_val)
            return dtype_node

        if tag == "string":
            # Handle <string>"value"</string> or <string>value</string>
            string_val = e.text if e.text else ""
            return self._add_node(string_val, "String", value=string_val)

        if tag == "unary_op":
            # Handle <unary_op op="~">...</unary_op>
            op = e.get("op", "~")
            unary_node = self._add_node(f"{op}", "UnaryOp", op=op)
            # Process operand
            children = [c for c in e if c.tag is not etree.Comment]
            if children:
                operand = self._walk_expression(children[0], unary_node)
                if operand:
                    self._link(unary_node, operand, "operand")
            return unary_node

        # Generic expression node
        expr_node = self._add_node(tag, "Expr")
        # Try to capture any child expressions
        for child in e:
            if child.tag is not etree.Comment:
                child_expr = self._walk_expression(child, expr_node)
                if child_expr:
                    self._link(expr_node, child_expr, "contains")
        return expr_node

    def _func_call(self, e: etree.Element, p: NodeId):
        """Process standalone call statement."""
        call = self._walk_call_chain(e, p)
        self._link(p, call, "contains")

    def _func_comment(self, e: etree.Element, p: NodeId):
        """
        Process comment node.
        
        Preserves code comments in graph for documentation generation.
        """
        comment_text = e.text.strip() if e.text else ""
        c = self._add_node(comment_text, "Comment", text=comment_text)
        self._link(p, c, "contains")
    
    def _func_generic(self, e: etree.Element, p: NodeId):
        """Fallback handler for unknown function body elements."""
        nid = self._add_node(str(e.tag), "FunctionBodyElement")
        self._link(p, nid, "contains")

    def _func_for(self, e: etree.Element, p: NodeId):
        """
        Process for loop with iteration variable and body.
        
        Creates new scope for loop variable.
        """
        var = e.get("var")
        range_attr = e.get("range")
        iterable = range_attr if range_attr else e.get("in", "range(...)")
        f = self._add_node(f"for {var} in {iterable}", "For")
        self._link(p, f, "contains")
        self._scope_stack.append({})
        lv = self._add_node(var, "LoopVariable")
        self._link(f, lv, "iterates")
        self._declare_symbol(var, lv)
        self._process_function_body(e, f)
        self._scope_stack.pop()

    def _func_print(self, e: etree.Element, p: NodeId):
        # Check for fstring element
        fstring_elem = e.find("fstring")
        expression_elem = e.find("expression")
        string_elem = e.find("string")
        var_elem = e.find("var")

        if fstring_elem is not None and fstring_elem.text:
            # This is an f-string
            fstring_text = fstring_elem.text.strip()
            # Check if it already has f-string syntax (starts with f' or f")
            if fstring_text.startswith("f'") or fstring_text.startswith('f"'):
                # Already has f-string wrapper (add_activate format)
                # Convert {{ to { and }} to } for proper f-string interpolation
                # (XML uses {{ to escape braces, but Python f-strings need single braces for variables)
                fstring_text = fstring_text.replace('{{', '{').replace('}}', '}')
                pr = self._add_node(f'print({fstring_text})', "Print")
            else:
                # Just the content, need to add f-string wrapper (passthrough format)
                pr = self._add_node(f'print(f"{fstring_text}")', "Print")
            self._link(p, pr, "contains")
            # Link to variables used in the f-string
            vars_elem = e.find("vars")
            if vars_elem is not None:
                for v in vars_elem:
                    if v.tag == "var" and v.text:
                        name = v.text.strip()
                        try:
                            self._link(pr, self._lookup(name), "uses")
                        except NameError:
                            pass
        elif string_elem is not None and string_elem.text:
            # This is a simple string literal
            string_text = string_elem.text.strip()
            pr = self._add_node(f'print({string_text})', "Print")
            self._link(p, pr, "contains")
        elif var_elem is not None and var_elem.text:
            # This is a variable reference like print(outputD)
            var_name = var_elem.text.strip()
            pr = self._add_node(f'print({var_name})', "Print")
            self._link(p, pr, "contains")
            try:
                self._link(pr, self._lookup(var_name), "uses")
            except NameError:
                pass
        elif expression_elem is not None and expression_elem.text:
            # This is a regular expression like "-" * 24
            expr_text = expression_elem.text.strip()
            pr = self._add_node(f'print({expr_text})', "Print")
            self._link(p, pr, "contains")
        else:
            # Fallback to old format
            fmt = e.findtext("format", "")
            pr = self._add_node(f'print("{fmt}")', "Print")
            self._link(p, pr, "contains")
            for v in e:
                if v.tag == "var":
                    name = v.text.strip() if v.text else ""
                    if name:
                        try:
                            self._link(pr, self._lookup(name), "uses")
                        except NameError:
                            pass
                elif v.tag == "index":
                    base_elem = v.find("var")
                    idx_elem = v.find("const") or v.find("var")
                    if base_elem is not None and base_elem.text:
                        base = base_elem.text.strip()
                        idx = idx_elem.text.strip() if idx_elem.text else "?"
                        try:
                            b_n = self._lookup(base)
                            i_n = self._add_node(idx, "Index")
                            acc = self._add_node("[]", "IndexAccess")
                            self._link(acc, b_n, "base")
                            self._link(acc, i_n, "index")
                            self._link(pr, acc, "uses")
                        except NameError:
                            pass

    # ===================================================================
    # SECTION 10: Control Flow Processing
    # ===================================================================
    # Handles if/else statements with proper branch separation.
    
    def _func_if(self, e: etree.Element, p: NodeId):
        """
        Process if/else statement with condition and branches.
        
        Key Design: Uses 'then' and 'else' edge types (not 'contains')
        to distinguish branches. This enables CodeGenerator to process
        branches separately without confusion.
        
        Process:
        1. Create If node with condition
        2. Process then branch, mark edges as 'then'
        3. Process else branch (if exists), mark edges as 'else'
        """
        # Try to get condition from attribute first
        cond_attr = e.get("condition")
        cond_node = None
        
        if cond_attr:
            # Parse condition string into expression
            cond_node = self._add_node(cond_attr, "ConditionExpr", condition_text=cond_attr)
        else:
            # Try to parse condition element
            cond_elem = e.find("condition")
            if cond_elem is not None:
                cond_node = self._walk_expression(cond_elem, p)
        
        i = self._add_node("if", "If", condition_text=cond_attr if cond_attr else "")
        self._link(p, i, "contains")
        
        if cond_node:
            self._link(i, cond_node, "condition")
        
        # Process then branch with 'then' edge type
        then_elem = e.find("then")
        if then_elem is not None:
            # Track edges before processing
            edges_before = set(self.graph.out_edges(i))
            
            for child in then_elem:
                if child.tag is etree.Comment:
                    continue
                tag = str(child.tag)
                h = getattr(self, f"_func_{tag.lower()}", None)
                if h:
                    h(child, i)
                else:
                    self._func_generic(child, i)
            
            # Find new edges and change their type to 'then'
            edges_after = set(self.graph.out_edges(i))
            new_edges = edges_after - edges_before
            for src, tgt in new_edges:
                if self.graph[src][tgt].get('type') == 'contains':
                    self.graph[src][tgt]['type'] = 'then'
        
        # Process else branch with 'else' edge type
        else_elem = e.find("else")
        if else_elem is not None:
            # Track edges before processing
            edges_before = set(self.graph.out_edges(i))
            
            for child in else_elem:
                if child.tag is etree.Comment:
                    continue
                tag = str(child.tag)
                h = getattr(self, f"_func_{tag.lower()}", None)
                if h:
                    h(child, i)
                else:
                    self._func_generic(child, i)
            
            # Find new edges and change their type to 'else'
            edges_after = set(self.graph.out_edges(i))
            new_edges = edges_after - edges_before
            for src, tgt in new_edges:
                if self.graph[src][tgt].get('type') == 'contains':
                    self.graph[src][tgt]['type'] = 'else'

    def _process_entrypoint(self, e: etree.Element, p: NodeId):
        """
        Process entry point (if __name__ == "__main__").
        
        Creates EntryPoint node containing main execution logic.
        """
        ep = self._add_node("EntryPoint", "EntryPoint")
        self._link(p, ep, "contains")
        if_elem = e.find("If")
        if if_elem is not None:
            self._func_if(if_elem, ep)

# ======================================================================
# CLI Entry Point
# ======================================================================
# Provides command-line interface for standalone graph generation.
def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <xml>")
        sys.exit(1)
    path = Path(sys.argv[1])
    if not path.is_file():
        print("File not found")
        sys.exit(1)

    builder = GraphBuilder(path)
    g = builder.build()

    out = path.with_suffix(".graphml")
    nx.write_graphml(g, out)
    print(f"Graph -> {out}")

    print("\n--- Summary ---")
    print(f"Nodes: {g.number_of_nodes()}  Edges: {g.number_of_edges()}")
    kinds = {}
    for n, d in g.nodes(data=True):
        k = d.get("kind", "?")
        kinds[k] = kinds.get(k, 0) + 1
    for k, c in sorted(kinds.items()):
        print(f"  {k:20} {c}")

    #builder.print_graph_readable()

if __name__ == "__main__":
    main()