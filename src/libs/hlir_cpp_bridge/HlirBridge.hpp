// SPDX-FileCopyrightText: 2026 Brock Sorenson
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "HlirTypes.hpp"
#include "HlirComponents.hpp"
#include <Python.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace hlir {

/// Main bridge to Python HLIR system
/// Provides C++ interface to ProgramBuilder and all HLIR operations
class HlirBridge {
public:
    /// Constructor - initializes Python interpreter and imports HLIR module
    /// @param programName Name of the HLIR program
    HlirBridge(const std::string& programName);

    /// Destructor - cleans up Python resources
    ~HlirBridge();

    // Prevent copying
    HlirBridge(const HlirBridge&) = delete;
    HlirBridge& operator=(const HlirBridge&) = delete;

    // ========================================================================
    // Core add_* methods (11 methods matching Python ProgramBuilder)
    // ========================================================================

    /// Add a symbol (variable or constant)
    /// @param name Symbol name
    /// @param value Symbol value (as string)
    /// @param typeHint Type hint (e.g., "int", "str")
    /// @param isConstant Whether this is a constant
    /// @param providedId Optional provided ID for the component
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addSymbol(
        const std::string& name,
        const std::string& value,
        const std::string& typeHint = "",
        bool isConstant = false,
        const ComponentId& providedId = ComponentId());

    /// Add a constant (convenience wrapper around addSymbol)
    /// @param name Constant name
    /// @param value Constant value (as string)
    /// @param typeHint Type hint
    /// @param providedId Optional provided ID for the component
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addConstant(
        const std::string& name,
        const std::string& value,
        const std::string& typeHint = "",
        const ComponentId& providedId = ComponentId());

    /// Add a tensor type
    /// @param name Type name
    /// @param shape Shape (can include symbolic expressions like "N // 4")
    /// @param dtype Data type (e.g., "bfloat16", "int32")
    /// @param layout Layout hint (optional)
    /// @param providedId Optional provided ID for the component
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addTensorType(
        const std::string& name,
        const std::vector<std::string>& shape,
        const std::string& dtype,
        const std::string& layout = "",
        const ComponentId& providedId = ComponentId());

    /// Add a tile (physical hardware location)
    /// @param name Tile name
    /// @param kind Tile kind (shim, mem, compute)
    /// @param x X coordinate
    /// @param y Y coordinate
    /// @param providedId Optional provided ID for the component
    /// @param metadata Additional metadata
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addTile(
        const std::string& name,
        TileKind kind,
        int x,
        int y,
        const ComponentId& providedId = ComponentId(),
        const std::map<std::string, std::string>& metadata = {});

    /// Add a FIFO (object FIFO for data movement)
    /// @param name FIFO name
    /// @param objTypeId Object type ID (tensor type ID) or empty for simple type
    /// @param objTypeStr Object type string (e.g., "int32") if objTypeId is empty
    /// @param depth FIFO depth
    /// @param producerId Producer tile ID (optional)
    /// @param consumerIds Consumer tile IDs
    /// @param providedId Optional provided ID for the component
    /// @param metadata Additional metadata
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addFifo(
        const std::string& name,
        const ComponentId& objTypeId,
        int depth,
        const std::optional<ComponentId>& producerId = std::nullopt,
        const std::vector<ComponentId>& consumerIds = {},
        const ComponentId& providedId = ComponentId(),
        const std::map<std::string, std::string>& metadata = {});

    /// Add a FIFO with simple type string (convenience overload)
    /// @param name FIFO name
    /// @param objTypeStr Object type string (e.g., "int32", "bfloat16")
    /// @param depth FIFO depth
    /// @param producerId Producer tile ID (optional)
    /// @param consumerIds Consumer tile IDs
    /// @param providedId Optional provided ID for the component
    /// @param metadata Additional metadata
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addFifoSimpleType(
        const std::string& name,
        const std::string& objTypeStr,
        int depth,
        const std::optional<ComponentId>& producerId = std::nullopt,
        const std::vector<ComponentId>& consumerIds = {},
        const ComponentId& providedId = ComponentId(),
        const std::map<std::string, std::string>& metadata = {});

