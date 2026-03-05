#!/usr/bin/env python3
"""
GraphExtender.py – extensible XML → graph conversion

Add new XML element support without touching GraphDriver.
Just inherit from GraphExtension, implement _process_<tag>(self, elem, parent_nid)
and register the class with register_extensions(builder).

Example:
    <Worker name="A1_B1_worker"> … </Worker>

will be handled by WorkerExtension.
"""

from __future__ import annotations
import sys
from pathlib import Path
from typing import Callable, Dict, Type
from lxml import etree
import networkx as nx

# ----------------------------------------------------------------------
# Types that match GraphDriver
# ----------------------------------------------------------------------
NodeId = str
Graph = nx.DiGraph


# ----------------------------------------------------------------------
# Base extension class
# ----------------------------------------------------------------------
class GraphExtension:
    """
    Base class for all XML → graph extensions.

    Sub-classes must:
      * set ``tag`` (lower-case XML tag name)
      * implement ``process(self, elem, parent_nid)`` – return the node id(s)
        that were attached to ``parent_nid``.
    """

    tag: str = ""                     # <-- override in subclass (lower case)

    def __init__(self, builder):
        """
        ``builder`` is the GraphBuilder instance – it gives full access to
        _add_node, _link, _lookup, _declare_symbol, etc.
        """
        self.builder = builder

    # ------------------------------------------------------------------
    # Helper shortcuts (identical to GraphBuilder private API)
    # ------------------------------------------------------------------
    def _new_id(self, prefix: str = "node") -> NodeId:
        return self.builder._new_id(prefix)

    def _add_node(self, label: str, kind: str, **attrs) -> NodeId:
        return self.builder._add_node(label, kind, **attrs)

    def _link(self, src: NodeId, dst: NodeId, edge_type: str = "depends_on"):
        self.builder._link(src, dst, edge_type)

    def _lookup(self, name: str) -> NodeId:
        return self.builder._lookup(name)

    def _declare_symbol(self, name: str, nid: NodeId):
        self.builder._declare_symbol(name, nid)

    # ------------------------------------------------------------------
    # Sub-classes implement this
    # ------------------------------------------------------------------
    def process(self, elem: etree.Element, parent_nid: NodeId) -> NodeId | None:
        """
        Entry point called by GraphBuilder when the tag matches.
        Return the primary node id that represents the element
        (or None if nothing was created).
        """
        raise NotImplementedError


