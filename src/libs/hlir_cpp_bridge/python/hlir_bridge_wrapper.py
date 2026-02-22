# SPDX-FileCopyrightText: 2026 Brock Sorenson
# SPDX-License-Identifier: GPL-3.0-only

"""
Python wrapper module for HLIR C++ bridge.
Wraps ProgramBuilder methods to return JSON-formatted responses.
Uses ComponentId-based references for all component interactions.
"""

import json
import sys
from pathlib import Path

# Add the HLIR module to Python path
hlir_path = Path(__file__).parent.parent.parent.parent / "src" / "aiecad_compiler"
sys.path.insert(0, str(hlir_path))

from hlir.builder import ProgramBuilder, RuntimeBuilder
from hlir.builder_result import ErrorCode


def error_code_to_string(code: ErrorCode) -> str:
    """Convert ErrorCode enum to string."""
    return code.name if code else "UNKNOWN_ERROR"


def success_response(component_id: str = None, data=None) -> str:
    """Build a successful JSON response."""
    response = {"success": True}
    if component_id is not None:
        response["id"] = component_id
    if data is not None:
        response["data"] = data
    return json.dumps(response)


def error_response(error_code: str, message: str, entity_id: str = "", dependencies: list = None) -> str:
    """Build an error JSON response."""
    response = {
        "success": False,
        "error_code": error_code,
        "error_message": message,
    }
    if entity_id:
        response["entity_id"] = entity_id
    if dependencies:
        response["dependencies"] = dependencies
    return json.dumps(response)


