# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

include(IRONSmithFunctions)
ironsmith_define_tests(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        LINK_LIBS
            IRONSmith::Utils
            Qt::Test
        TEST EnvironmentTests
            SOURCES
                EnvironmentTests.cpp
        TEST UtilsTypesTests
            SOURCES
                StrongIdTests.cpp
                VirtualPathTests.cpp
                PathUtilsTests.cpp
                PathPatternMatcherTests.cpp
                FileSystemUtilsTests.cpp
                GridLayoutTests.cpp
                TreeIndexTests.cpp
                TreeChangeSetTests.cpp
                DocumentBundleTests.cpp
                ContextMenuTests.cpp
)
