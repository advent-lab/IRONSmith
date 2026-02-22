# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

# Provides:
#   QScintilla_FOUND
#   QScintilla_INCLUDE_DIR
#   QScintilla_LIBRARY
#   Target: QScintilla::QScintilla

include(FindPackageHandleStandardArgs)

find_package(QScintilla CONFIG QUIET)

if (QScintilla_FOUND)
    if (TARGET QScintilla::QScintilla)
        return()
    endif()

    if (DEFINED QScintilla_INCLUDE_DIRS AND NOT DEFINED QScintilla_INCLUDE_DIR)
        set(QScintilla_INCLUDE_DIR "${QScintilla_INCLUDE_DIRS}")
    endif()
    if (DEFINED QScintilla_LIBRARIES AND NOT DEFINED QScintilla_LIBRARY)
        list(GET QScintilla_LIBRARIES 0 QScintilla_LIBRARY)
    endif()
endif()

set(_QSCI_PREFIX_HINTS)

if (DEFINED QSCINTILLA_ROOT)
    list(APPEND _QSCI_PREFIX_HINTS "${QSCINTILLA_ROOT}")
endif()

if (CMAKE_PREFIX_PATH)
    list(APPEND _QSCI_PREFIX_HINTS ${CMAKE_PREFIX_PATH})
endif()

list(APPEND _QSCI_PREFIX_HINTS
        /usr
        /usr/local
        /opt/local
)

if (WIN32)
    list(APPEND _QSCI_PREFIX_HINTS
            "C:/msys64/mingw64"
            "C:/msys64/ucrt64"
            "C:/msys64/mingw32"
    )

    if (DEFINED ENV{VCPKG_INSTALLATION_ROOT})
        list(APPEND _QSCI_PREFIX_HINTS "$ENV{VCPKG_INSTALLATION_ROOT}/installed")
    endif()
endif()

if (APPLE)
    list(APPEND _QSCI_PREFIX_HINTS
            /opt/homebrew
            /usr/local
            /opt/homebrew/opt/qscintilla2
            /usr/local/opt/qscintilla2
    )

    execute_process(
            COMMAND brew --prefix qscintilla2
            OUTPUT_VARIABLE _QSCI_BREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
    if (_QSCI_BREW_PREFIX)
        list(APPEND _QSCI_PREFIX_HINTS "${_QSCI_BREW_PREFIX}")
    endif()
endif()

list(REMOVE_DUPLICATES _QSCI_PREFIX_HINTS)

find_path(QScintilla_INCLUDE_DIR
        NAMES Qsci/qsciscintilla.h
        HINTS ${_QSCI_PREFIX_HINTS}
        PATH_SUFFIXES include
)

find_library(QScintilla_LIBRARY
        NAMES qscintilla2_qt6 qscintilla2_qt5 qscintilla2
        HINTS ${_QSCI_PREFIX_HINTS}
        PATH_SUFFIXES lib lib64
)

find_package_handle_standard_args(QScintilla
        REQUIRED_VARS QScintilla_INCLUDE_DIR QScintilla_LIBRARY
)

if (QScintilla_FOUND AND NOT TARGET QScintilla::QScintilla)
    add_library(QScintilla::QScintilla UNKNOWN IMPORTED)
    set_target_properties(QScintilla::QScintilla PROPERTIES
            IMPORTED_LOCATION "${QScintilla_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${QScintilla_INCLUDE_DIR}"
    )
endif()