// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/api/ProjectExplorerTypes.hpp"
#include "canvas/api/CanvasDocumentTypes.hpp"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

namespace ProjectExplorer { class IProjectExplorer; }
namespace Canvas::Api { class ICanvasDocumentService; }

namespace Aie::Internal {

class DesignBundleLoader;
class CanvasDocumentImporter;

class DesignOpenController final : public QObject
{
    Q_OBJECT

public:
    DesignOpenController(DesignBundleLoader* loader,
                         CanvasDocumentImporter* importer,
                         Canvas::Api::ICanvasDocumentService* canvasDocuments,
                         QObject* parent = nullptr);

    void setProjectExplorer(ProjectExplorer::IProjectExplorer* explorer);

    void openBundlePath(const QString& absolutePath);
    void closeActiveDesign(Canvas::Api::CanvasDocumentCloseReason reason);
    QString activeBundlePath() const;

signals:
    void designOpened(const QString& bundlePath, const QString& displayName, const QString& deviceId);
    void designClosed(const QString& bundlePath);
    void openFailed(const QString& message);

private:
    void handleOpenRequested(const QString& path, ProjectExplorer::ProjectEntryKind kind);
    void handleWorkspaceRootChanged(const QString& rootPath, bool userInitiated);
    void handleEntryRemoved(const QString& absolutePath, ProjectExplorer::ProjectEntryKind kind);
    void handleEntryRenamed(const QString& oldAbsolutePath,
                            const QString& newAbsolutePath,
                            ProjectExplorer::ProjectEntryKind kind);
    void openBundleInternal(const QString& absolutePath);
    QString resolveAbsolutePath(const QString& relPath) const;
    bool isPathInsideActiveBundle(const QString& candidatePath) const;

    QPointer<ProjectExplorer::IProjectExplorer> m_explorer;
    QPointer<Canvas::Api::ICanvasDocumentService> m_canvasDocuments;
    DesignBundleLoader* m_loader = nullptr;
    CanvasDocumentImporter* m_importer = nullptr;
    Canvas::Api::CanvasDocumentHandle m_activeDocument;
    QString m_activeBundlePath;
};

} // namespace Aie::Internal
