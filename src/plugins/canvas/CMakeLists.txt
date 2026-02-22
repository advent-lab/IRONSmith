# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

ironsmith_add_plugin(CanvasPlugin
        PLUGIN_NAME Canvas
        PLUGIN_CLASS Canvas::CanvasPlugin

        SOURCES
        #-== Core Canvas ==-#
            CanvasGlobal.hpp
            CanvasConstants.hpp
            CanvasTypes.hpp
            CanvasPorts.hpp
            CanvasFabric.hpp
            CanvasFabric.cpp
            CanvasRenderContext.hpp
            CanvasItem.hpp
            CanvasCommand.hpp
            CanvasCommandManager.hpp
            CanvasCommandManager.cpp
            CanvasCommands.hpp
            CanvasCommands.cpp
            CanvasStyle.hpp
            CanvasStyle.cpp
            CanvasBlock.hpp
            CanvasBlock.cpp
            CanvasInteractionTypes.hpp
            CanvasSelectionModel.hpp
            CanvasSelectionModel.cpp
            CanvasScene.hpp
            CanvasScene.cpp
            CanvasViewport.hpp
            CanvasViewport.cpp
            CanvasBlockContent.hpp
            CanvasBlockContent.cpp
            CanvasSymbolContent.hpp
            CanvasSymbolContent.cpp
            CanvasWire.hpp
            CanvasWire.cpp
            CanvasPlugin.cpp
            CanvasDocument.hpp
            CanvasDocument.cpp
            CanvasView.hpp
            CanvasView.cpp
            CanvasController.hpp
            CanvasController.cpp
            utils/CanvasGeometry.hpp
            utils/CanvasAutoPorts.hpp
            utils/CanvasAutoPorts.cpp
            utils/CanvasPortHitTest.hpp
            utils/CanvasPortHitTest.cpp
            utils/CanvasLinkHubStyle.hpp
            utils/CanvasLinkHubStyle.cpp
            utils/CanvasLinkWireStyle.hpp
            utils/CanvasLinkWireStyle.cpp
            utils/CanvasLinkingMode.hpp
            utils/CanvasLinkingMode.cpp
            utils/CanvasRenderContextBuilder.hpp
            utils/CanvasRenderContextBuilder.cpp
            utils/CanvasPortUsage.hpp
            utils/CanvasPortUsage.cpp
            utils/CanvasPortBindings.hpp
            utils/CanvasPortBindings.cpp
            services/CanvasHitTestService.hpp
            services/CanvasHitTestService.cpp
            services/CanvasGeometryService.hpp
            services/CanvasGeometryService.cpp
            services/CanvasLayoutEngine.hpp
            services/CanvasLayoutEngine.cpp
            controllers/CanvasInteractionHelpers.hpp
            controllers/CanvasInteractionHelpers.cpp
            controllers/CanvasSelectionController.hpp
            controllers/CanvasSelectionController.cpp
            controllers/CanvasContextMenuController.hpp
            controllers/CanvasContextMenuController.cpp
            controllers/CanvasDragController.hpp
            controllers/CanvasDragController.cpp
            controllers/CanvasLinkingController.hpp
            controllers/CanvasLinkingController.cpp

        #-== Canvas Tools ==-#
            Tools.hpp
            Tools.cpp

        #-== Public APIs ==-#
            api/ICanvasHost.hpp
            api/CanvasDocumentTypes.hpp
            api/ICanvasDocumentService.hpp
            api/CanvasGridTypes.hpp
            api/ICanvasBlockHandle.hpp
            api/ICanvasGridHost.hpp

        #-== Internal Interface Implementations ==-#
            internal/CanvasHostImpl.hpp
            internal/CanvasHostImpl.cpp
            internal/CanvasBlockHandleImpl.hpp
            internal/CanvasBlockHandleImpl.cpp
            internal/CanvasGridHostImpl.hpp
            internal/CanvasGridHostImpl.cpp
            internal/CanvasStyleHostImpl.hpp
            internal/CanvasStyleHostImpl.cpp
            api/CanvasStyleTypes.hpp
            api/ICanvasStyleHost.hpp
            internal/CanvasWireRouting.hpp
            internal/CanvasWireRouting.cpp
            document/CanvasDocumentJsonSerializer.hpp
            document/CanvasDocumentJsonSerializer.cpp
            document/CanvasDocumentServiceImpl.hpp
            document/CanvasDocumentServiceImpl.cpp

        PUBLIC_DEPENDS
            ${QT6_REQUIRED_COMPONENTS}
            IRONSmith::Utils

        PRIVATE_DEPENDS
            IRONSmith::ExtensionSystem
            IRONSmith::CorePlugin
)

if (BUILD_WITH_TESTS)
	add_subdirectory(tests)
endif()
