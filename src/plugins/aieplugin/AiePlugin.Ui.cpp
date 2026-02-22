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
#include "aieplugin/panels/AieNewDesignDialog.hpp"
#include "aieplugin/panels/AiePropertiesPanel.hpp"
#include "aieplugin/panels/AieToolPanel.hpp"

#include "codeeditor/api/ICodeEditorService.hpp"
#include "core/CoreConstants.hpp"
#include "core/api/IHeaderInfo.hpp"
#include "core/api/ISidebarRegistry.hpp"
#include "core/api/SidebarToolSpec.hpp"
#include "core/ui/IUiHost.hpp"
#include "extensionsystem/PluginManager.hpp"
#include "projectexplorer/api/IProjectExplorer.hpp"

#include <QtGui/QAction>
#include <QtWidgets/QMessageBox>

namespace Aie::Internal {

void AiePlugin::registerSidebarTools(const RuntimeDependencies& deps)
{
    registerLayoutSidebarTool(deps);
    registerPropertiesSidebarTool(deps);
    registerKernelsSidebarTool(deps);

    if (!m_sidebarRegistry)
        return;

    connect(m_sidebarRegistry, &Core::ISidebarRegistry::toolOpenStateChanged, this,
            [this](const QString& id, bool open) {
                if (id != kLayoutSidebarToolId &&
                    id != kKernelsSidebarToolId &&
                    id != kPropertiesSidebarToolId) {
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
        return new AieKernelsPanel(m_kernelRegistry, m_kernelAssignments, codeEditor, parent);
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

    const auto factory = [this](QWidget* parent) -> QWidget* {
        return new AiePropertiesPanel(m_service, parent);
    };

    QString error;
    if (!m_sidebarRegistry->registerTool(spec, factory, &error)) {
        qCWarning(aiepluginlog) << "AiePlugin: register properties tool failed:" << error;
        return;
    }

    m_propertiesToolRegistered = true;
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
}

void AiePlugin::connectHeaderInfo(const RuntimeDependencies& deps)
{
    if (!deps.headerInfo || !m_designOpenController)
        return;

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
}

void AiePlugin::showOpenError(Core::IUiHost* uiHost, const QString& message) const
{
    if (message.trimmed().isEmpty())
        return;

    QMessageBox::warning(resolveDialogParent(uiHost), QStringLiteral("Open Design"), message);
}

} // namespace Aie::Internal