class BuilderWrapper:
    """Wrapper around ProgramBuilder that returns JSON responses."""

    def __init__(self, program_name: str):
        self.builder = ProgramBuilder(program_name)
        self.runtime = None

    def _lookup_component(self, comp_id: str):
        """Helper to lookup component object by ID."""
        if not comp_id:
            return None
        result = self.builder.lookup_by_id(comp_id)
        if result.success:
            return result.component
        return None

    def _lookup_components(self, comp_ids: list):
        """Helper to lookup multiple components by ID."""
        return [self._lookup_component(cid) for cid in comp_ids]

    def add_symbol(self, name: str, value: str, type_hint: str = "", is_constant: bool = False, provided_id: str = None) -> str:
        """Add or update a symbol and return JSON response with component ID.

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            result = self.builder.add_symbol(name, value, type_hint, is_constant, provided_id=provided_id)
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_tensor_type(self, name: str, shape: list, dtype: str, layout: str = "", provided_id: str = None) -> str:
        """Add or update a tensor type and return JSON response with component ID.

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            result = self.builder.add_tensor_type(name, shape, dtype, layout, provided_id=provided_id)
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_tile(self, name: str, kind: str, x: int, y: int, metadata: dict = None, provided_id: str = None) -> str:
        """Add or update a tile and return JSON response with component ID.

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            metadata = metadata or {}
            result = self.builder.add_tile(name, kind, x, y, provided_id=provided_id, **metadata)
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_fifo(self, name: str, obj_type_id: str, depth: int, producer_id: str = None,
                 consumer_ids: list = None, metadata: dict = None, provided_id: str = None) -> str:
        """Add or update a FIFO with ID-based references.

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            consumer_ids = consumer_ids or []
            metadata = metadata or {}

            # Look up type (can be TensorType or None for simple types)
            obj_type = self._lookup_component(obj_type_id) if obj_type_id else None
            # Pass type name (string) instead of object for proper serialization
            obj_type_name = obj_type.name if obj_type and hasattr(obj_type, 'name') else obj_type

            # Look up producer tile
            producer = self._lookup_component(producer_id) if producer_id else None

            # Look up consumer tiles
            consumers = [self._lookup_component(cid) for cid in consumer_ids if cid]

            result = self.builder.add_fifo(name, obj_type_name, depth, producer, consumers, provided_id=provided_id, **metadata)
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_fifo_simple_type(self, name: str, obj_type_str: str, depth: int, producer_id: str = None,
                             consumer_ids: list = None, metadata: dict = None, provided_id: str = None) -> str:
        """Add or update a FIFO with simple type string.

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            consumer_ids = consumer_ids or []
            metadata = metadata or {}

            # Look up producer tile
            producer = self._lookup_component(producer_id) if producer_id else None

            # Look up consumer tiles
            consumers = [self._lookup_component(cid) for cid in consumer_ids if cid]

            result = self.builder.add_fifo(name, obj_type_str, depth, producer, consumers, provided_id=provided_id, **metadata)
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_fifo_split(self, name: str, source_id: str, num_outputs: int, output_type_id: str,
                       output_names: list, offsets: list, placement_id: str, metadata: dict = None, provided_id: str = None) -> str:
        """Add or update a FIFO split operation.

        Args:
            name: Split operation name
            source_id: Source FIFO component ID
            num_outputs: Number of outputs
            output_type_id: Output type component ID
            output_names: List of names for each output (strings, not IDs)
            offsets: List of offsets for each output
            placement_id: Placement tile component ID
            metadata: Additional metadata
            provided_id: Optional ID for update operation

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            metadata = metadata or {}

            # Look up components
            source = self._lookup_component(source_id)
            output_type = self._lookup_component(output_type_id)
            placement = self._lookup_component(placement_id)

            # output_names are already strings - pass directly to builder
            result = self.builder.add_fifo_split(
                name, source, num_outputs, output_type, output_names, offsets, placement, provided_id=provided_id, **metadata
            )
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_fifo_join(self, name: str, dest_id: str, num_inputs: int, input_type_id: str,
                      input_names: list, offsets: list, placement_id: str, metadata: dict = None, provided_id: str = None) -> str:
        """Add or update a FIFO join operation.

        Args:
            name: Join operation name
            dest_id: Destination FIFO component ID
            num_inputs: Number of inputs
            input_type_id: Input type component ID
            input_names: List of names for each input (strings, not IDs)
            offsets: List of offsets for each input
            placement_id: Placement tile component ID
            metadata: Additional metadata
            provided_id: Optional ID for update operation

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            metadata = metadata or {}

            # Look up components
            dest = self._lookup_component(dest_id)
            input_type = self._lookup_component(input_type_id)
            placement = self._lookup_component(placement_id)

            # input_names are already strings - pass directly to builder
            result = self.builder.add_fifo_join(
                name, dest, num_inputs, input_type, input_names, offsets, placement, provided_id=provided_id, **metadata
            )
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_fifo_forward(self, name: str, source_id: str, metadata: dict = None, provided_id: str = None) -> str:
        """Add or update a FIFO forward operation with ID-based reference.

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            metadata = metadata or {}
            source = self._lookup_component(source_id)

            result = self.builder.add_fifo_forward(name, source, provided_id=provided_id, **metadata)
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_external_kernel(self, name: str, kernel_name: str, source_file: str,
                            arg_type_ids: list, include_dirs: list = None,
                            metadata: dict = None, provided_id: str = None) -> str:
        """Add or update an external kernel with ID-based type references.

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            include_dirs = include_dirs or []
            metadata = metadata or {}

            # Look up argument types
            arg_types = self._lookup_components(arg_type_ids)

            result = self.builder.add_external_kernel(
                name, kernel_name, source_file, arg_types, include_dirs, provided_id=provided_id, **metadata
            )
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_core_function(self, name: str, parameters: list, acquires: list,
                          kernel_call: tuple, releases: list, metadata: dict = None, provided_id: str = None) -> str:
        """Add or update a core function and return JSON response with component ID.

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            metadata = metadata or {}
            result = self.builder.add_core_function(
                name, parameters, acquires, kernel_call, releases, provided_id=provided_id, **metadata
            )
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def add_worker(self, name: str, core_fn_id: str, fn_args_json: str, placement_id: str,
                   metadata: dict = None, provided_id: str = None) -> str:
        """Add or update a worker with ID-based references.

        If provided_id is given, updates an existing component instead of creating a new one.
        """
        try:
            metadata = metadata or {}

            # Look up core function and placement
            core_fn = self._lookup_component(core_fn_id)
            placement = self._lookup_component(placement_id)

            # Parse function arguments from JSON
            fn_args_data = json.loads(fn_args_json)
            fn_args = []

            for arg_data in fn_args_data:
                arg_type = arg_data["type"]
                arg_id = arg_data["id"]
                arg_component = self._lookup_component(arg_id)

                if arg_type == "kernel":
                    fn_args.append(arg_component)
                elif arg_type == "split":
                    # Split operation output - reference by NAME (string), not object
                    # This allows proper serialization as <arg ref="split_name" index="N"/>
                    direction = arg_data.get("direction", "cons")
                    index = arg_data.get("index", 0)
                    # arg_component is a Symbol wrapping SplitOperation, get the name
                    split_name = arg_component.name if hasattr(arg_component, 'name') else str(arg_component)
                    fn_args.append((split_name, direction, index))
                elif arg_type == "join":
                    # Join operation input - reference by NAME (string), not object
                    # This allows proper serialization as <arg ref="join_name" index="N"/>
                    direction = arg_data.get("direction", "prod")
                    index = arg_data.get("index", 0)
                    # arg_component is a Symbol wrapping JoinOperation, get the name
                    join_name = arg_component.name if hasattr(arg_component, 'name') else str(arg_component)
                    fn_args.append((join_name, direction, index))
                elif arg_type == "fifo":
                    direction = arg_data.get("direction", "prod")
                    index = arg_data.get("index")
                    if direction == "prod":
                        # Producer tuples use 3-tuple format: (fifo, "prod", None)
                        fn_args.append((arg_component, "prod", None))
                    else:
                        # Consumer tuples use 3-tuple format: (fifo, "cons", index)
                        fn_args.append((arg_component, "cons", index))

            result = self.builder.add_worker(name, core_fn, fn_args, placement, provided_id=provided_id, **metadata)
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Unknown error",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def lookup_by_id(self, comp_id: str) -> str:
        """Lookup component by ID."""
        try:
            result = self.builder.lookup_by_id(comp_id)
            if result.success:
                component = result.component
                component_dict = self._serialize_component(component)
                return success_response(data=component_dict)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Component not found"
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def lookup_by_name(self, comp_type: str, name: str) -> str:
        """Lookup component by type and name."""
        try:
            result = self.builder.lookup_by_name(comp_type, name)
            if result.success:
                return success_response(result.id)
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Component not found"
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def get_all_ids(self, comp_type: str) -> str:
        """Get all component IDs of a type."""
        try:
            # get_all_ids returns a dict {id: component}, not a BuilderResult
            id_dict = self.builder.get_all_ids(comp_type)
            ids = list(id_dict.keys())
            return json.dumps({"success": True, "ids": ids})
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def update_fifo_depth(self, comp_id: str, new_depth: int) -> str:
        """Update FIFO depth by looking up the component and modifying it directly."""
        try:
            result = self.builder.lookup_by_id(comp_id)
            if result.success:
                component = result.component
                if hasattr(component, 'depth'):
                    component.depth = new_depth
                    return success_response()
                else:
                    return error_response("INVALID_PARAMETER", "Component does not have a depth attribute")
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Component not found",
                    comp_id
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def remove(self, comp_id: str) -> str:
        """Remove component."""
        try:
            result = self.builder.remove(comp_id)
            if result.success:
                return success_response()
            else:
                return error_response(
                    error_code_to_string(result.error_code),
                    result.error_message or "Removal failed",
                    result.id or "",
                    result.dependencies or []
                )
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def create_runtime(self, name: str) -> str:
        """Create a runtime sequence."""
        try:
            self.runtime = self.builder.create_runtime(name)
            runtime_id = getattr(self.runtime, '_id', 'runtime_' + name)
            return success_response(runtime_id)
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def runtime_add_input_type(self, type_id: str) -> str:
        """Add input type to runtime by ID."""
        try:
            type_obj = self._lookup_component(type_id)
            # Pass type name (string) instead of object for proper serialization
            type_name = type_obj.name if hasattr(type_obj, 'name') else str(type_obj)
            self.runtime.add_input_type(type_name)
            return success_response()
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def runtime_add_output_type(self, type_id: str) -> str:
        """Add output type to runtime by ID."""
        try:
            type_obj = self._lookup_component(type_id)
            # Pass type name (string) instead of object for proper serialization
            type_name = type_obj.name if hasattr(type_obj, 'name') else str(type_obj)
            self.runtime.add_output_type(type_name)
            return success_response()
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def runtime_add_param(self, param_name: str) -> str:
        """Add parameter to runtime."""
        try:
            # Accumulate parameters instead of replacing them
            # self.runtime is a RuntimeBuilder, self.runtime.runtime is the RuntimeSequence
            if not self.runtime.runtime.param_names:
                self.runtime.runtime.param_names = []
            self.runtime.runtime.param_names.append(param_name)
            return success_response()
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def runtime_add_worker(self, worker_id: str) -> str:
        """Add worker to runtime for StartWorkers list."""
        try:
            worker = self._lookup_component(worker_id)
            self.runtime.add_worker(worker)
            return success_response()
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def runtime_add_fill(self, name: str, fifo_id: str, input_name: str, tile_id: str,
                         column: int = -1, use_tap: bool = False) -> str:
        """Add fill operation to runtime with ID-based references."""
        try:
            fifo = self._lookup_component(fifo_id)
            tile = self._lookup_component(tile_id)
            # Build metadata with column and use_tap
            metadata = {}
            if column >= 0:
                metadata['column'] = column
            if use_tap:
                metadata['use_tap'] = True
            self.runtime.add_fill(name, fifo, input_name, tile, **metadata)
            return success_response()
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def runtime_add_drain(self, name: str, fifo_id: str, output_name: str, tile_id: str,
                          column: int = -1, use_tap: bool = False) -> str:
        """Add drain operation to runtime with ID-based references."""
        try:
            fifo = self._lookup_component(fifo_id)
            tile = self._lookup_component(tile_id)
            # Build metadata with column and use_tap
            metadata = {}
            if column >= 0:
                metadata['column'] = column
            if use_tap:
                metadata['use_tap'] = True
            self.runtime.add_drain(name, fifo, output_name, tile, **metadata)
            return success_response()
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def runtime_build(self) -> str:
        """Build runtime sequence."""
        try:
            self.runtime.build()
            return success_response()
        except Exception as e:
            return error_response("INVALID_PARAMETER", str(e))

    def build(self) -> str:
        """Build and validate the program."""
        try:
            program = self.builder.build()
            return success_response()
        except Exception as e:
            return error_response("INVALID_PARAMETER", str(e))

    def get_program(self) -> str:
        """Get program without validation."""
        try:
            program = self.builder.get_program()
            return success_response()
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def export_to_gui_xml(self, file_path: str) -> str:
        """Export to GUI XML file."""
        try:
            from hlir.gui_serializer import GUIXMLSerializer
            serializer = GUIXMLSerializer()
            program = self.builder.get_program()
            serializer.serialize_to_file(program, file_path)
            return success_response()
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def export_to_gui_xml_string(self) -> str:
        """Export to GUI XML string."""
        try:
            from hlir.gui_serializer import GUIXMLSerializer
            serializer = GUIXMLSerializer()
            program = self.builder.get_program()
            xml_str = serializer.serialize_to_string(program)
            return success_response(data=xml_str)
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def get_stats(self) -> str:
        """Get program statistics."""
        try:
            program = self.builder.get_program()
            stats = {
                "num_symbols": len(program.symbols),
                "num_tiles": len(program.tiles),
                "num_fifos": len(program.fifos),
                "num_external_kernels": len(program.external_kernels),
                "num_core_functions": len(program.core_functions),
                "num_workers": len(program.workers),
                "has_runtime": program.runtime is not None
            }
            return success_response(data=stats)
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def serialize_to_temp_xml(self, file_path: str) -> str:
        """Serialize to temporary XML file."""
        try:
            from hlir.serializer import XMLSerializer
            serializer = XMLSerializer()
            program = self.builder.get_program()
            serializer.serialize_to_file(program, file_path)
            return success_response()
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def load_from_xml(self, file_path: str) -> str:
        """Load program from XML file."""
        try:
            return error_response("INVALID_PARAMETER", "XML loading not yet implemented")
        except Exception as e:
            return error_response("PYTHON_EXCEPTION", str(e))

    def _serialize_component(self, component):
        """Serialize a component to a dictionary."""
        if hasattr(component, '__dict__'):
            data = {}
            for key, value in component.__dict__.items():
                if not key.startswith('_'):
                    if isinstance(value, (str, int, bool, float)):
                        data[key] = value
                    elif isinstance(value, list):
                        data[key] = [str(v) for v in value]
                    elif value is not None:
                        data[key] = str(value)
            return data
        return {"raw": str(component)}


# Global storage for builder wrappers
_builders = {}


def create_builder(program_name: str):
    """Create a new ProgramBuilder wrapper."""
    wrapper = BuilderWrapper(program_name)
    _builders[program_name] = wrapper
    return wrapper


def get_builder(program_name: str):
    """Get existing builder wrapper."""
    return _builders.get(program_name)
