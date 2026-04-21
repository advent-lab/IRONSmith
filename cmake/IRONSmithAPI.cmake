# Copyright (C) 2016 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#
# Modifications Copyright (C) 2025 Samer Ali
# This file contains modifications for a university capstone project.

include_guard(GLOBAL)

include(IRONSmithBrandingProfile)
include(IRONSmithInternalAPI)

set(IRONSMITH_LIBRARIES)

function(_ironsmith_eval_condition _out_var)
    set(_result ON)
    if (ARGC GREATER 1)
        set(_cond_list "${ARGV1}")
        if (_cond_list)
            string(REPLACE ";" " " _cond_expr "${_cond_list}")
            if (NOT (${_cond_expr}))
                set(_result OFF)
            endif()
        endif()
    endif()
    set(${_out_var} "${_result}" PARENT_SCOPE)
endfunction()

# ironsmith_extend_target(target_name
#   [CONDITION expr...]
#   [PRIVATE_DEPENDS dep1 dep2 ...]
#   [PUBLIC_DEPENDS dep3 dep4 ...]
#   [PRIVATE_DEFINES FOO BAR ...]
#   [PUBLIC_DEFINES BAZ ...]
#   [PRIVATE_INCLUDES dir1 dir2 ...]
#   [PRIVATE_SYSTEM_INCLUDES dir3 dir4 ...]
#   [PUBLIC_INCLUDES dir5 ...]
#   [PUBLIC_SYSTEM_INCLUDES dir6 ...]
#   [SOURCES file1.cpp file2.cpp ...]
#   [EXPLICIT_MOC moc_this.cpp ...]
#   [SKIP_AUTOMOC dont_moc_this.cpp ...]
#   [PRIVATE_COMPILE_OPTIONS ...]
#   [PUBLIC_COMPILE_OPTIONS ...]
# )
function(ironsmith_extend_target target_name)
    set(_opt_args "")
    set(_single_args "")
    set(_multi_args
            CONDITION
            PRIVATE_DEPENDS
            PUBLIC_DEPENDS
            PRIVATE_DEFINES
            PUBLIC_DEFINES
            PRIVATE_INCLUDES
            PUBLIC_INCLUDES
            PRIVATE_SYSTEM_INCLUDES
            PUBLIC_SYSTEM_INCLUDES
            SOURCES
            EXPLICIT_MOC
            SKIP_AUTOMOC
            PRIVATE_COMPILE_OPTIONS
            PUBLIC_COMPILE_OPTIONS
    )

    cmake_parse_arguments(_args "${_opt_args}" "${_single_args}" "${_multi_args}" ${ARGN})

    if (_args_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "ironsmith_extend_target has unparsed arguments: ${_args_UNPARSED_ARGUMENTS}")
    endif ()

    _ironsmith_eval_condition(_enabled "${_args_CONDITION}")
    if (NOT _enabled)
        return()
    endif ()

    ironsmith_add_depends(${target_name}
            PUBLIC  ${_args_PUBLIC_DEPENDS}
            PRIVATE ${_args_PRIVATE_DEPENDS}
    )

    if (_args_PUBLIC_DEFINES OR _args_PRIVATE_DEFINES)
        target_compile_definitions(${target_name}
                PUBLIC  ${_args_PUBLIC_DEFINES}
                PRIVATE ${_args_PRIVATE_DEFINES}
        )
    endif()

    if (_args_PUBLIC_INCLUDES OR _args_PRIVATE_INCLUDES)
        target_include_directories(${target_name}
                PUBLIC  ${_args_PUBLIC_INCLUDES}
                PRIVATE ${_args_PRIVATE_INCLUDES}
        )
    endif()

    if (_args_PUBLIC_SYSTEM_INCLUDES)
        target_include_directories(${target_name}
                SYSTEM PUBLIC ${_args_PUBLIC_SYSTEM_INCLUDES}
        )
    endif()
    if (_args_PRIVATE_SYSTEM_INCLUDES)
        target_include_directories(${target_name}
                SYSTEM PRIVATE ${_args_PRIVATE_SYSTEM_INCLUDES}
        )
    endif()

    if (_args_SOURCES)
        target_sources(${target_name} PRIVATE ${_args_SOURCES})
    endif ()

    foreach (src IN LISTS _args_EXPLICIT_MOC)
        set_source_files_properties(${src} PROPERTIES SKIP_AUTOMOC OFF)
    endforeach ()

    foreach (src IN LISTS _args_SKIP_AUTOMOC)
        set_source_files_properties(${src} PROPERTIES SKIP_AUTOMOC ON)
    endforeach ()

    if (_args_PUBLIC_COMPILE_OPTIONS OR _args_PRIVATE_COMPILE_OPTIONS)
        target_compile_options(${target_name}
                PUBLIC  ${_args_PUBLIC_COMPILE_OPTIONS}
                PRIVATE ${_args_PRIVATE_COMPILE_OPTIONS}
        )
    endif()
