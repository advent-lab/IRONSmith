// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/NpuProfile.hpp"
#include "aieplugin/NpuProfileLoader.hpp"
#include "aieplugin/AieService.hpp"
#include "aieplugin/design/CanvasDocumentImporter.hpp"
#include "aieplugin/design/DesignBundleLoader.hpp"
#include "aieplugin/design/DesignOpenController.hpp"
#include "aieplugin/kernels/KernelAssignmentController.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"
#include "aieplugin/kernels/KernelToolboxController.hpp"
#include "aieplugin/state/AieSidebarState.hpp"
#include "aieplugin/state/AieWorkspaceState.hpp"
#include "aieplugin/hlir_sync/AieOutputLog.hpp"
#include "aieplugin/hlir_sync/HlirSyncService.hpp"
#include "aieplugin/AiePlugin.ForwardDecls.cpp"

#include <extensionsystem/IPlugin.hpp>
#include <utils/Result.hpp>

#include "core/api/ISidebarRegistry.hpp"
#include "core/ui/IUiHost.hpp"
#include "extensionsystem/PluginManager.hpp"

#include <QtCore/QLoggingCategory>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtWidgets/QApplication>

#include <memory>

extern int qInitResources_AiePluginResources();

namespace Aie::Internal {

Q_LOGGING_CATEGORY(aiepluginlog, "ironsmith.aie")

const QString kLayoutSidebarToolId    = QStringLiteral("IRONSmith.AieGridTools");
const QString kKernelsSidebarToolId   = QStringLiteral("IRONSmith.Kernels");
const QString kPropertiesSidebarToolId = QStringLiteral("IRONSmith.AieProperties");
const QString kLogSidebarToolId       = QStringLiteral("IRONSmith.AieLog");

class AiePlugin final : public ExtensionSystem::IPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.ironsmith.plugin" FILE "AiePlugin.json")

public:
    AiePlugin() = default;
    ~AiePlugin() override;

    Utils::Result initialize(const QStringList& arguments, ExtensionSystem::PluginManager& manager) override;
    void extensionsInitialized(ExtensionSystem::PluginManager& manager) override;
    ShutdownFlag aboutToShutdown() override;

private:
    struct RuntimeDependencies final {
        Core::IUiHost* uiHost = nullptr;
        Canvas::Api::ICanvasGridHost* gridHost = nullptr;
        Canvas::Api::ICanvasHost* canvasHost = nullptr;
        Canvas::Api::ICanvasDocumentService* canvasDocumentService = nullptr;
        Canvas::Api::ICanvasStyleHost* styleHost = nullptr;
        Core::IHeaderInfo* headerInfo = nullptr;
        ProjectExplorer::IProjectExplorer* projectExplorer = nullptr;
        CodeEditor::Api::ICodeEditorService* codeEditorService = nullptr;
    };

    static QWidget* resolveDialogParent(Core::IUiHost* uiHost);
    static void logResultErrors(const QString& context, const Utils::Result& result);
    static QString formatDeviceLabel(const Aie::NpuProfileCatalog& catalog, const QString& deviceId);

    Utils::Result resolveRuntimeDependencies(ExtensionSystem::PluginManager& manager, RuntimeDependencies& outDeps);
    Utils::Result configureService(const RuntimeDependencies& deps);
    void configureCanvasDefaults(const RuntimeDependencies& deps) const;
    Utils::Result configureDesignWorkflow(const RuntimeDependencies& deps);
    void configurePanelState();
    void registerSidebarTools(const RuntimeDependencies& deps);
    void registerLayoutSidebarTool(const RuntimeDependencies& deps);
    void registerKernelsSidebarTool(const RuntimeDependencies& deps);
    void registerPropertiesSidebarTool(const RuntimeDependencies& deps);
    void registerLogSidebarTool(const RuntimeDependencies& deps);
    void persistSidebarOpenState();
    void restoreSidebarOpenState();
    void connectHeaderInfo(const RuntimeDependencies& deps);
    void connectRibbonActions(const RuntimeDependencies& deps, ExtensionSystem::PluginManager& manager);
    void configureWorkspacePersistence(const RuntimeDependencies& deps);
    void configureKernelToolbox(const RuntimeDependencies& deps);
    void restoreWorkspaceDesign();
    bool isRestorableBundlePath(const QString& bundlePath, const QString& workspaceRoot) const;
    void showOpenError(Core::IUiHost* uiHost, const QString& message) const;

    QPointer<Core::ISidebarRegistry> m_sidebarRegistry;
    QPointer<AieService> m_service;
    QPointer<AiePanelState> m_panelState;
    QPointer<DesignOpenController> m_designOpenController;
    std::unique_ptr<DesignBundleLoader> m_bundleLoader;
    std::unique_ptr<CanvasDocumentImporter> m_canvasImporter;
    AieSidebarState m_sidebarState;
    AieWorkspaceState m_workspaceState;
    QPointer<KernelRegistryService> m_kernelRegistry;
    QPointer<KernelAssignmentController> m_kernelAssignments;
    QPointer<KernelToolboxController> m_kernelToolboxController;
    QPointer<AieOutputLog> m_outputLog;
    QPointer<HlirSyncService> m_hlirSync;
    QString m_workspaceRoot;
    bool m_layoutToolRegistered = false;
    bool m_kernelsToolRegistered = false;
    bool m_propertiesToolRegistered = false;
    bool m_logToolRegistered = false;
};

AiePlugin::~AiePlugin() = default;

