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
# Use CMake path (forward slashes) which Windows APIs accept fine.
cmake_path(GET Python3_EXECUTABLE PARENT_PATH _python3_home)
target_compile_definitions(HlirCppBridge PRIVATE
    PYTHON_HOME_DIR="${_python3_home}"
)

# Copy Python wrapper module to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/python/hlir_bridge_wrapper.py
    ${CMAKE_BINARY_DIR}/hlir_cpp_bridge/python/hlir_bridge_wrapper.py
    COPYONLY
)

# Print configuration
message(STATUS "HLIR C++ Bridge Configuration:")
message(STATUS "  Python3 Version: ${Python3_VERSION}")
message(STATUS "  Python3 Include: ${Python3_INCLUDE_DIRS}")
message(STATUS "  Python3 Libraries: ${Python3_LIBRARIES}")
message(STATUS "  nlohmann_json: Found")

# Bridge tests are compiled into the ironsmith executable (see src/app/CMakeLists.txt)
# and run via: ironsmith.exe --test-bridges
