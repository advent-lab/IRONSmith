# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

ironsmith_add_plugin(ProjectExplorerPlugin
        PLUGIN_NAME ProjectExplorer
        PLUGIN_CLASS ProjectExplorer::Internal::ProjectExplorerPlugin

        SOURCES
            ProjectExplorerPlugin.cpp
            ProjectExplorerGlobal.hpp
            ProjectExplorerModel.hpp
            ProjectExplorerModel.cpp
            ProjectExplorerDataSource.hpp
            ProjectExplorerDataSource.cpp
            ProjectExplorerIconProvider.hpp
            ProjectExplorerIconProvider.cpp
            ProjectExplorerTreeState.hpp
            ProjectExplorerTreeState.cpp
            state/ProjectExplorerPanelState.hpp
            state/ProjectExplorerPanelState.cpp
            state/ProjectExplorerSidebarState.hpp
            state/ProjectExplorerSidebarState.cpp
            search/ProjectExplorerSearchController.hpp
            search/ProjectExplorerSearchController.cpp
            search/ProjectExplorerSearchMatcher.hpp
            search/ProjectExplorerSearchMatcher.cpp
            search/ProjectExplorerSearchIndex.hpp
            search/ProjectExplorerSearchIndex.cpp
            ProjectExplorerActions.hpp
            ProjectExplorerActions.cpp
            views/ProjectExplorerItemDelegate.hpp
            views/ProjectExplorerItemDelegate.cpp
            metadata/ProjectExplorerMetadataService.hpp
            metadata/ProjectExplorerMetadataService.cpp
            metadata/ProjectExplorerThumbnailService.hpp
            metadata/ProjectExplorerThumbnailService.cpp
            filesystem/ProjectExplorerFileSystemService.hpp
            filesystem/ProjectExplorerFileSystemService.cpp
            filesystem/ProjectExplorerFileSystemController.hpp
            filesystem/ProjectExplorerFileSystemController.cpp
            ProjectExplorerFilterModel.hpp
            ProjectExplorerFilterModel.cpp
            ProjectExplorerService.hpp
            ProjectExplorerService.cpp
            ProjectExplorerPanel.hpp
            ProjectExplorerPanel.cpp
            api/IProjectExplorer.hpp
            api/ProjectExplorerMetaTypes.hpp
            api/ProjectExplorerMetaTypes.cpp
            api/ProjectExplorerTypes.hpp

        PUBLIC_DEPENDS
            IRONSmith::Utils

        PRIVATE_DEPENDS
            ${QT6_REQUIRED_COMPONENTS}
            IRONSmith::ExtensionSystem
            IRONSmith::CorePlugin
)

if (BUILD_WITH_TESTS)
    add_subdirectory(tests)
endif()
