// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#if !defined(AIEPLUGIN_INTERNAL_IMPL)
#error "AiePlugin.Ui.cpp is an internal implementation fragment. Include from AiePlugin.cpp."
#endif

#include "aieplugin/AieService.hpp"
#include "aieplugin/design/DesignOpenController.hpp"
#include "aieplugin/kernels/KernelAssignmentController.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"
#include "aieplugin/panels/AieKernelsPanel.hpp"
#include "aieplugin/panels/AieLogPanel.hpp"
#include "aieplugin/panels/AieNewDesignDialog.hpp"
#include "aieplugin/panels/AiePropertiesPanel.hpp"
#include "aieplugin/panels/AieToolPanel.hpp"
#include "aieplugin/symbol_table/SymbolsPanel.hpp"
#include "aieplugin/hlir_sync/AieOutputLog.hpp"
#include "aieplugin/hlir_sync/HlirDirectExecution.hpp"
#include "aieplugin/hlir_sync/HlirSyncService.hpp"

#include "codeeditor/api/ICodeEditorService.hpp"
#include "canvas/api/ICanvasDocumentService.hpp"
#include "core/CoreConstants.hpp"
#include "core/api/IHeaderInfo.hpp"
#include "core/api/ISidebarRegistry.hpp"
#include "core/api/SidebarToolSpec.hpp"
#include "core/ui/IUiHost.hpp"
#include "extensionsystem/PluginManager.hpp"
#include "projectexplorer/api/IProjectExplorer.hpp"

#include <QtCore/QTimer>
#include <QtGui/QAction>
#include <QtGui/QIcon>
#include <QtWidgets/QMessageBox>

