// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "codeeditor/CodeEditorGlobal.hpp"
#include "codeeditor/Constants.hpp"
#include "codeeditor/api/CodeEditorTypes.hpp"
#include "codeeditor/internal/CodeEditorServiceImpl.hpp"
#include "codeeditor/panels/CodeEditorPanel.hpp"
#include "codeeditor/state/CodeEditorWorkspaceState.hpp"

#include <core/api/ISidebarRegistry.hpp>
#include <core/api/SidebarToolSpec.hpp>
#include <core/ui/IUiHost.hpp>
#include <extensionsystem/IPlugin.hpp>
#include <extensionsystem/PluginManager.hpp>
#include <projectexplorer/api/IProjectExplorer.hpp>
#include <projectexplorer/api/ProjectExplorerTypes.hpp>
#include <utils/DocumentBundle.hpp>
#include <utils/PathUtils.hpp>
#include <utils/Result.hpp>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLoggingCategory>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QtGlobal>

Q_LOGGING_CATEGORY(ceditorlog, "ironsmith.codeeditor")

namespace CodeEditor::Internal {

namespace {

#if defined(Q_OS_WIN)
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

const QString kSidebarToolId = QString::fromLatin1(Constants::kSidebarToolId);
const QString kProjectOpenActionId = QStringLiteral("IRONSmith.CodeEditor.OpenInCode");
constexpr int kStateSaveDelayMs = 250;

bool isOpenableProjectKind(ProjectExplorer::ProjectEntryKind kind)
{
    return kind != ProjectExplorer::ProjectEntryKind::Folder
           && kind != ProjectExplorer::ProjectEntryKind::Design;
}

bool isPathInsidePrefix(const QString& candidatePath, const QString& prefixPath)
{
    if (candidatePath.isEmpty() || prefixPath.isEmpty())
        return false;

    QString normalizedPrefix = prefixPath;
    if (!normalizedPrefix.endsWith('/'))
        normalizedPrefix.append('/');

    return candidatePath.startsWith(normalizedPrefix, kPathCase);
}

} // namespace

class CodeEditorPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.ironsmith.plugin" FILE "CodeEditor.json")

public:
    Utils::Result initialize(const QStringList& arguments, ExtensionSystem::PluginManager& manager) override;
    void extensionsInitialized(ExtensionSystem::PluginManager& manager) override;
    ShutdownFlag aboutToShutdown() override;

private:
    void registerSidebarTool(Core::IUiHost* uiHost);
    void connectProjectExplorer(ProjectExplorer::IProjectExplorer* explorer);
    void connectPersistenceSignals();

    QString resolveAbsolutePath(const QString& path) const;
    void openInEditor(const QString& path, bool activateSession, bool revealSidebar);
    bool isSidebarOpen() const;
    void scheduleWorkspaceStateSave();
    void saveWorkspaceState();
    void restoreWorkspaceState();

    void handleProjectOpenRequested(const QString& path, ProjectExplorer::ProjectEntryKind kind);
    void handleProjectContextAction(const QString& actionId, const QString& path);
    void handleProjectEntryRemoved(const QString& absolutePath, ProjectExplorer::ProjectEntryKind kind);
    void handleProjectEntryRenamed(const QString& oldAbsolutePath,
                                   const QString& newAbsolutePath,
                                   ProjectExplorer::ProjectEntryKind kind);
    void showCodeSidebar();

    QPointer<CodeEditorServiceImpl> m_service;
    QPointer<Core::ISidebarRegistry> m_sidebarRegistry;
    QPointer<ProjectExplorer::IProjectExplorer> m_projectExplorer;

    CodeEditorWorkspaceState m_workspaceState;
    QTimer m_stateSaveTimer;
    QString m_workspaceRoot;
    bool m_sidebarRegistered = false;
    bool m_projectActionRegistered = false;
    bool m_restoringWorkspaceState = false;
};

