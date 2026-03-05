"""
Core semantic classes for AIECAD HLIR.

These classes represent the fundamental building blocks of an AIECAD design
at a semantic level, completely independent of XML representation.
"""

from dataclasses import dataclass, field
from typing import List, Dict, Optional, Union, Any
from enum import Enum
from .types import AnyType, TensorType, DataType


class TileKind(Enum):
    """Types of tiles in the AIE array."""
    SHIM = "shim"           # Interface tiles (row 0)
    MEM = "mem"             # Memory tiles
    COMPUTE = "compute"     # Compute tiles


@dataclass
class Tile:
    """
    Represents a physical tile in the AIE array.

    Attributes:
        name: Unique identifier for this tile
        kind: Type of tile (shim, mem, compute)
        x: Column coordinate
        y: Row coordinate
        metadata: Optional additional properties
    """
    name: str
    kind: TileKind
    x: int
    y: int
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self):
        if isinstance(self.kind, str):
            self.kind = TileKind(self.kind)

    def __str__(self):
        return f"Tile({self.x}, {self.y})<{self.kind.value}>"

    def __hash__(self):
        return hash((self.name, self.x, self.y))


@dataclass
class Symbol:
    """
    Represents a declared symbol (variable, type alias, constant).

    Symbols are used for type definitions, constants, and other
    named entities that need to be referenced.
    """
    name: str
    value: Any
    type_hint: Optional[str] = None  # e.g., "int", "TensorType", etc.
    is_constant: bool = False

    def __str__(self):
        return f"Symbol({self.name}: {self.type_hint or 'Any'})"


@dataclass
class ObjectFifo:
    """
    Represents a data movement channel (FIFO) in the AIE array.

    ObjectFifos are the edges in the dataflow graph. They connect
    producers to consumers and define the data movement paths.

    Attributes:
        name: Unique identifier
        obj_type: Type of data flowing through (TensorType, type name, etc.)
        depth: Number of buffers (typically 2)
        producer: Optional tile that produces data
        consumers: List of tiles that consume data
        metadata: Additional properties (context, direction, etc.)
    """
    name: str
    obj_type: Union[AnyType, str]
    depth: int = 2
    producer: Optional[Tile] = None
    consumers: List[Tile] = field(default_factory=list)
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        prod = self.producer.name if self.producer else "None"
        cons = ", ".join(c.name for c in self.consumers) if self.consumers else "None"
        return f"ObjectFifo({self.name}: {prod} -> [{cons}])"

    def __hash__(self):
        return hash(self.name)


@dataclass
class ExternalKernel:
    """
    Represents an external C/C++ kernel function.

    External kernels are the actual computation kernels compiled from
    C/C++ source files and linked into the AIE program.

    Attributes:
        name: Unique identifier
        kernel_name: Name of the C/C++ function
        source_file: Path to kernel source file
        arg_types: List of argument types
        include_dirs: Optional include directories
        metadata: Additional compilation flags, etc.
    """
    name: str
    kernel_name: str
    source_file: str
    arg_types: List[Union[AnyType, str]]
    include_dirs: List[str] = field(default_factory=list)
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        return f"ExternalKernel({self.name}: {self.kernel_name} from {self.source_file})"


@dataclass
class Acquire:
    """Represents an acquire operation on a FIFO."""
    fifo_param: str  # Parameter name
    count: int
    local_var: str  # Name of acquired element


@dataclass
class Release:
    """Represents a release operation on a FIFO."""
    fifo_param: str  # Parameter name
    count: int


@dataclass
class KernelCall:
    """Represents a call to an external kernel."""
    kernel_param: str  # Parameter name (ExternalKernel reference)
    args: List[str]  # Local variable names


@dataclass
class Assignment:
    """
    Represents an indexed assignment (e.g., for zero-init loops).

    Example: elem_out[i] = 0
    """
    target: str        # Variable being assigned to (e.g., "elem_out")
    index: str         # Index expression (e.g., "i")
    value: Union[int, str]  # Right-hand side value (e.g., 0)


