// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/design/DesignOpenController.hpp"

#include "aieplugin/design/CanvasDocumentImporter.hpp"
#include "aieplugin/design/DesignBundleLoader.hpp"
#include "aieplugin/design/DesignModel.hpp"

#include "canvas/api/ICanvasDocumentService.hpp"
#include "projectexplorer/api/IProjectExplorer.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLoggingCategory>

Q_LOGGING_CATEGORY(aieopenlog, "ironsmith.aie.open")

namespace Aie::Internal {

DesignOpenController::DesignOpenController(DesignBundleLoader* loader,
                                           CanvasDocumentImporter* importer,
                                           Canvas::Api::ICanvasDocumentService* canvasDocuments,
                                           QObject* parent)
    : QObject(parent)
    , m_canvasDocuments(canvasDocuments)
    , m_loader(loader)
    , m_importer(importer)
{
}

void DesignOpenController::setProjectExplorer(ProjectExplorer::IProjectExplorer* explorer)
{
    if (m_explorer == explorer)
        return;

    if (m_explorer)
        disconnect(m_explorer, nullptr, this, nullptr);

    m_explorer = explorer;
    if (!m_explorer)
        return;

    connect(m_explorer, &ProjectExplorer::IProjectExplorer::openRequested,
            this, &DesignOpenController::handleOpenRequested);
    connect(m_explorer, &ProjectExplorer::IProjectExplorer::workspaceRootChanged,
            this, &DesignOpenController::handleWorkspaceRootChanged);
    connect(m_explorer, &ProjectExplorer::IProjectExplorer::entryRemoved,
            this, &DesignOpenController::handleEntryRemoved);
    connect(m_explorer, &ProjectExplorer::IProjectExplorer::entryRenamed,
            this, &DesignOpenController::handleEntryRenamed);
}

void DesignOpenController::openBundlePath(const QString& absolutePath)
{
    openBundleInternal(absolutePath);
}

void DesignOpenController::closeActiveDesign(Canvas::Api::CanvasDocumentCloseReason reason)
{
    if (!m_activeDocument.isValid())
        return;

    if (m_canvasDocuments) {
        const Utils::Result closeResult = m_canvasDocuments->closeDocument(m_activeDocument, reason);
        if (!closeResult) {
            qCWarning(aieopenlog).noquote()
                << "DesignOpenController: failed to close active canvas document:"
                << closeResult.errors.join("\n");
        }
    }

    const QString closedBundlePath = m_activeBundlePath;
    m_activeDocument = {};
    m_activeBundlePath.clear();
    emit designClosed(closedBundlePath);
}

QString DesignOpenController::activeBundlePath() const
{
    return m_activeBundlePath;
}

void DesignOpenController::handleOpenRequested(const QString& path, ProjectExplorer::ProjectEntryKind kind)
{
    if (kind != ProjectExplorer::ProjectEntryKind::Design)
        return;

    const QString absolute = resolveAbsolutePath(path);
    if (absolute.isEmpty()) {
        emit openFailed(QStringLiteral("Unable to resolve design path."));
        return;
    }

    openBundleInternal(absolute);
}

void DesignOpenController::handleWorkspaceRootChanged(const QString& rootPath, bool userInitiated)
{
    Q_UNUSED(userInitiated);

    if (!m_activeDocument.isValid() || m_activeBundlePath.isEmpty())
        return;

    const QString normalizedRoot = QDir::cleanPath(rootPath.trimmed());
    if (normalizedRoot.isEmpty()) {
        closeActiveDesign(Canvas::Api::CanvasDocumentCloseReason::WorkspaceChanged);
        return;
    }

    const QString normalizedBundle = QDir::cleanPath(m_activeBundlePath);
    if (normalizedBundle == normalizedRoot)
        return;

    const QString rootPrefix = normalizedRoot + QDir::separator();
    if (!normalizedBundle.startsWith(rootPrefix, Qt::CaseInsensitive))
        closeActiveDesign(Canvas::Api::CanvasDocumentCloseReason::WorkspaceChanged);
}

void DesignOpenController::handleEntryRemoved(const QString& absolutePath, ProjectExplorer::ProjectEntryKind kind)
{
    if (!m_activeDocument.isValid())
        return;
    if (kind != ProjectExplorer::ProjectEntryKind::Design
        && kind != ProjectExplorer::ProjectEntryKind::Folder
        && kind != ProjectExplorer::ProjectEntryKind::Unknown) {
        return;
    }

    if (isPathInsideActiveBundle(absolutePath))
        closeActiveDesign(Canvas::Api::CanvasDocumentCloseReason::BundleDeleted);
}

void DesignOpenController::handleEntryRenamed(const QString& oldAbsolutePath,
                                              const QString& newAbsolutePath,
                                              ProjectExplorer::ProjectEntryKind kind)
{
    if (!m_activeDocument.isValid())
        return;
    if (kind != ProjectExplorer::ProjectEntryKind::Design
        && kind != ProjectExplorer::ProjectEntryKind::Folder
        && kind != ProjectExplorer::ProjectEntryKind::Unknown) {
        return;
    }

    if (!isPathInsideActiveBundle(oldAbsolutePath))
        return;

    Q_UNUSED(newAbsolutePath);
    closeActiveDesign(Canvas::Api::CanvasDocumentCloseReason::WorkspaceChanged);
}

QString DesignOpenController::resolveAbsolutePath(const QString& relPath) const
{
    const QFileInfo info(relPath);
    if (info.isAbsolute())
        return QDir::cleanPath(info.absoluteFilePath());

    if (!m_explorer)
        return {};

    const QString root = m_explorer->rootPath();
    if (root.isEmpty())
        return {};
    return QDir::cleanPath(QDir(root).filePath(relPath));
}

void DesignOpenController::openBundleInternal(const QString& absolutePath)
{
    if (!m_loader || !m_importer || !m_canvasDocuments) {
        emit openFailed(QStringLiteral("Design loader is not available."));
        return;
    }

    if (m_activeDocument.isValid())
        closeActiveDesign(Canvas::Api::CanvasDocumentCloseReason::OpenReplaced);

    DesignModel model;
    const Utils::Result loadResult = m_loader->load(absolutePath, model);
    if (!loadResult) {
        emit openFailed(loadResult.errors.join("\n"));
        return;
    }

    const Utils::Result profileResult = m_importer->applyProfile(model.deviceId);
    if (!profileResult) {
        emit openFailed(profileResult.errors.join("\n"));
        return;
    }

    Canvas::Api::CanvasDocumentHandle handle;
    if (model.canvasPersistenceExists) {
        Canvas::Api::CanvasDocumentOpenRequest request;
        request.bundlePath = model.bundlePath;
        request.persistencePath = model.canvasPersistencePath;
        request.activate = true;

        const Utils::Result openResult = m_canvasDocuments->openDocument(request, handle);
        if (!openResult) {
            emit openFailed(openResult.errors.join("\n"));
            return;
        }
    } else {
        bool initializeFromCurrentCanvas = false;
        if (model.hasDesignState()) {
            const Utils::Result legacyResult =
                m_importer->importLegacyDesignState(model.legacyDesignState);
            if (!legacyResult) {
                emit openFailed(legacyResult.errors.join("\n"));
                return;
            }
            initializeFromCurrentCanvas = true;
        }

        Canvas::Api::CanvasDocumentCreateRequest request;
        request.bundlePath = model.bundlePath;
        request.persistenceRelativePath = model.canvasPersistenceRelativePath;
        request.activate = true;
        request.initializeFromCurrentCanvas = initializeFromCurrentCanvas;
        request.metadata = QJsonObject{
            { QStringLiteral("schema"), QStringLiteral("aie.spec/1") },
            { QStringLiteral("deviceFamily"), model.deviceFamily },
            { QStringLiteral("deviceId"), model.deviceId }
        };

        const Utils::Result createResult = m_canvasDocuments->createDocument(request, handle);
        if (!createResult) {
            emit openFailed(createResult.errors.join("\n"));
            return;
        }
    }

    m_activeDocument = handle;
    m_activeBundlePath = model.bundlePath;

    const QString displayName = QFileInfo(model.bundlePath).fileName();
    emit designOpened(model.bundlePath, displayName, model.deviceId);
}

bool DesignOpenController::isPathInsideActiveBundle(const QString& candidatePath) const
{
    if (m_activeBundlePath.trimmed().isEmpty() || candidatePath.trimmed().isEmpty())
        return false;

    const QString active = QDir::cleanPath(m_activeBundlePath);
    const QString candidate = QDir::cleanPath(candidatePath);
    if (active == candidate)
        return true;

    const QString activePrefix = active + QDir::separator();
    const QString candidatePrefix = candidate + QDir::separator();
    if (active.startsWith(candidatePrefix, Qt::CaseInsensitive))
        return true;
    if (candidate.startsWith(activePrefix, Qt::CaseInsensitive))
        return true;
    return false;
}

} // namespace Aie::Internal