endfunction()

# ironsmith_add_plugin(target_name
#   [SKIP_INSTALL]
#   [INTERNAL_ONLY]
#   [SKIP_PCH]                 # parsed but ignored for now
#   [PLUGIN_NAME name]
#   [PLUGIN_PATH path]
#   [PLUGIN_CLASS class_name]
#   [VERSION ver]
#   [BUILD_DEFAULT ON|OFF]
#   [CONDITION expr...]
#   [PRIVATE_DEPENDS ...]
#   [PUBLIC_DEPENDS ...]
#   [PRIVATE_DEFINES ...]
#   [PUBLIC_DEFINES ...]
#   [PRIVATE_INCLUDES ...]
#   [PRIVATE_SYSTEM_INCLUDES ...]
#   [PUBLIC_INCLUDES ...]
#   [PUBLIC_SYSTEM_INCLUDES ...]
#   [SOURCES ...]
#   [EXPLICIT_MOC ...]
#   [SKIP_AUTOMOC ...]
#   [PRIVATE_COMPILE_OPTIONS ...]
#   [PUBLIC_COMPILE_OPTIONS ...]
# )
function(ironsmith_add_plugin target_name)
    set(_opt_args
            SKIP_INSTALL
            INTERNAL_ONLY
            SKIP_PCH
    )
    set(_single_args
            VERSION
            PLUGIN_PATH
            PLUGIN_NAME
            PLUGIN_CLASS
            BUILD_DEFAULT
    )
    set(_multi_args
            CONDITION
            PRIVATE_DEPENDS
            PUBLIC_DEPENDS
            PRIVATE_INCLUDES
            PUBLIC_INCLUDES
            PRIVATE_SYSTEM_INCLUDES
            PUBLIC_SYSTEM_INCLUDES
            SOURCES
            EXPLICIT_MOC
            SKIP_AUTOMOC
            PRIVATE_DEFINES
            PUBLIC_DEFINES
            PRIVATE_COMPILE_OPTIONS
            PUBLIC_COMPILE_OPTIONS
    )

    cmake_parse_arguments(_args "${_opt_args}" "${_single_args}" "${_multi_args}" ${ARGN})

    message(STATUS "Registering plugin: ${_args_PLUGIN_NAME}")
    if (_args_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "ironsmith_add_plugin had unparsed arguments: ${_args_UNPARSED_ARGUMENTS}")
    endif ()

    if (NOT DEFINED IRONSMITH_BUILD_PLUGINS_BY_DEFAULT)
        set(IRONSMITH_BUILD_PLUGINS_BY_DEFAULT ON CACHE BOOL
                "Build IRONSmith plugins by default."
        )
    endif()

    if (NOT DEFINED IRONSMITH_PLUGIN_INSTALL_DIR)
        set(IRONSMITH_PLUGIN_INSTALL_DIR "plugins")
    endif()

    set(_plugin_name "${target_name}")
    if (_args_PLUGIN_NAME)
        set(_plugin_name "${_args_PLUGIN_NAME}")
    endif ()

    set(_plugin_class "${target_name}Plugin")
    if (_args_PLUGIN_CLASS)
        set(_plugin_class "${_args_PLUGIN_CLASS}")
    endif ()

    set(_plugin_version "${IRONSMITH_VERSION}")
    if (_args_VERSION)
        set(_plugin_version "${_args_VERSION}")
    endif ()

    string(TOUPPER "${target_name}" _name_upper)
    string(REGEX REPLACE "[^A-Z0-9_]" "_" _name_upper_sanitized "${_name_upper}")
    set(_build_var "IRONSMITH_BUILD_PLUGIN_${_name_upper_sanitized}")

    if (DEFINED _args_BUILD_DEFAULT)
        set(_build_default "${_args_BUILD_DEFAULT}")
    else ()
        set(_build_default "${IRONSMITH_BUILD_PLUGINS_BY_DEFAULT}")
    endif ()

    if (DEFINED ENV{${_build_var}})
        set(_build_default "$ENV{${_build_var}}")
    endif ()

    if (_args_INTERNAL_ONLY)
        set(${_build_var} "${_build_default}")
    else ()
        set(${_build_var} "${_build_default}" CACHE BOOL
                "Build IRONSmith plugin ${_plugin_name}."
        )
    endif ()

    _ironsmith_eval_condition(_condition_result "${_args_CONDITION}")

    if (_condition_result AND ${_build_var})
        set(_plugin_enabled ON)
    else ()
        set(_plugin_enabled OFF)
    endif ()

    if (NOT _plugin_enabled)
        return()
    endif ()

    set(_plugin_dir "${IRONSMITH_PLUGIN_INSTALL_DIR}")
    if (_args_PLUGIN_PATH)
        set(_plugin_dir "${_args_PLUGIN_PATH}")
    endif()

    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${target_name}.json.in")
        list(APPEND _args_SOURCES "${target_name}.json.in")

        set(_configured_json "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.json")
        configure_file("${target_name}.json.in" "${_configured_json}")

        if (CMAKE_CONFIGURATION_TYPES)
            file(GENERATE
                    OUTPUT "${CMAKE_BINARY_DIR}/${_plugin_dir}/$<CONFIG>/${target_name}.json"
                    INPUT  "${_configured_json}"
            )
        else()
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/${_plugin_dir}")
            file(COPY_FILE
                    "${_configured_json}"
                    "${CMAKE_BINARY_DIR}/${_plugin_dir}/${target_name}.json"
                    ONLY_IF_DIFFERENT
            )
        endif()
    endif ()

    if (DEFINED IRONSMITH_STATIC_BUILD AND IRONSMITH_STATIC_BUILD)
        set(_lib_type STATIC)
    else ()
        set(_lib_type SHARED)
    endif ()

    add_library(${target_name} ${_lib_type})
    add_library(IRONSmith::${target_name} ALIAS ${target_name})

    get_filename_component(_public_build_interface_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    file(RELATIVE_PATH _include_dir_relative_path "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")

    if (NOT DEFINED IRONSMITH_HEADER_INSTALL_PATH)
        set(IRONSMITH_HEADER_INSTALL_PATH "include")
    endif()

    target_include_directories(${target_name}
            PRIVATE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
            PUBLIC
            "$<BUILD_INTERFACE:${_public_build_interface_dir}>"
            "$<INSTALL_INTERFACE:${IRONSMITH_HEADER_INSTALL_PATH}/${_include_dir_relative_path}>"
    )

    set_target_properties(${target_name} PROPERTIES
            IRONSMITH_PLUGIN_NAME       "${_plugin_name}"
            IRONSMITH_PLUGIN_CLASS_NAME "${_plugin_class}"
            IRONSMITH_PLUGIN_VERSION    "${_plugin_version}"
    )

    ironsmith_extend_target(${target_name}
            PRIVATE_DEPENDS         ${_args_PRIVATE_DEPENDS}
            PUBLIC_DEPENDS          ${_args_PUBLIC_DEPENDS}
            PRIVATE_DEFINES         ${_args_PRIVATE_DEFINES}
            PUBLIC_DEFINES          ${_args_PUBLIC_DEFINES}
            PRIVATE_INCLUDES        ${_args_PRIVATE_INCLUDES}
            PUBLIC_INCLUDES         ${_args_PUBLIC_INCLUDES}
            PRIVATE_SYSTEM_INCLUDES ${_args_PRIVATE_SYSTEM_INCLUDES}
            PUBLIC_SYSTEM_INCLUDES  ${_args_PUBLIC_SYSTEM_INCLUDES}
            SOURCES                 ${_args_SOURCES}
            EXPLICIT_MOC            ${_args_EXPLICIT_MOC}
            SKIP_AUTOMOC            ${_args_SKIP_AUTOMOC}
            PRIVATE_COMPILE_OPTIONS ${_args_PRIVATE_COMPILE_OPTIONS}
            PUBLIC_COMPILE_OPTIONS  ${_args_PUBLIC_COMPILE_OPTIONS}
    )

    if (CMAKE_CONFIGURATION_TYPES)
        set(_out_dir "${CMAKE_BINARY_DIR}/${_plugin_dir}/$<CONFIG>")
    else()
        set(_out_dir "${CMAKE_BINARY_DIR}/${_plugin_dir}")
    endif()

    set_target_properties(${target_name} PROPERTIES
            OUTPUT_NAME              "${_plugin_name}"
            RUNTIME_OUTPUT_DIRECTORY "${_out_dir}"
            LIBRARY_OUTPUT_DIRECTORY "${_out_dir}"
            ARCHIVE_OUTPUT_DIRECTORY "${_out_dir}"
            CXX_EXTENSIONS           OFF
    )

    set_property(GLOBAL APPEND PROPERTY IRONSMITH_PLUGINS "${target_name}")

    if (NOT _args_SKIP_INSTALL AND NOT (DEFINED IRONSMITH_STATIC_BUILD AND IRONSMITH_STATIC_BUILD))
        install(TARGETS ${target_name}
                RUNTIME DESTINATION "${_plugin_dir}" OPTIONAL
                LIBRARY DESTINATION "${_plugin_dir}" OPTIONAL
                ARCHIVE DESTINATION "${_plugin_dir}" OPTIONAL
        )
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${target_name}.json.in")
            install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.json"
                    DESTINATION "${_plugin_dir}")
        endif()
    endif()