# ----------------------------------------------------------------------
# Concrete extension: <Worker …>
# ----------------------------------------------------------------------
class WorkerExtension(GraphExtension):
    """Handles <Worker name="…"> … </Worker>"""

    tag = "worker"

    # ------------------------------------------------------------------
    # Core processing
    # ------------------------------------------------------------------
    def process(self, elem: etree.Element, parent_nid: NodeId) -> NodeId:
        name = elem.get("name")
        if not name:
            raise ValueError("<Worker> missing required attribute 'name'")

        # 1. Worker node
        worker_nid = self._add_node(name, "Worker")
        self._link(parent_nid, worker_nid, "contains")
        self._declare_symbol(name, worker_nid)

        # 2. Core function reference
        core_fn = elem.find("core_fn")
        if core_fn is not None:
            ref = core_fn.get("ref")
            if ref:
                try:
                    fn_nid = self._lookup(ref)
                    self._link(worker_nid, fn_nid, "core_fn")
                except NameError:
                    # keep a placeholder so the graph stays valid
                    placeholder = self._add_node(ref, "MissingCoreFn")
                    self._link(worker_nid, placeholder, "core_fn")

        # 3. Function arguments
        fn_args = elem.find("fn_args")
        if fn_args is not None:
            for arg_elem in fn_args:
                if arg_elem.tag != "arg":
                    continue
                # Check if arg contains a method_chain
                method_chain = arg_elem.find("method_chain")
                if method_chain is not None:
                    arg_nid = self._walk_method_chain(method_chain, None)  # Don't link intermediate nodes
                    if arg_nid:
                        self._link(worker_nid, arg_nid, "has_arg")
                else:
                    # Process children of arg element
                    arg_children = [c for c in arg_elem if c.tag is not etree.Comment]
                    if arg_children:
                        arg_nid = self._walk_expression(arg_children[0], worker_nid)
                        if arg_nid:
                            self._link(worker_nid, arg_nid, "has_arg")

        # 4. Placement (Tile constructor)
        placement = elem.find("placement")
        if placement is not None:
            ctor = placement.find("constructor")
            if ctor is not None:
                ctor_nid = self._walk_constructor(ctor, worker_nid)
                if ctor_nid:
                    self._link(worker_nid, ctor_nid, "placed_by")

        return worker_nid

    # ------------------------------------------------------------------
    # Expression walker (re-used from GraphDriver logic)
    # ------------------------------------------------------------------
    def _walk_expression(self, elem: etree.Element, parent_nid: NodeId) -> NodeId | None:
        """
        Recursively turn any expression inside <arg> into graph nodes.
        Supports:
          * <var ref="…"/>
          * <index><base>…</base><index_value>…</index_value></index>
          * <method name="…"/>
          * nested calls, etc.
        """
        if elem is None:
            return None

        tag = elem.tag

        # ----- var -----
        if tag == "var":
            ref = elem.get("ref") or (elem.text or "").strip()
            if not ref:
                return None
            try:
                nid = self._lookup(ref)
                self._link(parent_nid, nid, "uses")
                return nid
            except NameError:
                # placeholder
                nid = self._add_node(ref, "VarRef")
                self._link(parent_nid, nid, "uses")
                return nid

        # ----- index -----
        if tag == "index":
            idx_nid = self._add_node("[]", "IndexExpr")
            self._link(parent_nid, idx_nid, "has_arg")

            base = elem.find("base")
            if base is not None:
                base_children = [c for c in base if c.tag is not etree.Comment]
                if base_children:
                    base_nid = self._walk_expression(base_children[0], idx_nid)
                    if base_nid:
                        self._link(idx_nid, base_nid, "base")

            idx_val = elem.find("index_value")
            if idx_val is not None:
                idx_children = [c for c in idx_val if c.tag is not etree.Comment]
                if idx_children:
                    idx_nid_val = self._walk_expression(idx_children[0], idx_nid)
                    if idx_nid_val:
                        self._link(idx_nid, idx_nid_val, "index")

            # method chain after index (e.g. .cons())
            method = elem.find("method")
            if method is not None:
                method_nid = self._walk_method(method, idx_nid)
                if method_nid:
                    self._link(idx_nid, method_nid, "nested_call")
            return idx_nid

        # ----- const -----
        if tag == "const":
            val = elem.text.strip() if elem.text else "0"
            const_nid = self._add_node(val, "ConstExpr", value=val)
            return const_nid

        # ----- method (chained after var or index) -----
        if tag == "method":
            return self._walk_method(elem, parent_nid)

        # ----- binary_op -----
        if tag == "binary_op":
            op = elem.get("op", "+")  # Default to + if no operator
            binop_nid = self._add_node(f"{op}", "BinaryOp", op=op)
            self._link(parent_nid, binop_nid, "has_arg")
            # Process left and right operands
            children = [c for c in elem if c.tag is not etree.Comment]
            if len(children) >= 1:
                left_nid = self._walk_expression(children[0], binop_nid)
                if left_nid:
                    self._link(binop_nid, left_nid, "lhs")
            if len(children) >= 2:
                right_nid = self._walk_expression(children[1], binop_nid)
                if right_nid:
                    self._link(binop_nid, right_nid, "rhs")
            return binop_nid

        # ----- generic fallback -----
        expr_nid = self._add_node(tag, "Expr")
        self._link(parent_nid, expr_nid, "has_arg")
        for child in elem:
            if child.tag is etree.Comment:
                continue
            child_nid = self._walk_expression(child, expr_nid)
            if child_nid:
                self._link(expr_nid, child_nid, "contains")
        return expr_nid

    # ------------------------------------------------------------------
    # Helper: method call (obj.method())
    # ------------------------------------------------------------------
    def _walk_method(self, elem: etree.Element, parent_nid: NodeId) -> NodeId | None:
        name = elem.get("name")
        if not name:
            return None
        # Check if there's an object reference
        ref = elem.get("ref")
        if ref:
            method_nid = self._add_node(name, "MethodCall", object_ref=ref)
        else:
            method_nid = self._add_node(name, "MethodCall")
        self._link(parent_nid, method_nid, "calls")
        return method_nid

    # ------------------------------------------------------------------
    # Helper: constructor call (Tile(0,5))
    # ------------------------------------------------------------------
    def _walk_constructor(self, elem: etree.Element, parent_nid: NodeId) -> NodeId | None:
        ref = elem.get("ref")
        if not ref:
            return None
        ctor_nid = self._add_node(f"{ref}()", "ConstructorCall")
        self._link(parent_nid, ctor_nid, "calls")

        for arg in elem:
            if arg.tag != "arg":
                continue
            arg_nid = self._walk_expression(arg, ctor_nid)
            if arg_nid:
                self._link(ctor_nid, arg_nid, "has_arg")
        return ctor_nid

    # ------------------------------------------------------------------
    # Helper: method chain (obj.method1().method2())
    # ------------------------------------------------------------------
    def _walk_method_chain(self, elem: etree.Element, parent_nid: NodeId | None) -> NodeId | None:
        """
        Process <method_chain> which contains:
          <base> - the base expression (var, index, etc.)
          <call> - one or more method calls (with optional kwargs)
        
        Returns a MethodChain node that contains the entire chain structure.
        This allows proper reconstruction without ambiguity.
        """
        # Create a MethodChain node to represent the entire expression
        chain_nid = self._add_node("method_chain", "MethodChain")

        # Get the base expression
        base_elem = elem.find("base")
        base_nid = None

        if base_elem is not None:
            # New format with explicit <base> element
            base_children = [c for c in base_elem if c.tag is not etree.Comment]
            if not base_children:
                return None

            # Create a temporary parent if none provided
            temp_parent = parent_nid if parent_nid else self._new_id("temp")
            base_nid = self._walk_expression(base_children[0], temp_parent)
            if not base_nid:
                return None

            # Link base to chain
            self._link(chain_nid, base_nid, "base")
            prev_nid = base_nid
        else:
            # Old format: first method has ref attribute
            first_call = elem.find("call")
            if first_call is None:
                return None

            first_method = first_call.find("method")
            if first_method is None:
                return None

            method_ref = first_method.get("ref")
            if method_ref:
                # Create a variable reference node as the base
                try:
                    base_nid = self._lookup(method_ref)
                except NameError:
                    base_nid = self._add_node(method_ref, "VarRef")
                self._link(chain_nid, base_nid, "base")
                prev_nid = base_nid
            else:
                return None

        # Process each call in the chain, linking them in order
        for call_elem in elem.findall("call"):
            method_elem = call_elem.find("method")
            if method_elem is not None:
                method_name = method_elem.get("name")
                if method_name:
                    method_nid = self._add_node(method_name, "MethodCall")
                    self._link(prev_nid, method_nid, "calls")
                    self._link(chain_nid, method_nid, "has_call")  # Also link from chain node
                    
                    # Process kwargs — may be children of <method> or <call> (forward pattern)
                    kwarg_sources = list(method_elem) + [
                        c for c in call_elem if c.tag == "kwarg"
                    ]
                    for kwarg in kwarg_sources:
                        if kwarg.tag == "kwarg":
                            kw_name = kwarg.get("name")
                            kw_value = kwarg.get("value")

                            # Check if kwarg has child elements (list, constructor, etc.)
                            kw_children = [c for c in kwarg if c.tag is not etree.Comment]
                            if kw_children:
                                # Handle complex kwargs
                                kw_nid = self._add_node(f"{kw_name}=...", "Kwarg", name=kw_name)
                                self._link(method_nid, kw_nid, "has_kwarg")

                                for child in kw_children:
                                    child_nid = self._process_kwarg_value(child, kw_nid)
                                    if child_nid:
                                        self._link(kw_nid, child_nid, "contains")
                            elif kw_value:
                                # Simple kwarg with value attribute
                                kw_nid = self._add_node(f"{kw_name}={kw_value}", "Kwarg",
                                                      name=kw_name, value=kw_value)
                                self._link(method_nid, kw_nid, "has_kwarg")
                    
                    prev_nid = method_nid
        
        # Return the MethodChain node
        return chain_nid
    
    # ------------------------------------------------------------------
    # Helper: process kwarg values (lists, constructors, etc.)
    # ------------------------------------------------------------------
    def _process_kwarg_value(self, elem: etree.Element, parent_nid: NodeId) -> NodeId | None:
        """Process various kwarg value types"""
        tag = elem.tag
        
        if tag == "list":
            list_nid = self._add_node("list", "List")
            for item in elem:
                if item.tag == "type_ref":
                    type_name = item.text.strip() if item.text else ""
                    if type_name:
                        type_nid = self._add_node(type_name, "TypeRef")
                        self._link(list_nid, type_nid, "contains")
                elif item.tag == "string":
                    str_val = item.text.strip() if item.text else ""
                    str_nid = self._add_node(str_val, "String")
                    self._link(list_nid, str_nid, "contains")
                elif item.tag == "const":
                    # Handle literal/symbolic constants in lists (e.g., offsets)
                    const_val = item.text.strip() if item.text else ""
                    const_nid = self._add_node(const_val, "Const", value=const_val)
                    self._link(list_nid, const_nid, "contains")
                elif item.tag == "binary_op":
                    # Handle expressions in lists
                    expr_nid = self._walk_expression(item, list_nid)
                    if expr_nid:
                        self._link(list_nid, expr_nid, "contains")
            return list_nid
        
        elif tag == "constructor":
            # Handle constructor calls like Tile(0, 1)
            return self._walk_constructor(elem, parent_nid)
        
        elif tag == "binary_op":
            # Handle binary operations
            return self._walk_expression(elem, parent_nid)
        
        elif tag == "method":
            # Handle method references
            ref = elem.get("ref")
            name = elem.get("name")
            if ref and name:
                method_nid = self._add_node(f"{ref}.{name}", "MethodRef")
                return method_nid
        
        return None


