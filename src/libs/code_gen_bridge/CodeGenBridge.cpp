#include "CodeGenBridge.hpp"
#include <Python.h>
#include <fstream>
#include <sstream>
#include <array>
#include <cstdio>
#include <memory>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

namespace codegen {

namespace {

std::filesystem::path getExeDir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::string shellQuote(const std::string& value)
{
#ifdef _WIN32
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"')
            quoted += "\\\"";
        else
            quoted += ch;
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'')
            quoted += "'\\''";
        else
            quoted += ch;
    }
    quoted += "'";
    return quoted;
#endif
}

std::string pythonExecutable()
{
    // Prefer the Python bundled alongside the installed application.
#ifdef _WIN32
    auto bundled = getExeDir() / ".." / "python" / "python.exe";
#else
    auto bundled = getExeDir() / ".." / "python" / "bin" / "python3";
#endif
    if (std::filesystem::exists(bundled))
        return bundled.string();
#ifdef CODEGEN_PYTHON_EXECUTABLE
    return CODEGEN_PYTHON_EXECUTABLE;
#else
    return "python3";
#endif
}

int normalizeExitCode(int exitCode)
{
#ifdef _WIN32
    return exitCode;
#else
    if (exitCode != -1 && WIFEXITED(exitCode))
        return WEXITSTATUS(exitCode);
    return exitCode;
#endif
}

} // namespace

CodeGenBridge::CodeGenBridge()
    : m_pythonInitialized(false)
{
    // Initialize Python if not already initialized
    if (!Py_IsInitialized()) {
        Py_Initialize();
        m_pythonInitialized = true;
    }
}

CodeGenBridge::~CodeGenBridge() {
    // Only finalize if we initialized
    if (m_pythonInitialized && Py_IsInitialized()) {
        Py_Finalize();
    }
}

CodeGenResult<CodeGenOutput> CodeGenBridge::runCodeGen(
    const std::filesystem::path& xmlFilePath,
    const CodeGenOptions& options)
{
    // Validate XML file exists
    auto validation = validateXmlFile(xmlFilePath);
    if (!validation) {
        return std::unexpected(validation.error());
    }

    // Build arguments for main.py
    // main.py accepts: <xml_file> [--run]
    std::vector<std::string> args;

    // Add XML file path (required positional argument)
    args.push_back(xmlFilePath.string());

    // Note: main.py generates output files in the same directory as the input XML

    // Find main.py — prefer installed location, then fall back to build-tree path.
    auto installedMainPy = getExeDir() / ".." / "resources" / "codegen" / "main.py";
#ifdef CODEGEN_MAIN_PY
    std::filesystem::path mainPyPath = std::filesystem::exists(installedMainPy)
        ? installedMainPy : std::filesystem::path(CODEGEN_MAIN_PY);
#else
    std::filesystem::path mainPyPath = std::filesystem::exists(installedMainPy)
        ? installedMainPy : std::filesystem::path("src/aiecad_compiler/main.py");
#endif

    if (!std::filesystem::exists(mainPyPath)) {
        return std::unexpected(std::vector<CodeGenDiagnostic>{
            CodeGenDiagnostic{CodeGenError::FILE_NOT_FOUND,
                "Code generator main.py not found",
                "Expected at: " + mainPyPath.string()}
        });
    }

    // Run the code generator
    return runPythonScript(mainPyPath.string(), args);
}

CodeGenResult<CodeGenOutput> CodeGenBridge::runCustomScript(
    const std::filesystem::path& scriptPath,
    const std::vector<std::string>& args,
    const std::optional<std::filesystem::path>& workingDir)
{
    if (!std::filesystem::exists(scriptPath)) {
        return std::unexpected(std::vector<CodeGenDiagnostic>{
            CodeGenDiagnostic{CodeGenError::FILE_NOT_FOUND,
                "Python script not found: " + scriptPath.string()}
        });
    }

    return runPythonScript(scriptPath.string(), args, workingDir);
}

bool CodeGenBridge::isAvailable() const {
    // Check if Python is initialized
    if (!Py_IsInitialized()) {
        return false;
    }

    auto installedMainPy = getExeDir() / ".." / "resources" / "codegen" / "main.py";
    if (std::filesystem::exists(installedMainPy))
        return true;
#ifdef CODEGEN_MAIN_PY
    return std::filesystem::exists(std::filesystem::path(CODEGEN_MAIN_PY));
#else
    return std::filesystem::exists(std::filesystem::path("src/aiecad_compiler/main.py"));
#endif
}

