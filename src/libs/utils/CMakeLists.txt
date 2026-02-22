# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

ironsmith_add_library(Utils
        SOURCES
            Result.hpp
			Macros.hpp
			ScopeGuard.hpp
			UtilsGlobal.hpp
			StrongId.hpp
			TreeIds.hpp
			TreeIndex.hpp
			TreeChangeSet.hpp
            VirtualPath.hpp
            VirtualPath.cpp
            PathUtils.hpp
            PathUtils.cpp
            DocumentBundle.hpp
            DocumentBundle.cpp
            contextmenu/ContextMenu.hpp
            contextmenu/ContextMenu.cpp
            ui/SidebarPanelFrame.hpp
            ui/SidebarPanelFrame.cpp
            ui/ColorSwatchButton.hpp
            ui/ColorSwatchButton.cpp
            ui/LabeledSlider.hpp
            ui/LabeledSlider.cpp
            ui/ConfirmationDialog.hpp
            ui/ConfirmationDialog.cpp
            ui/BaseDialog.hpp
            ui/BaseDialog.cpp
            ui/FormDialog.hpp
            ui/FormDialog.cpp
            ui/GridSpec.hpp
            ui/GridLayout.hpp
            ui/GridLayout.cpp
            async/AsyncTask.hpp
            async/DebouncedInvoker.hpp
            filesystem/RecursiveFileSystemWatcher.hpp
            filesystem/RecursiveFileSystemWatcher.cpp
            filesystem/FileSystemUtils.hpp
            filesystem/FileSystemUtils.cpp
            filesystem/JsonFileUtils.hpp
            filesystem/JsonFileUtils.cpp
            filesystem/PathPatternMatcher.hpp
            filesystem/PathPatternMatcher.cpp

        #-== Environment ==-#
            Environment.hpp
            EnvironmentQtPolicy.hpp
            EnvironmentQtPolicy.cpp

        PUBLIC_DEPENDS
			${QT6_REQUIRED_COMPONENTS}
            Qt6::Core # cmake has to be aware of these targets before request to link (i.e., `find_package(...)`)
            Qt6::Widgets
)

if (BUILD_WITH_TESTS)
    add_subdirectory(tests)
endif ()
