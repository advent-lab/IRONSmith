// SPDX-FileCopyrightText: 2026 Brock Sorenson
// SPDX-License-Identifier: GPL-3.0-only

#include "HlirBridge.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>

using json = nlohmann::json;

namespace hlir {

// ============================================================================
// Constructor / Destructor
// ============================================================================

HlirBridge::HlirBridge(const std::string& programName)
    : m_programName(programName)
{
    // Set Python home to find the correct standard library
    // Use PYTHONHOME env var if set, otherwise use the CMake-detected path
    const char* pythonHome = std::getenv("PYTHONHOME");
    if (!pythonHome) {
#ifdef PYTHON_HOME_DIR
        pythonHome = PYTHON_HOME_DIR;
#endif
    }
    if (pythonHome) {
        size_t len = strlen(pythonHome);
        static std::wstring pythonHomePath(pythonHome, pythonHome + len);
        Py_SetPythonHome(pythonHomePath.c_str());
    }

    Py_Initialize();

    // Add Python module paths
    PyRun_SimpleString("import sys");
    PyRun_SimpleString("sys.path.insert(0, 'src/libs/hlir_cpp_bridge/python')");
    PyRun_SimpleString("sys.path.insert(0, 'hlir_cpp_bridge/python')");
    PyRun_SimpleString("sys.path.insert(0, 'src/aiecad_compiler')");
    PyRun_SimpleString("sys.path.insert(0, 'aiecad_compiler')");

    // Import wrapper module
    PyObject* moduleName = PyUnicode_FromString("hlir_bridge_wrapper");
    m_hlirModule = PyImport_Import(moduleName);
    Py_DECREF(moduleName);

    if (!m_hlirModule) {
        PyErr_Print();
        throw std::runtime_error("Failed to import hlir_bridge_wrapper module");
    }

    // Create builder
    PyObject* createBuilderFunc = PyObject_GetAttrString(m_hlirModule, "create_builder");
    if (!createBuilderFunc || !PyCallable_Check(createBuilderFunc)) {
        Py_XDECREF(createBuilderFunc);
        throw std::runtime_error("Failed to find create_builder function");
    }

    PyObject* args = Py_BuildValue("(s)", programName.c_str());
    m_builder = PyObject_CallObject(createBuilderFunc, args);
    Py_DECREF(args);
    Py_DECREF(createBuilderFunc);

    if (!m_builder) {
        PyErr_Print();
        throw std::runtime_error("Failed to create ProgramBuilder");
    }
}

HlirBridge::~HlirBridge() {
    Py_XDECREF(m_runtime);
    Py_XDECREF(m_builder);
    Py_XDECREF(m_hlirModule);
    Py_Finalize();
}

// ============================================================================
// Helper methods
// ============================================================================

HlirResult<PyObject*> HlirBridge::callPythonFunction(PyObject* function, PyObject* args) {
    if (!function || !PyCallable_Check(function)) {
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::MISSING_FUNCTION, "Function not callable"}});
    }

    PyObject* result = PyObject_CallObject(function, args);
    if (!result) {
        PyErr_Print();
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::PYTHON_EXCEPTION, "Python call failed"}});
    }

    return result;
}

HlirResult<PyObject*> HlirBridge::callBuilderMethod(const char* methodName, PyObject* args) {
    PyObject* method = PyObject_GetAttrString(m_builder, methodName);
    if (!method || !PyCallable_Check(method)) {
        Py_XDECREF(method);
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::MISSING_FUNCTION,
            std::string("Method not found: ") + methodName}});
    }

    auto result = callPythonFunction(method, args);
    Py_DECREF(method);
    return result;
}

HlirResult<PyObject*> HlirBridge::callRuntimeMethod(const char* methodName, PyObject* args) {
    if (!m_runtime) {
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::INVALID_PARAMETER, "No runtime created"}});
    }

    PyObject* method = PyObject_GetAttrString(m_runtime, methodName);
    if (!method || !PyCallable_Check(method)) {
        Py_XDECREF(method);
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::MISSING_FUNCTION,
            std::string("Runtime method not found: ") + methodName}});
    }

    auto result = callPythonFunction(method, args);
    Py_DECREF(method);
    return result;
}

