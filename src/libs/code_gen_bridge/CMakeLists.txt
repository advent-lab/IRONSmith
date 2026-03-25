cmake_minimum_required(VERSION 3.20)

# Find Python3
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

# Create the code generation bridge library
ironsmith_add_library(CodeGenBridge
    SOURCES
        CodeGenBridge.hpp
        CodeGenBridge.cpp
    PUBLIC_DEPENDS
        Python3::Python
)

# Set include directories
target_include_directories(CodeGenBridge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Bake the absolute path to main.py so it's found regardless of working directory.
target_compile_definitions(CodeGenBridge PRIVATE
    CODEGEN_MAIN_PY="${CMAKE_SOURCE_DIR}/src/aiecad_compiler/main.py"
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(CodeGenBridge PRIVATE _WIN32)
endif()

# C++23 required for std::expected
target_compile_features(CodeGenBridge PUBLIC cxx_std_23)

