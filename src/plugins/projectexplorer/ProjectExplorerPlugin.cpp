// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "projectexplorer/ProjectExplorerGlobal.hpp"
#include "projectexplorer/ProjectExplorerDataSource.hpp"
#include "projectexplorer/ProjectExplorerPanel.hpp"
#include "projectexplorer/ProjectExplorerService.hpp"
#include "projectexplorer/api/ProjectExplorerMetaTypes.hpp"
#include "projectexplorer/metadata/ProjectExplorerMetadataService.hpp"
#include "projectexplorer/metadata/ProjectExplorerThumbnailService.hpp"
#include "projectexplorer/filesystem/ProjectExplorerFileSystemService.hpp"
#include "projectexplorer/filesystem/ProjectExplorerFileSystemController.hpp"
#include "projectexplorer/state/ProjectExplorerSidebarState.hpp"

#include <extensionsystem/IPlugin.hpp>
#include <extensionsystem/PluginManager.hpp>
#include <utils/Result.hpp>
#include <utils/DocumentBundle.hpp>

#include "core/api/ISidebarRegistry.hpp"
#include "core/api/SidebarToolSpec.hpp"
#include "core/CoreConstants.hpp"
#include "core/ui/IUiHost.hpp"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QLoggingCategory>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <QFileInfo>

Q_LOGGING_CATEGORY(projectexplorerlog, "ironsmith.projectexplorer")

namespace ProjectExplorer::Internal {

namespace {

ProjectExplorer::ProjectEntryKind inferEntryKindFromPath(const QString& path)
{
    const QFileInfo info(path);
    if (info.suffix().compare(Utils::DocumentBundle::extension(), Qt::CaseInsensitive) == 0)
        return ProjectExplorer::ProjectEntryKind::Design;
    return ProjectExplorer::ProjectEntryKind::Unknown;
}

const QString kProjectSidebarToolId = QStringLiteral("IRONSmith.ProjectExplorer");

} // namespace

class ProjectExplorerPlugin final : public ExtensionSystem::IPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.ironsmith.plugin" FILE "ProjectExplorer.json")

public:
    ProjectExplorerPlugin() = default;

    Utils::Result initialize(const QStringList& arguments, ExtensionSystem::PluginManager& manager) override;
    void extensionsInitialized(ExtensionSystem::PluginManager& manager) override;
    ShutdownFlag aboutToShutdown() override;

private:
    void connectRibbonActions(Core::IUiHost* uiHost, QAction* openAction);
    void openRootFolder(Core::IUiHost* uiHost);

	QPointer<Core::ISidebarRegistry> m_sidebarRegistry;
    QPointer<ProjectExplorerService> m_service;
	QPointer<ProjectExplorerDataSource> m_dataSource;
    QPointer<ProjectExplorerFileSystemService> m_fileSystem;
    QPointer<ProjectExplorerFileSystemController> m_fileController;
    QPointer<ProjectExplorerMetadataService> m_metadataService;
	QPointer<ProjectExplorerThumbnailService> m_thumbnailService;
    ProjectExplorerSidebarState m_sidebarState;
	bool m_registered = false;
};

Utils::Result ProjectExplorerPlugin::initialize(const QStringList& arguments, ExtensionSystem::PluginManager& manager)
{
    Q_UNUSED(arguments);

    qCInfo(projectexplorerlog) << "ProjectExplorerPlugin: initialize";
    ProjectExplorer::registerProjectExplorerMetaTypes();
    m_service = new ProjectExplorerService(this);
    m_dataSource = new ProjectExplorerDataSource(this);
    m_fileSystem = new ProjectExplorerFileSystemService(this);
    m_metadataService = new ProjectExplorerMetadataService(this);
    m_thumbnailService = new ProjectExplorerThumbnailService(this);
    connect(m_dataSource, &ProjectExplorerDataSource::rootLabelChanged,
            m_service, &ProjectExplorerService::setRootLabel);
    connect(m_dataSource, &ProjectExplorerDataSource::rootPathChanged,
            m_service, [this](const QString& path) {
                if (m_service)
                    m_service->setRootPath(path, /*userInitiated=*/true);
            });
    connect(m_dataSource, &ProjectExplorerDataSource::entriesChanged,
            m_service, &ProjectExplorerService::setEntries);
    connect(m_service, &ProjectExplorerService::refreshRequested,
            m_dataSource, &ProjectExplorerDataSource::refresh);
    if (m_fileSystem) {
        connect(m_service, &ProjectExplorerService::rootPathChanged,
                m_fileSystem, &ProjectExplorerFileSystemService::setRootPath);
        connect(m_fileSystem, &ProjectExplorerFileSystemService::refreshRequested,
                m_dataSource, &ProjectExplorerDataSource::refresh);
    } else {
        return Utils::Result::failure("ProjectExplorerPlugin: filesystem service is null");
    }
    m_service->setRootPath(m_dataSource->rootPath(), /*userInitiated=*/false);
    m_dataSource->refresh();
    ExtensionSystem::PluginManager::addObject(m_service);
    ExtensionSystem::PluginManager::addObject(m_metadataService);
    ExtensionSystem::PluginManager::addObject(m_thumbnailService);
    return Utils::Result::success();
}

