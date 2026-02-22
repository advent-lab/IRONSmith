# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

ironsmith_add_plugin(AiePlugin
        PLUGIN_NAME AiePlugin
        PLUGIN_CLASS Aie::Internal::AiePlugin

        SOURCES
            AiePlugin.cpp
            AieGlobal.hpp
            AieConstants.hpp
            AieStyleCatalog.hpp
            AieStyleCatalog.cpp
            AieService.hpp
            AieService.cpp
            AiePlugin.json
            AieDeviceTopologies.json
            AiePluginResources.qrc
            NpuProfile.hpp
            NpuProfileLoader.hpp
            NpuProfileLoader.cpp
            NpuProfileCanvasMapper.hpp
            NpuProfileCanvasMapper.cpp
            AieCanvasCoordinator.hpp
            AieCanvasCoordinator.cpp
            api/IAieHost.hpp
            state/AiePanelState.hpp
            state/AiePanelState.cpp
            state/AieSidebarState.hpp
            state/AieSidebarState.cpp
            state/AieWorkspaceState.hpp
            state/AieWorkspaceState.cpp
            state/AieKernelsPanelState.hpp
            state/AieKernelsPanelState.cpp
            state/AieLayoutSettings.hpp
            state/AieLayoutSettings.cpp
            kernels/KernelCatalog.hpp
            kernels/KernelCatalog.cpp
            kernels/KernelRegistryService.hpp
            kernels/KernelRegistryService.cpp
            kernels/KernelAssignmentController.hpp
            kernels/KernelAssignmentController.cpp
            kernels/KernelToolboxController.hpp
            kernels/KernelToolboxController.cpp
            panels/AieToolPanel.hpp
            panels/AieToolPanel.cpp
            panels/AieKernelsPanel.hpp
            panels/AieKernelsPanel.cpp
            panels/AiePropertiesPanel.hpp
            panels/AiePropertiesPanel.cpp
            panels/KernelCreateDialog.hpp
            panels/KernelCreateDialog.cpp
            panels/KernelPreviewDialog.hpp
            panels/KernelPreviewDialog.cpp
            panels/KernelPreviewUtils.hpp
            panels/KernelPreviewUtils.cpp
            panels/AieNewDesignDialog.hpp
            panels/AieNewDesignDialog.cpp
            design/DesignModel.hpp
            design/DesignModel.cpp
            design/DesignBundleLoader.hpp
            design/DesignBundleLoader.cpp
            design/DesignBundleCreator.hpp
            design/DesignBundleCreator.cpp
            design/DesignState.hpp
            design/DesignStateJson.hpp
            design/DesignStateJson.cpp
            design/DesignStateCanvas.hpp
            design/DesignStateCanvas.cpp
            design/CanvasDocumentImporter.hpp
            design/CanvasDocumentImporter.cpp
            design/DesignOpenController.hpp
            design/DesignOpenController.cpp

        PUBLIC_DEPENDS
            ${QT6_REQUIRED_COMPONENTS}
            IRONSmith::Utils

        PRIVATE_DEPENDS
            IRONSmith::ExtensionSystem
            IRONSmith::CorePlugin
            IRONSmith::CanvasPlugin
            IRONSmith::ProjectExplorerPlugin
            IRONSmith::CodeEditorPlugin
)

target_compile_definitions(AiePlugin PRIVATE
        IRONSMITH_BUILTIN_KERNELS_DIR="${PROJECT_SOURCE_DIR}/resources/kernels"
)

if (BUILD_WITH_TESTS)
    add_subdirectory(tests)
endif()
