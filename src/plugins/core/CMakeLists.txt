# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

ironsmith_add_plugin(CorePlugin
        PLUGIN_NAME Core
        PLUGIN_CLASS Core::CorePlugin

        SOURCES
        #-== Core Plugin Definition ==-#
            CorePlugin.hpp
            CorePlugin.cpp
            Core.json
            ICore.hpp
            CoreImpl.cpp
            CoreImpl.hpp
            CoreConstants.hpp
            CoreGlobal.hpp

        #-== Globar Menu Bar ==-#
            GlobalMenuBar.hpp
            GlobalMenuBar.cpp

        #-== Command Ribbon ==-#
            CommandRibbon.hpp
            CommandRibbon.cpp

        #-== Status Bar ==-#
            StatusBarField.hpp
            StatusBarField.cpp
            HeaderInfoService.hpp
            HeaderInfoService.cpp

        #-== Side Bar ==-#
            SidebarModel.hpp
            SidebarModel.cpp
            SidebarRegistryImpl.hpp
            SidebarRegistryImpl.cpp
            state/CoreUiState.hpp
            state/CoreUiState.cpp

        PUBLIC_DEPENDS
            ${QT6_REQUIRED_COMPONENTS}
            ExtensionSystem
            Utils
)

add_subdirectory(ui)
add_subdirectory(widgets)
add_subdirectory(api)

if (BUILD_WITH_TESTS)
    add_subdirectory(tests)
endif ()