namespace Aie::Internal {

void AiePlugin::registerSidebarTools(const RuntimeDependencies& deps)
{
    registerLayoutSidebarTool(deps);
    registerPropertiesSidebarTool(deps);
    registerSymbolsSidebarTool(deps);
    registerKernelsSidebarTool(deps);
    registerLogSidebarTool(deps);

    if (!m_sidebarRegistry)
        return;

    if (m_propertiesShortcutController)
        m_propertiesShortcutController->setSidebarRegistry(m_sidebarRegistry);

    connect(m_sidebarRegistry, &Core::ISidebarRegistry::toolOpenStateChanged, this,
            [this](const QString& id, bool open) {
                if (id != kLayoutSidebarToolId &&
                    id != kKernelsSidebarToolId &&
                    id != kPropertiesSidebarToolId &&
                    id != kSymbolsSidebarToolId) {
                    return;
                }
                m_sidebarState.setPanelOpen(id, open);
            });

    restoreSidebarOpenState();
}

void AiePlugin::registerLayoutSidebarTool(const RuntimeDependencies& deps)
{
    if (!deps.uiHost)
        return;

    if (!m_sidebarRegistry)
        m_sidebarRegistry = deps.uiHost->sidebarRegistry();
    if (!m_sidebarRegistry || m_layoutToolRegistered)
        return;

    Core::SidebarToolSpec spec;
    spec.id = kLayoutSidebarToolId;
    spec.title = QStringLiteral("AIE");
    spec.iconResource = QStringLiteral(":/ui/icons/svg/hammer_icon.svg");
    spec.side = Core::SidebarSide::Right;
    spec.family = Core::SidebarFamily::Vertical;
    spec.region = Core::SidebarRegion::Exclusive;
    spec.rail = Core::SidebarRail::Top;
    spec.order = 0;
    spec.toolTip = QStringLiteral("AIE Grid Tools");

    const auto factory = [this](QWidget* parent) -> QWidget* {
        return new AieToolPanel(m_service ? m_service->coordinator() : nullptr, parent);
    };

    QString error;
    if (!m_sidebarRegistry->registerTool(spec, factory, &error)) {
        qCWarning(aiepluginlog) << "AiePlugin: registerTool failed:" << error;
        return;
    }

    m_layoutToolRegistered = true;
}

void AiePlugin::registerKernelsSidebarTool(const RuntimeDependencies& deps)
{
    if (!deps.uiHost)
        return;

    if (!m_sidebarRegistry)
        m_sidebarRegistry = deps.uiHost->sidebarRegistry();
    if (!m_sidebarRegistry || m_kernelsToolRegistered)
        return;

    Core::SidebarToolSpec spec;
    spec.id = kKernelsSidebarToolId;
    spec.title = QStringLiteral("Kernels");
    spec.iconResource = QStringLiteral(":/ui/icons/svg/compute_icon.svg");
    spec.side = Core::SidebarSide::Left;
    spec.family = Core::SidebarFamily::Vertical;
    spec.region = Core::SidebarRegion::Exclusive;
    spec.rail = Core::SidebarRail::Top;
    spec.order = 1;
    spec.toolTip = QStringLiteral("Kernel Catalog");

    const auto factory =
        [this, codeEditor = QPointer<CodeEditor::Api::ICodeEditorService>(deps.codeEditorService)](QWidget* parent)
        -> QWidget* {
        auto* panel = new AieKernelsPanel(m_kernelRegistry, m_kernelAssignments, codeEditor, m_outputLog, parent);
        m_kernelsPanel = panel;
        return panel;
    };

    QString error;
    if (!m_sidebarRegistry->registerTool(spec, factory, &error)) {
        qCWarning(aiepluginlog) << "AiePlugin: register kernels tool failed:" << error;
        return;
    }

    m_kernelsToolRegistered = true;
}

void AiePlugin::registerPropertiesSidebarTool(const RuntimeDependencies& deps)
{
    if (!deps.uiHost)
        return;

    if (!m_sidebarRegistry)
        m_sidebarRegistry = deps.uiHost->sidebarRegistry();
    if (!m_sidebarRegistry || m_propertiesToolRegistered)
        return;

    Core::SidebarToolSpec spec;
    spec.id = kPropertiesSidebarToolId;
    spec.title = QStringLiteral("Properties");
    spec.iconResource = QStringLiteral(":/ui/icons/svg/select_hand_pointer_icon.svg");
    spec.side = Core::SidebarSide::Right;
    spec.family = Core::SidebarFamily::Vertical;
    spec.region = Core::SidebarRegion::Exclusive;
    spec.rail = Core::SidebarRail::Top;
    spec.order = 1;
    spec.toolTip = QStringLiteral("Selection Properties");

    const auto factory = [this, canvasDocuments = QPointer<Canvas::Api::ICanvasDocumentService>(deps.canvasDocumentService)](QWidget* parent) -> QWidget* {
        auto* panel = new AiePropertiesPanel(m_service, canvasDocuments, parent);
        panel->setSymbolsController(m_symbolsController);
        panel->setKernelAssignmentController(m_kernelAssignments);
        return panel;
    };

    QString error;
    if (!m_sidebarRegistry->registerTool(spec, factory, &error)) {
        qCWarning(aiepluginlog) << "AiePlugin: register properties tool failed:" << error;
        return;
    }

    m_propertiesToolRegistered = true;
}

void AiePlugin::registerLogSidebarTool(const RuntimeDependencies& deps)
{
    if (!deps.uiHost)
        return;

    if (!m_sidebarRegistry)
        m_sidebarRegistry = deps.uiHost->sidebarRegistry();
    if (!m_sidebarRegistry || m_logToolRegistered)
        return;

    Core::SidebarToolSpec spec;
    spec.id = kLogSidebarToolId;
    spec.title = QStringLiteral("Log");
    spec.iconResource = QStringLiteral(":/ui/icons/svg/text_file_icon.svg");
    spec.side = Core::SidebarSide::Right;
    spec.family = Core::SidebarFamily::Vertical;
    spec.region = Core::SidebarRegion::Exclusive;
    spec.rail = Core::SidebarRail::Bottom;
    spec.order = 0;
    spec.toolTip = QStringLiteral("Build & Verification Log");

    const auto factory = [this](QWidget* parent) -> QWidget* {
        auto* panel = new AieLogPanel(m_outputLog, parent);
        m_logPanel = panel;
        return panel;
    };

    QString error;
    if (!m_sidebarRegistry->registerTool(spec, factory, &error)) {
        qCWarning(aiepluginlog) << "AiePlugin: register log panel failed:" << error;
        return;
    }

    m_logToolRegistered = true;
}

void AiePlugin::registerSymbolsSidebarTool(const RuntimeDependencies& deps)
{
    if (!deps.uiHost)
        return;

    if (!m_sidebarRegistry)
        m_sidebarRegistry = deps.uiHost->sidebarRegistry();
    if (!m_sidebarRegistry || m_symbolsToolRegistered)
        return;

    Core::SidebarToolSpec spec;
    spec.id = kSymbolsSidebarToolId;
    spec.title = QStringLiteral("Symbols");
    spec.iconResource = QStringLiteral(":/ui/icons/svg/table_icon.svg");
    spec.side = Core::SidebarSide::Right;
    spec.family = Core::SidebarFamily::Vertical;
    spec.region = Core::SidebarRegion::Additive;
    spec.rail = Core::SidebarRail::Top;
    spec.order = 2;
    spec.toolTip = QStringLiteral("Symbols");

    const auto factory = [controller = QPointer<SymbolsController>(m_symbolsController)](QWidget* parent)
        -> QWidget* {
        return new SymbolsPanel(controller, parent);
    };

    QString error;
    if (!m_sidebarRegistry->registerTool(spec, factory, &error)) {
        qCWarning(aiepluginlog) << "AiePlugin: register symbols tool failed:" << error;
        return;
    }

    m_symbolsToolRegistered = true;
}

void AiePlugin::persistSidebarOpenState()
{
    if (!m_sidebarRegistry)
        return;

    if (m_layoutToolRegistered)
        m_sidebarState.setPanelOpen(kLayoutSidebarToolId, m_sidebarRegistry->isToolOpen(kLayoutSidebarToolId));

    if (m_kernelsToolRegistered)
        m_sidebarState.setPanelOpen(kKernelsSidebarToolId, m_sidebarRegistry->isToolOpen(kKernelsSidebarToolId));

    if (m_propertiesToolRegistered)
        m_sidebarState.setPanelOpen(kPropertiesSidebarToolId, m_sidebarRegistry->isToolOpen(kPropertiesSidebarToolId));

    if (m_symbolsToolRegistered)
        m_sidebarState.setPanelOpen(kSymbolsSidebarToolId, m_sidebarRegistry->isToolOpen(kSymbolsSidebarToolId));
}

void AiePlugin::restoreSidebarOpenState()
{
    if (!m_sidebarRegistry)
        return;

    if (m_layoutToolRegistered && m_sidebarState.panelOpen(kLayoutSidebarToolId))
        m_sidebarRegistry->requestShowTool(kLayoutSidebarToolId);

    if (m_kernelsToolRegistered && m_sidebarState.panelOpen(kKernelsSidebarToolId))
        m_sidebarRegistry->requestShowTool(kKernelsSidebarToolId);

    if (m_propertiesToolRegistered && m_sidebarState.panelOpen(kPropertiesSidebarToolId))
        m_sidebarRegistry->requestShowTool(kPropertiesSidebarToolId);

    if (m_symbolsToolRegistered && m_sidebarState.panelOpen(kSymbolsSidebarToolId))
        m_sidebarRegistry->requestShowTool(kSymbolsSidebarToolId);
}

void AiePlugin::connectHeaderInfo(const RuntimeDependencies& deps)
{
    if (!deps.headerInfo || !m_designOpenController)
        return;

    connect(m_designOpenController, &DesignOpenController::designOpened, this,
            [this](const QString& bundlePath, const QString&, const QString&) {
                if (!m_logsByDesign.contains(bundlePath))
                    m_logsByDesign.insert(bundlePath, new AieOutputLog(this));
                m_outputLog = m_logsByDesign.value(bundlePath);
                if (auto* panel = qobject_cast<AieLogPanel*>(m_logPanel.data()))
                    panel->setLog(m_outputLog);
                if (auto* panel = qobject_cast<AieKernelsPanel*>(m_kernelsPanel.data()))
                    panel->setOutputLog(m_outputLog);
                if (m_outputLog && m_sidebarRegistry) {
                    connect(m_outputLog, &AieOutputLog::entryAdded, this,
                            [this](bool success, const QString&) {
                                if (!success && m_sidebarRegistry)
                                    m_sidebarRegistry->requestShowTool(kLogSidebarToolId);
                            });
                }
            });

    connect(m_designOpenController, &DesignOpenController::designOpened, this,
            [this, header = QPointer<Core::IHeaderInfo>(deps.headerInfo)]
            (const QString&, const QString& displayName, const QString& deviceId) {
                if (!header)
                    return;
                header->setDesignLabel(displayName);
                if (m_service)
                    header->setDeviceLabel(formatDeviceLabel(m_service->catalog(), deviceId));
                else
                    header->setDeviceLabel(deviceId);
            });

    connect(m_designOpenController, &DesignOpenController::designClosed, this,
            [header = QPointer<Core::IHeaderInfo>(deps.headerInfo)](const QString&) {
                if (!header)
                    return;
                header->setDesignLabel(QString());
                header->setDeviceLabel(QString());
            });
}

void AiePlugin::connectRibbonActions(const RuntimeDependencies& deps,
                                     ExtensionSystem::PluginManager& manager)
{
    if (!deps.uiHost)
        return;

    QAction* newDesignAction = deps.uiHost->ribbonCommand(Core::Constants::RIBBON_TAB_HOME,
                                                           Core::Constants::RIBBON_TAB_HOME_PROJECT_GROUP,
                                                           Core::Constants::PROJECT_NEW_ITEMID);
    if (!newDesignAction) {
        qCWarning(aiepluginlog) << "AiePlugin: New Design action not available.";
        return;
    }

    auto* managerPtr = &manager;
    connect(newDesignAction, &QAction::triggered, this,
            [this, managerPtr, uiHost = QPointer<Core::IUiHost>(deps.uiHost)]() {
                QWidget* parent = resolveDialogParent(uiHost);

                AieNewDesignDialog dialog(parent);
                if (dialog.exec() != QDialog::Accepted)
                    return;

                const auto result = dialog.result();
                if (!result.created)
                    return;

                if (auto* projectExplorer = managerPtr->getObject<ProjectExplorer::IProjectExplorer>())
                    projectExplorer->refresh();

                if (m_designOpenController)
                    m_designOpenController->openBundlePath(result.bundlePath);
            });

    deps.uiHost->ensureRibbonGroup(Core::Constants::RIBBON_TAB_OUTPUT,
                                   QStringLiteral("IRONSmith.Ribbon.Output.BuildGroup"),
                                   tr("Build"));

    auto* actCodeGen = new QAction(tr("Generate\nCode"), this);
    actCodeGen->setIcon(QIcon(QStringLiteral(":/ui/icons/svg/generate_code.svg")));
    connect(actCodeGen, &QAction::triggered, this, [this]() {
        if (m_sidebarRegistry)
            m_sidebarRegistry->requestShowTool(kLogSidebarToolId);
        QTimer::singleShot(200, this, [this]() {
            if (m_hlirSync)
                m_hlirSync->generateCode();
        });
    });

    auto* actVerify = new QAction(tr("Verify\nDesign"), this);
    actVerify->setIcon(QIcon(QStringLiteral(":/ui/icons/svg/verify_design.svg")));
    connect(actVerify, &QAction::triggered, this, [this]() {
        if (m_sidebarRegistry)
            m_sidebarRegistry->requestShowTool(kLogSidebarToolId);
        QTimer::singleShot(200, this, [this]() {
            if (m_hlirSync)
                m_hlirSync->verifyDesign();
        });
    });

    auto* actExecute = new QAction(tr("Execute\nCode"), this);
    actExecute->setIcon(QIcon(QStringLiteral(":/ui/icons/svg/python_icon.svg")));
    connect(actExecute, &QAction::triggered, this, [this]() {
        if (m_directExec && m_hlirSync)
            m_directExec->execute(m_hlirSync->generatedScriptPath());
    });

    if (m_hlirSync && m_outputLog) {
        connect(m_hlirSync, &HlirSyncService::runStarted, this,
                [this]() {
                    if (m_sidebarRegistry)
                        m_sidebarRegistry->requestShowTool(kLogSidebarToolId);
                    m_outputLog->startRun();
                });

        connect(m_hlirSync, &HlirSyncService::stepLogged, this,
                [this](bool ok, const QString& label) {
                    m_outputLog->appendRunStep(ok, label);
                });

        connect(m_hlirSync, &HlirSyncService::codeGenFinished, this,
                [this](bool success, const QString& message) {
                    const QString header = success
                        ? QStringLiteral("Code generation succeeded.")
                        : QStringLiteral("Code generation failed.");
                    m_outputLog->finalizeRun(success, header + u'\n' + message);

                    if (success && m_codeEditorService) {
                        const QString generatedFile = m_hlirSync->generatedScriptPath();
                        if (!generatedFile.isEmpty()) {
                            // openFile() reuses the existing tab if the file is already open,
                            // activates it, and reveals the code editor sidebar.
                            CodeEditor::Api::CodeEditorOpenRequest req;
                            req.filePath     = generatedFile;
                            req.languageHint = QStringLiteral("python");
                            req.activate     = true;
                            req.readOnly     = false;
                            CodeEditor::Api::CodeEditorSessionHandle handle;
                            const Utils::Result openResult = m_codeEditorService->openFile(req, handle);
                            if (!openResult)
                                qCWarning(aiepluginlog) << "AiePlugin: failed to open generated file:" << openResult.errors;
                        }
                    }
                });

        connect(m_hlirSync, &HlirSyncService::verificationFinished, this,
                [this](bool passed, const QString& message) {
                    m_outputLog->finalizeRun(passed, message);
                });
    }

    if (m_directExec && m_outputLog) {
        connect(m_directExec, &HlirDirectExecution::runStarted, this,
                [this]() {
                    if (m_sidebarRegistry)
                        m_sidebarRegistry->requestShowTool(kLogSidebarToolId);
                    m_outputLog->startRun();
                });

        connect(m_directExec, &HlirDirectExecution::stepLogged, this,
                [this](bool ok, const QString& label) {
                    m_outputLog->appendRunStep(ok, label);
                });

        connect(m_directExec, &HlirDirectExecution::executeFinished, this,
                [this](bool success, const QString& message) {
                    m_outputLog->finalizeRun(success, message);
                });
    }

    Core::RibbonPresentation codeGenPres;
    codeGenPres.size = Core::RibbonVisualSize::Large;
    codeGenPres.iconPlacement = Core::RibbonIconPlacement::AboveText;
    codeGenPres.iconPx = 40;

    Core::RibbonPresentation btnPres;
    btnPres.size = Core::RibbonVisualSize::Large;
    btnPres.iconPlacement = Core::RibbonIconPlacement::AboveText;

    auto codeGenRoot = Core::RibbonNode::makeRow(QStringLiteral("codegen_root"));
    codeGenRoot->addCommand(QStringLiteral("output.codegen"),
                            actCodeGen, Core::RibbonControlType::Button, codeGenPres);
    codeGenRoot->addCommand(QStringLiteral("output.verify"),
                            actVerify, Core::RibbonControlType::Button, btnPres);
    codeGenRoot->addCommand(QStringLiteral("output.execute"),
                            actExecute, Core::RibbonControlType::Button, btnPres);

    const auto ribbonResult = deps.uiHost->setRibbonGroupLayout(
        Core::Constants::RIBBON_TAB_OUTPUT,
        QStringLiteral("IRONSmith.Ribbon.Output.BuildGroup"),
        std::move(codeGenRoot));

    if (!ribbonResult)
        qCWarning(aiepluginlog) << "AiePlugin: failed to set Output ribbon layout:" << ribbonResult.error;
}

void AiePlugin::showOpenError(Core::IUiHost* uiHost, const QString& message) const
{
    if (message.trimmed().isEmpty())
        return;

    QMessageBox::warning(resolveDialogParent(uiHost), QStringLiteral("Open Design"), message);
}

} // namespace Aie::Internal