Utils::Result CodeEditorPlugin::initialize(const QStringList& arguments, ExtensionSystem::PluginManager& manager)
{
    Q_UNUSED(arguments);
    Q_UNUSED(manager);

    qCInfo(ceditorlog) << "CodeEditorPlugin: initialize";

    qRegisterMetaType<CodeEditor::Api::CodeEditorSessionHandle>(
        "CodeEditor::Api::CodeEditorSessionHandle");
    qRegisterMetaType<CodeEditor::Api::CodeEditorCloseReason>(
        "CodeEditor::Api::CodeEditorCloseReason");

    m_service = new CodeEditorServiceImpl(this);
    ExtensionSystem::PluginManager::addObject(m_service);
    connectPersistenceSignals();

    m_stateSaveTimer.setSingleShot(true);
    m_stateSaveTimer.setInterval(kStateSaveDelayMs);
    connect(&m_stateSaveTimer, &QTimer::timeout, this, &CodeEditorPlugin::saveWorkspaceState);

    return Utils::Result::success();
}

void CodeEditorPlugin::extensionsInitialized(ExtensionSystem::PluginManager& manager)
{
    qCInfo(ceditorlog) << "CodeEditorPlugin: extensionsInitialized";

    auto* uiHost = manager.getObject<Core::IUiHost>();
    if (!uiHost) {
        qCWarning(ceditorlog) << "CodeEditorPlugin: IUiHost not available";
    } else {
        if (m_service)
            m_service->setUiHost(uiHost);
        registerSidebarTool(uiHost);
    }

    auto* explorer = manager.getObject<ProjectExplorer::IProjectExplorer>();
    if (!explorer) {
        qCWarning(ceditorlog) << "CodeEditorPlugin: IProjectExplorer not available";
    } else {
        connectProjectExplorer(explorer);
    }
}

ExtensionSystem::IPlugin::ShutdownFlag CodeEditorPlugin::aboutToShutdown()
{
    qCInfo(ceditorlog) << "CodeEditorPlugin: aboutToShutdown";
    saveWorkspaceState();

    if (m_projectExplorer && m_projectActionRegistered) {
        m_projectExplorer->unregisterAction(kProjectOpenActionId);
        m_projectActionRegistered = false;
    }

    if (m_sidebarRegistry && m_sidebarRegistered) {
        QString error;
        if (!m_sidebarRegistry->unregisterTool(kSidebarToolId, &error))
            qCWarning(ceditorlog) << "CodeEditorPlugin: unregisterTool failed:" << error;
        m_sidebarRegistered = false;
    }

    if (m_service) {
        m_restoringWorkspaceState = true;
        m_service->closeAllFiles(CodeEditor::Api::CodeEditorCloseReason::Shutdown);
        m_restoringWorkspaceState = false;
        ExtensionSystem::PluginManager::removeObject(m_service);
        m_service = nullptr;
    }

    return ShutdownFlag::SynchronousShutdown;
}

void CodeEditorPlugin::registerSidebarTool(Core::IUiHost* uiHost)
{
    if (!uiHost || m_sidebarRegistered)
        return;

    m_sidebarRegistry = uiHost->sidebarRegistry();
    if (!m_sidebarRegistry) {
        qCWarning(ceditorlog) << "CodeEditorPlugin: ISidebarRegistry not available";
        return;
    }

    Core::SidebarToolSpec spec;
    spec.id = kSidebarToolId;
    spec.title = QStringLiteral("Code");
    spec.iconResource = QStringLiteral(":/ui/icons/svg/code_icon.svg");
    spec.side = Core::SidebarSide::Right;
    spec.family = Core::SidebarFamily::Vertical;
    spec.region = Core::SidebarRegion::Exclusive;
    spec.rail = Core::SidebarRail::Top;
    spec.order = 1;
    spec.toolTip = QStringLiteral("Code Editor");

    const auto factory = [this](QWidget* parent) -> QWidget* {
        return new CodeEditorPanel(m_service, parent);
    };

    QString error;
    if (!m_sidebarRegistry->registerTool(spec, factory, &error)) {
        qCWarning(ceditorlog) << "CodeEditorPlugin: registerTool failed:" << error;
        return;
    }

    m_sidebarRegistered = true;

    connect(m_sidebarRegistry, &Core::ISidebarRegistry::toolOpenStateChanged,
            this,
            [this](const QString& id, bool open) {
                Q_UNUSED(open);
                if (id != kSidebarToolId)
                    return;
                scheduleWorkspaceStateSave();
            });
}

