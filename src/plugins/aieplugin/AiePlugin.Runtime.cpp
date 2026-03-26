// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#if !defined(AIEPLUGIN_INTERNAL_IMPL)
#error "AiePlugin.Runtime.cpp is an internal implementation fragment. Include from AiePlugin.cpp."
#endif

#include "aieplugin/AieConstants.hpp"
#include "aieplugin/AieService.hpp"
#include "aieplugin/AieStyleCatalog.hpp"
#include "aieplugin/design/CanvasDocumentImporter.hpp"
#include "aieplugin/design/DesignBundleLoader.hpp"
#include "aieplugin/design/DesignOpenController.hpp"
#include "aieplugin/kernels/KernelAssignmentController.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"
#include "aieplugin/kernels/KernelToolboxController.hpp"
#include "aieplugin/state/AiePanelState.hpp"

#include "canvas/api/ICanvasDocumentService.hpp"
#include "canvas/api/ICanvasGridHost.hpp"
#include "canvas/api/ICanvasHost.hpp"
#include "canvas/api/ICanvasStyleHost.hpp"
#include "codeeditor/api/ICodeEditorService.hpp"
#include "core/api/IHeaderInfo.hpp"
#include "core/ui/IUiHost.hpp"
#include "extensionsystem/PluginManager.hpp"
#include "projectexplorer/api/IProjectExplorer.hpp"

namespace Aie::Internal {

Utils::Result AiePlugin::resolveRuntimeDependencies(ExtensionSystem::PluginManager& manager,
                                                    RuntimeDependencies& outDeps)
{
    RuntimeDependencies deps;
    deps.uiHost = manager.getObject<Core::IUiHost>();
    if (!deps.uiHost)
        return Utils::Result::failure(QStringLiteral("IUiHost is not available."));

    deps.gridHost = manager.getObject<Canvas::Api::ICanvasGridHost>();
    if (!deps.gridHost)
        return Utils::Result::failure(QStringLiteral("Canvas grid host is not available."));

    deps.canvasHost = manager.getObject<Canvas::Api::ICanvasHost>();
    deps.canvasDocumentService = manager.getObject<Canvas::Api::ICanvasDocumentService>();
    deps.styleHost = manager.getObject<Canvas::Api::ICanvasStyleHost>();
    deps.headerInfo = manager.getObject<Core::IHeaderInfo>();
    deps.projectExplorer = manager.getObject<ProjectExplorer::IProjectExplorer>();
    deps.codeEditorService = manager.getObject<CodeEditor::Api::ICodeEditorService>();

    outDeps = deps;
    return Utils::Result::success();
}

Utils::Result AiePlugin::configureService(const RuntimeDependencies& deps)
{
    if (!m_service)
        return Utils::Result::failure(QStringLiteral("AIE service is not available."));

    m_service->setGridHost(deps.gridHost);
    m_service->setStyleHost(deps.styleHost);
    m_service->setCanvasHost(deps.canvasHost);

    if (m_symbolsController)
        m_symbolsController->setCanvasDocumentService(deps.canvasDocumentService);

    const Utils::Result loadResult =
        m_service->loadProfileCatalog(QString::fromLatin1(Aie::kDeviceTopologiesResource));
    if (!loadResult)
        return loadResult;

    m_service->setBaseStyles(createDefaultBlockStyles());

    if (m_kernelAssignments) {
        m_kernelAssignments->setCoordinator(m_service->coordinator());
        m_kernelAssignments->setCanvasHost(deps.canvasHost);
    }

    if (m_propertiesShortcutController)
        m_propertiesShortcutController->setCanvasHost(deps.canvasHost);

    if (m_kernelRegistry) {
        const Utils::Result reloadResult = m_kernelRegistry->reload();
        if (!reloadResult)
            return reloadResult;
    }

    return Utils::Result::success();
}

void AiePlugin::configureCanvasDefaults(const RuntimeDependencies& deps) const
{
    if (!deps.canvasHost)
        return;

    deps.canvasHost->setEmptyStateText(QStringLiteral("No design open."),
                                       QStringLiteral("Create or open a design to start."));
    deps.canvasHost->setCanvasActive(false);
}

Utils::Result AiePlugin::configureDesignWorkflow(const RuntimeDependencies& deps)
{
    if (!m_service)
        return Utils::Result::failure(QStringLiteral("AIE service is not available."));

    if (!m_bundleLoader)
        m_bundleLoader = std::make_unique<DesignBundleLoader>(&m_service->catalog());
    if (!m_canvasImporter)
        m_canvasImporter = std::make_unique<CanvasDocumentImporter>(m_service);

    if (!deps.canvasDocumentService)
        return Utils::Result::failure(QStringLiteral("Canvas document service is not available."));

    if (!m_designOpenController) {
        m_designOpenController = new DesignOpenController(m_bundleLoader.get(),
                                                          m_canvasImporter.get(),
                                                          deps.canvasDocumentService,
                                                          this);
    }

    m_designOpenController->setProjectExplorer(deps.projectExplorer);

    connect(m_designOpenController, &DesignOpenController::openFailed, this,
            [this, uiHost = QPointer<Core::IUiHost>(deps.uiHost)](const QString& message) {
                showOpenError(uiHost, message);
            });

    connect(m_designOpenController, &DesignOpenController::designOpened, this,
            [this](const QString& bundlePath, const QString&, const QString&) {
                if (m_kernelAssignments)
                    m_kernelAssignments->rehydrateAssignmentsFromCanvas();

                // Attach HLIR sync to the new document; output goes under <bundle>/codegen/
                if (m_hlirSync && m_service && m_service->canvasHost()) {
                    const QString outputDir = bundlePath + QStringLiteral("/codegen");
                    m_hlirSync->attachDocument(m_service->canvasHost()->document(), outputDir);
                }
            });

    connect(m_designOpenController, &DesignOpenController::designClosed, this,
            [this](const QString&) {
                if (m_kernelAssignments)
                    m_kernelAssignments->clearAssignments();

                // Detach HLIR sync; removes all tracked components from the bridge
                if (m_hlirSync)
                    m_hlirSync->detachDocument();
            });

    return Utils::Result::success();
}

void AiePlugin::configurePanelState(const RuntimeDependencies& deps)
{
    if (!m_service)
        return;

    if (!m_panelState)
        m_panelState = new AiePanelState(m_service->coordinator(), this);
    else
        m_panelState->setCoordinator(m_service->coordinator());

    m_panelState->setCanvasDocumentService(deps.canvasDocumentService);
}

void AiePlugin::configureKernelToolbox(const RuntimeDependencies& deps)
{
    m_codeEditorService = deps.codeEditorService;

    if (m_kernelAssignments)
        m_kernelAssignments->setCodeEditorService(deps.codeEditorService);

    if (!m_kernelToolboxController)
        return;

    m_kernelToolboxController->setCodeEditorService(deps.codeEditorService);
    m_kernelToolboxController->setDialogParent(resolveDialogParent(deps.uiHost));
    m_kernelToolboxController->setProjectExplorer(deps.projectExplorer);
}

} // namespace Aie::Internal
