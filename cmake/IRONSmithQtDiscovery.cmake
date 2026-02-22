include_guard(GLOBAL)

option(
    IRONSMITH_AUTO_SET_QT6_DIR
    "Auto-detect Qt6_DIR when it was not provided by the user."
    ON
)

function(_ironsmith_try_set_qt6_dir _candidate _reason)
    if (NOT _candidate)
        return()
    endif()

    if (EXISTS "${_candidate}/Qt6Config.cmake")
        set(Qt6_DIR "${_candidate}" CACHE PATH "Directory containing Qt6Config.cmake" FORCE)
        message(STATUS "IRONSmith: Qt6_DIR='${Qt6_DIR}' (${_reason})")
    endif()
endfunction()

function(_ironsmith_append_if_exists _list_var _path)
    if (EXISTS "${_path}")
        list(APPEND ${_list_var} "${_path}")
        set(${_list_var} "${${_list_var}}" PARENT_SCOPE)
    endif()
endfunction()

function(ironsmith_configure_qt6_discovery)
    if (DEFINED Qt6_DIR AND EXISTS "${Qt6_DIR}/Qt6Config.cmake")
        message(STATUS "IRONSmith: Qt6_DIR already set to '${Qt6_DIR}'.")
        return()
    endif()

    if (NOT IRONSMITH_AUTO_SET_QT6_DIR)
        return()
    endif()

    foreach(_env_name IN ITEMS Qt6_DIR QTDIR Qt6_ROOT QT_ROOT_DIR)
        if (DEFINED ENV{${_env_name}})
            set(_env_value "$ENV{${_env_name}}")
            _ironsmith_try_set_qt6_dir("${_env_value}" "from environment variable ${_env_name}")
            if (DEFINED Qt6_DIR)
                return()
            endif()

            _ironsmith_try_set_qt6_dir("${_env_value}/lib/cmake/Qt6" "from environment variable ${_env_name}")
            if (DEFINED Qt6_DIR)
                return()
            endif()

            _ironsmith_try_set_qt6_dir("${_env_value}/lib64/cmake/Qt6" "from environment variable ${_env_name}")
            if (DEFINED Qt6_DIR)
                return()
            endif()
        endif()
    endforeach()

    if (CMAKE_PREFIX_PATH)
        foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
            _ironsmith_try_set_qt6_dir("${_prefix}" "from CMAKE_PREFIX_PATH")
            if (DEFINED Qt6_DIR)
                return()
            endif()

            _ironsmith_try_set_qt6_dir("${_prefix}/lib/cmake/Qt6" "from CMAKE_PREFIX_PATH")
            if (DEFINED Qt6_DIR)
                return()
            endif()

            _ironsmith_try_set_qt6_dir("${_prefix}/lib64/cmake/Qt6" "from CMAKE_PREFIX_PATH")
            if (DEFINED Qt6_DIR)
                return()
            endif()
        endforeach()
    endif()

    set(_qt_roots)
    if (APPLE)
        find_program(_IRONSMITH_BREW_EXECUTABLE brew)
        if (_IRONSMITH_BREW_EXECUTABLE)
            foreach(_brew_qt_formula IN ITEMS qt@6 qt qtbase)
                execute_process(
                    COMMAND "${_IRONSMITH_BREW_EXECUTABLE}" --prefix "${_brew_qt_formula}"
                    OUTPUT_VARIABLE _brew_qt_prefix
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                    RESULT_VARIABLE _brew_qt_result
                )
                if (_brew_qt_result EQUAL 0 AND EXISTS "${_brew_qt_prefix}")
                    list(APPEND _qt_roots "${_brew_qt_prefix}")
                endif()
            endforeach()
        endif()

        list(APPEND _qt_roots
            "/opt/homebrew/opt/qt@6"
            "/opt/homebrew/opt/qt"
            "/opt/homebrew/opt/qtbase"
            "/usr/local/opt/qt@6"
            "/usr/local/opt/qt"
            "/usr/local/opt/qtbase"
            "/opt/local/libexec/qt6"
            "/Applications/Qt"
        )
    elseif(WIN32)
        list(APPEND _qt_roots
            "C:/Qt"
            "C:/vcpkg/installed"
            "C:/tools/vcpkg/installed"
        )
        if (DEFINED ENV{USERPROFILE})
            list(APPEND _qt_roots "$ENV{USERPROFILE}/Qt")
        endif()
        if (DEFINED ENV{VCPKG_ROOT})
            list(APPEND _qt_roots "$ENV{VCPKG_ROOT}/installed")
        endif()
    else()
        list(APPEND _qt_roots
            "/usr/lib/qt6"
            "/usr/lib/x86_64-linux-gnu"
            "/usr/local/Qt"
            "/opt/Qt"
            "/usr/local/lib/qt6"
            "/opt/homebrew/opt/qt@6"
            "/opt/homebrew/opt/qt"
            "/opt/homebrew/opt/qtbase"
        )
    endif()

    if (DEFINED ENV{HOME})
        list(APPEND _qt_roots "$ENV{HOME}/Qt")
    endif()

    list(REMOVE_DUPLICATES _qt_roots)

    set(_qt6_candidates)
    foreach(_root IN LISTS _qt_roots)
        _ironsmith_append_if_exists(_qt6_candidates "${_root}/lib/cmake/Qt6")
        _ironsmith_append_if_exists(_qt6_candidates "${_root}/lib64/cmake/Qt6")

        file(
            GLOB _versioned_qt6_configs
            "${_root}/*/*/lib/cmake/Qt6/Qt6Config.cmake"
            "${_root}/*/*/lib64/cmake/Qt6/Qt6Config.cmake"
        )

        foreach(_qt6_config IN LISTS _versioned_qt6_configs)
            get_filename_component(_qt6_dir "${_qt6_config}" DIRECTORY)
            list(APPEND _qt6_candidates "${_qt6_dir}")
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES _qt6_candidates)

    set(_desktop_candidates)
    set(_fallback_candidates)
    foreach(_candidate IN LISTS _qt6_candidates)
        if (NOT EXISTS "${_candidate}/Qt6Config.cmake")
            continue()
        endif()

        if (_candidate MATCHES "wasm")
            list(APPEND _fallback_candidates "${_candidate}")
        else()
            list(APPEND _desktop_candidates "${_candidate}")
        endif()
    endforeach()

    if (_desktop_candidates)
        list(GET _desktop_candidates 0 _selected_candidate)
        set(Qt6_DIR "${_selected_candidate}" CACHE PATH "Directory containing Qt6Config.cmake" FORCE)
        message(STATUS "IRONSmith: Auto-detected Qt6_DIR='${Qt6_DIR}'.")
        return()
    endif()

    if (_fallback_candidates)
        list(GET _fallback_candidates 0 _selected_candidate)
        set(Qt6_DIR "${_selected_candidate}" CACHE PATH "Directory containing Qt6Config.cmake" FORCE)
        message(WARNING
            "IRONSmith: Auto-detected only a WebAssembly Qt kit at '${Qt6_DIR}'. "
            "Set -DQt6_DIR=<desktop kit>/lib/cmake/Qt6 if this is a desktop build."
        )
        return()
    endif()

    message(STATUS
        "IRONSmith: Could not auto-detect Qt6_DIR. "
        "Configure with -DQt6_DIR=<path>/lib/cmake/Qt6 or set CMAKE_PREFIX_PATH."
    )
endfunction()