void ProjectExplorerPlugin::extensionsInitialized(ExtensionSystem::PluginManager& manager)
{
    qCInfo(projectexplorerlog) << "ProjectExplorerPlugin: extensionsInitialized";

    auto* uiHost = manager.getObject<Core::IUiHost>();
    if (!uiHost) {
        qCWarning(projectexplorerlog) << "ProjectExplorerPlugin: IUiHost not available";
        return;
    }

    m_sidebarRegistry = uiHost->sidebarRegistry();
    if (!m_sidebarRegistry) {
        qCWarning(projectexplorerlog) << "ProjectExplorerPlugin: ISidebarRegistry not available";
        return;
    }

	    if (m_service && m_fileSystem && !m_fileController) {
	        m_fileController = new ProjectExplorerFileSystemController(m_service, m_fileSystem, this);
        m_fileController->setDialogParent(uiHost->playgroundOverlayHost());
        connect(m_service, &ProjectExplorerService::contextActionRequested,
                m_fileController, &ProjectExplorerFileSystemController::handleContextAction);
        connect(m_service, &ProjectExplorerService::revealPathRequested,
                m_fileController, &ProjectExplorerFileSystemController::handleRevealPath);
	        connect(m_service, &ProjectExplorerService::openRootRequested,
	                this, [this, uiHost]() { openRootFolder(uiHost); });
            connect(m_fileSystem, &ProjectExplorerFileSystemService::operationCompleted,
                    this, [this](ProjectExplorerFileSystemService::Operation op,
                                 const QString& path,
                                 const QString& newPath) {
                        if (!m_service)
                            return;
                        switch (op) {
                            case ProjectExplorerFileSystemService::Operation::Delete:
                                m_service->notifyEntryRemoved(path, inferEntryKindFromPath(path));
                                break;
                            case ProjectExplorerFileSystemService::Operation::Rename:
                                if (!newPath.trimmed().isEmpty()) {
                                    m_service->notifyEntryRenamed(path,
                                                                  newPath,
                                                                  inferEntryKindFromPath(newPath));
                                }
                                break;
                            default:
                                break;
                        }
                    });
	    }

    QAction* openAction = uiHost->ribbonCommand(Core::Constants::RIBBON_TAB_HOME,
                                                Core::Constants::RIBBON_TAB_HOME_PROJECT_GROUP,
                                                Core::Constants::PROJECT_OPEN_ITEMID);
    if (!openAction)
        qCWarning(projectexplorerlog) << "ProjectExplorerPlugin: Open action not available";

    Core::SidebarToolSpec spec;
    spec.id = kProjectSidebarToolId;
    spec.title = QStringLiteral("Project");
    spec.iconResource = QStringLiteral(":/ui/icons/svg/folder.svg");
    spec.side = Core::SidebarSide::Left;
    spec.family = Core::SidebarFamily::Vertical;
    spec.region = Core::SidebarRegion::Exclusive;
    spec.rail = Core::SidebarRail::Top;
    spec.order = 0;
    spec.toolTip = QStringLiteral("Project Explorer");

    const auto factory = [this](QWidget* parent) -> QWidget* {
        return new ProjectExplorerPanel(m_service, parent);
    };

    QString error;
    if (!m_sidebarRegistry->registerTool(spec, factory, &error)) {
        qCWarning(projectexplorerlog) << "ProjectExplorerPlugin: registerTool failed:" << error;
        return;
    }

    m_registered = true;

    connect(m_sidebarRegistry, &Core::ISidebarRegistry::toolOpenStateChanged,
            this,
            [this](const QString& id, bool open) {
                if (id != kProjectSidebarToolId)
                    return;
                m_sidebarState.setPanelOpen(open);
            });

    if (m_sidebarState.panelOpen())
        m_sidebarRegistry->requestShowTool(kProjectSidebarToolId);

    if (openAction)
        connectRibbonActions(uiHost, openAction);
}

ExtensionSystem::IPlugin::ShutdownFlag ProjectExplorerPlugin::aboutToShutdown()
{
    qCInfo(projectexplorerlog) << "ProjectExplorerPlugin: aboutToShutdown";
    if (m_sidebarRegistry && m_registered) {
        m_sidebarState.setPanelOpen(m_sidebarRegistry->isToolOpen(kProjectSidebarToolId));
        QString error;
        if (!m_sidebarRegistry->unregisterTool(kProjectSidebarToolId, &error)) {
            qCWarning(projectexplorerlog) << "ProjectExplorerPlugin: unregisterTool failed:" << error;
        }
    }
    if (m_service) {
        ExtensionSystem::PluginManager::removeObject(m_service);
        m_service = nullptr;
    }
    if (m_metadataService) {
        ExtensionSystem::PluginManager::removeObject(m_metadataService);
        m_metadataService = nullptr;
    }
    if (m_thumbnailService) {
        ExtensionSystem::PluginManager::removeObject(m_thumbnailService);
        m_thumbnailService = nullptr;
    }
    return ShutdownFlag::SynchronousShutdown;
}

void ProjectExplorerPlugin::connectRibbonActions(Core::IUiHost* uiHost, QAction* openAction)
{
    if (!uiHost || !m_dataSource)
        return;

    connect(openAction, &QAction::triggered, this, [this, uiHost]() {
        openRootFolder(uiHost);
    });
}

void ProjectExplorerPlugin::openRootFolder(Core::IUiHost* uiHost)
{
    if (!m_dataSource)
        return;

    QWidget* parent = uiHost ? uiHost->playgroundOverlayHost() : nullptr;
    if (!parent)
        parent = QApplication::activeWindow();

    const QString initialDir = m_dataSource->rootPath();
    const QString chosen = QFileDialog::getExistingDirectory(parent,
                                                             QStringLiteral("Open Folder"),
                                                             initialDir,
                                                             QFileDialog::ShowDirsOnly);
    if (chosen.isEmpty())
        return;

    qCInfo(projectexplorerlog) << "ProjectExplorerPlugin: open folder" << chosen;
    m_dataSource->setRootPath(chosen);
}

} // namespace ProjectExplorer::Internal

#include "ProjectExplorerPlugin.moc"