# ----------------------------------------------------------------------
# Registry & auto-wiring into GraphBuilder
# ----------------------------------------------------------------------
_EXTENSION_REGISTRY: Dict[str, Type[GraphExtension]] = {}


def register_extension(cls: Type[GraphExtension]):
    """Register a GraphExtension subclass by its lower-case tag."""
    tag = cls.tag.lower()
    if not tag:
        raise ValueError(f"{cls.__name__}.tag must be set")
    _EXTENSION_REGISTRY[tag] = cls
    return cls


def register_extensions(builder) -> None:
    """
    Call this **once** from GraphBuilder.__init__ (or right after creation).

    It injects a generic dispatcher ``_process_ext_<tag>`` for every registered
    extension, so GraphDriver can stay untouched.
    """
    for tag, ext_cls in _EXTENSION_REGISTRY.items():
        method_name = f"_process_ext_{tag}"

        def make_handler(ext_cls=ext_cls):  # closure captures ext_cls
            def handler(self, elem: etree.Element, parent_nid: NodeId):
                ext = ext_cls(self)               # self == GraphBuilder instance
                ext.process(elem, parent_nid)
            return handler

        # Bind the generated handler into the builder instance as a bound method
        import types
        setattr(builder, method_name, types.MethodType(make_handler(), builder))


