"""
BuilderResult - Return type for ProgramBuilder operations.

Provides success/failure status, component IDs, and error messages
for GUI integration and interactive editing.
"""

from dataclasses import dataclass
from typing import Optional, Any, List
from enum import Enum


class ErrorCode(Enum):
    """Error codes for builder operations."""
    SUCCESS = "success"
    DUPLICATE_NAME = "duplicate_name"
    NOT_FOUND = "not_found"
    DEPENDENCY_EXISTS = "dependency_exists"
    INVALID_PARAMETER = "invalid_parameter"
    INVALID_REFERENCE = "invalid_reference"


@dataclass
class BuilderResult:
    """
    Result of a builder operation.

    Attributes:
        success: Whether the operation succeeded
        id: Component ID if successful (UUID string)
        component: The created/modified component object
        error_code: Error code if failed
        error_message: Human-readable error message
        dependencies: List of component IDs that depend on this component

    Example usage:
        result = builder.add_fifo("my_fifo", obj_type="chunk_ty", depth=2)
        if result.success:
            print(f"Created FIFO with ID: {result.id}")
        else:
            print(f"Error: {result.error_message}")
    """
    success: bool
    id: Optional[str] = None
    component: Optional[Any] = None
    error_code: Optional[ErrorCode] = None
    error_message: Optional[str] = None
    dependencies: Optional[List[str]] = None

    @classmethod
    def ok(cls, component_id: str, component: Any):
        """Create a successful result."""
        return cls(
            success=True,
            id=component_id,
            component=component,
            error_code=ErrorCode.SUCCESS
        )

    @classmethod
    def error(cls, code: ErrorCode, message: str, dependencies: Optional[List[str]] = None):
        """Create an error result."""
        return cls(
            success=False,
            error_code=code,
            error_message=message,
            dependencies=dependencies
        )

    @classmethod
    def duplicate(cls, name: str, component_type: str, existing_id: str):
        """Create a duplicate name error."""
        return cls(
            success=False,
            id=existing_id,
            error_code=ErrorCode.DUPLICATE_NAME,
            error_message=f"{component_type} '{name}' already exists with ID {existing_id}"
        )

    @classmethod
    def not_found(cls, component_id: str):
        """Create a not found error."""
        return cls.error(
            ErrorCode.NOT_FOUND,
            f"Component with ID '{component_id}' not found"
        )

    @classmethod
    def has_dependencies(cls, component_id: str, component_type: str, deps: List[str]):
        """Create a dependency error."""
        dep_str = ", ".join(deps)
        return cls.error(
            ErrorCode.DEPENDENCY_EXISTS,
            f"Cannot remove {component_type} '{component_id}': used by {dep_str}",
            dependencies=deps
        )

    def __bool__(self):
        """Allow result to be used in boolean context."""
        return self.success

    def __str__(self):
        """String representation for debugging."""
        if self.success:
            return f"BuilderResult(success=True, id={self.id})"
        else:
            return f"BuilderResult(success=False, error={self.error_message})"