void CodeEditorPlugin::connectProjectExplorer(ProjectExplorer::IProjectExplorer* explorer)
{
    if (!explorer)
        return;

    m_projectExplorer = explorer;
    m_workspaceRoot = Utils::PathUtils::normalizePath(explorer->rootPath());

    connect(explorer, &ProjectExplorer::IProjectExplorer::openRequested,
            this, &CodeEditorPlugin::handleProjectOpenRequested);
    connect(explorer, &ProjectExplorer::IProjectExplorer::contextActionRequested,
            this, &CodeEditorPlugin::handleProjectContextAction);
    connect(explorer, &ProjectExplorer::IProjectExplorer::entryRemoved,
            this, &CodeEditorPlugin::handleProjectEntryRemoved);
    connect(explorer, &ProjectExplorer::IProjectExplorer::entryRenamed,
            this, &CodeEditorPlugin::handleProjectEntryRenamed);

    connect(explorer, &ProjectExplorer::IProjectExplorer::workspaceRootChanged,
            this,
            [this](const QString& rootPath, bool userInitiated) {
                Q_UNUSED(userInitiated);
                saveWorkspaceState();
                if (!m_service)
                    return;

                m_restoringWorkspaceState = true;
                const Utils::Result closeResult =
                    m_service->closeAllFiles(CodeEditor::Api::CodeEditorCloseReason::WorkspaceChanged);
                if (!closeResult) {
                    qCWarning(ceditorlog).noquote()
                        << QStringLiteral("CodeEditorPlugin: failed to close files for workspace change: %1")
                               .arg(closeResult.errors.join(QStringLiteral("; ")));
                }
                m_restoringWorkspaceState = false;

                m_workspaceRoot = Utils::PathUtils::normalizePath(rootPath);
                restoreWorkspaceState();
            });

    ProjectExplorer::ProjectExplorerActionSpec action;
    action.id = kProjectOpenActionId;
    action.text = QStringLiteral("Open in Code");
    action.section = ProjectExplorer::ProjectExplorerActionSection::Primary;
    action.requiresItem = true;
    action.disallowRoot = true;

    explorer->registerAction(action);
    m_projectActionRegistered = true;

    restoreWorkspaceState();
}

QString CodeEditorPlugin::resolveAbsolutePath(const QString& path) const
{
    const QString cleaned = Utils::PathUtils::normalizePath(path);
    if (cleaned.isEmpty())
        return {};

    const QFileInfo info(cleaned);
    if (info.isAbsolute())
        return QDir::cleanPath(info.absoluteFilePath());

    if (!m_workspaceRoot.isEmpty())
        return QDir::cleanPath(QDir(m_workspaceRoot).absoluteFilePath(cleaned));

    return QDir::cleanPath(QFileInfo(cleaned).absoluteFilePath());
}

void CodeEditorPlugin::openInEditor(const QString& path, bool activateSession, bool revealSidebar)
{
    if (!m_service)
        return;

    const QString absolutePath = resolveAbsolutePath(path);
    if (absolutePath.isEmpty())
        return;

    if (Utils::DocumentBundle::hasBundleExtension(absolutePath))
        return;

    const QFileInfo info(absolutePath);
    if (!info.exists() || !info.isFile())
        return;

    CodeEditor::Api::CodeEditorOpenRequest request;
    request.filePath = absolutePath;
    request.activate = activateSession;
    request.readOnly = !info.isWritable();

    CodeEditor::Api::CodeEditorSessionHandle handle;
    const Utils::Result result = m_service->openFile(request, handle);
    if (!result) {
        qCWarning(ceditorlog).noquote()
            << QStringLiteral("CodeEditorPlugin: openFile failed for '%1': %2")
                   .arg(absolutePath, result.errors.join(QStringLiteral("; ")));
        return;
    }

    if (revealSidebar)
        showCodeSidebar();
}

bool CodeEditorPlugin::isSidebarOpen() const
{
    if (!m_sidebarRegistry || !m_sidebarRegistered)
        return false;
    return m_sidebarRegistry->isToolOpen(kSidebarToolId);
}