# ----------------------------------------------------------------------
# Extension: <ExternalFunction>
# ----------------------------------------------------------------------
class ExternalFunctionExtension(GraphExtension):
    """Handles <ExternalFunction name="..."> ... </ExternalFunction>"""
    
    tag = "externalfunction"
    
    def process(self, elem: etree.Element, parent_nid: NodeId) -> NodeId:
        name = elem.get("name")
        if not name:
            raise ValueError("<ExternalFunction> missing required attribute 'name'")
        
        # Create ExternalFunction node
        ext_func_nid = self._add_node(name, "ExternalFunction")
        self._link(parent_nid, ext_func_nid, "contains")
        self._declare_symbol(name, ext_func_nid)
        
        # Process attributes (kwargs)
        attrs = elem.find("attributes")
        if attrs is not None:
            for kw in attrs:
                if kw.tag == "kwarg":
                    kw_name = kw.get("name")
                    kw_value = kw.get("value")
                    
                    # Check if kwarg has child elements (list, etc.)
                    kw_children = [c for c in kw if c.tag is not etree.Comment]
                    if kw_children:
                        # Handle complex kwargs like lists
                        kw_nid = self._add_node(f"{kw_name}=...", "Kwarg", name=kw_name)
                        self._link(ext_func_nid, kw_nid, "has_kwarg")
                        
                        for child in kw_children:
                            if child.tag == "list":
                                list_nid = self._add_node("list", "List")
                                self._link(kw_nid, list_nid, "contains")
                                
                                for item in child:
                                    if item.tag == "type_ref":
                                        type_name = item.text.strip() if item.text else ""
                                        if type_name:
                                            type_nid = self._add_node(type_name, "TypeRef")
                                            self._link(list_nid, type_nid, "contains")
                                    elif item.tag == "string":
                                        str_val = item.text.strip() if item.text else ""
                                        str_nid = self._add_node(str_val, "String")
                                        self._link(list_nid, str_nid, "contains")
                    else:
                        # Simple kwarg
                        kw_nid = self._add_node(f"{kw_name}={kw_value}", "Kwarg",
                                              name=kw_name, value=kw_value)
                        self._link(ext_func_nid, kw_nid, "has_kwarg")
        
        return ext_func_nid


