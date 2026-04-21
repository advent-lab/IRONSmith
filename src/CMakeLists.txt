include(GNUInstallDirs)

set(IRONSMITH_INSTALL_NAME "ironsmith" CACHE STRING "Install subdir name used under standard install roots")

set(IRONSMITH_INSTALL_DATADIR
        "${CMAKE_INSTALL_DATADIR}/${IRONSMITH_INSTALL_NAME}"
        CACHE PATH "Install directory for IRONSmith data files"
)

set(IRONSMITH_INSTALL_LIBDIR
        "${CMAKE_INSTALL_LIBDIR}/${IRONSMITH_INSTALL_NAME}"
        CACHE PATH "Install directory for IRONSmith libraries"
)

set(IRONSMITH_INSTALL_PLUGINDIR
        "${IRONSMITH_INSTALL_LIBDIR}/plugins"
        CACHE PATH "Install directory for IRONSmith system plugins"
)

set(IRONSMITH_INSTALL_LIBEXECDIR
        "${CMAKE_INSTALL_LIBEXECDIR}/${IRONSMITH_INSTALL_NAME}"
        CACHE PATH "Install directory for IRONSmith helper executables"
)

set(IRONSMITH_INSTALL_CMAKEDIR
        "${CMAKE_INSTALL_LIBDIR}/cmake/IRONSmith"
        CACHE PATH "Install directory for IRONSmith CMake package files"
)

if (NOT DEFINED IRONSMITH_HEADER_INSTALL_PATH)
    set(IRONSMITH_HEADER_INSTALL_PATH "${CMAKE_INSTALL_INCLUDEDIR}" CACHE PATH "Install directory for IRONSmith headers")
endif()

set(IRONSMITH_PLUGIN_INSTALL_DIR "${IRONSMITH_INSTALL_PLUGINDIR}")
set(IRONSMITH_LIBRARY_INSTALL_DIR "${IRONSMITH_INSTALL_LIBDIR}")
set(IRONSMITH_CMAKE_INSTALL_DIR "${IRONSMITH_INSTALL_CMAKEDIR}")

set(IRONSMITH_BUILD_BINDIR
        "${CMAKE_BINARY_DIR}/bin"
        CACHE PATH "Build-tree output directory for executables"
)

set(IRONSMITH_BUILD_LIBDIR
        "${CMAKE_BINARY_DIR}/lib"
        CACHE PATH "Build-tree output directory for libraries"
)

set(IRONSMITH_BUILD_DATADIR
        "${CMAKE_BINARY_DIR}/share/${IRONSMITH_INSTALL_NAME}"
        CACHE PATH "Build-tree output directory for data files"
)

set(IRONSMITH_BUILD_PLUGINDIR
        "${IRONSMITH_BUILD_LIBDIR}/${IRONSMITH_INSTALL_NAME}/plugins"
        CACHE PATH "Build-tree output directory for system plugins"
)

set(IRONSMITH_BUILD_LIBEXECDIR
        "${CMAKE_BINARY_DIR}/libexec/${IRONSMITH_INSTALL_NAME}"
        CACHE PATH "Build-tree output directory for helper executables"
)

set(IRONSMITH_SRC_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs")
set(IRONSMITH_SRC_PLUGINS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugins")
set(IRONSMITH_SRC_APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/app")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY  "${IRONSMITH_BUILD_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY  "${IRONSMITH_BUILD_LIBDIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY  "${IRONSMITH_BUILD_LIBDIR}")

add_subdirectory(libs)
add_subdirectory(plugins)
add_subdirectory(app)

# Install kernel source files so the installed application can reference them.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/kernels"
        DESTINATION "resources")

# Install codegen Python scripts so CodeGenBridge finds main.py at runtime.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/aiecad_compiler/"
        DESTINATION "resources/codegen"
        FILES_MATCHING
            PATTERN "*.py"
            PATTERN "__pycache__" EXCLUDE
            PATTERN "*.pyc" EXCLUDE)