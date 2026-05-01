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
        dims_to_stream: Optional symbol name for dims_to_stream on .split() (applied to all outputs)
        metadata: Additional properties
    """
    name: str
    source: Union[ObjectFifo, str]
    num_outputs: int
    output_type: Union[AnyType, str]
    output_names: List[str]
    offsets: List[Union[int, str]]  # Can be symbolic expressions
    placement: Tile
    dims_to_stream: Optional[str] = None
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
        dims_from_stream: Optional symbol name for dims_from_stream on .join() (applied to all inputs)
        metadata: Additional properties
    """
    name: str
    dest: Union[ObjectFifo, str]
    num_inputs: int
    input_type: Union[AnyType, str]
    input_names: List[str]
    offsets: List[Union[int, str]]  # Can be symbolic expressions
    placement: Tile
    dims_from_stream: Optional[str] = None
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        dst = self.dest if isinstance(self.dest, str) else self.dest.name
        return f"Join({self.name}: {self.num_inputs} inputs -> {dst} @ {self.placement})"


@dataclass
class ForwardOperation(FifoOperation):
    """
    Forward operation: Simple passthrough from consumer to producer.

    Represents the pattern: base_fifo.cons(dims_from_stream=X).forward(dims_to_stream=Y, placement=Tile(x, y))

    Attributes:
        name: Name of the resulting forwarded FIFO
        source: Source ObjectFifo to forward
        placement: Optional tile where the forward occurs (e.g., a mem tile)
        dims_to_stream: Optional symbol name for dims_to_stream kwarg on .forward()
        dims_from_stream: Optional symbol name for dims_from_stream kwarg on .cons()
        metadata: Additional properties
    """
    name: str
    source: Union[ObjectFifo, str]
    placement: Optional["Tile"] = None
    dims_to_stream: Optional[str] = None
    dims_from_stream: Optional[str] = None
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
        name: Optional name for this TAP (used when stored in symbol table)
        metadata: Additional properties
    """
    tensor_dims: List[Union[int, str]]
    offset: Union[int, str]
    sizes: List[Union[int, str]]
    strides: List[Union[int, str]]
    name: Optional[str] = None
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __str__(self):
        dims_str = ", ".join(str(d) for d in self.tensor_dims)
        sizes_str = ", ".join(str(s) for s in self.sizes)
        strides_str = ", ".join(str(s) for s in self.strides)
        name_str = f"{self.name}: " if self.name else ""
        return (f"TensorAccessPattern({name_str}dims=[{dims_str}], "
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

def convert_tap_to_tiler2d(
    tap: TensorAccessPattern,
    name: str,
    prune_step: bool = False,
    index: int = 0
) -> TensorTiler2DSpec:
    """
    Convert a TensorAccessPattern to TensorTiler2DSpec for TensorTiler2D code generation.
    
    This function analyzes the TAP parameters and converts them to equivalent
    TensorTiler2D.group_tiler() parameters. Only supports regular 2D tiling patterns.
    
    Args:
        tap: TensorAccessPattern to convert
        name: Variable name for the resulting tiler
        prune_step: Whether to prune steps (usually False)
        index: Index into the returned list (usually 0)
    
    Returns:
        TensorTiler2DSpec: Equivalent TensorTiler2D specification
    
    Raises:
        ValueError: If the TAP is not compatible with TensorTiler2D conversion
    
    Examples:
        # Simple 2D tiling: 256x256 tensor, 32x32 tiles
        tap = TensorAccessPattern(
            tensor_dims=[256, 256],
            offset=0,
            sizes=[32, 32],
            strides=[256, 1]  # Row-major
        )
        tiler = convert_tap_to_tiler2d(tap, "my_tap")
        # Results in: TensorTiler2D.group_tiler((256, 256), (32, 32), (8, 8))[0]
    """
    # Validate tensor is 2D
    if len(tap.tensor_dims) != 2:
        raise ValueError(
            f"TensorTiler2D only supports 2D tensors, got {len(tap.tensor_dims)}D: {tap.tensor_dims}"
        )
    
    if len(tap.sizes) != 2:
        raise ValueError(
            f"TensorTiler2D requires 2D sizes, got {len(tap.sizes)}D: {tap.sizes}"
        )
    
    if len(tap.strides) < 2:
        raise ValueError(
            f"TensorTiler2D requires at least 2 strides, got {len(tap.strides)}: {tap.strides}"
        )
    
    # Extract dimensions (handle both int and str for symbolic expressions)
    tensor_dims = tap.tensor_dims
    tile_dims = tap.sizes
    
    # Calculate tile_counts: tensor_dims / tile_dims (element-wise)
    # Support symbolic expressions by creating division strings
    tile_counts = []
    for i in range(2):
        tensor_dim = tensor_dims[i]
        tile_dim = tile_dims[i]
        
        # If both are integers, compute directly
        if isinstance(tensor_dim, int) and isinstance(tile_dim, int):
            if tensor_dim % tile_dim != 0:
                raise ValueError(
                    f"Tensor dimension {i} ({tensor_dim}) must be evenly divisible "
                    f"by tile dimension ({tile_dim})"
                )
            tile_counts.append(tensor_dim // tile_dim)
        else:
            # For symbolic expressions, create division expression
            tile_counts.append(f"{tensor_dim} // {tile_dim}")
    
    # Validate offset
    # TensorTiler2D doesn't have an offset parameter, so we only support offset=0
    # or use pattern_repeat for simple offset cases
    offset = tap.offset
    pattern_repeat = None
    
    if offset != 0 and offset != "0":
        # Check if offset can be handled via pattern_repeat
        # This is a simplified check; more complex cases may need additional logic
        if isinstance(offset, int) and offset > 0:
            # For now, we don't automatically convert offset to pattern_repeat
            # as the relationship is complex and depends on the access pattern
            raise ValueError(
                f"TensorTiler2D conversion does not support non-zero offset ({offset}). "
                f"Consider using offset=0 and adjusting the source pointer in fill/drain operations."
            )
        elif isinstance(offset, str) and offset.strip() != "0":
            raise ValueError(
                f"TensorTiler2D conversion does not support non-zero symbolic offset ({offset}). "
                f"Consider using offset=0 and adjusting the source pointer in fill/drain operations."
            )
    
    # Validate strides match expected row-major or column-major pattern
    # For 2D tensors, we expect either:
    # - Row-major: strides = [tensor_dims[1], 1] or [tensor_dims[1] * element_size, element_size]
    # - Column-major: strides = [1, tensor_dims[0]] or [element_size, tensor_dims[0] * element_size]
    
    strides = tap.strides[:2]  # Take first 2 strides
    
    # Try to determine if this is a standard tiling pattern
    # We'll be lenient here and just check for basic patterns
    is_valid_pattern = False
    
    # Check for row-major pattern (most common)
    if len(strides) >= 2:
        stride_0, stride_1 = strides[0], strides[1]
        
        # Row-major: stride[0] should be tensor_dims[1] (or multiple), stride[1] should be 1 (or small)
        # We'll accept if stride[1] is 1 or a small constant
        if isinstance(stride_1, int) and stride_1 <= 4:
            is_valid_pattern = True
        elif stride_1 == "1" or stride_1 == 1:
            is_valid_pattern = True
        
        # Also check if strides match tensor dimensions (symbolic case)
        if isinstance(stride_0, str) and isinstance(tensor_dims[1], str):
            if stride_0 == tensor_dims[1] or stride_0 == f"{tensor_dims[1]}":
                is_valid_pattern = True
        elif isinstance(stride_0, int) and isinstance(tensor_dims[1], int):
            # Allow stride_0 to be tensor_dims[1] or a multiple (for element sizes)
            if stride_0 % tensor_dims[1] == 0 or tensor_dims[1] % stride_0 == 0:
                is_valid_pattern = True
    
    if not is_valid_pattern:
        raise ValueError(
            f"TensorTiler2D conversion requires standard row-major or column-major stride pattern. "
            f"Got strides={strides} for tensor_dims={tensor_dims}. "
            f"Expected row-major pattern like [{tensor_dims[1]}, 1] or similar."
        )
    
    # Create TensorTiler2DSpec
    return TensorTiler2DSpec(
        name=name,
        tensor_dims=tensor_dims,
        tile_dims=tile_dims,
        tile_counts=tile_counts,
        pattern_repeat=pattern_repeat,
        prune_step=prune_step,
        index=index,
        metadata=tap.metadata.copy() if tap.metadata else {}
    )



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