CodeGenResult<std::string> CodeGenBridge::getVersion() {
    // Run Python to get version
    PyRun_SimpleString("import sys");

    PyObject* sysModule = PyImport_ImportModule("sys");
    if (!sysModule) {
        return std::unexpected(std::vector<CodeGenDiagnostic>{
            CodeGenDiagnostic{CodeGenError::PYTHON_ERROR, "Failed to import sys module"}
        });
    }

    PyObject* version = PyObject_GetAttrString(sysModule, "version");
    Py_DECREF(sysModule);

    if (!version) {
        return std::unexpected(std::vector<CodeGenDiagnostic>{
            CodeGenDiagnostic{CodeGenError::PYTHON_ERROR, "Failed to get Python version"}
        });
    }

    const char* versionStr = PyUnicode_AsUTF8(version);
    std::string result = versionStr ? versionStr : "unknown";
    Py_DECREF(version);

    return result;
}

CodeGenResult<CodeGenOutput> CodeGenBridge::runPythonScript(
    const std::string& script,
    const std::vector<std::string>& args,
    const std::optional<std::filesystem::path>& workingDir)
{
    std::string command = shellQuote(pythonExecutable()) + " " + shellQuote(script);
    for (const auto& arg : args)
        command += " " + shellQuote(arg);
#ifndef _WIN32
    command += " 2>&1";
#else
    command = "cmd /c \"" + command + "\"";
#endif

    std::filesystem::path originalDir;
    if (workingDir) {
        originalDir = std::filesystem::current_path();
        std::filesystem::current_path(*workingDir);
    }

    std::array<char, 128> buffer;
    std::string result;
    int exitCode = 0;

#ifdef _WIN32
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif

    if (!pipe) {
        if (workingDir)
            std::filesystem::current_path(originalDir);
        return std::unexpected(std::vector<CodeGenDiagnostic>{
            CodeGenDiagnostic{CodeGenError::PYTHON_ERROR,
                "Failed to execute Python script",
                "Command: " + command}
        });
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        result += buffer.data();

#ifdef _WIN32
    exitCode = _pclose(pipe.release());
#else
    exitCode = pclose(pipe.release());
#endif
    exitCode = normalizeExitCode(exitCode);

    if (workingDir)
        std::filesystem::current_path(originalDir);

    // Check for errors
    if (exitCode != 0) {
        return std::unexpected(std::vector<CodeGenDiagnostic>{
            CodeGenDiagnostic{CodeGenError::PYTHON_ERROR,
                "Code generation failed with exit code " + std::to_string(exitCode),
                result}
        });
    }

    // main.py generates files in the same directory as the input XML
    // Extract XML path from first argument
    std::filesystem::path xmlPath = args.empty() ? "" : args[0];
    std::filesystem::path outputDir = xmlPath.parent_path();
    if (outputDir.empty()) {
        outputDir = ".";
    }

    // Collect generated files (*.graphml and generated_*.py)
    auto generatedFiles = collectGeneratedFiles(outputDir);

    CodeGenOutput output;
    output.outputDirectory = outputDir;
    output.generatedFiles = std::move(generatedFiles);
    output.pythonOutput = result;
    output.exitCode = exitCode;

    return output;
}

CodeGenResult<void> CodeGenBridge::validateXmlFile(const std::filesystem::path& xmlPath) {
    if (!std::filesystem::exists(xmlPath)) {
        return std::unexpected(std::vector<CodeGenDiagnostic>{
            CodeGenDiagnostic{CodeGenError::FILE_NOT_FOUND,
                "XML file not found: " + xmlPath.string()}
        });
    }

    if (xmlPath.extension() != ".xml") {
        return std::unexpected(std::vector<CodeGenDiagnostic>{
            CodeGenDiagnostic{CodeGenError::INVALID_XML,
                "File is not an XML file: " + xmlPath.string()}
        });
    }

    // Try to open the file to check if it's readable
    std::ifstream file(xmlPath);
    if (!file.is_open()) {
        return std::unexpected(std::vector<CodeGenDiagnostic>{
            CodeGenDiagnostic{CodeGenError::FILE_NOT_FOUND,
                "Cannot read XML file: " + xmlPath.string()}
        });
    }

    return {};
}

std::vector<std::filesystem::path> CodeGenBridge::collectGeneratedFiles(
    const std::filesystem::path& outputDir)
{
    std::vector<std::filesystem::path> files;

    if (!std::filesystem::exists(outputDir)) {
        return files;
    }

    try {
        // Only collect specific generated files, not all files in directory
        // main.py generates:
        // - <name>.graphml
        // - generated_<name>.py
        // - <name>_complete.xml (if GUI XML was used)
        for (const auto& entry : std::filesystem::directory_iterator(outputDir)) {
            if (entry.is_regular_file()) {
                auto filename = entry.path().filename().string();
                // Only include files that match the generated pattern
                if (filename.ends_with(".graphml") ||
                    filename.starts_with("generated_") ||
                    filename.ends_with("_complete.xml")) {
                    files.push_back(entry.path());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        // If we can't iterate, just return empty list
        return files;
    }

    return files;
}

} // namespace codegen