HlirResult<std::string> HlirBridge::extractJsonString(PyObject* obj) {
    if (!obj) {
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::PYTHON_EXCEPTION, "Null Python object"}});
    }

    const char* jsonCStr = PyUnicode_AsUTF8(obj);
    if (!jsonCStr) {
        PyErr_Print();
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::PYTHON_EXCEPTION, "Failed to extract string"}});
    }

    return std::string(jsonCStr);
}

PyObject* HlirBridge::buildMetadataDict(const std::map<std::string, std::string>& metadata) {
    PyObject* dict = PyDict_New();
    for (const auto& [key, value] : metadata) {
        PyObject* pyKey = PyUnicode_FromString(key.c_str());
        PyObject* pyValue = PyUnicode_FromString(value.c_str());
        PyDict_SetItem(dict, pyKey, pyValue);
        Py_DECREF(pyKey);
        Py_DECREF(pyValue);
    }
    return dict;
}

PyObject* HlirBridge::buildPythonList(const std::vector<std::string>& items) {
    PyObject* list = PyList_New(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        PyObject* item = PyUnicode_FromString(items[i].c_str());
        PyList_SetItem(list, i, item);
    }
    return list;
}

PyObject* HlirBridge::buildPythonList(const std::vector<int>& items) {
    PyObject* list = PyList_New(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        PyObject* item = PyLong_FromLong(items[i]);
        PyList_SetItem(list, i, item);
    }
    return list;
}

// Helper to build list of ComponentId strings
PyObject* buildComponentIdList(const std::vector<ComponentId>& ids) {
    PyObject* list = PyList_New(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        PyObject* item = PyUnicode_FromString(ids[i].value.c_str());
        PyList_SetItem(list, i, item);
    }
    return list;
}

// ============================================================================
// Template specializations for parseJsonResult
// ============================================================================

template<>
HlirResult<ComponentId> HlirBridge::parseJsonResult<ComponentId>(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);

        if (!j["success"].get<bool>()) {
            std::string errorCodeStr = j.value("error_code", "UNKNOWN_ERROR");
            ErrorCode code = ErrorCode::UNKNOWN_ERROR;
            if (errorCodeStr == "DUPLICATE_NAME") code = ErrorCode::DUPLICATE_NAME;
            else if (errorCodeStr == "NOT_FOUND") code = ErrorCode::NOT_FOUND;
            else if (errorCodeStr == "DEPENDENCY_EXISTS") code = ErrorCode::DEPENDENCY_EXISTS;
            else if (errorCodeStr == "INVALID_PARAMETER") code = ErrorCode::INVALID_PARAMETER;

            std::string message = j.value("error_message", "Unknown error");
            std::string entityId = j.value("entity_id", "");
            std::vector<std::string> deps;
            if (j.contains("dependencies")) {
                for (const auto& dep : j["dependencies"]) {
                    deps.push_back(dep.get<std::string>());
                }
            }

            return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{code, message, entityId, deps}});
        }

        return ComponentId{j["id"].get<std::string>()};
    }
    catch (const json::exception& e) {
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::JSON_PARSE_ERROR, e.what()}});
    }
}

template<>
HlirResult<void> HlirBridge::parseJsonResult<void>(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);

        if (!j["success"].get<bool>()) {
            std::string errorCodeStr = j.value("error_code", "UNKNOWN_ERROR");
            ErrorCode code = ErrorCode::UNKNOWN_ERROR;
            if (errorCodeStr == "DUPLICATE_NAME") code = ErrorCode::DUPLICATE_NAME;
            else if (errorCodeStr == "NOT_FOUND") code = ErrorCode::NOT_FOUND;
            else if (errorCodeStr == "DEPENDENCY_EXISTS") code = ErrorCode::DEPENDENCY_EXISTS;
            else if (errorCodeStr == "INVALID_PARAMETER") code = ErrorCode::INVALID_PARAMETER;

            std::string message = j.value("error_message", "Unknown error");
            std::string entityId = j.value("entity_id", "");
            std::vector<std::string> deps;
            if (j.contains("dependencies")) {
                for (const auto& dep : j["dependencies"]) {
                    deps.push_back(dep.get<std::string>());
                }
            }

            return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{code, message, entityId, deps}});
        }

        return {};
    }
    catch (const json::exception& e) {
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::JSON_PARSE_ERROR, e.what()}});
    }
}