    /// Add a FIFO split operation
    /// @param name Operation name
    /// @param sourceId Source FIFO ID
    /// @param numOutputs Number of output splits
    /// @param outputTypeId Output type ID
    /// @param outputNames Names for each output (will be created by split operation)
    /// @param offsets Split offsets
    /// @param placementId Placement tile ID
    /// @param providedId Optional provided ID for the component
    /// @param metadata Additional metadata
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addFifoSplit(
        const std::string& name,
        const ComponentId& sourceId,
        int numOutputs,
        const ComponentId& outputTypeId,
        const std::vector<std::string>& outputNames,
        const std::vector<int>& offsets,
        const ComponentId& placementId,
        const ComponentId& providedId = ComponentId(),
        const std::map<std::string, std::string>& metadata = {});

    /// Add a FIFO join operation
    /// @param name Operation name
    /// @param destId Destination FIFO ID
    /// @param numInputs Number of inputs to join
    /// @param inputTypeId Input type ID
    /// @param inputNames Names for each input (will be created by join operation)
    /// @param offsets Join offsets
    /// @param placementId Placement tile ID
    /// @param providedId Optional provided ID for the component
    /// @param metadata Additional metadata
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addFifoJoin(
        const std::string& name,
        const ComponentId& destId,
        int numInputs,
        const ComponentId& inputTypeId,
        const std::vector<std::string>& inputNames,
        const std::vector<int>& offsets,
        const ComponentId& placementId,
        const ComponentId& providedId = ComponentId(),
        const std::map<std::string, std::string>& metadata = {});

    /// Add a FIFO forward operation
    /// @param name Operation name
    /// @param sourceId Source FIFO ID
    /// @param providedId Optional provided ID for the component
    /// @param metadata Additional metadata
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addFifoForward(
        const std::string& name,
        const ComponentId& sourceId,
        const ComponentId& providedId = ComponentId(),
        const std::map<std::string, std::string>& metadata = {});

    /// Add an external kernel declaration
    /// @param name Kernel name
    /// @param kernelName C/C++ function name
    /// @param sourceFile Source file path
    /// @param argTypeIds Argument type IDs (tensor types or other types)
    /// @param includeDirs Include directories
    /// @param providedId Optional provided ID for the component
    /// @param metadata Additional metadata
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addExternalKernel(
        const std::string& name,
        const std::string& kernelName,
        const std::string& sourceFile,
        const std::vector<ComponentId>& argTypeIds,
        const std::vector<std::string>& includeDirs = {},
        const ComponentId& providedId = ComponentId(),
        const std::map<std::string, std::string>& metadata = {});

    /// Acquire info for core functions
    struct AcquireSpec {
        std::string paramName;  // Parameter name in function signature
        int numElements;
        std::string varName;  // Variable name for acquired data
    };

    /// Release info for core functions
    struct ReleaseSpec {
        std::string paramName;  // Parameter name in function signature
        int numElements;
    };

    /// Kernel call specification
    struct KernelCallSpec {
        std::string kernelParamName;  // Name of kernel parameter
        std::vector<std::string> argVarNames;  // Variable names to pass
    };

    /// Add a core function (kernel wrapper with acquire/release)
    /// @param name Function name
    /// @param parameters Parameter names (for kernel and FIFOs)
    /// @param acquires Acquire operations
    /// @param kernelCall Kernel call specification
    /// @param releases Release operations
    /// @param providedId Optional provided ID for the component
    /// @param metadata Additional metadata
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addCoreFunction(
        const std::string& name,
        const std::vector<std::string>& parameters,
        const std::vector<AcquireSpec>& acquires,
        const KernelCallSpec& kernelCall,
        const std::vector<ReleaseSpec>& releases,
        const ComponentId& providedId = ComponentId(),
        const std::map<std::string, std::string>& metadata = {});

