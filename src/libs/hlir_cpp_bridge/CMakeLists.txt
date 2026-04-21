# Find Python
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# Find or fetch nlohmann_json
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

ironsmith_add_library(HlirCppBridge
    SOURCES
        # Headers
        HlirTypes.hpp
        HlirComponents.hpp
        HlirBridge.hpp

        # Implementation
        HlirBridge.cpp

    PUBLIC_DEPENDS
        nlohmann_json::nlohmann_json

    PRIVATE_DEPENDS
        Python3::Python
)

# Add Python include directories
target_include_directories(HlirCppBridge PRIVATE ${Python3_INCLUDE_DIRS})

# Bake the Python home path so the embedded interpreter finds its standard library.
# On Windows the stdlib is at <home>/Lib (one level up from stdlib = home).
# On macOS frameworks the stdlib is at <home>/lib/python3.x (two levels up = home).
if(WIN32)
    cmake_path(GET Python3_STDLIB PARENT_PATH _python3_home)
else()
    cmake_path(GET Python3_STDLIB PARENT_PATH _python3_stdlib_parent)
    cmake_path(GET _python3_stdlib_parent PARENT_PATH _python3_home)
endif()
# Convert backslashes to forward slashes so the path is a valid C++ string literal
string(REPLACE "\\" "/" _python3_home "${_python3_home}")
target_compile_definitions(HlirCppBridge PRIVATE
    PYTHON_HOME_DIR="${_python3_home}"
    HLIR_AIECAD_DIR="${CMAKE_SOURCE_DIR}/src/aiecad_compiler"
    HLIR_BRIDGE_PYTHON_DIR="${CMAKE_BINARY_DIR}/hlir_cpp_bridge/python"
)

# Copy Python wrapper module to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/python/hlir_bridge_wrapper.py
    ${CMAKE_BINARY_DIR}/hlir_cpp_bridge/python/hlir_bridge_wrapper.py
    COPYONLY
)

# Install Python wrapper module to resources/hlir_bridge/ so the runtime path
# resolution in HlirBridge.cpp finds it in installed distributions.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/python/hlir_bridge_wrapper.py
        DESTINATION "resources/hlir_bridge")

# Install Python DLL (Windows runtime dependency for the embedded interpreter).
if(WIN32 AND Python3_RUNTIME_LIBRARY_DIRS)
    file(GLOB _python_dlls "${Python3_RUNTIME_LIBRARY_DIRS}/python*.dll")
    install(FILES ${_python_dlls} DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

# Print configuration
message(STATUS "HLIR C++ Bridge Configuration:")
message(STATUS "  Python3 Version: ${Python3_VERSION}")
message(STATUS "  Python3 Include: ${Python3_INCLUDE_DIRS}")
message(STATUS "  Python3 Libraries: ${Python3_LIBRARIES}")
message(STATUS "  nlohmann_json: Found")

# Bridge tests are compiled into the ironsmith executable (see src/app/CMakeLists.txt)
# and run via: ironsmith.exe --test-bridges
