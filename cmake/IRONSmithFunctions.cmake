# Copyright (c) Meta Platforms, Inc. and affiliates.
# Copyright (c) 2025 Samer Ali
#
# This file is based on code from the Folly project and has been
# modified for a university capstone project.
#
# Original source:
#   https://github.com/facebook/folly/blob/main/CMake/FollyFunctions.cmake
#
# Modifications by:
#   Samer Ali
#   ASU School of Computing and Augmented Intelligence
#   swali@asu.edu
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

function(ironsmith_define_tests)
    set(directory_count 0)
    set(test_count 0)
    set(currentArg 0)

    while (currentArg LESS ${ARGC})
        if ("x${ARGV${currentArg}}" STREQUAL "xDIRECTORY")
            math(EXPR currentArg "${currentArg} + 1")
            if (NOT currentArg LESS ${ARGC})
                message(FATAL_ERROR "Expected base directory!")
            endif()

            set(cur_dir ${directory_count})
            math(EXPR directory_count "${directory_count} + 1")
            set(directory_${cur_dir}_name "${ARGV${currentArg}}")
            # We need a single list of sources to get source_group to work nicely.
            set(directory_${cur_dir}_source_list)

            math(EXPR currentArg "${currentArg} + 1")

            while (currentArg LESS ${ARGC})
                if ("x${ARGV${currentArg}}" STREQUAL "xDIRECTORY")
                    break()
                elseif ("x${ARGV${currentArg}}" STREQUAL "xLINK_LIBS")
                    math(EXPR currentArg "${currentArg} + 1")
                    if (NOT currentArg LESS ${ARGC})
                        message(FATAL_ERROR "Expected library targets to link tests against!")
                    endif ()
                    while (currentArg LESS ${ARGC} AND NOT
                            "x${ARGV${currentArg}}" STREQUAL "xTEST" OR
                            "x${ARGV${currentArg}}" STREQUAL "xBENCHMARK")
                        list(APPEND test_link_dep_libs ${ARGV${currentArg}})
                        math(EXPR currentArg "${currentArg} + 1")
                    endwhile ()
                elseif ("x${ARGV${currentArg}}" STREQUAL "xTEST" OR
                        "x${ARGV${currentArg}}" STREQUAL "xBENCHMARK")
                    set(cur_test ${test_count})
                    math(EXPR test_count "${test_count} + 1")

                    set(test_${cur_test}_is_benchmark $<STREQUAL:"x${ARGV${currentArg}}","xBENCHMARK">)

                    math(EXPR currentArg "${currentArg} + 1")
                    if (NOT currentArg LESS ${ARGC})
                        message(FATAL_ERROR "Expected test name!")
                    endif()

                    set(test_${cur_test}_name "${ARGV${currentArg}}")
                    math(EXPR currentArg "${currentArg} + 1")
                    set(test_${cur_test}_directory ${cur_dir})
                    set(test_${cur_test}_content_dir)
                    set(test_${cur_test}_headers)
                    set(test_${cur_test}_sources)
                    set(test_${cur_test}_tag)

                    set(argumentState 0)
                    while (currentArg LESS ${ARGC})
                        if ("x${ARGV${currentArg}}" STREQUAL "xHEADERS")
                            set(argumentState 1)
                        elseif ("x${ARGV${currentArg}}" STREQUAL "xSOURCES")
                            set(argumentState 2)
                        elseif ("x${ARGV${currentArg}}" STREQUAL "xCONTENT_DIR")
                            math(EXPR currentArg "${currentArg} + 1")
                            if (NOT currentArg LESS ${ARGC})
                                message(FATAL_ERROR "Expected content directory name!")
                            endif()
                            set(test_${cur_test}_content_dir "${ARGV${currentArg}}")
                        elseif ("x${ARGV${currentArg}}" STREQUAL "xTEST" OR
                                "x${ARGV${currentArg}}" STREQUAL "xBENCHMARK" OR
                                "x${ARGV${currentArg}}" STREQUAL "xDIRECTORY")
                            break()
                        elseif (argumentState EQUAL 0)
                            if ("x${ARGV${currentArg}}" STREQUAL "xBROKEN")
                                list(APPEND test_${cur_test}_tag "BROKEN")
                            elseif ("x${ARGV${currentArg}}" STREQUAL "xHANGING")
                                list(APPEND test_${cur_test}_tag "HANGING")
                            elseif ("x${ARGV${currentArg}}" STREQUAL "xSLOW")
                                list(APPEND test_${cur_test}_tag "SLOW")
                            elseif ("x${ARGV${currentArg}}" STREQUAL "xWINDOWS_DISABLED")
                                list(APPEND test_${cur_test}_tag "WINDOWS_DISABLED")
                            elseif ("x${ARGV${currentArg}}" STREQUAL "xAPPLE_DISABLED")
                                list(APPEND test_${cur_test}_tag "APPLE_DISABLED")
                            else()
                                message(FATAL_ERROR "Unknown test tag '${ARGV${currentArg}}'!")
                            endif()
                        elseif (argumentState EQUAL 1)
                            list(APPEND test_${cur_test}_headers
                                    "${directory_${cur_dir}_name}${ARGV${currentArg}}"
                            )
                        elseif (argumentState EQUAL 2)
                            list(APPEND test_${cur_test}_sources
                                    "${directory_${cur_dir}_name}${ARGV${currentArg}}"
                            )
                        else()
                            message(FATAL_ERROR "Unknown argument state!")
                        endif()
                        math(EXPR currentArg "${currentArg} + 1")
                    endwhile()

                    list(APPEND directory_${cur_dir}_source_list
                            ${test_${cur_test}_sources} ${test_${cur_test}_headers})
                else()
                    message(FATAL_ERROR "Unknown argument inside directory '${ARGV${currentArg}}'!")
                endif()
            endwhile()
        else()
            message(FATAL_ERROR "Unknown argument '${ARGV${currentArg}}'!")
        endif()
    endwhile()

    set(cur_dir 0)
    while (cur_dir LESS directory_count)
        source_group("" FILES ${directory_${cur_dir}_source_list})
        math(EXPR cur_dir "${cur_dir} + 1")
    endwhile()

    set(cur_test 0)
    while (cur_test LESS test_count)
        set(cur_test_name ${test_${cur_test}_name})
        set(cur_dir_name ${directory_${test_${cur_test}_directory}_name})
        if ("BROKEN" IN_LIST test_${cur_test}_tag AND NOT IRONSMITH_BUILD_BROKEN_TESTS)
            message("Skipping broken test ${cur_dir_name}${cur_test_name}, enable with IRONSMITH_BUILD_BROKEN_TESTS")
        elseif ("SLOW" IN_LIST test_${cur_test}_tag AND NOT IRONSMITH_BUILD_SLOW_TESTS)
            message("Skipping slow test ${cur_dir_name}${cur_test_name}, enable with IRONSMITH_BUILD_SLOW_TESTS")
        elseif ("HANGING" IN_LIST test_${cur_test}_tag AND NOT IRONSMITH_BUILD_HANGING_TESTS)
            message("Skipping hanging test ${cur_dir_name}${cur_test_name}, enable with IRONSMITH_BUILD_HANGING_TESTS")
        elseif ("WINDOWS_DISABLED" IN_LIST test_${cur_test}_tag AND WIN32 AND NOT IRONSMITH_BUILD_WINDOWS_DISABLED)
            message("Skipping windows disabled test ${cur_dir_name}${cur_test_name}, enable with IRONSMITH_BUILD_WINDOWS_DISABLED")
        elseif ("APPLE_DISABLED" IN_LIST test_${cur_test}_tag AND APPLE AND NOT IRONSMITH_BUILD_APPLE_DISABLED)
            message("Skipping apple disabled test ${cur_dir_name}${cur_test_name}, enable with IRONSMITH_BUILD_APPLE_DISABLED")
        elseif (${test_${cur_test}_is_benchmark} AND NOT IRONSMITH_BUILD_BENCHMARKS)
            message("Skipping benchmark ${cur_dir_name}${cur_test_name}, enable with IRONSMITH_BUILD_BENCHMARKS")
        else()
            add_executable(${cur_test_name}
                    ${test_${cur_test}_headers}
                    ${test_${cur_test}_sources}
            )
            if (NOT ${test_${cur_test}_is_benchmark})
                if (HAVE_CMAKE_GTEST)
                    # If we have CMake's built-in gtest support use it to add each test
                    # function as a separate test.
                    gtest_add_tests(TARGET ${cur_test_name}
                            TEST_PREFIX "${cur_test_name}."
                            TEST_LIST test_cases)
                    set_tests_properties(${test_cases} PROPERTIES TIMEOUT 120)
                    if (WIN32)
                        set_tests_properties(${test_cases} PROPERTIES
                                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/lib/ironsmith/plugins"
                        )
                    endif()
                else()
                    # Otherwise add each test executable as a single test.
                    add_test(
                            NAME ${cur_test_name}
                            COMMAND ${cur_test_name}
                    )
                    set_tests_properties(${cur_test_name} PROPERTIES TIMEOUT 120)
                    if (WIN32)
                        set_tests_properties(${cur_test_name} PROPERTIES
                                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/lib/ironsmith/plugins"
                        )
                    endif()
                endif()
            endif()
            if (NOT "x${test_${cur_test}_content_dir}" STREQUAL "x")
                # Copy the content directory to the output directory tree so that
                # tests can be run easily from Visual Studio without having to change
                # the working directory for each test individually.
                file(
                        COPY "${cur_dir_name}${test_${cur_test}_content_dir}"
                        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/ironsmith/${cur_dir_name}${test_${cur_test}_content_dir}"
                )
                add_custom_command(TARGET ${cur_test_name} POST_BUILD COMMAND
                        ${CMAKE_COMMAND} ARGS -E copy_directory
                        "${cur_dir_name}${test_${cur_test}_content_dir}"
                        "$<TARGET_FILE_DIR:${cur_test_name}>/ironsmith/${cur_dir_name}${test_${cur_test}_content_dir}"
                        COMMENT "Copying test content for ${cur_test_name}" VERBATIM
                )
            endif()
            # Strip the tailing test directory name for the folder name.
            string(REPLACE "test/" "" test_dir_name "${cur_dir_name}")
            set_property(TARGET ${cur_test_name} PROPERTY FOLDER "Tests/${test_dir_name}")
            target_link_libraries(${cur_test_name} PRIVATE ironsmith_test_support "${test_link_dep_libs}")
            # TODO: add `apply_ironsmith_compile_options_to_target()` function
            # apply_ironsmith_compile_options_to_target(${cur_test_name})
        endif()
        math(EXPR cur_test "${cur_test} + 1")
    endwhile()