endfunction(ironsmith_add_plugin)

# ironsmith_add_library(name
#   [STATIC] [SHARED] [OBJECT]
#   [DESTINATION dir]
#   [BUILD_DEFAULT ON|OFF]
#   [CONDITION expr...]
#   [PRIVATE_DEPENDS ...]
#   [PUBLIC_DEPENDS ...]
#   [PRIVATE_DEFINES ...]
#   [PUBLIC_DEFINES ...]
#   [PRIVATE_INCLUDES ...]
#   [PRIVATE_SYSTEM_INCLUDES ...]
#   [PUBLIC_INCLUDES ...]
#   [PUBLIC_SYSTEM_INCLUDES ...]
#   [SOURCES ...]
#   [EXPLICIT_MOC ...]
#   [SKIP_AUTOMOC ...]
#   [PRIVATE_COMPILE_OPTIONS ...]
#   [PUBLIC_COMPILE_OPTIONS ...]
# )
function(ironsmith_add_library target_name)
    set(_opt_args
            STATIC
            SHARED
            OBJECT
            SKIP_PCH       # parsed but unused for now
    )
    set(_single_args
            DESTINATION
            BUILD_DEFAULT
    )
    set(_multi_args
            CONDITION
            PRIVATE_DEPENDS
            PUBLIC_DEPENDS
            PRIVATE_DEFINES
            PUBLIC_DEFINES
            PRIVATE_SYSTEM_INCLUDES
            PUBLIC_SYSTEM_INCLUDES
            PRIVATE_INCLUDES
            PUBLIC_INCLUDES
            SOURCES
            EXPLICIT_MOC
            SKIP_AUTOMOC
            PRIVATE_COMPILE_OPTIONS
            PUBLIC_COMPILE_OPTIONS
    )

    cmake_parse_arguments(_arg "${_opt_args}" "${_single_args}" "${_multi_args}" ${ARGN})

    if (_arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "ironsmith_add_library had unparsed arguments: ${_arg_UNPARSED_ARGUMENTS}"
        )
    endif()

    if (NOT DEFINED IRONSMITH_BUILD_LIBRARIES_BY_DEFAULT)
        set(IRONSMITH_BUILD_LIBRARIES_BY_DEFAULT ON CACHE BOOL
                "Build IRONSmith libraries by default."
        )
    endif()

    string(TOUPPER "${target_name}" _name_upper)
    string(REGEX REPLACE "[^A-Z0-9_]" "_" _name_upper_sanitized "${_name_upper}")
    set(_build_var "IRONSMITH_BUILD_LIBRARY_${_name_upper_sanitized}")

    if (DEFINED _arg_BUILD_DEFAULT)
        set(_build_default "${_arg_BUILD_DEFAULT}")
    else()
        set(_build_default "${IRONSMITH_BUILD_LIBRARIES_BY_DEFAULT}")
    endif()

    if (DEFINED ENV{${_build_var}})
        set(_build_default "$ENV{${_build_var}}")
    endif()

    set(${_build_var} "${_build_default}" CACHE BOOL
            "Build IRONSmith library ${target_name}."
    )

    _ironsmith_eval_condition(_condition_result "${_arg_CONDITION}")

    if (_condition_result AND ${_build_var})
        set(_lib_enabled ON)
    else()
        set(_lib_enabled OFF)
    endif()

    if (NOT _lib_enabled)
        return()
    endif()

    if (_arg_OBJECT)
        set(_lib_type OBJECT)
    else()
        if (_arg_STATIC OR (DEFINED IRONSMITH_STATIC_BUILD AND IRONSMITH_STATIC_BUILD AND NOT _arg_SHARED))
            set(_lib_type STATIC)
        else()
            set(_lib_type SHARED)
        endif()
    endif()

    add_library(${target_name} ${_lib_type})
    add_library(IRONSmith::${target_name} ALIAS ${target_name})

    get_filename_component(_public_build_interface_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    file(RELATIVE_PATH _include_dir_relative_path "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")

    if (NOT DEFINED IRONSMITH_HEADER_INSTALL_PATH)
        set(IRONSMITH_HEADER_INSTALL_PATH "include")
    endif()

    target_include_directories(${target_name}
            PRIVATE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
            PUBLIC
            "$<BUILD_INTERFACE:${_public_build_interface_dir}>"
            "$<INSTALL_INTERFACE:${IRONSMITH_HEADER_INSTALL_PATH}/${_include_dir_relative_path}>"
    )

    ironsmith_extend_target(${target_name}
            PRIVATE_DEPENDS         ${_arg_PRIVATE_DEPENDS}
            PUBLIC_DEPENDS          ${_arg_PUBLIC_DEPENDS}
            PRIVATE_DEFINES         ${_arg_PRIVATE_DEFINES}
            PUBLIC_DEFINES          ${_arg_PUBLIC_DEFINES}
            PRIVATE_INCLUDES        ${_arg_PRIVATE_INCLUDES}
            PRIVATE_SYSTEM_INCLUDES ${_arg_PRIVATE_SYSTEM_INCLUDES}
            PUBLIC_INCLUDES         ${_arg_PUBLIC_INCLUDES}
            PUBLIC_SYSTEM_INCLUDES  ${_arg_PUBLIC_SYSTEM_INCLUDES}
            SOURCES                 ${_arg_SOURCES}
            EXPLICIT_MOC            ${_arg_EXPLICIT_MOC}
            SKIP_AUTOMOC            ${_arg_SKIP_AUTOMOC}
            PRIVATE_COMPILE_OPTIONS ${_arg_PRIVATE_COMPILE_OPTIONS}
            PUBLIC_COMPILE_OPTIONS  ${_arg_PUBLIC_COMPILE_OPTIONS}
    )

    if (DEFINED IRONSMITH_LIBRARY_INSTALL_DIR)
        set(_DESTINATION "${IRONSMITH_LIBRARY_INSTALL_DIR}")
    else()
        set(_DESTINATION "lib")
    endif()

    if (_arg_DESTINATION)
        set(_DESTINATION "${_arg_DESTINATION}")
    endif()

    if (CMAKE_CONFIGURATION_TYPES)
        set(_out_dir "${CMAKE_BINARY_DIR}/${_DESTINATION}/$<CONFIG>")
    else()
        set(_out_dir "${CMAKE_BINARY_DIR}/${_DESTINATION}")
    endif()

    set_target_properties(${target_name} PROPERTIES
            OUTPUT_NAME    "${target_name}"
            CXX_EXTENSIONS OFF
    )

    if (NOT _lib_type STREQUAL "OBJECT")
        set_target_properties(${target_name} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${_out_dir}"
                LIBRARY_OUTPUT_DIRECTORY "${_out_dir}"
                ARCHIVE_OUTPUT_DIRECTORY "${_out_dir}"
        )

        # On Windows the DLL (RUNTIME) must live alongside the executable so the
        # loader finds it at startup. Import libs (ARCHIVE) stay in lib/ironsmith/.
        if(WIN32)
            install(TARGETS ${target_name}
                    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL
                    LIBRARY DESTINATION "${_DESTINATION}" OPTIONAL
                    ARCHIVE DESTINATION "${_DESTINATION}" OPTIONAL
            )
        else()
            install(TARGETS ${target_name}
                    RUNTIME DESTINATION "${_DESTINATION}" OPTIONAL
                    LIBRARY DESTINATION "${_DESTINATION}" OPTIONAL
                    ARCHIVE DESTINATION "${_DESTINATION}" OPTIONAL
            )
        endif()
    endif()

    set_property(GLOBAL APPEND PROPERTY IRONSMITH_LIBRARIES "${target_name}")
endfunction()