@dataclass
class ForLoop:
    """
    Represents a for loop: for <var> in range_(<count>): <body>.

    Used to represent nested loops inside CoreFunction bodies
    (e.g., zero-init loop and inner compute loop in matrix_vector_mul).
    """
    var: str                    # Loop variable name (e.g., "i" or "_")
    count: Union[int, str]      # Range count (can be symbolic, e.g., "m")
    body: List[Any] = field(default_factory=list)  # List of body statements


@dataclass
class CoreFunction:
    """
    Represents a Python function that wraps kernel invocations.

    Core functions define the acquire/call/release semantics for
    invoking external kernels with FIFO-based data.

    Two modes:
    1. Flat mode (body_stmts is None): flat acquires → kernel_call → releases,
       optionally wrapped in a single for loop via loop_count metadata.
    2. Body mode (body_stmts is set): explicit body statement list supporting
       nested ForLoop, Assignment, Acquire, Release, and KernelCall elements.
       This mode is required for patterns like matrix_vector_mul that have
       outer acquires, zero-init loops, inner loops, etc.

    Attributes:
        name: Function name
        parameters: List of parameter names (first is kernel, rest are FIFOs)
        acquires: List of acquire operations (flat mode only)
        kernel_call: The kernel invocation (flat mode only)
        releases: List of release operations (flat mode only)
        body_stmts: Explicit body statements (body mode, overrides flat fields)
        metadata: Additional properties
    """
    name: str
    parameters: List[str]
    acquires: List[Acquire] = field(default_factory=list)
    kernel_call: Optional[KernelCall] = None
    releases: List[Release] = field(default_factory=list)
    body_stmts: Optional[List[Any]] = None  # If set, overrides flat structure
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        params = ", ".join(self.parameters)
        return f"CoreFunction({self.name}({params}))"


class FifoAccessMode(Enum):
    """How a worker accesses a FIFO."""
    PRODUCER = "prod"
    CONSUMER = "cons"


@dataclass
class FifoBinding:
    """
    Represents a binding of a FIFO to a worker parameter.

    Attributes:
        fifo: The ObjectFifo being bound
        mode: Access mode (producer or consumer)
        index: Optional index for split/join results
    """
    fifo: Union[ObjectFifo, str]
    mode: FifoAccessMode
    index: Optional[int] = None

    def __str__(self):
        fifo_name = self.fifo if isinstance(self.fifo, str) else self.fifo.name
        idx = f"[{self.index}]" if self.index is not None else ""
        return f"{fifo_name}{idx}.{self.mode.value}()"


@dataclass
class Worker:
    """
    Represents a worker that executes a core function on a specific tile.

    Workers are the compute units that bind core functions to tiles
    and connect them to specific FIFOs.

    Attributes:
        name: Unique identifier
        core_fn: CoreFunction to execute
        fn_args: List of arguments (ExternalKernel + FIFO bindings)
        placement: Tile where this worker executes
        metadata: Additional properties
    """
    name: str
    core_fn: Union[CoreFunction, str]
    fn_args: List[Union[ExternalKernel, FifoBinding, str]]
    placement: Tile
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        cf_name = self.core_fn if isinstance(self.core_fn, str) else self.core_fn.name
        return f"Worker({self.name}: {cf_name} @ {self.placement})"


@dataclass
class RuntimeOp:
    """Base class for runtime operations (fill/drain)."""
    placement: Tile
    metadata: dict = None  # Additional attributes (column, use_tap, etc.)

    def __post_init__(self):
        if self.metadata is None:
            self.metadata = {}


@dataclass
class RuntimeFill(RuntimeOp):
    """Fill operation: Host -> NPU."""
    fifo: Union[ObjectFifo, str] = None
    source_param: str = None  # Runtime sequence parameter
    tap: Optional[Any] = None  # TensorAccessPattern or None


@dataclass
class RuntimeDrain(RuntimeOp):
    """Drain operation: NPU -> Host."""
    fifo: Union[ObjectFifo, str] = None
    dest_param: str = None  # Runtime sequence parameter
    wait: bool = True
    tap: Optional[Any] = None  # TensorAccessPattern or None


