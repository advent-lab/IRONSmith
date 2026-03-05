// SPDX-FileCopyrightText: 2026 Brock Sorenson
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <expected>
#include <string>
#include <vector>
#include <optional>

namespace hlir {

/// Error codes matching Python ErrorCode enum
enum class ErrorCode {
    SUCCESS,
    DUPLICATE_NAME,
    NOT_FOUND,
    DEPENDENCY_EXISTS,
    INVALID_PARAMETER,
    PYTHON_EXCEPTION,
    MISSING_FUNCTION,
    JSON_PARSE_ERROR,
    UNKNOWN_ERROR
};

/// Convert ErrorCode to string
inline const char* errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "SUCCESS";
        case ErrorCode::DUPLICATE_NAME: return "DUPLICATE_NAME";
        case ErrorCode::NOT_FOUND: return "NOT_FOUND";
        case ErrorCode::DEPENDENCY_EXISTS: return "DEPENDENCY_EXISTS";
        case ErrorCode::INVALID_PARAMETER: return "INVALID_PARAMETER";
        case ErrorCode::PYTHON_EXCEPTION: return "PYTHON_EXCEPTION";
        case ErrorCode::MISSING_FUNCTION: return "MISSING_FUNCTION";
        case ErrorCode::JSON_PARSE_ERROR: return "JSON_PARSE_ERROR";
        case ErrorCode::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
    }
    return "UNKNOWN_ERROR";
}

/// Diagnostic information for errors
struct HlirDiagnostic {
    ErrorCode code;
    std::string message;
    std::string entityId;  // Component ID if applicable
    std::vector<std::string> dependencies;  // For DEPENDENCY_EXISTS errors

    HlirDiagnostic(ErrorCode c, std::string msg,
                   std::string id = "",
                   std::vector<std::string> deps = {})
        : code(c), message(std::move(msg)),
          entityId(std::move(id)), dependencies(std::move(deps)) {}
};

/// Result type for operations that may fail
template<typename T>
using HlirResult = std::expected<T, std::vector<HlirDiagnostic>>;

/// Generic component ID (UUID from Python)
struct ComponentId {
    std::string value;

    ComponentId() = default;
    explicit ComponentId(std::string id) : value(std::move(id)) {}

    bool operator==(const ComponentId& other) const {
        return value == other.value;
    }

    bool operator!=(const ComponentId& other) const {
        return value != other.value;
    }

    bool empty() const { return value.empty(); }
};

/// Component types for lookup operations
enum class ComponentType {
    SYMBOL,
    TILE,
    FIFO,
    EXTERNAL_KERNEL,
    CORE_FUNCTION,
    WORKER,
    RUNTIME,
    SPLIT_OPERATION,
    JOIN_OPERATION,
    FORWARD_OPERATION,
    TENSOR_TYPE
};

/// Convert ComponentType to string
inline const char* componentTypeToString(ComponentType type) {
    switch (type) {
        case ComponentType::SYMBOL: return "Symbol";
        case ComponentType::TILE: return "Tile";
        case ComponentType::FIFO: return "ObjectFifo";
        case ComponentType::EXTERNAL_KERNEL: return "ExternalKernel";
        case ComponentType::CORE_FUNCTION: return "CoreFunction";
        case ComponentType::WORKER: return "Worker";
        case ComponentType::RUNTIME: return "RuntimeSequence";
        case ComponentType::SPLIT_OPERATION: return "SplitOperation";
        case ComponentType::JOIN_OPERATION: return "JoinOperation";
        case ComponentType::FORWARD_OPERATION: return "ForwardOperation";
        case ComponentType::TENSOR_TYPE: return "TensorType";
    }
    return "Unknown";
}

/// Tile kinds
enum class TileKind {
    SHIM,
    MEM,
    COMPUTE
};

/// Convert string to TileKind
inline std::optional<TileKind> stringToTileKind(const std::string& kind) {
    if (kind == "shim") return TileKind::SHIM;
    if (kind == "mem") return TileKind::MEM;
    if (kind == "compute") return TileKind::COMPUTE;
    return std::nullopt;
}

/// Convert TileKind to string
inline const char* tileKindToString(TileKind kind) {
    switch (kind) {
        case TileKind::SHIM: return "shim";
        case TileKind::MEM: return "mem";
        case TileKind::COMPUTE: return "compute";
    }
    return "unknown";
}

} // namespace hlir