template<>
HlirResult<std::string> HlirBridge::parseJsonResult<std::string>(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);

        if (!j["success"].get<bool>()) {
            std::string errorCodeStr = j.value("error_code", "UNKNOWN_ERROR");
            ErrorCode code = ErrorCode::UNKNOWN_ERROR;
            std::string message = j.value("error_message", "Unknown error");
            return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{code, message}});
        }

        return j["data"].dump();
    }
    catch (const json::exception& e) {
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::JSON_PARSE_ERROR, e.what()}});
    }
}

template<>
HlirResult<std::vector<ComponentId>> HlirBridge::parseJsonResult<std::vector<ComponentId>>(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);

        if (!j["success"].get<bool>()) {
            std::string message = j.value("error_message", "Unknown error");
            return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::UNKNOWN_ERROR, message}});
        }

        std::vector<ComponentId> ids;
        for (const auto& idStr : j["ids"]) {
            ids.push_back(ComponentId{idStr.get<std::string>()});
        }
        return ids;
    }
    catch (const json::exception& e) {
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::JSON_PARSE_ERROR, e.what()}});
    }
}

template<>
HlirResult<ProgramStats> HlirBridge::parseJsonResult<ProgramStats>(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);

        if (!j["success"].get<bool>()) {
            std::string message = j.value("error_message", "Unknown error");
            return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::UNKNOWN_ERROR, message}});
        }

        return ProgramStats::fromJson(j["data"]);
    }
    catch (const json::exception& e) {
        return std::unexpected(std::vector<HlirDiagnostic>{HlirDiagnostic{ErrorCode::JSON_PARSE_ERROR, e.what()}});
    }
}

// ============================================================================
// Add methods - ID-based implementation
// ============================================================================

