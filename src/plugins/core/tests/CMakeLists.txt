# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

include(IRONSmithFunctions)
ironsmith_define_tests(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        LINK_LIBS
            IRONSmith::CorePlugin
        TEST SidebarModelTest
            SOURCES
                SidebarModelTests.cpp
                CoreUiStateTests.cpp
                CommandRibbonTests.cpp
        TEST NewTestGroup
            SOURCES


)
