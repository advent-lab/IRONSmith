# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

find_package(QScintilla QUIET)

if (QScintilla_FOUND)
    set(IRONSMITH_QScintilla QScintilla::QScintilla)
    set(_CODEEDITOR_HAVE_QSCI "CODEEDITOR_HAVE_QSCI=1")
    message(STATUS "CodeEditorPlugin: QScintilla enabled")
else ()
    set(IRONSMITH_QScintilla)
    set(_CODEEDITOR_HAVE_QSCI "CODEEDITOR_HAVE_QSCI=0")
    message(WARNING "CodeEditorPlugin: QScintilla not found. Building plain-text fallback backend.")
endif ()

ironsmith_add_plugin(CodeEditorPlugin
        PLUGIN_NAME CodeEditorPlugin
        PLUGIN_CLASS CodeEditor::Internal::CodeEditorPlugin

        SOURCES
        #-== CodeEditor Core ==-#
            CodeEditor.json
            CodeEditorGlobal.hpp
            CodeEditorPlugin.cpp
            Constants.hpp
            CodeEditorTextView.hpp
            CodeEditorTextView.cpp
            style/CodeEditorStyleManager.hpp
            style/CodeEditorStyleManager.cpp
            style/CodeEditorStyles.qrc
            state/CodeEditorWorkspaceState.hpp
            state/CodeEditorWorkspaceState.cpp

        #-== CodeEditor Public APIs ==-#
            api/CodeEditorTypes.hpp
            api/ICodeEditorService.hpp

        #-== CodeEditor Internal Implementations ==-#
            internal/CodeEditorServiceImpl.hpp
            internal/CodeEditorServiceImpl.cpp

        #-== CodeEditor UI Panels ==-#
            panels/CodeEditorPanel.hpp
            panels/CodeEditorPanel.cpp

        PRIVATE_DEPENDS
            ${QT6_REQUIRED_COMPONENTS}
            ${IRONSMITH_QScintilla}
            IRONSmith::ExtensionSystem
            IRONSmith::CorePlugin
            IRONSmith::ProjectExplorerPlugin

        PUBLIC_DEPENDS
            IRONSmith::Utils

        PRIVATE_DEFINES
            CODEEDITOR_LIBRARY

        PUBLIC_DEFINES
            "${_CODEEDITOR_HAVE_QSCI}"
            CODEEDITOR_BUILD_SHARED=1
)

if (WIN32 AND QScintilla_FOUND)
    # Derive the runtime DLL name from the import library (lib/libqscintilla2_qt6.dll.a →
    # bin/qscintilla2_qt6.dll) and copy it to IRONSMITH_BUILD_BINDIR at build time.
    get_filename_component(_QSCI_LIB_NAME_WE "${QScintilla_LIBRARY}" NAME_WE)
    string(REGEX REPLACE "^lib" "" _QSCI_DLL_NAME "${_QSCI_LIB_NAME_WE}")
    get_filename_component(_QSCI_LIB_DIR "${QScintilla_LIBRARY}" DIRECTORY)
    find_file(_QSCI_DLL
        NAMES "${_QSCI_DLL_NAME}.dll"
        HINTS "${_QSCI_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
    )
    if (_QSCI_DLL)
        add_custom_command(TARGET CodeEditorPlugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_QSCI_DLL}"
                "${IRONSMITH_BUILD_BINDIR}"
            COMMENT "Copying QScintilla DLL to bin/"
        )
    else()
        message(WARNING "CodeEditorPlugin: QScintilla DLL not found; may fail at runtime.")
    endif()
endif()

if (BUILD_WITH_TESTS)
    add_subdirectory(tests)
endif ()