Utils::Result AiePlugin::initialize(const QStringList& arguments, ExtensionSystem::PluginManager& manager)
{
    Q_UNUSED(arguments);
    Q_UNUSED(manager);

    qInitResources_AiePluginResources();

    m_service = new AieService(this);
    m_kernelRegistry = new KernelRegistryService(this);
    m_kernelAssignments = new KernelAssignmentController(this);
    m_kernelToolboxController = new KernelToolboxController(this);
    m_outputLog = new AieOutputLog(this);
    m_hlirSync = new HlirSyncService(this);

    if (m_kernelAssignments)
        m_kernelAssignments->setRegistry(m_kernelRegistry);

    if (m_kernelToolboxController) {
        m_kernelToolboxController->setRegistry(m_kernelRegistry);
        m_kernelToolboxController->setAssignments(m_kernelAssignments);
    }

    ExtensionSystem::PluginManager::addObject(m_service);
    if (m_kernelRegistry)
        ExtensionSystem::PluginManager::addObject(m_kernelRegistry);

    return Utils::Result::success();
}

void AiePlugin::extensionsInitialized(ExtensionSystem::PluginManager& manager)
{
    if (!m_service) {
        qCWarning(aiepluginlog) << "AiePlugin: service unavailable during extensionsInitialized.";
        return;
    }

    RuntimeDependencies deps;
    const Utils::Result depsResult = resolveRuntimeDependencies(manager, deps);
    if (!depsResult) {
        logResultErrors(QStringLiteral("AiePlugin: missing required runtime dependencies."), depsResult);
        return;
    }

    const Utils::Result serviceResult = configureService(deps);
    if (!serviceResult) {
        logResultErrors(QStringLiteral("AiePlugin: failed to configure AIE service."), serviceResult);
        return;
    }

    configureCanvasDefaults(deps);

    const Utils::Result workflowResult = configureDesignWorkflow(deps);
    if (!workflowResult) {
        logResultErrors(QStringLiteral("AiePlugin: failed to configure design workflow."), workflowResult);
        return;
    }

    configurePanelState();
    registerSidebarTools(deps);
    connectHeaderInfo(deps);
    connectRibbonActions(deps, manager);
    configureWorkspacePersistence(deps);
    configureKernelToolbox(deps);
}

ExtensionSystem::IPlugin::ShutdownFlag AiePlugin::aboutToShutdown()
{
    if (m_designOpenController && !m_workspaceRoot.isEmpty()) {
        m_workspaceState.setActiveBundlePathForRoot(m_workspaceRoot,
                                                    m_designOpenController->activeBundlePath());
    }

    persistSidebarOpenState();

    if (m_sidebarRegistry && m_layoutToolRegistered) {
        QString error;
        if (!m_sidebarRegistry->unregisterTool(kLayoutSidebarToolId, &error)) {
            qCWarning(aiepluginlog) << "AiePlugin: unregister AIE tools failed:" << error;
        }
    }

    if (m_sidebarRegistry && m_kernelsToolRegistered) {
        QString error;
        if (!m_sidebarRegistry->unregisterTool(kKernelsSidebarToolId, &error)) {
            qCWarning(aiepluginlog) << "AiePlugin: unregister kernels panel failed:" << error;
        }
    }

    if (m_sidebarRegistry && m_propertiesToolRegistered) {
        QString error;
        if (!m_sidebarRegistry->unregisterTool(kPropertiesSidebarToolId, &error)) {
            qCWarning(aiepluginlog) << "AiePlugin: unregister properties panel failed:" << error;
        }
    }

    if (m_sidebarRegistry && m_logToolRegistered) {
        QString error;
        if (!m_sidebarRegistry->unregisterTool(kLogSidebarToolId, &error)) {
            qCWarning(aiepluginlog) << "AiePlugin: unregister log panel failed:" << error;
        }
    }

    if (m_kernelRegistry) {
        ExtensionSystem::PluginManager::removeObject(m_kernelRegistry);
        m_kernelRegistry = nullptr;
    }

    if (m_kernelToolboxController) {
        m_kernelToolboxController->setProjectExplorer(nullptr);
        m_kernelToolboxController = nullptr;
    }

    if (m_service) {
        ExtensionSystem::PluginManager::removeObject(m_service);
        m_service = nullptr;
    }

    return ShutdownFlag::SynchronousShutdown;
}

QWidget* AiePlugin::resolveDialogParent(Core::IUiHost* uiHost)
{
    if (uiHost)
        return uiHost->playgroundOverlayHost();
    return QApplication::activeWindow();
}

void AiePlugin::logResultErrors(const QString& context, const Utils::Result& result)
{
    if (result.ok)
        return;

    qCWarning(aiepluginlog).noquote() << context;
    for (const auto& error : result.errors)
        qCWarning(aiepluginlog).noquote() << "  " << error;
}

QString AiePlugin::formatDeviceLabel(const Aie::NpuProfileCatalog& catalog, const QString& deviceId)
{
    const auto* profile = findProfileById(catalog, deviceId);
    if (!profile) {
        if (deviceId.isEmpty())
            return QStringLiteral("PHOENIX-XDNA1");
        return deviceId.toUpper();
    }

    const QString name = profile->name.trimmed().toUpper();
    const QString family = profile->family.trimmed().toUpper();

    if (!name.isEmpty() && !family.isEmpty())
        return QStringLiteral("%1-%2").arg(name, family);
    if (!name.isEmpty())
        return name;
    if (!family.isEmpty())
        return family;

    return profile->id.trimmed().toUpper();
}

} // namespace Aie::Internal

#define AIEPLUGIN_INTERNAL_IMPL
#include "aieplugin/AiePlugin.Runtime.cpp"
#include "aieplugin/AiePlugin.Ui.cpp"
#include "aieplugin/AiePlugin.Workspace.cpp"
#undef AIEPLUGIN_INTERNAL_IMPL

#include "AiePlugin.moc"
