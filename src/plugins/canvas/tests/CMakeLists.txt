# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

include(IRONSmithFunctions)

ironsmith_define_tests(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    LINK_LIBS
        IRONSmith::CanvasPlugin
        ${QT6_REQUIRED_COMPONENTS}
    TEST CanvasTests
    SOURCES
        CanvasToolsTests.cpp
        CanvasFabricTests.cpp
        CanvasKeepoutTests.cpp
        CanvasCommandTests.cpp
        CanvasPortTests.cpp
        CanvasDeleteCommandTests.cpp
        CanvasWireCommandTests.cpp
        CanvasWireStyleTests.cpp
        CanvasDocumentServiceTests.cpp
        CanvasGridHostTests.cpp
)