@dataclass
class RuntimeSequence:
    """
    Represents the runtime control flow (host <-> NPU transfers).

    The runtime sequence defines:
    - Input/output parameter types
    - Worker start operations
    - Fill operations (host -> NPU)
    - Drain operations (NPU -> host)

    Attributes:
        name: Unique identifier
        input_types: List of input tensor types
        output_types: List of output tensor types
        param_names: Names of parameters (matching input/output order)
        workers: List of workers to start
        operations: List of fill/drain operations
        metadata: Additional properties
    """
    name: str
    input_types: List[Union[AnyType, str]] = field(default_factory=list)
    output_types: List[Union[AnyType, str]] = field(default_factory=list)
    param_names: List[str] = field(default_factory=list)
    workers: List[Union[Worker, str]] = field(default_factory=list)
    operations: List[Union[RuntimeFill, RuntimeDrain]] = field(default_factory=list)
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        return f"RuntimeSequence({self.name}: {len(self.workers)} workers, {len(self.operations)} ops)"


@dataclass
class Program:
    """
    Top-level container for an AIECAD program.

    The Program represents a complete AIE design with all its components:
    - Symbols (types, constants)
    - Tiles (physical placement)
    - ObjectFifos (data movement edges)
    - External kernels
    - Core functions
    - Workers
    - Runtime sequence

    The program forms a directed graph where:
    - Nodes are tiles and workers
    - Edges are ObjectFifos

    Attributes:
        name: Program name
        symbols: Symbol table (types, constants, variables)
        tiles: All tiles in the design
        fifos: All ObjectFifos (edges in dataflow graph)
        external_kernels: External C/C++ kernel declarations
        core_functions: Python wrapper functions for kernels
        workers: Worker assignments to tiles
        runtime: Runtime sequence for host control
        metadata: Additional program-level properties
    """
    name: str
    symbols: Dict[str, Symbol] = field(default_factory=dict)
    tiles: Dict[str, Tile] = field(default_factory=dict)
    fifos: Dict[str, ObjectFifo] = field(default_factory=dict)
    external_kernels: Dict[str, ExternalKernel] = field(default_factory=dict)
    core_functions: Dict[str, CoreFunction] = field(default_factory=dict)
    workers: Dict[str, Worker] = field(default_factory=dict)
    runtime: Optional[RuntimeSequence] = None
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        return (f"Program({self.name}: "
                f"{len(self.tiles)} tiles, "
                f"{len(self.fifos)} fifos, "
                f"{len(self.workers)} workers)")

    def get_symbol(self, name: str) -> Optional[Symbol]:
        """Look up a symbol by name."""
        return self.symbols.get(name)

    def get_tile(self, name: str) -> Optional[Tile]:
        """Look up a tile by name."""
        return self.tiles.get(name)

    def get_fifo(self, name: str) -> Optional[ObjectFifo]:
        """Look up a FIFO by name."""
        return self.fifos.get(name)

    def get_worker(self, name: str) -> Optional[Worker]:
        """Look up a worker by name."""
        return self.workers.get(name)

    def validate(self) -> List[str]:
        """
        Validate the program for consistency.

        Returns:
            List of error messages (empty if valid)
        """
        errors = []

        # Check for duplicate names across all namespaces
        all_names = set()
        for name_dict in [self.symbols, self.tiles, self.fifos,
                          self.external_kernels, self.core_functions, self.workers]:
            for name in name_dict.keys():
                if name in all_names:
                    errors.append(f"Duplicate name '{name}' across namespaces")
                all_names.add(name)

        # Validate workers reference valid core functions
        for worker in self.workers.values():
            if isinstance(worker.core_fn, str):
                if worker.core_fn not in self.core_functions:
                    errors.append(f"Worker '{worker.name}' references unknown core function '{worker.core_fn}'")

        # Validate FIFOs have valid producers/consumers
        for fifo in self.fifos.values():
            if fifo.producer and fifo.producer.name not in self.tiles:
                errors.append(f"FIFO '{fifo.name}' has unknown producer tile '{fifo.producer.name}'")
            for consumer in fifo.consumers:
                if consumer.name not in self.tiles:
                    errors.append(f"FIFO '{fifo.name}' has unknown consumer tile '{consumer.name}'")

        return errors