    /// Function argument (for worker binding)
    struct FunctionArg {
        enum class Type { KERNEL, FIFO, SPLIT, JOIN };
        Type type;
        ComponentId componentId;
        std::string fifoDirection;  // "prod" or "cons" for FIFOs
        int fifoIndex = 0;  // Index for consumer FIFOs or split/join output/input index

        static FunctionArg kernel(const ComponentId& id) {
            return FunctionArg{Type::KERNEL, id, "", 0};
        }

        static FunctionArg fifoProducer(const ComponentId& id) {
            return FunctionArg{Type::FIFO, id, "prod", 0};
        }

        static FunctionArg fifoConsumer(const ComponentId& id, int index = 0) {
            return FunctionArg{Type::FIFO, id, "cons", index};
        }

        /// Reference a split operation output as consumer
        /// @param splitOpId Split operation component ID
        /// @param outputIndex Index of the split output (0, 1, ...)
        static FunctionArg splitConsumer(const ComponentId& splitOpId, int outputIndex) {
            return FunctionArg{Type::SPLIT, splitOpId, "cons", outputIndex};
        }

        /// Reference a join operation input as producer
        /// @param joinOpId Join operation component ID
        /// @param inputIndex Index of the join input (0, 1, ...)
        static FunctionArg joinProducer(const ComponentId& joinOpId, int inputIndex) {
            return FunctionArg{Type::JOIN, joinOpId, "prod", inputIndex};
        }
    };

    /// Add a worker (assigns core function to tile)
    /// @param name Worker name
    /// @param coreFnId Core function ID
    /// @param fnArgs Function arguments (kernel and FIFO bindings)
    /// @param placementId Placement tile ID
    /// @param providedId Optional provided ID for the component
    /// @param metadata Additional metadata
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> addWorker(
        const std::string& name,
        const ComponentId& coreFnId,
        const std::vector<FunctionArg>& fnArgs,
        const ComponentId& placementId,
        const ComponentId& providedId = ComponentId(),
        const std::map<std::string, std::string>& metadata = {});

    // ========================================================================
    // Lookup operations
    // ========================================================================

    /// Lookup component by ID
    /// @param id Component ID
    /// @return JSON string with component data
    HlirResult<std::string> lookupById(const ComponentId& id);

    /// Lookup component by name
    /// @param type Component type
    /// @param name Component name
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> lookupByName(
        ComponentType type,
        const std::string& name);

    /// Get all component IDs of a specific type
    /// @param type Component type
    /// @return List of ComponentIds
    HlirResult<std::vector<ComponentId>> getAllIds(ComponentType type);

    // ========================================================================
    // Update/Remove operations
    // ========================================================================

    /// Update FIFO depth
    /// @param id FIFO component ID
    /// @param newDepth New depth value
    /// @return Success or errors
    HlirResult<void> updateFifoDepth(const ComponentId& id, int newDepth);

    /// Remove component by ID
    /// @param id Component ID to remove
    /// @return Success or errors (including dependency information)
    HlirResult<void> remove(const ComponentId& id);

    // ========================================================================
    // Runtime operations
    // ========================================================================

    /// Create a runtime sequence
    /// @param name Runtime name
    /// @return ComponentId on success, errors on failure
    HlirResult<ComponentId> createRuntime(const std::string& name);

    /// Add input type to runtime
    /// @param typeId Type ID
    /// @return Success or errors
    HlirResult<void> runtimeAddInputType(const ComponentId& typeId);

    /// Add output type to runtime
    /// @param typeId Type ID
    /// @return Success or errors
    HlirResult<void> runtimeAddOutputType(const ComponentId& typeId);

    /// Add parameter to runtime
    /// @param paramName Parameter name (string name for runtime parameter)
    /// @return Success or errors
    HlirResult<void> runtimeAddParam(const std::string& paramName);

    /// Add worker to runtime for StartWorkers
    /// @param workerId Worker ID
    /// @return Success or errors
    HlirResult<void> runtimeAddWorker(const ComponentId& workerId);

