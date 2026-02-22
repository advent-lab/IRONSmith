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

# Example executable for exercising the HLIR C++ bridge from C++
set(IRONSMITH_BUILD_HLIR_CPP_BRIDGE_EXAMPLE ON CACHE BOOL
    "Build the hlir_cpp_bridge_example executable."
)

if (IRONSMITH_BUILD_HLIR_CPP_BRIDGE_EXAMPLE)
    add_executable(hlir_cpp_bridge_example
        example_usage.cpp
    )

    target_link_libraries(hlir_cpp_bridge_example
        PRIVATE
            IRONSmith::HlirCppBridge
            Python3::Python
            nlohmann_json::nlohmann_json
    )

    set_target_properties(hlir_cpp_bridge_example PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        CXX_EXTENSIONS OFF
    )
endif()