HlirResult<ComponentId> HlirBridge::addSymbol(
    const std::string& name,
    const std::string& value,
    const std::string& typeHint,
    bool isConstant,
    const ComponentId& providedId)
{
    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(sssOs)",
        name.c_str(), value.c_str(), typeHint.c_str(),
        isConstant ? Py_True : Py_False, providedIdStr);

    auto pyRes = callBuilderMethod("add_symbol", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addConstant(
    const std::string& name,
    const std::string& value,
    const std::string& typeHint,
    const ComponentId& providedId)
{
    return addSymbol(name, value, typeHint, true, providedId);
}

HlirResult<ComponentId> HlirBridge::addTensorType(
    const std::string& name,
    const std::vector<std::string>& shape,
    const std::string& dtype,
    const std::string& layout,
    const ComponentId& providedId)
{
    PyObject* shapeList = buildPythonList(shape);
    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(sOsss)",
        name.c_str(), shapeList, dtype.c_str(), layout.c_str(), providedIdStr);

    auto pyRes = callBuilderMethod("add_tensor_type", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addTile(
    const std::string& name,
    TileKind kind,
    int x,
    int y,
    const ComponentId& providedId,
    const std::map<std::string, std::string>& metadata)
{
    PyObject* metadataDict = buildMetadataDict(metadata);
    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(ssiiOs)",
        name.c_str(), tileKindToString(kind), x, y, metadataDict, providedIdStr);

    auto pyRes = callBuilderMethod("add_tile", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addFifo(
    const std::string& name,
    const ComponentId& objTypeId,
    int depth,
    const std::optional<ComponentId>& producerId,
    const std::vector<ComponentId>& consumerIds,
    const ComponentId& providedId,
    const std::map<std::string, std::string>& metadata)
{
    PyObject* consumersList = buildComponentIdList(consumerIds);
    PyObject* metadataDict = buildMetadataDict(metadata);

    const char* objTypeStr = objTypeId.empty() ? "" : objTypeId.value.c_str();
    const char* producerStr = (producerId && !producerId->empty()) ? producerId->value.c_str() : "";
    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(ssisOOs)",
        name.c_str(), objTypeStr, depth,
        producerStr, consumersList, metadataDict, providedIdStr);

    auto pyRes = callBuilderMethod("add_fifo", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addFifoSimpleType(
    const std::string& name,
    const std::string& objTypeStr,
    int depth,
    const std::optional<ComponentId>& producerId,
    const std::vector<ComponentId>& consumerIds,
    const ComponentId& providedId,
    const std::map<std::string, std::string>& metadata)
{
    PyObject* consumersList = buildComponentIdList(consumerIds);
    PyObject* metadataDict = buildMetadataDict(metadata);

    const char* producerStr = (producerId && !producerId->empty()) ? producerId->value.c_str() : "";
    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(ssisOOs)",
        name.c_str(), objTypeStr.c_str(), depth,
        producerStr, consumersList, metadataDict, providedIdStr);

    auto pyRes = callBuilderMethod("add_fifo_simple_type", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addFifoSplit(
    const std::string& name,
    const ComponentId& sourceId,
    int numOutputs,
    const ComponentId& outputTypeId,
    const std::vector<std::string>& outputNames,
    const std::vector<int>& offsets,
    const ComponentId& placementId,
    const ComponentId& providedId,
    const std::map<std::string, std::string>& metadata)
{
    PyObject* outputNamesList = buildPythonList(outputNames);
    PyObject* offsetsList = buildPythonList(offsets);
    PyObject* metadataDict = buildMetadataDict(metadata);

    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(ssisOOsOs)",
        name.c_str(), sourceId.value.c_str(), numOutputs,
        outputTypeId.value.c_str(), outputNamesList, offsetsList,
        placementId.value.c_str(), metadataDict, providedIdStr);

    auto pyRes = callBuilderMethod("add_fifo_split", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addFifoJoin(
    const std::string& name,
    const ComponentId& destId,
    int numInputs,
    const ComponentId& inputTypeId,
    const std::vector<std::string>& inputNames,
    const std::vector<int>& offsets,
    const ComponentId& placementId,
    const ComponentId& providedId,
    const std::map<std::string, std::string>& metadata)
{
    PyObject* inputNamesList = buildPythonList(inputNames);
    PyObject* offsetsList = buildPythonList(offsets);
    PyObject* metadataDict = buildMetadataDict(metadata);

    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(ssisOOsOs)",
        name.c_str(), destId.value.c_str(), numInputs,
        inputTypeId.value.c_str(), inputNamesList, offsetsList,
        placementId.value.c_str(), metadataDict, providedIdStr);

    auto pyRes = callBuilderMethod("add_fifo_join", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addFifoForward(
    const std::string& name,
    const ComponentId& sourceId,
    const ComponentId& providedId,
    const std::map<std::string, std::string>& metadata)
{
    PyObject* metadataDict = buildMetadataDict(metadata);

    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(ssOs)",
        name.c_str(), sourceId.value.c_str(), metadataDict, providedIdStr);

    auto pyRes = callBuilderMethod("add_fifo_forward", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addExternalKernel(
    const std::string& name,
    const std::string& kernelName,
    const std::string& sourceFile,
    const std::vector<ComponentId>& argTypeIds,
    const std::vector<std::string>& includeDirs,
    const ComponentId& providedId,
    const std::map<std::string, std::string>& metadata)
{
    PyObject* argTypesList = buildComponentIdList(argTypeIds);
    PyObject* includeDirsList = buildPythonList(includeDirs);
    PyObject* metadataDict = buildMetadataDict(metadata);

    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(sssOOOs)",
        name.c_str(), kernelName.c_str(), sourceFile.c_str(),
        argTypesList, includeDirsList, metadataDict, providedIdStr);

    auto pyRes = callBuilderMethod("add_external_kernel", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addCoreFunction(
    const std::string& name,
    const std::vector<std::string>& parameters,
    const std::vector<AcquireSpec>& acquires,
    const KernelCallSpec& kernelCall,
    const std::vector<ReleaseSpec>& releases,
    const ComponentId& providedId,
    const std::map<std::string, std::string>& metadata)
{
    PyObject* paramsList = buildPythonList(parameters);

    // Build acquires list
    PyObject* acquiresList = PyList_New(acquires.size());
    for (size_t i = 0; i < acquires.size(); ++i) {
        const auto& acq = acquires[i];
        PyObject* acquire = Py_BuildValue("(sis)",
            acq.paramName.c_str(), acq.numElements, acq.varName.c_str());
        PyList_SetItem(acquiresList, i, acquire);
    }

    // Build kernel call
    PyObject* argsList = buildPythonList(kernelCall.argVarNames);
    PyObject* kernelCallTuple = Py_BuildValue("(sO)",
        kernelCall.kernelParamName.c_str(), argsList);
    Py_DECREF(argsList);

    // Build releases list
    PyObject* releasesList = PyList_New(releases.size());
    for (size_t i = 0; i < releases.size(); ++i) {
        const auto& rel = releases[i];
        PyObject* release = Py_BuildValue("(si)",
            rel.paramName.c_str(), rel.numElements);
        PyList_SetItem(releasesList, i, release);
    }

    PyObject* metadataDict = buildMetadataDict(metadata);

    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(sOOOOOs)",
        name.c_str(), paramsList, acquiresList, kernelCallTuple,
        releasesList, metadataDict, providedIdStr);

    auto pyRes = callBuilderMethod("add_core_function", args);
    Py_DECREF(args);
    Py_DECREF(kernelCallTuple);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::addWorker(
    const std::string& name,
    const ComponentId& coreFnId,
    const std::vector<FunctionArg>& fnArgs,
    const ComponentId& placementId,
    const ComponentId& providedId,
    const std::map<std::string, std::string>& metadata)
{
    PyObject* metadataDict = buildMetadataDict(metadata);

    // Build function arguments as JSON
    json fnArgsJson = json::array();
    for (const auto& arg : fnArgs) {
        json argJson;
        if (arg.type == FunctionArg::Type::KERNEL) {
            argJson["type"] = "kernel";
            argJson["id"] = arg.componentId.value;
        } else if (arg.type == FunctionArg::Type::SPLIT) {
            // Split operation output - reference by name with index
            argJson["type"] = "split";
            argJson["id"] = arg.componentId.value;
            argJson["direction"] = arg.fifoDirection;
            argJson["index"] = arg.fifoIndex;
        } else if (arg.type == FunctionArg::Type::JOIN) {
            // Join operation input - reference by name with index
            argJson["type"] = "join";
            argJson["id"] = arg.componentId.value;
            argJson["direction"] = arg.fifoDirection;
            argJson["index"] = arg.fifoIndex;
        } else {
            // Regular FIFO
            argJson["type"] = "fifo";
            argJson["id"] = arg.componentId.value;
            argJson["direction"] = arg.fifoDirection;
            argJson["index"] = arg.fifoIndex;
        }
        fnArgsJson.push_back(argJson);
    }

    std::string fnArgsJsonStr = fnArgsJson.dump();

    const char* providedIdStr = providedId.value.empty() ? nullptr : providedId.value.c_str();

    PyObject* args = Py_BuildValue("(ssssOs)",
        name.c_str(), coreFnId.value.c_str(),
        fnArgsJsonStr.c_str(), placementId.value.c_str(),
        metadataDict, providedIdStr);

    auto pyRes = callBuilderMethod("add_worker", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

// ============================================================================
// Lookup operations
// ============================================================================

HlirResult<std::string> HlirBridge::lookupById(const ComponentId& id) {
    PyObject* args = Py_BuildValue("(s)", id.value.c_str());

    auto pyRes = callBuilderMethod("lookup_by_id", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<std::string>(jsonRes.value());
}

HlirResult<ComponentId> HlirBridge::lookupByName(
    ComponentType type,
    const std::string& name)
{
    PyObject* args = Py_BuildValue("(ss)",
        componentTypeToString(type), name.c_str());

    auto pyRes = callBuilderMethod("lookup_by_name", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<std::vector<ComponentId>> HlirBridge::getAllIds(ComponentType type) {
    PyObject* args = Py_BuildValue("(s)", componentTypeToString(type));

    auto pyRes = callBuilderMethod("get_all_ids", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<std::vector<ComponentId>>(jsonRes.value());
}

// ============================================================================
// Update/Remove operations
// ============================================================================

HlirResult<void> HlirBridge::updateFifoDepth(const ComponentId& id, int newDepth) {
    PyObject* args = Py_BuildValue("(si)", id.value.c_str(), newDepth);

    auto pyRes = callBuilderMethod("update_fifo_depth", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::remove(const ComponentId& id) {
    PyObject* args = Py_BuildValue("(s)", id.value.c_str());

    auto pyRes = callBuilderMethod("remove", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

// ============================================================================
// Runtime operations
// ============================================================================

HlirResult<ComponentId> HlirBridge::createRuntime(const std::string& name) {
    PyObject* args = Py_BuildValue("(s)", name.c_str());

    auto pyRes = callBuilderMethod("create_runtime", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    Py_XDECREF(m_runtime);
    m_runtime = pyRes.value();
    Py_INCREF(m_runtime);

    auto jsonRes = extractJsonString(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ComponentId>(jsonRes.value());
}

HlirResult<void> HlirBridge::runtimeAddInputType(const ComponentId& typeId) {
    PyObject* args = Py_BuildValue("(s)", typeId.value.c_str());

    auto pyRes = callBuilderMethod("runtime_add_input_type", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::runtimeAddOutputType(const ComponentId& typeId) {
    PyObject* args = Py_BuildValue("(s)", typeId.value.c_str());

    auto pyRes = callBuilderMethod("runtime_add_output_type", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::runtimeAddParam(const std::string& paramName) {
    PyObject* args = Py_BuildValue("(s)", paramName.c_str());

    auto pyRes = callBuilderMethod("runtime_add_param", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::runtimeAddWorker(const ComponentId& workerId) {
    PyObject* args = Py_BuildValue("(s)", workerId.value.c_str());

    auto pyRes = callBuilderMethod("runtime_add_worker", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::runtimeAddFill(
    const std::string& name,
    const ComponentId& fifoId,
    const std::string& inputName,
    const ComponentId& tileId,
    int column,
    bool useTap)
{
    // Build args with column and useTap parameters
    PyObject* args = Py_BuildValue("(ssssiO)",
        name.c_str(), fifoId.value.c_str(),
        inputName.c_str(), tileId.value.c_str(),
        column, useTap ? Py_True : Py_False);

    auto pyRes = callBuilderMethod("runtime_add_fill", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::runtimeAddDrain(
    const std::string& name,
    const ComponentId& fifoId,
    const std::string& outputName,
    const ComponentId& tileId,
    int column,
    bool useTap)
{
    // Build args with column and useTap parameters
    PyObject* args = Py_BuildValue("(ssssiO)",
        name.c_str(), fifoId.value.c_str(),
        outputName.c_str(), tileId.value.c_str(),
        column, useTap ? Py_True : Py_False);

    auto pyRes = callBuilderMethod("runtime_add_drain", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::runtimeBuild() {
    PyObject* args = PyTuple_New(0);

    auto pyRes = callBuilderMethod("runtime_build", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

// ============================================================================
// Program building and export
// ============================================================================

HlirResult<void> HlirBridge::build() {
    PyObject* args = PyTuple_New(0);

    auto pyRes = callBuilderMethod("build", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::getProgram() {
    PyObject* args = PyTuple_New(0);

    auto pyRes = callBuilderMethod("get_program", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::exportToGuiXml(const std::string& filePath) {
    PyObject* args = Py_BuildValue("(s)", filePath.c_str());

    auto pyRes = callBuilderMethod("export_to_gui_xml", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<std::string> HlirBridge::exportToGuiXmlString() {
    PyObject* args = PyTuple_New(0);

    auto pyRes = callBuilderMethod("export_to_gui_xml_string", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<std::string>(jsonRes.value());
}

HlirResult<ProgramStats> HlirBridge::getStats() {
    PyObject* args = PyTuple_New(0);

    auto pyRes = callBuilderMethod("get_stats", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<ProgramStats>(jsonRes.value());
}

HlirResult<void> HlirBridge::serializeToTempXml(const std::string& filePath) {
    PyObject* args = Py_BuildValue("(s)", filePath.c_str());

    auto pyRes = callBuilderMethod("serialize_to_temp_xml", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

HlirResult<void> HlirBridge::loadFromXml(const std::string& filePath) {
    PyObject* args = Py_BuildValue("(s)", filePath.c_str());

    auto pyRes = callBuilderMethod("load_from_xml", args);
    Py_DECREF(args);

    if (!pyRes) return std::unexpected(pyRes.error());

    auto jsonRes = extractJsonString(pyRes.value());
    Py_DECREF(pyRes.value());

    if (!jsonRes) return std::unexpected(jsonRes.error());
    return parseJsonResult<void>(jsonRes.value());
}

// ============================================================================
// Component data fromJson implementations
// ============================================================================

TileData TileData::fromJson(const json& j) {
    TileData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();

    std::string kindStr = j["kind"].get<std::string>();
    auto kindOpt = stringToTileKind(kindStr);
    data.kind = kindOpt.value_or(TileKind::COMPUTE);

    data.x = j["x"].get<int>();
    data.y = j["y"].get<int>();

    if (j.contains("metadata")) {
        for (auto& [key, value] : j["metadata"].items()) {
            data.metadata[key] = value.get<std::string>();
        }
    }

    return data;
}

FifoData FifoData::fromJson(const json& j) {
    FifoData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    data.objType = j["obj_type"].get<std::string>();
    data.depth = j["depth"].get<int>();

    if (j.contains("producer") && !j["producer"].is_null()) {
        data.producer = ComponentId{j["producer"].get<std::string>()};
    }

    if (j.contains("consumers")) {
        for (const auto& c : j["consumers"]) {
            data.consumers.push_back(ComponentId{c.get<std::string>()});
        }
    }

    if (j.contains("metadata")) {
        for (auto& [key, value] : j["metadata"].items()) {
            data.metadata[key] = value.get<std::string>();
        }
    }

    return data;
}

ProgramStats ProgramStats::fromJson(const json& j) {
    ProgramStats stats;
    stats.numSymbols = j["num_symbols"].get<int>();
    stats.numTiles = j["num_tiles"].get<int>();
    stats.numFifos = j["num_fifos"].get<int>();
    stats.numExternalKernels = j["num_external_kernels"].get<int>();
    stats.numCoreFunctions = j["num_core_functions"].get<int>();
    stats.numWorkers = j["num_workers"].get<int>();
    stats.hasRuntime = j["has_runtime"].get<bool>();
    return stats;
}

// Stub implementations
ExternalKernelData ExternalKernelData::fromJson(const json& j) {
    ExternalKernelData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    return data;
}

SymbolData SymbolData::fromJson(const json& j) {
    SymbolData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    return data;
}

TensorTypeData TensorTypeData::fromJson(const json& j) {
    TensorTypeData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    return data;
}

CoreFunctionData CoreFunctionData::fromJson(const json& j) {
    CoreFunctionData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    return data;
}

WorkerData WorkerData::fromJson(const json& j) {
    WorkerData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    return data;
}

SplitOperationData SplitOperationData::fromJson(const json& j) {
    SplitOperationData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    return data;
}

JoinOperationData JoinOperationData::fromJson(const json& j) {
    JoinOperationData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    return data;
}

ForwardOperationData ForwardOperationData::fromJson(const json& j) {
    ForwardOperationData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    return data;
}

RuntimeSequenceData RuntimeSequenceData::fromJson(const json& j) {
    RuntimeSequenceData data;
    data.id = ComponentId{j["id"].get<std::string>()};
    data.name = j["name"].get<std::string>();
    return data;
}

} // namespace hlir