# ----------------------------------------------------------------------
# Extension: <CoreFunction>
# ----------------------------------------------------------------------
class CoreFunctionExtension(GraphExtension):
    """Handles <CoreFunction name="..."> ... </CoreFunction>"""
    
    tag = "corefunction"
    
    def process(self, elem: etree.Element, parent_nid: NodeId) -> NodeId:
        name = elem.get("name")
        if not name:
            raise ValueError("<CoreFunction> missing required attribute 'name'")
        
        # Create CoreFunction node
        func_nid = self._add_node(name, "CoreFunction")
        self._link(parent_nid, func_nid, "contains")
        self._declare_symbol(name, func_nid)
        
        # Process parameters
        params = elem.find("parameters")
        if params is not None:
            for param in params:
                if param.tag == "param":
                    p_name = param.get("name")
                    p_nid = self._add_node(p_name, "Parameter")
                    self._link(func_nid, p_nid, "has_param")
        
        # Process body
        body = elem.find("body")
        if body is not None:
            for stmt in body:
                if stmt.tag is etree.Comment:
                    continue
                self._process_body_statement(stmt, func_nid)
        
        return func_nid
    
    def _process_body_statement(self, elem: etree.Element, parent_nid: NodeId):
        """Process statements in CoreFunction body"""
        tag = elem.tag
        
        if tag == "Acquire":
            # <Acquire name="elementA"><call>...</call></Acquire>
            var_name = elem.get("name")
            acq_nid = self._add_node(var_name, "Acquire")
            self._link(parent_nid, acq_nid, "contains")
            
            call_elem = elem.find("call")
            if call_elem is not None:
                call_nid = self._walk_call(call_elem, acq_nid)
                if call_nid:
                    self._link(acq_nid, call_nid, "contains")
        
        elif tag == "Release":
            # <Release><call>...</call></Release>
            rel_nid = self._add_node("release", "Release")
            self._link(parent_nid, rel_nid, "contains")
            
            call_elem = elem.find("call")
            if call_elem is not None:
                call_nid = self._walk_call(call_elem, rel_nid)
                if call_nid:
                    self._link(rel_nid, call_nid, "contains")
        
        elif tag == "Call":
            # <Call><function ref="...">...</function></Call>
            call_nid = self._add_node("call", "Call")
            self._link(parent_nid, call_nid, "contains")
            
            function = elem.find("function")
            if function is not None:
                func_ref = function.get("ref")
                if func_ref:
                    try:
                        func_nid = self._lookup(func_ref)
                        self._link(call_nid, func_nid, "calls")
                    except NameError:
                        func_nid = self._add_node(func_ref, "FunctionCall")
                        self._link(call_nid, func_nid, "calls")
                
                # Process arguments
                for arg in function:
                    if arg.tag == "arg":
                        arg_children = [c for c in arg if c.tag is not etree.Comment]
                        if arg_children:
                            arg_child = arg_children[0]
                            if arg_child.tag == "var":
                                var_ref = arg_child.get("ref") or (arg_child.text or "").strip()
                                if var_ref:
                                    try:
                                        var_nid = self._lookup(var_ref)
                                        self._link(call_nid, var_nid, "has_arg")
                                    except NameError:
                                        var_nid = self._add_node(var_ref, "VarRef")
                                        self._link(call_nid, var_nid, "has_arg")

        elif tag == "For":
            # <For var="_" range="range_(...)">...</For>
            loop_var = elem.get("var", "_")
            range_expr = elem.get("range", "range_(...)")
            for_nid = self._add_node(f"for {loop_var} in {range_expr}", "For")
            self._link(parent_nid, for_nid, "contains")

            # Process nested body statements inside the For loop
            for child in elem:
                if child.tag is not etree.Comment:
                    self._process_body_statement(child, for_nid)

        elif tag == "Assignment":
            # <Assignment target="elem_out" index="i" value="0"/>
            target = elem.get("target", "")
            index = elem.get("index", "")
            value = elem.get("value", "")
            assign_nid = self._add_node(target, "Assignment",
                                        target=target, index=index, value=value)
            self._link(parent_nid, assign_nid, "contains")

    def _walk_call(self, elem: etree.Element, parent_nid: NodeId) -> NodeId:
        """Walk a call element and create call node"""
        call_nid = self._add_node("call", "Call")
        
        # Find method or function
        method = elem.find("method")
        function = elem.find("function")
        
        if method is not None:
            obj_ref = method.get("ref")
            method_name = method.get("name")
            if obj_ref and method_name:
                try:
                    obj_nid = self._lookup(obj_ref)
                    method_nid = self._add_node(method_name, "MethodCall")
                    self._link(call_nid, method_nid, "calls")
                    self._link(method_nid, obj_nid, "object")
                except NameError:
                    method_nid = self._add_node(f"{obj_ref}.{method_name}", "MethodCall")
                    self._link(call_nid, method_nid, "calls")
            
            # Process method arguments (e.g., acquire(1), release(1))
            for arg in method:
                if arg.tag == "arg":
                    arg_children = [c for c in arg if c.tag is not etree.Comment]
                    if arg_children:
                        arg_child = arg_children[0]
                        if arg_child.tag == "const":
                            const_val = arg_child.text.strip() if arg_child.text else "0"
                            const_nid = self._add_node(const_val, "ConstExpr", value=const_val)
                            self._link(call_nid, const_nid, "has_arg")
                        elif arg_child.tag == "var":
                            var_ref = arg_child.get("ref") or (arg_child.text or "").strip()
                            if var_ref:
                                try:
                                    var_nid = self._lookup(var_ref)
                                    self._link(call_nid, var_nid, "has_arg")
                                except NameError:
                                    var_nid = self._add_node(var_ref, "VarRef")
                                    self._link(call_nid, var_nid, "has_arg")
        
        elif function is not None:
            func_ref = function.get("ref")
            if func_ref:
                try:
                    func_nid = self._lookup(func_ref)
                    self._link(call_nid, func_nid, "calls")
                except NameError:
                    func_nid = self._add_node(func_ref, "FunctionCall")
                    self._link(call_nid, func_nid, "calls")
            
            # Process arguments - use simple approach
            for arg in function:
                if arg.tag == "arg":
                    # Get first child of arg
                    arg_children = [c for c in arg if c.tag is not etree.Comment]
                    if arg_children:
                        arg_child = arg_children[0]
                        if arg_child.tag == "var":
                            var_ref = arg_child.get("ref") or (arg_child.text or "").strip()
                            if var_ref:
                                try:
                                    var_nid = self._lookup(var_ref)
                                    self._link(call_nid, var_nid, "has_arg")
                                except NameError:
                                    var_nid = self._add_node(var_ref, "VarRef")
                                    self._link(call_nid, var_nid, "has_arg")
        
        return call_nid


# ----------------------------------------------------------------------
# Extension: <List>
# ----------------------------------------------------------------------
class ListExtension(GraphExtension):
    """Handles <List name="..."> ... </List>"""
    
    tag = "list"
    
    def process(self, elem: etree.Element, parent_nid: NodeId) -> NodeId:
        name = elem.get("name")
        if not name:
            raise ValueError("<List> missing required attribute 'name'")
        
        # Create List node
        list_nid = self._add_node(name, "List")
        self._link(parent_nid, list_nid, "contains")
        self._declare_symbol(name, list_nid)
        
        # Process items
        items = elem.find("items")
        if items is not None:
            for item in items:
                if item.tag == "var":
                    var_ref = item.get("ref")
                    if var_ref:
                        try:
                            var_nid = self._lookup(var_ref)
                            self._link(list_nid, var_nid, "contains")
                        except NameError:
                            var_nid = self._add_node(var_ref, "VarRef")
                            self._link(list_nid, var_nid, "contains")
        
        return list_nid


# ----------------------------------------------------------------------
# Register the extensions that ship with this file
# ----------------------------------------------------------------------
register_extension(WorkerExtension)
register_extension(ExternalFunctionExtension)
register_extension(CoreFunctionExtension)
register_extension(ListExtension)