    /// Add fill operation to runtime
    /// @param name Fill operation name
    /// @param fifoId FIFO ID
    /// @param inputName Input parameter name
    /// @param tileId Tile ID
    /// @param column Column index (optional, -1 to omit)
    /// @param useTap Whether to use tensor access pattern
    /// @return Success or errors
    HlirResult<void> runtimeAddFill(
        const std::string& name,
        const ComponentId& fifoId,
        const std::string& inputName,
        const ComponentId& tileId,
        int column = -1,
        bool useTap = false);

    /// Add drain operation to runtime
    /// @param name Drain operation name
    /// @param fifoId FIFO ID
    /// @param outputName Output parameter name
    /// @param tileId Tile ID
    /// @param column Column index (optional, -1 to omit)
    /// @param useTap Whether to use tensor access pattern
    /// @return Success or errors
    HlirResult<void> runtimeAddDrain(
        const std::string& name,
        const ComponentId& fifoId,
        const std::string& outputName,
        const ComponentId& tileId,
        int column = -1,
        bool useTap = false);

    /// Build runtime sequence
    /// @return Success or errors
    HlirResult<void> runtimeBuild();

    // ========================================================================
    // Program building and export
    // ========================================================================

    /// Build and validate the program
    /// @return Success or errors with validation diagnostics
    HlirResult<void> build();

    /// Get program without validation
    /// @return Success or errors
    HlirResult<void> getProgram();

    /// Export to GUI XML
    /// @param filePath Output file path
    /// @return Success or errors
    HlirResult<void> exportToGuiXml(const std::string& filePath);

    /// Export to GUI XML string
    /// @return XML string on success, errors on failure
    HlirResult<std::string> exportToGuiXmlString();

    /// Get program statistics
    /// @return Program statistics
    HlirResult<ProgramStats> getStats();

    // ========================================================================
    // Serialization (for switching designs)
    // ========================================================================

    /// Serialize current program to temporary XML file
    /// @param filePath Output file path
    /// @return Success or errors
    HlirResult<void> serializeToTempXml(const std::string& filePath);

    /// Load program from XML file
    /// @param filePath Input file path
    /// @return Success or errors
    HlirResult<void> loadFromXml(const std::string& filePath);

private:
    /// Python module and builder objects
    PyObject* m_hlirModule = nullptr;
    PyObject* m_builder = nullptr;
    PyObject* m_runtime = nullptr;

    std::string m_programName;

    /// Call a Python function with arguments
    /// @param function Python function object
    /// @param args Python arguments tuple
    /// @return PyObject result on success, errors on failure
    HlirResult<PyObject*> callPythonFunction(PyObject* function, PyObject* args);

    /// Call a method on the builder
    /// @param methodName Method name
    /// @param args Python arguments tuple
    /// @return PyObject result on success, errors on failure
    HlirResult<PyObject*> callBuilderMethod(const char* methodName, PyObject* args);

    /// Call a method on the runtime
    /// @param methodName Method name
    /// @param args Python arguments tuple
    /// @return PyObject result on success, errors on failure
    HlirResult<PyObject*> callRuntimeMethod(const char* methodName, PyObject* args);

    /// Parse JSON result from Python
    /// @param jsonStr JSON string from Python
    /// @return Parsed result with component ID or void
    template<typename T>
    HlirResult<T> parseJsonResult(const std::string& jsonStr);

    /// Extract JSON string from Python object
    /// @param obj Python object (should be string)
    /// @return JSON string on success, errors on failure
    HlirResult<std::string> extractJsonString(PyObject* obj);

    /// Build Python kwargs dictionary from metadata
    /// @param metadata Metadata map
    /// @return Python dictionary
    PyObject* buildMetadataDict(const std::map<std::string, std::string>& metadata);

    /// Convert vector of strings to Python list
    PyObject* buildPythonList(const std::vector<std::string>& items);

    /// Convert vector of ints to Python list
    PyObject* buildPythonList(const std::vector<int>& items);
};

} // namespace hlir
