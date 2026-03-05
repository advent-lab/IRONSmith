"""
FIFO operations and runtime patterns for AIECAD HLIR.

Defines split, join, forward operations on ObjectFifos
as well as tensor access patterns for DMA operations.
"""

from dataclasses import dataclass, field
from typing import List, Union, Optional, Any, Dict
from .core import ObjectFifo, Tile
from .types import AnyType


class FifoOperation:
    """Base class for FIFO operations."""
    pass


@dataclass
class SplitOperation(FifoOperation):
    """
    Split operation: Divides a FIFO consumer into multiple outputs.

    Represents the pattern: base_fifo.cons().split(...)

    Attributes:
        name: Name of the resulting split FIFO array
        source: Source ObjectFifo to split
        num_outputs: Number of output FIFOs
        output_type: Type of each output FIFO
        output_names: Names for each split output
        offsets: Byte offsets for each output (symbolic expressions)
        placement: Tile where split occurs
        metadata: Additional properties
    """
    name: str
    source: Union[ObjectFifo, str]
    num_outputs: int
    output_type: Union[AnyType, str]
    output_names: List[str]
    offsets: List[Union[int, str]]  # Can be symbolic expressions
    placement: Tile
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        src = self.source if isinstance(self.source, str) else self.source.name
        return f"Split({self.name}: {src} -> {self.num_outputs} outputs @ {self.placement})"


@dataclass
class JoinOperation(FifoOperation):
    """
    Join operation: Combines multiple inputs into a single FIFO producer.

    Represents the pattern: base_fifo.prod().join(...)

    Attributes:
        name: Name of the resulting join FIFO array
        dest: Destination ObjectFifo to join into
        num_inputs: Number of input FIFOs
        input_type: Type of each input FIFO
        input_names: Names for each join input
        offsets: Byte offsets for each input (symbolic expressions)
        placement: Tile where join occurs
        metadata: Additional properties
    """
    name: str
    dest: Union[ObjectFifo, str]
    num_inputs: int
    input_type: Union[AnyType, str]
    input_names: List[str]
    offsets: List[Union[int, str]]  # Can be symbolic expressions
    placement: Tile
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        dst = self.dest if isinstance(self.dest, str) else self.dest.name
        return f"Join({self.name}: {self.num_inputs} inputs -> {dst} @ {self.placement})"


@dataclass
class ForwardOperation(FifoOperation):
    """
    Forward operation: Simple passthrough from consumer to producer.

    Represents the pattern: base_fifo.cons().forward(placement=Tile(x, y))

    Attributes:
        name: Name of the resulting forwarded FIFO
        source: Source ObjectFifo to forward
        placement: Optional tile where the forward occurs (e.g., a mem tile)
        metadata: Additional properties
    """
    name: str
    source: Union[ObjectFifo, str]
    placement: Optional["Tile"] = None
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        src = self.source if isinstance(self.source, str) else self.source.name
        placement_str = f" @ {self.placement}" if self.placement else ""
        return f"Forward({self.name}: {src}.cons().forward(){placement_str})"


@dataclass
class TensorAccessPattern:
    """
    Represents a multi-dimensional tensor access pattern for DMA operations.

    TensorAccessPattern (TAP) defines how to slice and access multi-dimensional
    tensors during fill/drain operations. It specifies the tensor dimensions,
    offset, sizes, and strides for the access.

    Attributes:
        tensor_dims: Full tensor dimensions (can be symbolic expressions)
        offset: Starting offset (can be symbolic expression)
        sizes: Multi-dimensional access sizes (can be symbolic expressions)
        strides: Multi-dimensional strides (can be symbolic expressions)
        metadata: Additional properties
    """
    tensor_dims: List[Union[int, str]]
    offset: Union[int, str]
    sizes: List[Union[int, str]]
    strides: List[Union[int, str]]
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        dims_str = ", ".join(str(d) for d in self.tensor_dims)
        sizes_str = ", ".join(str(s) for s in self.sizes)
        strides_str = ", ".join(str(s) for s in self.strides)
        return (f"TensorAccessPattern(dims=[{dims_str}], "
                f"offset={self.offset}, "
                f"sizes=[{sizes_str}], "
                f"strides=[{strides_str}])")


@dataclass
class TensorTiler2DSpec:
    """
    Represents a TensorTiler2D.group_tiler() call for DMA access pattern generation.

    Captures the parameters for TensorTiler2D.group_tiler():
        TensorTiler2D.group_tiler(
            tensor_dims, tile_dims, tile_counts,
            pattern_repeat=<value>,  # optional
            prune_step=<bool>
        )[index]

    Example (matrix_vector_mul A tensor):
        TensorTiler2D.group_tiler(
            (n_fifo_elems, A_elem_size), (1, 512),
            (n_fifo_elems, A_elem_size // 512),
            prune_step=False,
        )[0]

    Attributes:
        name: Variable name for this tiler (e.g., "a_tap")
        tensor_dims: Full tensor dimensions (2 elements)
        tile_dims: Tile dimensions (2 elements)
        tile_counts: Number of tiles per dimension (2 elements)
        pattern_repeat: Optional repeat count (e.g., rows_per_core)
        prune_step: Whether to prune steps (usually False)
        index: Index into the returned list (usually 0)
        metadata: Additional properties
    """
    name: str
    tensor_dims: List[Union[int, str]]
    tile_dims: List[Union[int, str]]
    tile_counts: List[Union[int, str]]
    pattern_repeat: Optional[Union[int, str]] = None
    prune_step: bool = False
    index: int = 0
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        dims_str = ", ".join(str(d) for d in self.tensor_dims)
        return (f"TensorTiler2DSpec({self.name}: "
                f"dims=[{dims_str}], index={self.index})")


@dataclass
class FillOperation:
    """
    Fill operation: Transfer data from host memory to NPU FIFO.

    Represents runtime fill operations that move data into the AIE array.

    Attributes:
        name: Unique identifier for this operation
        fifo: Target ObjectFifo to fill
        source_param: Runtime parameter name (source tensor)
        placement: Tile where fill occurs (typically shim tile)
        tap: Optional tensor access pattern for multi-dimensional access
        metadata: Additional properties (column, etc.)
    """
    name: str
    fifo: Union[ObjectFifo, str]
    source_param: str
    placement: Tile
    tap: Optional[TensorAccessPattern] = None
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        fifo_name = self.fifo if isinstance(self.fifo, str) else self.fifo.name
        tap_str = " (with TAP)" if self.tap else ""
        return f"Fill({self.name}: {self.source_param} -> {fifo_name} @ {self.placement}{tap_str})"


@dataclass
class DrainOperation:
    """
    Drain operation: Transfer data from NPU FIFO to host memory.

    Represents runtime drain operations that move data out of the AIE array.

    Attributes:
        name: Unique identifier for this operation
        fifo: Source ObjectFifo to drain
        dest_param: Runtime parameter name (destination tensor)
        placement: Tile where drain occurs (typically shim tile)
        wait: Whether to wait for completion
        tap: Optional tensor access pattern for multi-dimensional access
        metadata: Additional properties (column, etc.)
    """
    name: str
    fifo: Union[ObjectFifo, str]
    dest_param: str
    placement: Tile
    wait: bool = True
    tap: Optional[TensorAccessPattern] = None
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        fifo_name = self.fifo if isinstance(self.fifo, str) else self.fifo.name
        tap_str = " (with TAP)" if self.tap else ""
        wait_str = " [wait]" if self.wait else ""
        return f"Drain({self.name}: {fifo_name} -> {self.dest_param} @ {self.placement}{tap_str}{wait_str})"
