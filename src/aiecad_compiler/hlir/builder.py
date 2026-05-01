"""
Fluent builder API for constructing AIECAD programs.

Provides user-friendly methods for building programs without
manually wiring every field. Enforces invariants and provides
helpful validation.

Enhanced with ID tracking, removal, and update capabilities for
interactive GUI editing.
"""

from typing import List, Union, Optional, Dict, Any, Tuple
from uuid import uuid4
from .core import (
    Program, Tile, TileKind, ObjectFifo, ExternalKernel,
    CoreFunction, Worker, RuntimeSequence, Symbol,
    FifoAccessMode, FifoBinding, Acquire, Release, KernelCall,
    RuntimeFill, RuntimeDrain
)
from .operations import (
    SplitOperation, JoinOperation, ForwardOperation,
    TensorAccessPattern, TensorTiler2DSpec, FillOperation, DrainOperation
)
from .core import Assignment, ForLoop
from .types import DataType, TensorType, AnyType, make_tensor_type
from .builder_result import BuilderResult, ErrorCode


class ProgramBuilder:
    """
    Fluent builder for constructing AIECAD programs.

    Example usage:
        builder = ProgramBuilder("my_program")

        # Add tiles
        shim = builder.add_tile("shim0", kind="shim", x=0, y=0)
        compute = builder.add_tile("compute0", kind="compute", x=0, y=2)

        # Add types
        chunk_ty = builder.add_tensor_type("chunk_ty", shape=[1024], dtype="int32")

        # Add FIFOs
        fifo_in = builder.add_fifo("fifo_in", obj_type="chunk_ty", depth=2)

        # Build program
        program = builder.build()
    """

    def __init__(self, name: str):
        """
        Initialize a new program builder with ID tracking.

        Args:
            name: Program name
        """
        self.program = Program(name=name)

        # ID tracking for interactive editing
        self._id_map: Dict[str, Tuple[str, Any]] = {}  # id -> (type, component)
        self._name_index: Dict[Tuple[str, str], str] = {}  # (type, name) -> id
        self._component_to_id: Dict[int, str] = {}  # id(obj) -> id

    def _generate_id(self) -> str:
        """Generate a unique ID for a component."""
        return str(uuid4())

    def _register_component(self, comp_type: str, name: str, component: Any,
                            inject: bool = True, provided_id: Optional[str] = None) -> str:
        """
        Register a component with ID tracking.

        Args:
            comp_type: Component type string
            name: Component name
            component: Component object
            inject: Whether to inject into program
            provided_id: Optional ID to use (for updates)

        Returns:
            component_id (UUID string)
        """
        name_key = (comp_type, name)

        # If ID is provided, this is an update operation
        if provided_id:
            if provided_id in self._id_map:
                # Update: replace the component but keep the same ID
                old_type, old_component = self._id_map[provided_id]

                # Remove old component from tracking
                old_obj_id = id(old_component)
                if old_obj_id in self._component_to_id:
                    del self._component_to_id[old_obj_id]

                # If name changed, update name index
                if old_type == comp_type:
                    old_name = getattr(old_component, 'name', None)
                    if old_name and old_name != name:
                        old_name_key = (comp_type, old_name)
                        if old_name_key in self._name_index:
                            del self._name_index[old_name_key]

                # Register new component with existing ID
                self._id_map[provided_id] = (comp_type, component)
                self._name_index[name_key] = provided_id
                self._component_to_id[id(component)] = provided_id

                return provided_id
            else:
                # Provided ID doesn't exist, use it as new
                comp_id = provided_id
                self._id_map[comp_id] = (comp_type, component)
                self._name_index[name_key] = comp_id
                self._component_to_id[id(component)] = comp_id
                return comp_id

        # Check for duplicate name
        if name_key in self._name_index:
            existing_id = self._name_index[name_key]
            return existing_id

        # Normal registration with generated ID
        comp_id = self._generate_id()
        self._id_map[comp_id] = (comp_type, component)
        self._name_index[name_key] = comp_id
        self._component_to_id[id(component)] = comp_id

        return comp_id

    def _check_dependencies(self, comp_id: str, comp_type: str, component: Any) -> List[str]:
        """Check what components depend on this component."""
        deps = []

        if comp_type == 'tensor_type':
            component_name = getattr(component, 'name', None)
            for fifo_name, fifo in self.program.fifos.items():
                if isinstance(fifo.obj_type, str) and fifo.obj_type == component_name:
                    deps.append(f"FIFO '{fifo_name}'")

        elif comp_type == 'tile':
            for worker in self.program.workers.values():
                if hasattr(worker, 'placement') and worker.placement == component:
                    deps.append(f"Worker '{worker.name}'")

        elif comp_type == 'fifo':
            for worker in self.program.workers.values():
                if hasattr(worker, 'fn_args'):
                    for arg in worker.fn_args:
                        if isinstance(arg, FifoBinding) and arg.fifo == component:
                            deps.append(f"Worker '{worker.name}'")
                            break

        elif comp_type == 'external_kernel':
            # Check if any CoreFunctions use this kernel
            for func in self.program.core_functions.values():
                if hasattr(func, 'kernel_call') and func.kernel_call:
                    if hasattr(func, 'parameters') and len(func.parameters) > 0:
                        for worker in self.program.workers.values():
                            if hasattr(worker, 'core_fn') and worker.core_fn == func:
                                if hasattr(worker, 'fn_args') and len(worker.fn_args) > 0:
                                    first_arg = worker.fn_args[0]
                                    if first_arg == component.name or first_arg == component:
                                        deps.append(f"Worker '{worker.name}' via CoreFunction '{func.name}'")
                                        break

        elif comp_type == 'core_function':
            # Check if any Workers use this core function
            for worker in self.program.workers.values():
                if hasattr(worker, 'core_fn'):
                    if worker.core_fn == component or (isinstance(worker.core_fn, str) and worker.core_fn == component.name):
                        deps.append(f"Worker '{worker.name}'")

        elif comp_type == 'worker':
            # Check if Runtime references this worker
            if self.program.runtime and hasattr(self.program.runtime, 'workers'):
                for worker_ref in self.program.runtime.workers:
                    if worker_ref == component or (isinstance(worker_ref, str) and worker_ref == component.name):
                        deps.append(f"Runtime '{self.program.runtime.name}'")
                        break

        return deps

    def add_symbol(self, name: str, value: Any, type_hint: Optional[str] = None,
                   is_constant: bool = False, provided_id: Optional[str] = None) -> BuilderResult:
        """
        Add or update a symbol (variable, constant, type alias) in the program.

        Args:
            name: Symbol name
            value: Symbol value
            type_hint: Optional type hint
            is_constant: Whether this is a constant
            provided_id: Optional ID for update operation

        Returns:
            BuilderResult with component ID and symbol object.

        Notes:
            If provided_id exists, updates the component with that ID (keeping dependencies intact).
            If provided_id is new or None, creates a new component.
            If name exists without provided_id, returns duplicate error.
        """
        comp_type = 'constant' if is_constant else 'symbol'

        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.symbols:
                del self.program.symbols[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.symbols:
            name_key = (comp_type, name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, comp_type, existing_id)

        # Create new symbol
        symbol = Symbol(
            name=name,
            value=value,
            type_hint=type_hint,
            is_constant=is_constant
        )
        self.program.symbols[name] = symbol

        # Register with provided ID or generate new one
        comp_id = self._register_component(comp_type, name, symbol, provided_id=provided_id)

        return BuilderResult.ok(comp_id, symbol)

    def add_constant(self, name: str, value: Union[int, float, str],
                     type_hint: Optional[str] = None, provided_id: Optional[str] = None) -> BuilderResult:
        """
        Add or update a constant in the program.

        Args:
            name: Constant name
            value: Constant value
            type_hint: Optional type hint
            provided_id: Optional ID for update operation

        Returns:
            BuilderResult with component ID and symbol object
        """
        return self.add_symbol(name, value, type_hint, is_constant=True, provided_id=provided_id)

    def add_tensor_type(self, name: str, shape: List[Union[int, str]],
                        dtype: Union[str, DataType],
                        layout: Optional[str] = None, provided_id: Optional[str] = None) -> BuilderResult:
        """
        Add or update a tensor type definition in the program.

        Args:
            name: Type name
            shape: Shape dimensions (can be symbolic)
            dtype: Data type
            layout: Optional layout hint
            provided_id: Optional ID for update operation

        Returns:
            BuilderResult with component ID and TensorType object

        Example:
            >>> result = builder.add_tensor_type("chunk_ty", shape=[1024], dtype="int32")
            >>> tensor_id = result.id  # Use this ID for further operations
        """
        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.symbols:
                del self.program.symbols[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.symbols:
            name_key = ('tensor_type', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'tensor_type', existing_id)

        # Create new tensor type
        tensor_ty = make_tensor_type(shape, dtype, layout)
        symbol = Symbol(name=name, value=tensor_ty, type_hint="TensorType")
        self.program.symbols[name] = symbol

        # Register with provided ID or generate new one
        comp_id = self._register_component('tensor_type', name, symbol, provided_id=provided_id)

        return BuilderResult.ok(comp_id, tensor_ty)

    def add_tile(self, name: str, kind: Union[str, TileKind],
                 x: int, y: int, provided_id: Optional[str] = None, **metadata) -> BuilderResult:
        """
        Add or update a tile in the program.

        Args:
            name: Unique tile name
            kind: Tile kind (shim, mem, compute)
            x: Column coordinate
            y: Row coordinate
            provided_id: Optional ID for update operation
            **metadata: Additional properties

        Returns:
            BuilderResult with component ID and Tile object
        """
        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.tiles:
                del self.program.tiles[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.tiles:
            name_key = ('tile', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'tile', existing_id)

        # Create new tile
        if isinstance(kind, str):
            kind = TileKind(kind.lower())

        tile = Tile(name=name, kind=kind, x=x, y=y, metadata=metadata)
        self.program.tiles[name] = tile

        # Register with provided ID or generate new one
        comp_id = self._register_component('tile', name, tile, provided_id=provided_id)

        return BuilderResult.ok(comp_id, tile)

    def add_fifo(self, name: str, obj_type: Union[AnyType, str],
                 depth: int = 2, producer: Optional[Union[Tile, str]] = None,
                 consumers: Optional[List[Union[Tile, str]]] = None,
                 provided_id: Optional[str] = None, **metadata) -> BuilderResult:
        """
        Add or update an ObjectFifo in the program.

        Args:
            name: Unique FIFO name
            obj_type: Type of data (TensorType or type name)
            depth: Buffer depth (default 2)
            producer: Optional producer tile
            consumers: Optional consumer tiles
            provided_id: Optional ID for update operation
            **metadata: Additional properties (context, direction, etc.)

        Returns:
            BuilderResult with component ID and ObjectFifo object
        """
        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.fifos:
                del self.program.fifos[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.fifos:
            name_key = ('fifo', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'fifo', existing_id)

        # Resolve tile references
        if isinstance(producer, str):
            producer = self.program.tiles.get(producer)

        if consumers:
            resolved_consumers = []
            for c in consumers:
                if isinstance(c, str):
                    c = self.program.tiles.get(c)
                if c:
                    resolved_consumers.append(c)
            consumers = resolved_consumers
        else:
            consumers = []

        # Create new FIFO
        fifo = ObjectFifo(
            name=name,
            obj_type=obj_type,
            depth=depth,
            producer=producer,
            consumers=consumers,
            metadata=metadata
        )
        self.program.fifos[name] = fifo

        # Register with provided ID or generate new one
        comp_id = self._register_component('fifo', name, fifo, provided_id=provided_id)

        return BuilderResult.ok(comp_id, fifo)

    def add_fifo_split(self, name: str, source: Union[ObjectFifo, str],
                       num_outputs: int, output_type: Union[AnyType, str],
                       output_names: List[str], offsets: List[Union[int, str]],
                       placement: Union[Tile, str], provided_id: Optional[str] = None,
                       **metadata) -> BuilderResult:
        """
        Add or update a split operation on a FIFO.

        Args:
            name: Name of split result
            source: Source FIFO to split
            num_outputs: Number of outputs
            output_type: Type of each output
            output_names: Names for each output
            offsets: Byte offsets for each output
            placement: Tile where split occurs
            provided_id: Optional ID for update operation
            **metadata: Additional properties

        Returns:
            BuilderResult with component ID and SplitOperation object

        Notes:
            If provided_id exists, updates the component with that ID (keeping dependencies intact).
            If provided_id is new or None, creates a new component.
            If name exists without provided_id, returns duplicate error.
        """
        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.symbols:
                del self.program.symbols[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.symbols:
            name_key = ('fifo_split', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'fifo_split', existing_id)

        if isinstance(source, str):
            source = self.program.fifos.get(source, source)
        if isinstance(placement, str):
            placement = self.program.tiles.get(placement, placement)

        dims_to_stream = metadata.pop("dims_to_stream", None) or None

        split_op = SplitOperation(
            name=name,
            source=source,
            num_outputs=num_outputs,
            output_type=output_type,
            output_names=output_names,
            offsets=offsets,
            placement=placement,
            dims_to_stream=dims_to_stream,
            metadata=metadata
        )

        # Store split result as a symbol so it can be referenced
        symbol = Symbol(name=name, value=split_op, type_hint="SplitOperation")
        self.program.symbols[name] = symbol

        # Register with provided ID or generate new one
        comp_id = self._register_component('fifo_split', name, symbol, provided_id=provided_id)

        return BuilderResult.ok(comp_id, split_op)

    def add_fifo_join(self, name: str, dest: Union[ObjectFifo, str],
                      num_inputs: int, input_type: Union[AnyType, str],
                      input_names: List[str], offsets: List[Union[int, str]],
                      placement: Union[Tile, str], provided_id: Optional[str] = None,
                      **metadata) -> BuilderResult:
        """
        Add or update a join operation on a FIFO.

        Args:
            name: Name of join result
            dest: Destination FIFO to join into
            num_inputs: Number of inputs
            input_type: Type of each input
            input_names: Names for each input
            offsets: Byte offsets for each input
            placement: Tile where join occurs
            provided_id: Optional ID for update operation
            **metadata: Additional properties

        Returns:
            BuilderResult with component ID and JoinOperation object

        Notes:
            If provided_id exists, updates the component with that ID (keeping dependencies intact).
            If provided_id is new or None, creates a new component.
            If name exists without provided_id, returns duplicate error.
        """
        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.symbols:
                del self.program.symbols[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.symbols:
            name_key = ('fifo_join', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'fifo_join', existing_id)

        if isinstance(dest, str):
            dest = self.program.fifos.get(dest, dest)
        if isinstance(placement, str):
            placement = self.program.tiles.get(placement, placement)

        dims_from_stream = metadata.pop("dims_from_stream", None) or None

        join_op = JoinOperation(
            name=name,
            dest=dest,
            num_inputs=num_inputs,
            input_type=input_type,
            input_names=input_names,
            offsets=offsets,
            placement=placement,
            dims_from_stream=dims_from_stream,
            metadata=metadata
        )

        # Store join result as a symbol
        symbol = Symbol(name=name, value=join_op, type_hint="JoinOperation")
        self.program.symbols[name] = symbol

        # Register with provided ID or generate new one
        comp_id = self._register_component('fifo_join', name, symbol, provided_id=provided_id)

        return BuilderResult.ok(comp_id, join_op)

    def add_fifo_forward(self, name: str, source: Union[ObjectFifo, str],
                         placement: Optional[Union[Tile, str]] = None,
                         provided_id: Optional[str] = None, **metadata) -> BuilderResult:
        """
        Add or update a forward operation on a FIFO.

        Args:
            name: Name of forward result
            source: Source FIFO to forward
            placement: Optional tile where the forward occurs (e.g., a mem tile)
            provided_id: Optional ID for update operation
            **metadata: Additional properties

        Returns:
            BuilderResult with component ID and ForwardOperation object

        Notes:
            If provided_id exists, updates the component with that ID (keeping dependencies intact).
            If provided_id is new or None, creates a new component.
            If name exists without provided_id, returns duplicate error.
        """
        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.symbols:
                del self.program.symbols[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.symbols:
            name_key = ('fifo_forward', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'fifo_forward', existing_id)

        if isinstance(source, str):
            source = self.program.fifos.get(source, source)
        if isinstance(placement, str):
            placement = self.program.tiles.get(placement, placement)

        dims_to_stream = metadata.pop("dims_to_stream", None) or None
        dims_from_stream = metadata.pop("dims_from_stream", None) or None

        forward_op = ForwardOperation(
            name=name,
            source=source,
            placement=placement,
            dims_to_stream=dims_to_stream,
            dims_from_stream=dims_from_stream,
            metadata=metadata
        )

        # Store forward result as a symbol
        symbol = Symbol(name=name, value=forward_op, type_hint="ForwardOperation")
        self.program.symbols[name] = symbol

        # Register with provided ID or generate new one
        comp_id = self._register_component('fifo_forward', name, symbol, provided_id=provided_id)

        return BuilderResult.ok(comp_id, forward_op)

    def add_tiler2d(self, name: str,
                    tensor_dims: List[Union[int, str]],
                    tile_dims: List[Union[int, str]],
                    tile_counts: List[Union[int, str]],
                    pattern_repeat: Optional[Union[int, str]] = None,
                    prune_step: bool = False,
                    index: int = 0,
                    provided_id: Optional[str] = None,
                    **metadata) -> BuilderResult:
        """
        Add a TensorTiler2D.group_tiler() specification.

        Creates a TensorTiler2DSpec that generates the Python call:
            name = TensorTiler2D.group_tiler(
                tensor_dims, tile_dims, tile_counts,
                [pattern_repeat=<value>,]
                prune_step=<bool>
            )[index]

        Args:
            name: Variable name for this tiler (e.g., "a_tap")
            tensor_dims: Full tensor dimensions (2 elements, can be symbolic)
            tile_dims: Tile dimensions (2 elements)
            tile_counts: Number of tiles per dimension (2 elements, can be symbolic)
            pattern_repeat: Optional repeat count
            prune_step: Whether to prune steps (usually False)
            index: Index into the returned list (usually 0)
            provided_id: Optional ID for update operation
            **metadata: Additional properties

        Returns:
            BuilderResult with component ID and TensorTiler2DSpec object
        """
        # If ID provided and exists, remove old component from program dict
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.symbols:
                del self.program.symbols[old_name]
        elif name in self.program.symbols:
            name_key = ('tiler2d', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'tiler2d', existing_id)

        tiler = TensorTiler2DSpec(
            name=name,
            tensor_dims=tensor_dims,
            tile_dims=tile_dims,
            tile_counts=tile_counts,
            pattern_repeat=pattern_repeat,
            prune_step=prune_step,
            index=index,
            metadata=metadata
        )

        symbol = Symbol(name=name, value=tiler, type_hint="TensorTiler2DSpec")
        self.program.symbols[name] = symbol

        comp_id = self._register_component('tiler2d', name, symbol, provided_id=provided_id)

        return BuilderResult.ok(comp_id, tiler)

    def add_tap(self, name: str,
                tensor_dims: List[Union[int, str]],
                offset: Union[int, str],
                sizes: List[Union[int, str]],
                strides: List[Union[int, str]],
                prune_step: bool = False,
                index: int = 0,
                use_tiler2d: bool = True,
                provided_id: Optional[str] = None,
                **metadata) -> BuilderResult:
        """
        Add a Tensor Access Pattern (TAP) to the symbol table.
        
        This method accepts TensorAccessPattern parameters and can either:
        1. Convert to TensorTiler2DSpec for optimized 2D tiling (use_tiler2d=True)
        2. Store as TensorAccessPattern for direct code generation (use_tiler2d=False)
        
        The TAP is stored in the symbol table and can be referenced by name in
        fill/drain operations.
        
        Args:
            name: Variable name for this TAP (e.g., "a_tap")
            tensor_dims: Full tensor dimensions (can be symbolic)
            offset: Starting offset into the tensor
            sizes: Access pattern sizes (can be symbolic)
            strides: Access pattern strides (can be symbolic)
            prune_step: Whether to prune steps (for TensorTiler2D, usually False)
            index: Index into the returned list (for TensorTiler2D, usually 0)
            use_tiler2d: If True, convert to TensorTiler2D; if False, use TensorAccessPattern
            provided_id: Optional ID for update operation
            **metadata: Additional properties
        
        Returns:
            BuilderResult with component ID and TAP object (TensorTiler2DSpec or TensorAccessPattern)
        
        Examples:
            # Using TensorTiler2D (optimized for 2D regular tiling)
            builder.add_tap(
                name="my_tap",
                tensor_dims=[256, 256],
                offset=0,
                sizes=[32, 32],
                strides=[256, 1],
                use_tiler2d=True  # Default
            )
            
            # Using TensorAccessPattern (for complex patterns or non-2D)
            builder.add_tap(
                name="complex_tap",
                tensor_dims=[256, 256, 256],
                offset=128,
                sizes=[32, 32, 32],
                strides=[256*256, 256, 1],
                use_tiler2d=False
            )
        """
        from .operations import TensorAccessPattern, convert_tap_to_tiler2d
        
        # If ID provided and exists, remove old component from program dict
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.symbols:
                del self.program.symbols[old_name]
        elif name in self.program.symbols:
            name_key = ('tap', name)
            existing_id = self._name_index.get(name_key, "")
            # Fallback: scan _id_map if _name_index lost the entry (e.g. after partial reset)
            if not existing_id:
                for cid, (ctype, comp) in self._id_map.items():
                    if ctype == 'tap' and getattr(comp, 'name', None) == name:
                        existing_id = cid
                        self._name_index[name_key] = cid  # repair index
                        break
            return BuilderResult.duplicate(name, 'tap', existing_id)
        
        # Create TensorAccessPattern
        tap = TensorAccessPattern(
            tensor_dims=tensor_dims,
            offset=offset,
            sizes=sizes,
            strides=strides,
            name=name,
            metadata=metadata
        )
        
        if use_tiler2d:
            # Convert to TensorTiler2DSpec
            try:
                tiler = convert_tap_to_tiler2d(tap, name, prune_step=prune_step, index=index)
            except ValueError as e:
                return BuilderResult.error(
                    ErrorCode.INVALID_PARAMETER,
                    f"Failed to convert TAP to TensorTiler2D: {str(e)}"
                )
            
            # Store in symbol table as TensorTiler2DSpec
            symbol = Symbol(name=name, value=tiler, type_hint="TensorTiler2DSpec")
            self.program.symbols[name] = symbol
            
            comp_id = self._register_component('tap', name, symbol, provided_id=provided_id)
            
            return BuilderResult.ok(comp_id, tiler)
        else:
            # Store as TensorAccessPattern
            symbol = Symbol(name=name, value=tap, type_hint="TensorAccessPattern")
            self.program.symbols[name] = symbol
            
            comp_id = self._register_component('tap', name, symbol, provided_id=provided_id)
            
            return BuilderResult.ok(comp_id, tap)


    def add_external_kernel(self, name: str, kernel_name: str,
                            source_file: str, arg_types: List[Union[AnyType, str]],
                            include_dirs: Optional[List[str]] = None,
                            provided_id: Optional[str] = None,
                            **metadata) -> BuilderResult:
        """
        Add or update an external kernel declaration.

        Args:
            name: Unique kernel name
            kernel_name: C/C++ function name
            source_file: Path to source file
            arg_types: Argument types
            include_dirs: Optional include directories
            provided_id: Optional ID for update operation
            **metadata: Additional properties

        Returns:
            BuilderResult with component ID and ExternalKernel object

        Notes:
            If provided_id exists, updates the component with that ID (keeping dependencies intact).
            If provided_id is new or None, creates a new component.
            If name exists without provided_id, returns duplicate error.
        """
        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.external_kernels:
                del self.program.external_kernels[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.external_kernels:
            name_key = ('external_kernel', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'external_kernel', existing_id)

        kernel = ExternalKernel(
            name=name,
            kernel_name=kernel_name,
            source_file=source_file,
            arg_types=arg_types,
            include_dirs=include_dirs or [],
            metadata=metadata
        )
        self.program.external_kernels[name] = kernel

        # Register with provided ID or generate new one
        comp_id = self._register_component('external_kernel', name, kernel, provided_id=provided_id)

        return BuilderResult.ok(comp_id, kernel)

    def add_core_function(self, name: str, parameters: List[str],
                          acquires: Optional[List[tuple]] = None,
                          kernel_call: Optional[tuple] = None,
                          releases: Optional[List[tuple]] = None,
                          body_stmts: Optional[List[Any]] = None,
                          provided_id: Optional[str] = None,
                          **metadata) -> BuilderResult:
        """
        Add or update a core function definition.

        Args:
            name: Function name
            parameters: Parameter names (first is kernel, rest are FIFOs)
            acquires: List of (fifo_param, count, local_var) tuples (flat mode)
            kernel_call: Tuple of (kernel_param, args_list) (flat mode)
            releases: List of (fifo_param, count) tuples (flat mode)
            body_stmts: Explicit body statement list (body mode, overrides flat fields).
                        May contain Acquire, Release, KernelCall, ForLoop, Assignment objects.
            provided_id: Optional ID for update operation
            **metadata: Additional properties

        Returns:
            BuilderResult with component ID and CoreFunction object

        Notes:
            If provided_id exists, updates the component with that ID (keeping dependencies intact).
            If provided_id is new or None, creates a new component.
            If name exists without provided_id, returns duplicate error.

        Example (flat mode):
            >>> builder.add_core_function(
            ...     "add_fn",
            ...     parameters=["kernel", "fifoA", "fifoB", "fifoC"],
            ...     acquires=[("fifoA", 1, "elemA"), ("fifoB", 1, "elemB"), ("fifoC", 1, "elemC")],
            ...     kernel_call=("kernel", ["elemA", "elemB", "elemC"]),
            ...     releases=[("fifoA", 1), ("fifoB", 1), ("fifoC", 1)]
            ... )

        Example (body mode with nested loops):
            >>> from hlir.core import Acquire, Release, KernelCall, ForLoop, Assignment
            >>> builder.add_core_function(
            ...     "core_fn",
            ...     parameters=["a_in", "b_in", "c_out", "matvec"],
            ...     body_stmts=[
            ...         Acquire("c_out", 1, "elem_out"),
            ...         ForLoop("i", "m", [Assignment("elem_out", "i", 0)]),
            ...         ForLoop("_", "K_div_k", [
            ...             Acquire("a_in", 1, "elem_a"),
            ...             Acquire("b_in", 1, "elem_b"),
            ...             KernelCall("matvec", ["elem_a", "elem_b", "elem_out"]),
            ...             Release("a_in", 1),
            ...             Release("b_in", 1),
            ...         ]),
            ...         Release("c_out", 1),
            ...     ]
            ... )
        """
        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.core_functions:
                del self.program.core_functions[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.core_functions:
            name_key = ('core_function', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'core_function', existing_id)

        # If body_stmts is provided, use body mode
        if body_stmts is not None:
            func = CoreFunction(
                name=name,
                parameters=parameters,
                body_stmts=body_stmts,
                metadata=metadata
            )
        else:
            # Convert tuples to proper objects (flat mode)
            acquire_objs = []
            if acquires:
                for fifo_param, count, local_var in acquires:
                    acquire_objs.append(Acquire(fifo_param, count, local_var))

            kernel_call_obj = None
            if kernel_call:
                kernel_param, args = kernel_call
                kernel_call_obj = KernelCall(kernel_param, args)

            release_objs = []
            if releases:
                for fifo_param, count in releases:
                    release_objs.append(Release(fifo_param, count))

            func = CoreFunction(
                name=name,
                parameters=parameters,
                acquires=acquire_objs,
                kernel_call=kernel_call_obj,
                releases=release_objs,
                metadata=metadata
            )

        self.program.core_functions[name] = func

        # Register with provided ID or generate new one
        comp_id = self._register_component('core_function', name, func, provided_id=provided_id)

        return BuilderResult.ok(comp_id, func)

    def add_worker(self, name: str, core_fn: Union[CoreFunction, str],
                   fn_args: List[Union[str, tuple]], placement: Union[Tile, str],
                   provided_id: Optional[str] = None, **metadata) -> BuilderResult:
        """
        Add or update a worker to the program.

        Args:
            name: Unique worker name
            core_fn: CoreFunction to execute (or name)
            fn_args: List of arguments - first is kernel name, rest are (fifo, mode, index) tuples
            placement: Tile where worker executes
            provided_id: Optional ID for update operation
            **metadata: Additional properties

        Returns:
            BuilderResult with component ID and Worker object

        Notes:
            If provided_id exists, updates the component with that ID (keeping dependencies intact).
            If provided_id is new or None, creates a new component.
            If name exists without provided_id, returns duplicate error.

        Example:
            >>> builder.add_worker(
            ...     "worker_0",
            ...     core_fn="add_fn",
            ...     fn_args=["kernel", ("fifo_a", "cons", 0), ("fifo_b", "cons", 0), ("fifo_c", "prod", None)],
            ...     placement="tile_compute0"
            ... )
        """
        # If ID provided and exists, remove old component from program dict (update operation)
        if provided_id and provided_id in self._id_map:
            _, old_component = self._id_map[provided_id]
            old_name = getattr(old_component, 'name', None)
            if old_name and old_name in self.program.workers:
                del self.program.workers[old_name]
        # Check for duplicate name only if not updating
        elif name in self.program.workers:
            name_key = ('worker', name)
            existing_id = self._name_index.get(name_key, "")
            return BuilderResult.duplicate(name, 'worker', existing_id)

        # Resolve references
        if isinstance(core_fn, str):
            core_fn = self.program.core_functions.get(core_fn, core_fn)
        if isinstance(placement, str):
            placement = self.program.tiles.get(placement, placement)

        # Convert fn_args tuples to proper objects
        processed_args = []
        for arg in fn_args:
            if isinstance(arg, tuple):
                # It's a FIFO binding: (fifo, mode, index)
                fifo, mode, index = arg
                if isinstance(fifo, str):
                    fifo = self.program.fifos.get(fifo, fifo)
                if isinstance(mode, str):
                    mode = FifoAccessMode(mode.lower())
                processed_args.append(FifoBinding(fifo, mode, index))
            elif isinstance(arg, str):
                # It's a reference to external kernel or other symbol
                processed_args.append(arg)
            else:
                processed_args.append(arg)

        worker = Worker(
            name=name,
            core_fn=core_fn,
            fn_args=processed_args,
            placement=placement,
            metadata=metadata
        )
        self.program.workers[name] = worker

        # Register with provided ID or generate new one
        comp_id = self._register_component('worker', name, worker, provided_id=provided_id)

        return BuilderResult.ok(comp_id, worker)

    def create_runtime(self, name: str = "runtime") -> 'RuntimeBuilder':
        """
        Create a runtime sequence builder.

        Args:
            name: Runtime name

        Returns:
            RuntimeBuilder for fluent construction
        """
        return RuntimeBuilder(self, name)

    def set_runtime(self, runtime: RuntimeSequence) -> 'ProgramBuilder':
        """
        Set the program's runtime sequence.

        Args:
            runtime: RuntimeSequence to set

        Returns:
            Self for chaining
        """
        self.program.runtime = runtime
        return self

    def build(self) -> Program:
        """
        Build and validate the final program.

        Returns:
            Constructed Program

        Raises:
            ValueError: If program is invalid
        """
        errors = self.program.validate()
        if errors:
            error_msg = "\n  - ".join(["Program validation failed:"] + errors)
            raise ValueError(error_msg)

        return self.program

    def get_program(self) -> Program:
        """Get the current program (without validation)."""
        return self.program


class RuntimeBuilder:
    """
    Builder for constructing runtime sequences.

    Example:
        rt = builder.create_runtime()
        rt.add_input_type("vector_ty", [4096], "int32")
        rt.add_output_type("vector_ty", [4096], "int32")
        rt.add_params(["a_in", "c_out"])
        rt.add_worker("worker_0")
        rt.add_fill("fill_0", "fifo_in", "a_in", "shim0")
        rt.add_drain("drain_0", "fifo_out", "c_out", "shim0", wait=True)
        rt.build()
    """

    def __init__(self, prog_builder: ProgramBuilder, name: str):
        """
        Initialize runtime builder.

        Args:
            prog_builder: Parent program builder
            name: Runtime name
        """
        self.prog_builder = prog_builder
        self.runtime = RuntimeSequence(name=name)

    def add_input_type(self, type_ref: Union[AnyType, str]) -> 'RuntimeBuilder':
        """
        Add an input type to the runtime sequence.

        Args:
            type_ref: Type reference or TensorType

        Returns:
            Self for chaining
        """
        self.runtime.input_types.append(type_ref)
        return self

    def add_output_type(self, type_ref: Union[AnyType, str]) -> 'RuntimeBuilder':
        """
        Add an output type to the runtime sequence.

        Args:
            type_ref: Type reference or TensorType

        Returns:
            Self for chaining
        """
        self.runtime.output_types.append(type_ref)
        return self

    def add_params(self, param_names: List[str]) -> 'RuntimeBuilder':
        """
        Set runtime parameter names.

        Args:
            param_names: List of parameter names

        Returns:
            Self for chaining
        """
        self.runtime.param_names = param_names
        return self

    def add_worker(self, worker: Union[Worker, str]) -> 'RuntimeBuilder':
        """
        Add a worker to start in the runtime.

        Args:
            worker: Worker object or name

        Returns:
            Self for chaining
        """
        if isinstance(worker, str):
            worker = self.prog_builder.program.workers.get(worker, worker)
        self.runtime.workers.append(worker)
        return self

    def add_fill(self, name: str, fifo: Union[ObjectFifo, str],
                 source_param: str, placement: Union[Tile, str],
                 tap: Optional[Union[TensorAccessPattern, TensorTiler2DSpec]] = None,
                 **metadata) -> 'RuntimeBuilder':
        """
        Add a fill operation.

        Args:
            name: Operation name
            fifo: Target FIFO
            source_param: Source parameter name
            placement: Tile where fill occurs
            tap: Optional tensor access pattern (TensorAccessPattern or TensorTiler2DSpec)
            **metadata: Additional properties

        Returns:
            Self for chaining
        """
        if isinstance(fifo, str):
            fifo = self.prog_builder.program.fifos.get(fifo, fifo)
        if isinstance(placement, str):
            placement = self.prog_builder.program.tiles.get(placement, placement)

        fill_op = RuntimeFill(
            placement=placement,
            fifo=fifo,
            source_param=source_param,
            tap=tap,
            metadata=metadata
        )
        self.runtime.operations.append(fill_op)
        return self

    def add_drain(self, name: str, fifo: Union[ObjectFifo, str],
                  dest_param: str, placement: Union[Tile, str],
                  wait: bool = True,
                  tap: Optional[Union[TensorAccessPattern, TensorTiler2DSpec]] = None,
                  **metadata) -> 'RuntimeBuilder':
        """
        Add a drain operation.

        Args:
            name: Operation name
            fifo: Source FIFO
            dest_param: Destination parameter name
            placement: Tile where drain occurs
            wait: Whether to wait for completion
            tap: Optional tensor access pattern (TensorAccessPattern or TensorTiler2DSpec)
            **metadata: Additional properties

        Returns:
            Self for chaining
        """
        if isinstance(fifo, str):
            fifo = self.prog_builder.program.fifos.get(fifo, fifo)
        if isinstance(placement, str):
            placement = self.prog_builder.program.tiles.get(placement, placement)

        drain_op = RuntimeDrain(
            placement=placement,
            fifo=fifo,
            dest_param=dest_param,
            wait=wait,
            tap=tap,
            metadata=metadata
        )
        self.runtime.operations.append(drain_op)
        return self

    def build(self) -> RuntimeSequence:
        """
        Build the runtime and attach it to the program.

        Returns:
            Constructed RuntimeSequence
        """
        self.prog_builder.set_runtime(self.runtime)
        return self.runtime


# =============================================================================
# Enhanced ProgramBuilder methods for interactive GUI editing
# =============================================================================

def _remove_component(builder: ProgramBuilder, comp_id: str) -> BuilderResult:
    """Remove a component by ID with dependency checking."""
    if comp_id not in builder._id_map:
        return BuilderResult.not_found(comp_id)

    comp_type, component = builder._id_map[comp_id]
    deps = builder._check_dependencies(comp_id, comp_type, component)
    if deps:
        return BuilderResult.has_dependencies(comp_id, comp_type, deps)

    # Remove from program
    component_name = getattr(component, 'name', None)
    if comp_type in ['constant', 'tensor_type', 'symbol', 'fifo_split', 'fifo_join', 'fifo_forward', 'tiler2d', 'tap'] and component_name in builder.program.symbols:
        del builder.program.symbols[component_name]
    elif comp_type == 'tile' and component_name in builder.program.tiles:
        del builder.program.tiles[component_name]
    elif comp_type == 'fifo' and component_name in builder.program.fifos:
        del builder.program.fifos[component_name]
    elif comp_type == 'external_kernel' and component_name in builder.program.external_kernels:
        del builder.program.external_kernels[component_name]
    elif comp_type == 'core_function' and component_name in builder.program.core_functions:
        del builder.program.core_functions[component_name]
    elif comp_type == 'worker' and component_name in builder.program.workers:
        del builder.program.workers[component_name]

    # Remove from tracking
    del builder._id_map[comp_id]
    if component_name:
        name_key = (comp_type, component_name)
        if name_key in builder._name_index:
            del builder._name_index[name_key]
    if id(component) in builder._component_to_id:
        del builder._component_to_id[id(component)]

    return BuilderResult.ok(comp_id, component)


def _update_fifo(builder: ProgramBuilder, old_id: str, new_id: str) -> BuilderResult:
    """Replace an old FIFO with a new one."""
    if old_id not in builder._id_map:
        return BuilderResult.not_found(old_id)
    if new_id not in builder._id_map:
        return BuilderResult.not_found(new_id)

    old_type, old_fifo = builder._id_map[old_id]
    new_type, new_fifo = builder._id_map[new_id]

    if old_type != 'fifo' or new_type != 'fifo':
        return BuilderResult.error(ErrorCode.INVALID_PARAMETER, "Both IDs must refer to ObjectFifos")

    if old_fifo.name in builder.program.fifos:
        del builder.program.fifos[old_fifo.name]
    builder.program.fifos[new_fifo.name] = new_fifo

    del builder._id_map[old_id]
    old_name_key = ('fifo', old_fifo.name)
    if old_name_key in builder._name_index:
        del builder._name_index[old_name_key]

    return BuilderResult.ok(new_id, new_fifo)


def _lookup_by_id(builder: ProgramBuilder, comp_id: str) -> BuilderResult:
    """Look up component by ID."""
    if comp_id not in builder._id_map:
        return BuilderResult.not_found(comp_id)
    comp_type, component = builder._id_map[comp_id]
    return BuilderResult.ok(comp_id, component)


def _lookup_by_name(builder: ProgramBuilder, comp_type: str, name: str) -> BuilderResult:
    """Look up component by type and name."""
    name_key = (comp_type, name)
    if name_key not in builder._name_index:
        return BuilderResult.error(ErrorCode.NOT_FOUND, f"{comp_type} '{name}' not found")
    comp_id = builder._name_index[name_key]
    _, component = builder._id_map[comp_id]
    return BuilderResult.ok(comp_id, component)


def _get_all_ids(builder: ProgramBuilder, comp_type: Optional[str] = None) -> Dict[str, Any]:
    """Get all component IDs, optionally filtered by type."""
    if comp_type is None:
        return {cid: comp for cid, (_, comp) in builder._id_map.items()}
    else:
        return {cid: comp for cid, (ct, comp) in builder._id_map.items() if ct == comp_type}


# Add methods to ProgramBuilder
ProgramBuilder.remove = lambda self, comp_id: _remove_component(self, comp_id)
ProgramBuilder.update_fifo = lambda self, old_id, new_id: _update_fifo(self, old_id, new_id)
ProgramBuilder.lookup_by_id = lambda self, comp_id: _lookup_by_id(self, comp_id)
ProgramBuilder.lookup_by_name = lambda self, comp_type, name: _lookup_by_name(self, comp_type, name)
ProgramBuilder.get_all_ids = lambda self, comp_type=None: _get_all_ids(self, comp_type)


def ProgramBuilderFromXML(xml_file: str) -> ProgramBuilder:
    """
    Load ProgramBuilder from existing GUI XML file.

    Supports basic GUI XML components:
    - Const, TypeAbstraction
    - ExternalFunction, CoreFunction
    - ObjectFifo, ObjectFifoForward
    - Note: Split, Join, Worker, Runtime import coming soon

    Example:
        builder = ProgramBuilderFromXML("design.xml")
        builder.add_fifo("new_fifo", "chunk_ty", 2)
    """
    from xml.etree.ElementTree import parse as parse_xml
    import re

    def parse_tile(tile_str):
        """Parse 'Tile(x, y)' string to Tile object or return string for builder lookup."""
        if not tile_str:
            return "shim0"  # Default tile reference
        # Try to match Tile(x, y) pattern
        match = re.match(r'Tile\((\d+),\s*(\d+)\)', tile_str.strip())
        if match:
            x = int(match.group(1))
            y = int(match.group(2))
            # Return as string for builder to resolve
            return f"tile_{x}_{y}"
        # Otherwise return as-is (might be a tile name reference)
        return tile_str.strip()

    def safe_text(elem, default=''):
        """Safely get text from element."""
        return elem.text.strip() if elem is not None and elem.text else default

    tree = parse_xml(xml_file)
    root = tree.getroot()
    program_name = root.get('name', 'imported_design')
    builder = ProgramBuilder(program_name)

    # Parse Symbols section
    symbols = root.find('Symbols')
    if symbols is not None:
        # Parse constants
        for const in symbols.findall('Const'):
            name = const.get('name')
            type_hint = const.get('type', 'int')
            value_str = const.text.strip() if const.text else "0"
            value = int(value_str) if type_hint == 'int' else (float(value_str) if type_hint == 'float' else value_str)
            builder.add_constant(name, value, type_hint)

        # Parse type definitions
        for typedef in symbols.findall('TypeAbstraction'):
            name = typedef.get('name')
            ndarray = typedef.find('ndarray')
            if ndarray is not None:
                shape_elem = ndarray.find('shape')
                dtype_elem = ndarray.find('dtype')
                shape = [shape_elem.text.strip()] if shape_elem is not None else []
                dtype = dtype_elem.text.strip() if dtype_elem is not None else 'int32'
                builder.add_tensor_type(name, shape, dtype)

    # Parse DataFlow section
    dataflow = root.find('DataFlow')
    if dataflow is not None:
        # Parse external functions (kernels)
        for ext_func in dataflow.findall('ExternalFunction'):
            name = ext_func.get('name', '')
            kernel_elem = ext_func.find('kernel')
            source_elem = ext_func.find('source')
            arg_types_elem = ext_func.find('arg_types')

            kernel_name = safe_text(kernel_elem)
            source_file = safe_text(source_elem)
            arg_types = []
            if arg_types_elem is not None:
                for type_elem in arg_types_elem.findall('type'):
                    arg_types.append(safe_text(type_elem))

            # Extract metadata from attributes
            metadata = {}
            for key, value in ext_func.attrib.items():
                if key != 'name':
                    metadata[key] = value

            if name:
                builder.add_external_kernel(name, kernel_name, source_file, arg_types, **metadata)

        # Parse core functions
        for core_func in dataflow.findall('CoreFunction'):
            name = core_func.get('name', '')
            params_elem = core_func.find('parameters')
            body_elem = core_func.find('body')

            parameters = []
            if params_elem is not None:
                for param in params_elem.findall('param'):
                    param_name = param.get('name')
                    if param_name:
                        parameters.append(param_name)

            # Parse body operations
            acquires = []
            releases = []
            kernel_call = None

            if body_elem is not None:
                for acq in body_elem.findall('Acquire'):
                    # add_core_function expects tuples: (fifo_param, count, local_var)
                    acquires.append((
                        acq.get('source', ''),
                        int(acq.get('count', '1')),
                        acq.get('name', '')
                    ))

                for rel in body_elem.findall('Release'):
                    # add_core_function expects tuples: (fifo_param, count)
                    releases.append((
                        rel.get('source', ''),
                        int(rel.get('count', '1'))
                    ))

                call_elem = body_elem.find('Call')
                if call_elem is not None:
                    # kernel_call is a tuple: (kernel_param, args_list)
                    args_str = call_elem.get('args', '')
                    args = [arg.strip() for arg in args_str.split(',')] if args_str else []
                    kernel_call = (call_elem.get('function', ''), args)

            # Extract metadata from attributes
            metadata = {}
            for key, value in core_func.attrib.items():
                if key != 'name':
                    metadata[key] = value

            if name:
                builder.add_core_function(name, parameters, acquires, kernel_call, releases, **metadata)

        # Parse ObjectFifos
        for fifo_elem in dataflow.findall('ObjectFifo'):
            name = fifo_elem.get('name', '')
            type_elem = fifo_elem.find('type')
            depth_elem = fifo_elem.find('depth')
            obj_type = safe_text(type_elem, 'int32')
            depth = int(safe_text(depth_elem, '2'))
            if name:
                builder.add_fifo(name, obj_type, depth)

        # Parse ObjectFifoForward operations
        for forward_elem in dataflow.findall('ObjectFifoForward'):
            name = forward_elem.get('name', '')
            source = forward_elem.get('source', '')
            if name and source:
                builder.add_fifo_forward(name, source)

        # Parse ObjectFifoSplit operations
        for split_elem in dataflow.findall('ObjectFifoSplit'):
            name = split_elem.get('name', '')
            source_elem = split_elem.find('source')
            num_outputs_elem = split_elem.find('num_outputs')
            output_type_elem = split_elem.find('output_type')
            placement_elem = split_elem.find('placement')

            source = safe_text(source_elem)
            num_outputs = int(safe_text(num_outputs_elem, '2'))
            output_type = safe_text(output_type_elem, 'int32')
            placement_str = parse_tile(safe_text(placement_elem))

            # Generate output names based on num_outputs
            output_names = [f"{name}_out_{i}" for i in range(num_outputs)]
            # Default offsets (evenly split) - cast to List[Union[int, str]]
            offsets: List[Union[int, str]] = [str(i) for i in range(num_outputs)]

            # Extract metadata from attributes
            metadata = {}
            for key, value in split_elem.attrib.items():
                if key != 'name':
                    metadata[key] = value

            if name and source:
                builder.add_fifo_split(name, source, num_outputs, output_type,
                                       output_names, offsets, placement_str, **metadata)

        # Parse ObjectFifoJoin operations
        for join_elem in dataflow.findall('ObjectFifoJoin'):
            name = join_elem.get('name', '')
            dest_elem = join_elem.find('dest')
            num_inputs_elem = join_elem.find('num_inputs')
            input_type_elem = join_elem.find('input_type')
            placement_elem = join_elem.find('placement')

            dest = safe_text(dest_elem)
            num_inputs = int(safe_text(num_inputs_elem, '2'))
            input_type = safe_text(input_type_elem, 'int32')
            placement_str = parse_tile(safe_text(placement_elem))

            # Generate input names based on num_inputs
            input_names = [f"{name}_in_{i}" for i in range(num_inputs)]
            # Default offsets (evenly split) - cast to List[Union[int, str]]
            offsets: List[Union[int, str]] = [str(i) for i in range(num_inputs)]

            # Extract metadata from attributes
            metadata = {}
            for key, value in join_elem.attrib.items():
                if key != 'name':
                    metadata[key] = value

            if name and dest:
                builder.add_fifo_join(name, dest, num_inputs, input_type,
                                      input_names, offsets, placement_str, **metadata)

        # Parse Workers
        for worker_elem in dataflow.findall('Worker'):
            name = worker_elem.get('name', '')
            cf_elem = worker_elem.find('core_function')
            args_elem = worker_elem.find('arguments')
            placement_elem = worker_elem.find('placement')

            core_fn = safe_text(cf_elem)
            placement_str = parse_tile(safe_text(placement_elem))

            # Parse arguments - format: [str | (fifo, mode, index)]
            fn_args: List[Union[str, tuple]] = []
            if args_elem is not None:
                for arg in args_elem.findall('arg'):
                    ref = arg.get('ref', '')
                    index = arg.get('index')
                    mode = arg.get('mode')

                    if mode:
                        # It's a FIFO binding - tuple format: (fifo, mode, index)
                        # Convert GUI XML mode (consumer/producer) to enum value (cons/prod)
                        mode_abbrev = "cons" if mode.lower() == "consumer" else "prod"
                        fn_args.append((
                            ref,
                            mode_abbrev,
                            int(index) if index else None
                        ))
                    else:
                        # It's a simple reference (e.g., external function)
                        fn_args.append(ref)

            # Extract metadata from attributes
            metadata = {}
            for key, value in worker_elem.attrib.items():
                if key != 'name':
                    metadata[key] = value

            if name and core_fn:
                builder.add_worker(name, core_fn, fn_args, placement_str, **metadata)

        # Parse Runtime
        runtime_elem = dataflow.find('Runtime')
        if runtime_elem is not None:
            rt_name = runtime_elem.get('name', 'runtime')
            seq_elem = runtime_elem.find('Sequence')

            if seq_elem is not None:
                # Parse input types and parameter names
                inputs_str = seq_elem.get('inputs', '')
                params_str = seq_elem.get('as', '')

                input_types = [t.strip() for t in inputs_str.split(',')] if inputs_str else []
                param_names = [p.strip() for p in params_str.split(',')] if params_str else []

                rt = builder.create_runtime(rt_name)

                # Add input/output types
                for input_type in input_types:
                    rt.add_input_type(input_type)

                # Add parameters
                if param_names:
                    rt.add_params(param_names)

                # Parse Start/StartWorkers (support both for backwards compatibility)
                start_workers_elem = seq_elem.find('Start') or seq_elem.find('StartWorkers')
                if start_workers_elem is not None and start_workers_elem.text:
                    worker_names = [w.strip() for w in start_workers_elem.text.split(',')]
                    for worker_name in worker_names:
                        if worker_name:
                            rt.add_worker(worker_name)

                # Parse Fill operations
                for fill_elem in seq_elem.findall('Fill'):
                    target = fill_elem.get('target', '')
                    source = fill_elem.get('source', '')
                    placement_elem = fill_elem.find('placement')
                    placement_str = parse_tile(safe_text(placement_elem))

                    # Extract metadata from attributes
                    metadata = {}
                    for key, value in fill_elem.attrib.items():
                        if key not in ['target', 'source']:
                            metadata[key] = value

                    # Parse TAP if present
                    tap = None
                    tap_elem = fill_elem.find('TensorAccessPattern')
                    if tap_elem is not None:
                        dims_elem = tap_elem.find('tensor_dims')
                        offset_elem = tap_elem.find('offset')
                        sizes_elem = tap_elem.find('sizes')
                        strides_elem = tap_elem.find('strides')

                        dims_text = safe_text(dims_elem, '')
                        offset_text = safe_text(offset_elem, '0')
                        sizes_text = safe_text(sizes_elem, '')
                        strides_text = safe_text(strides_elem, '')

                        # Cast to List[Union[int, str]] for type compatibility
                        tensor_dims: List[Union[int, str]] = [d.strip() for d in dims_text.split(',')] if dims_text else []
                        sizes_list: List[Union[int, str]] = [s.strip() for s in sizes_text.split(',')] if sizes_text else []
                        strides_list: List[Union[int, str]] = [s.strip() for s in strides_text.split(',')] if strides_text else []

                        tap = TensorAccessPattern(
                            tensor_dims=tensor_dims,
                            offset=offset_text,
                            sizes=sizes_list,
                            strides=strides_list
                        )

                    if target and source:
                        rt.add_fill(name=f"fill_{target}", fifo=target, source_param=source,
                                    placement=placement_str, tap=tap, **metadata)

                # Parse Drain operations
                for drain_elem in seq_elem.findall('Drain'):
                    source = drain_elem.get('source', '')
                    target = drain_elem.get('target', '')
                    wait_elem = drain_elem.find('wait')
                    wait = wait_elem is not None and safe_text(wait_elem).lower() == 'true'
                    placement_elem = drain_elem.find('placement')
                    placement_str = parse_tile(safe_text(placement_elem))

                    # Extract metadata from attributes
                    metadata = {}
                    for key, value in drain_elem.attrib.items():
                        if key not in ['source', 'target']:
                            metadata[key] = value

                    # Parse TAP if present
                    tap = None
                    tap_elem = drain_elem.find('TensorAccessPattern')
                    if tap_elem is not None:
                        dims_elem = tap_elem.find('tensor_dims')
                        offset_elem = tap_elem.find('offset')
                        sizes_elem = tap_elem.find('sizes')
                        strides_elem = tap_elem.find('strides')

                        dims_text = safe_text(dims_elem, '')
                        offset_text = safe_text(offset_elem, '0')
                        sizes_text = safe_text(sizes_elem, '')
                        strides_text = safe_text(strides_elem, '')

                        # Cast to List[Union[int, str]] for type compatibility
                        tensor_dims: List[Union[int, str]] = [d.strip() for d in dims_text.split(',')] if dims_text else []
                        sizes_list: List[Union[int, str]] = [s.strip() for s in sizes_text.split(',')] if sizes_text else []
                        strides_list: List[Union[int, str]] = [s.strip() for s in strides_text.split(',')] if strides_text else []

                        tap = TensorAccessPattern(
                            tensor_dims=tensor_dims,
                            offset=offset_text,
                            sizes=sizes_list,
                            strides=strides_list
                        )

                    if source and target:
                        rt.add_drain(name=f"drain_{source}", fifo=source, dest_param=target,
                                     placement=placement_str, wait=wait, tap=tap, **metadata)

                # Build the runtime
                rt.build()

    return builder