endfunction()

function(ironsmith_add_gtest)
    set(_single_args
            DIRECTORY
            NAME
            CONTENT_DIR
    )

    set(_multi_args
            SOURCES
            HEADERS
            LINK_LIBS
            TAGS
    )

    cmake_parse_arguments(_args "${_opt_args}" "${_single_args}" "${_multi_args}" ${ARGN})

    if (NOT _args_DIRECTORY OR NOT _args_NAME)
        message(FATAL_ERROR "ironsmith_add_gtest requires DIRECTORY and NAME")
    endif()

    set(_def_args
            DIRECTORY ${_args_DIRECTORY}
            TEST ${_args_NAME}
            ${_args_TAGS}
            HEADERS ${_args_HEADERS}
            SOURCES ${_args_SOURCES}
    )

    if (DEFINED _args_CONTENT_DIR AND NOT "x${_args_CONTENT_DIR}" STREQUAL "x")
        list(APPEND _def_args CONTENT_DIR ${_args_CONTENT_DIR})
    endif()

    ironsmith_define_tests(${_def_args})

    if (_args_LINK_LIBS)
        target_link_libraries(${_args_NAME} PRIVATE ${_args_LINK_LIBS})
    endif()
endfunction()

function(ironsmith_add_test_plugin)
    set(_opt_args "")

    set(_single_args
            NAME
            OUTPUT_DIR)

    set(_mult_args
            SOURCES
            LINK_LIBS)

    cmake_parse_arguments(_args "${_opt_args}" "${_single_args}" "${_multi_args}" ${ARGN})

    if (NOT _args_NAME OR NOT _args_OUTPUT_DIR)
        message(FATAL_ERROR "ironsmith_add_test_plugin requires NAME and OUTPUT_DIR")
    endif ()

    add_library(${_args_NAME} PRIVATE ${_args_LINK_LIBS})

    set_target_properties(${_args_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${_args_OUTPUT_DIR}"
            LIBRARY_OUTPUT_DIRECTORY "${_args_OUTPUT_DIR}")

    if (APPLE)
        set_target_properties(${_args_NAME} PROPERTIES
            BUILD_RPATH "@loader_path")
    elseif (UNIX)
        set_target_properties(${_args_NAME} PROPERTIES
                BUILD_RPATH "$ORIGIN")
    endif ()
endfunction(ironsmith_add_test_plugin)

function(ironsmith_add_plugin_integration_test)
    set(_opt_args "")

    set(_single_args
            DIRECTORY
            NAME
            PLUGIN_DIR)

    set(_mult_args
            SOURCES
            HEADERS
            LINK_LIBS
            PLUGINS)

    cmake_parse_arguments(_args "${_opt_args}" "${_single_args}" "${_multi_args}" ${ARGN})

    if (NOT _args_DIRECTORY OR NOT _args_NAME OR NOT _args_PLUGIN_DIR)
        message(FATAL_ERROR "ironsmith_add_plugin_integration_test requires DIRECTORY, NAME, and PLUGIN_DIR")
    endif ()

    ironsmith_define_tests(
        DIRECTORY ${_args_DIRECTORY}
            TEST ${_args_NAME}
                HEADERS ${_args_HEADERS}
                SOURCES ${_args_SOURCES}
    )

    if (_args_LINK_LIBS)
        target_link_libraries(${_args_NAME} PRIVATE "${_args_LINK_LIBS}")
    endif ()

    if (_args_PLUGINS)
        add_dependencies(${_args_NAME} ${_args_PLUGINS})
    endif ()

    set_tests_properties(${_args_NAME} PROPERTIES
        ENVIRONMENT "IRONSMITH_PLUGIN_DIR=${_args_PLUGIN_DIR}"
    )
endfunction(ironsmith_add_plugin_integration_test)

# HOW TO USE:
######################################################################

# 1) Normal library unit test (no plugins involved)
#    In a library’s CMakeLists.txt, you keep using gtest executables.
#    Example for libs/extensionsystem/tests/CMakeLists.txt:
#
# ironsmith_add_gtest(
#        DIRECTORY libs/extensionsystem/tests/
#        NAME tst_DependencyResolution
#        SOURCES
#            /tst_DependencyResolution.cpp
#        LINK_LIBS
#            ironsmith_extensionsystem
# )
# that gives you one executable `tst_DependencyResolution` and gtest
# registers the individual tests inside it.

# 2) Plugin integration test (build test plugins + load them)
#    Make a test directory for the integration runner, for example:
#
#    libs/extensionsystem/tests/ (runner lives here)
#    and a test plugin sources directory, for example:
#
#    libs/extensionsystem/tests/test_plugins/ (plugin A/B source)
#
#    Then in libs/extensionsystem/tests/CMakeLists.txt:
#    set(TEST_PLUGIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/test_plugins_out")
#
#    ironsmith_add_test_plugin(
#            NAME TestPluginB
#            OUTPUT_DIR "${TEST_PLUGIN_DIR}"
#            SOURCES
#               ${CMAKE_CURRENT_LIST_DIR}/test_plugins/TestPluginB.cpp
#            LINK_LIBS
#               ironsmith_extensionsystem
#    )
#
#    ironsmith_add_test_plugin(
#            NAME TestPluginA
#            OUTPUT_DIR "${TEST_PLUGIN_DIR}"
#            SOURCES
#               ${CMAKE_CURRENT_LIST_DIR}/test_plugins/TestPluginA.cpp
#            LINK_LIBS
#               ironsmith_extensionsystem
#    )
#
#    ironsmith_add_plugin_integration_test(
#            DIRECTORY libs/extensionsystem/tests/
#            NAME tst_ExtensionSystemIntegration
#            SOURCES
#               /tst_ExtensionSystemIntegration.cpp
#            LINK_LIBS
#               ironsmith_extensionsystem
#            PLUGINS
#               TestPluginA;TestPluginB
#            PLUGIN_DIR
#               "${TEST_PLUGIN_DIR}"
#    )
# Then tst_ExtensionSystemIntegration.cpp reads IRONSMITH_PLUGIN_DIR,
# points PluginManager at it, and asserts init/shutdown order.