void CodeEditorPlugin::connectPersistenceSignals()
{
    if (!m_service)
        return;

    const auto schedule = [this]() { scheduleWorkspaceStateSave(); };

    connect(m_service, &CodeEditor::Api::ICodeEditorService::fileOpened,
            this, [schedule](const CodeEditor::Api::CodeEditorSessionHandle&) { schedule(); });
    connect(m_service, &CodeEditor::Api::ICodeEditorService::fileClosed,
            this, [schedule](const CodeEditor::Api::CodeEditorSessionHandle&, CodeEditor::Api::CodeEditorCloseReason) { schedule(); });
    connect(m_service, &CodeEditor::Api::ICodeEditorService::activeFileChanged,
            this, [schedule](const CodeEditor::Api::CodeEditorSessionHandle&) { schedule(); });
    connect(m_service, &CodeEditor::Api::ICodeEditorService::filePathChanged,
            this, [schedule](const CodeEditor::Api::CodeEditorSessionHandle&, const QString&, const QString&) { schedule(); });
    connect(m_service, &CodeEditor::Api::ICodeEditorService::zoomLevelChanged,
            this, [schedule](int) { schedule(); });
}

void CodeEditorPlugin::scheduleWorkspaceStateSave()
{
    if (m_restoringWorkspaceState)
        return;
    if (m_workspaceRoot.isEmpty())
        return;
    if (!m_stateSaveTimer.isActive())
        m_stateSaveTimer.start();
}

void CodeEditorPlugin::saveWorkspaceState()
{
    if (m_workspaceRoot.isEmpty() || !m_service)
        return;
    if (m_stateSaveTimer.isActive())
        m_stateSaveTimer.stop();

    CodeEditorWorkspaceState::Snapshot snapshot;
    snapshot.panelOpen = isSidebarOpen();
    snapshot.zoomLevel = m_service->zoomLevel();

    const QVector<CodeEditor::Api::CodeEditorSessionHandle> openHandles = m_service->openFiles();
    snapshot.openFiles.reserve(openHandles.size());
    for (const auto& handle : openHandles) {
        if (handle.filePath.trimmed().isEmpty())
            continue;
        snapshot.openFiles.push_back(resolveAbsolutePath(handle.filePath));
    }

    const auto activeHandle = m_service->activeFile();
    if (activeHandle.isValid())
        snapshot.activeFilePath = resolveAbsolutePath(activeHandle.filePath);

    m_workspaceState.saveForRoot(m_workspaceRoot, snapshot);
}

void CodeEditorPlugin::restoreWorkspaceState()
{
    if (m_workspaceRoot.isEmpty() || !m_service)
        return;

    const CodeEditorWorkspaceState::Snapshot snapshot = m_workspaceState.loadForRoot(m_workspaceRoot);

    m_restoringWorkspaceState = true;

    m_service->setZoomLevel(snapshot.zoomLevel);

    for (const QString& storedPath : snapshot.openFiles) {
        const QString absolutePath = resolveAbsolutePath(storedPath);
        if (absolutePath.isEmpty())
            continue;
        if (Utils::DocumentBundle::hasBundleExtension(absolutePath))
            continue;

        const QFileInfo info(absolutePath);
        if (!info.exists() || !info.isFile())
            continue;

        CodeEditor::Api::CodeEditorOpenRequest request;
        request.filePath = absolutePath;
        request.activate = false;
        request.readOnly = !info.isWritable();

        CodeEditor::Api::CodeEditorSessionHandle handle;
        const Utils::Result openResult = m_service->openFile(request, handle);
        if (!openResult) {
            qCWarning(ceditorlog).noquote()
                << QStringLiteral("CodeEditorPlugin: restore openFile failed for '%1': %2")
                       .arg(absolutePath, openResult.errors.join(QStringLiteral("; ")));
        }
    }

    if (!snapshot.activeFilePath.isEmpty()) {
        const QString activePath = resolveAbsolutePath(snapshot.activeFilePath);
        const QVector<CodeEditor::Api::CodeEditorSessionHandle> handles = m_service->openFiles();
        for (const auto& handle : handles) {
            if (QString::compare(resolveAbsolutePath(handle.filePath), activePath, kPathCase) != 0)
                continue;
            m_service->setActiveFile(handle);
            break;
        }
    }

    if (m_sidebarRegistry && m_sidebarRegistered) {
        if (snapshot.panelOpen)
            m_sidebarRegistry->requestShowTool(kSidebarToolId);
        else
            m_sidebarRegistry->requestHideTool(kSidebarToolId);
    }

    m_restoringWorkspaceState = false;
}

