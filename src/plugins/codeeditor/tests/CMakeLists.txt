# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

include(IRONSmithFunctions)

ironsmith_define_tests(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        LINK_LIBS
            IRONSmith::CodeEditorPlugin
            ${QT6_REQUIRED_COMPONENTS}
            Qt6::Test
        TEST CodeEditorTests
            SOURCES
                CodeEditorServiceTests.cpp
)
