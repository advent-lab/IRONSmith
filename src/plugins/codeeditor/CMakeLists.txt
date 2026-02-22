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

        PUBLIC_DEFINES
            "${_CODEEDITOR_HAVE_QSCI}"
)

if (BUILD_WITH_TESTS)
    add_subdirectory(tests)
endif ()
