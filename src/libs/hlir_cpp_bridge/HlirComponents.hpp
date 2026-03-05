// SPDX-FileCopyrightText: 2026 Brock Sorenson
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "HlirTypes.hpp"
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace hlir {

using json = nlohmann::json;

/// Component data returned from Python (parsed from JSON)
/// Full component information including all fields and metadata

struct TileData {
    ComponentId id;
    std::string name;
    TileKind kind;
    int x;
    int y;
    std::map<std::string, std::string> metadata;

    static TileData fromJson(const json& j);
};

struct FifoData {
    ComponentId id;
    std::string name;
    std::string objType;  // Can be tensor type name or simple type string
    int depth;
    std::optional<ComponentId> producer;
    std::vector<ComponentId> consumers;
    std::map<std::string, std::string> metadata;

    static FifoData fromJson(const json& j);
};

struct ExternalKernelData {
    ComponentId id;
    std::string name;
    std::string kernelName;
    std::string sourceFile;
    std::vector<std::string> argTypes;
    std::vector<std::string> includeDirs;
    std::map<std::string, std::string> metadata;

    static ExternalKernelData fromJson(const json& j);
};

struct SymbolData {
    ComponentId id;
    std::string name;
    std::string value;  // String representation
    std::string typeHint;
    bool isConstant;

    static SymbolData fromJson(const json& j);
};

struct TensorTypeData {
    ComponentId id;
    std::string name;
    std::vector<std::string> shape;  // Can include symbolic expressions
    std::string dtype;
    std::string layout;

    static TensorTypeData fromJson(const json& j);
};

struct AcquireInfo {
    std::string fifoParam;
    int numElements;
    std::string varName;
};

struct ReleaseInfo {
    std::string fifoParam;
    int numElements;
};

struct KernelCallInfo {
    std::string kernelParam;
    std::vector<std::string> argNames;
};

struct CoreFunctionData {
    ComponentId id;
    std::string name;
    std::vector<std::string> parameters;
    std::vector<AcquireInfo> acquires;
    KernelCallInfo kernelCall;
    std::vector<ReleaseInfo> releases;
    std::map<std::string, std::string> metadata;

    static CoreFunctionData fromJson(const json& j);
};

struct WorkerData {
    ComponentId id;
    std::string name;
    ComponentId coreFunctionId;
    std::string coreFunctionName;
    std::vector<std::string> fnArgs;  // Simplified - actual structure more complex
    ComponentId placementId;
    std::string placementName;
    std::map<std::string, std::string> metadata;

    static WorkerData fromJson(const json& j);
};

struct SplitOperationData {
    ComponentId id;
    std::string name;
    ComponentId sourceId;
    std::string sourceName;
    int numOutputs;
    std::string outputType;
    std::vector<std::string> outputNames;
    std::vector<int> offsets;
    ComponentId placementId;
    std::string placementName;
    std::map<std::string, std::string> metadata;

    static SplitOperationData fromJson(const json& j);
};

struct JoinOperationData {
    ComponentId id;
    std::string name;
    ComponentId destId;
    std::string destName;
    int numInputs;
    std::string inputType;
    std::vector<std::string> inputNames;
    std::vector<int> offsets;
    ComponentId placementId;
    std::string placementName;
    std::map<std::string, std::string> metadata;

    static JoinOperationData fromJson(const json& j);
};

struct ForwardOperationData {
    ComponentId id;
    std::string name;
    ComponentId sourceId;
    std::string sourceName;
    std::map<std::string, std::string> metadata;

    static ForwardOperationData fromJson(const json& j);
};

struct RuntimeSequenceData {
    ComponentId id;
    std::string name;
    std::vector<std::string> inputTypes;
    std::vector<std::string> outputTypes;
    std::vector<std::string> paramNames;
    std::vector<ComponentId> workerIds;
    std::map<std::string, std::string> metadata;

    static RuntimeSequenceData fromJson(const json& j);
};

/// Program statistics
struct ProgramStats {
    int numSymbols;
    int numTiles;
    int numFifos;
    int numExternalKernels;
    int numCoreFunctions;
    int numWorkers;
    bool hasRuntime;

    static ProgramStats fromJson(const json& j);
};

} // namespace hlir
