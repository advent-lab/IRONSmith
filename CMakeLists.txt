cmake_minimum_required(VERSION 3.27)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(IRONSmithBrandingProfile)

project(IRONSmith
        VERSION   ${IRONSMITH_VERSION}
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

function(env_with_default _env_name _out_var _default)
    if (DEFINED ENV{${_env_name}})
        set(${_out_var} "$ENV{${_env_name}}" PARENT_SCOPE)
    else()
        set(${_out_var} "${_default}" PARENT_SCOPE)
    endif()
endfunction()

env_with_default("BUILD_WITH_TESTS"    _BUILD_WITH_TESTS_DEFAULT    OFF)
env_with_default("BUILD_WITH_PCH"      _BUILD_WITH_PCH_DEFAULT      ON)
env_with_default("BUILD_WITH_SANITIZE" _BUILD_WITH_SANITIZE_DEFAULT OFF)

option(BUILD_WITH_TESTS     "Build with tests"                             ${_BUILD_WITH_TESTS_DEFAULT})
option(BUILD_WITH_PCH       "Build with precompiled headers"              ${_BUILD_WITH_PCH_DEFAULT})
option(BUILD_WITH_SANITIZE  "Build with sanitizer enabled (Debug builds)"  ${_BUILD_WITH_SANITIZE_DEFAULT})

set(SANITIZE_FLAGS "" CACHE STRING "Additional sanitizer flags (e.g., -fsanitize=address,undefined)")

include(IRONSmithQtDiscovery)
ironsmith_configure_qt6_discovery()

if (BUILD_WITH_TESTS)
    set(_QT_TEST_COMPONENT Test)
endif()

find_package(Qt6 REQUIRED
        COMPONENTS
        Core
        Gui
        Widgets
        Network
        Xml
        Concurrent
        Test
        ${_QT_TEST_COMPONENT}
)
set(QT6_REQUIRED_COMPONENTS Qt6::Core Qt6::Widgets Qt6::Gui)

find_package(Qt6 OPTIONAL_COMPONENTS
        Qml
        Quick
        QuickWidgets
        Sql
        Svg
        PrintSupport
        LinguistTools
        Help
        SerialPort
        Designer
        DesignerComponents
        QUIET
)

include(IRONSmithAPI)

if (BUILD_WITH_SANITIZE)
    if (SANITIZE_FLAGS STREQUAL "")
        message(FATAL_ERROR
                "BUILD_WITH_SANITIZE=ON but SANITIZE_FLAGS is empty. "
                "Example: -DSANITIZE_FLAGS=-fsanitize=address,undefined"
        )
    endif()

    add_library(iron_sanitize_flags INTERFACE)
    target_compile_options(iron_sanitize_flags INTERFACE
            $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:${SANITIZE_FLAGS}>
    )
    target_link_options(iron_sanitize_flags INTERFACE
            $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:${SANITIZE_FLAGS}>
    )
endif()

if (BUILD_WITH_TESTS)
    enable_testing()
    set(TOP_DIR "${PROJECT_SOURCE_DIR}")
    include(IRONSmithTests)
endif()

add_subdirectory(src)