void CodeEditorPlugin::handleProjectOpenRequested(const QString& path, ProjectExplorer::ProjectEntryKind kind)
{
    if (!isOpenableProjectKind(kind))
        return;

    openInEditor(path, /*activateSession=*/true, /*revealSidebar=*/true);
}

void CodeEditorPlugin::handleProjectContextAction(const QString& actionId, const QString& path)
{
    if (actionId != kProjectOpenActionId)
        return;

    openInEditor(path, /*activateSession=*/true, /*revealSidebar=*/true);
}

void CodeEditorPlugin::handleProjectEntryRemoved(const QString& absolutePath,
                                                 ProjectExplorer::ProjectEntryKind kind)
{
    if (!m_service)
        return;

    const QString removedPath = resolveAbsolutePath(absolutePath);
    if (removedPath.isEmpty())
        return;

    const bool isFolderLike = (kind == ProjectExplorer::ProjectEntryKind::Folder
                               || kind == ProjectExplorer::ProjectEntryKind::Unknown);

    QVector<CodeEditor::Api::CodeEditorSessionHandle> toClose;
    const auto sessions = m_service->openFiles();
    for (const auto& session : sessions) {
        const QString sessionPath = resolveAbsolutePath(session.filePath);
        const bool exactMatch = (QString::compare(sessionPath, removedPath, kPathCase) == 0);
        const bool nestedMatch = isFolderLike && isPathInsidePrefix(sessionPath, removedPath);
        if (exactMatch || nestedMatch)
            toClose.push_back(session);
    }

    for (const auto& handle : toClose)
        m_service->closeFile(handle, CodeEditor::Api::CodeEditorCloseReason::FileDeleted);
}

void CodeEditorPlugin::handleProjectEntryRenamed(const QString& oldAbsolutePath,
                                                 const QString& newAbsolutePath,
                                                 ProjectExplorer::ProjectEntryKind kind)
{
    if (!m_service)
        return;

    const QString oldPath = resolveAbsolutePath(oldAbsolutePath);
    const QString newPath = resolveAbsolutePath(newAbsolutePath);
    if (oldPath.isEmpty() || newPath.isEmpty())
        return;

    const bool isFolderLike = (kind == ProjectExplorer::ProjectEntryKind::Folder
                               || kind == ProjectExplorer::ProjectEntryKind::Unknown);

    QString oldPrefix = oldPath;
    if (!oldPrefix.endsWith('/'))
        oldPrefix.append('/');

    const auto sessions = m_service->openFiles();
    for (const auto& session : sessions) {
        const QString sessionPath = resolveAbsolutePath(session.filePath);

        QString targetPath;
        if (QString::compare(sessionPath, oldPath, kPathCase) == 0) {
            targetPath = newPath;
        } else if (isFolderLike && sessionPath.startsWith(oldPrefix, kPathCase)) {
            const QString suffix = sessionPath.mid(oldPrefix.size());
            targetPath = QDir::cleanPath(QDir(newPath).filePath(suffix));
        }

        if (targetPath.isEmpty())
            continue;

        const Utils::Result update = m_service->updateFilePath(session, targetPath);
        if (!update) {
            qCWarning(ceditorlog).noquote()
                << QStringLiteral("CodeEditorPlugin: updateFilePath failed for '%1' -> '%2': %3")
                       .arg(session.filePath, targetPath, update.errors.join(QStringLiteral("; ")));
        }
    }
}

void CodeEditorPlugin::showCodeSidebar()
{
    if (m_sidebarRegistry)
        m_sidebarRegistry->requestShowTool(kSidebarToolId);
}

} // namespace CodeEditor::Internal

#include "CodeEditorPlugin.moc"
