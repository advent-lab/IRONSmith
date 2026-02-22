// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "projectexplorer/filesystem/ProjectExplorerFileSystemController.hpp"

#include "projectexplorer/ProjectExplorerActions.hpp"
#include "projectexplorer/ProjectExplorerService.hpp"
#include "projectexplorer/filesystem/ProjectExplorerFileSystemService.hpp"

#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QUrl>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMessageBox>

#include <utils/ui/ConfirmationDialog.hpp>

namespace ProjectExplorer::Internal {

ProjectExplorerFileSystemController::ProjectExplorerFileSystemController(ProjectExplorerService* service,
                                                                         ProjectExplorerFileSystemService* fs,
                                                                         QObject* parent)
    : QObject(parent)
    , m_service(service)
    , m_fs(fs)
{
    if (m_fs) {
        connect(m_fs, &ProjectExplorerFileSystemService::operationFailed,
                this, &ProjectExplorerFileSystemController::showFailure);
    }
}

void ProjectExplorerFileSystemController::setDialogParent(QWidget* parent)
{
    m_dialogParent = parent;
}

bool ProjectExplorerFileSystemController::confirmDeletes() const
{
    return m_confirmDeletes;
}

void ProjectExplorerFileSystemController::setConfirmDeletes(bool enabled)
{
    if (m_confirmDeletes == enabled)
        return;
    m_confirmDeletes = enabled;
    emit confirmDeletesChanged(m_confirmDeletes);
}

void ProjectExplorerFileSystemController::handleRevealPath(const QString& relPath)
{
    if (m_fs)
        m_fs->revealPath(relPath);
}

void ProjectExplorerFileSystemController::handleContextAction(const QString& actionId, const QString& relPath)
{
    if (!m_fs)
        return;

    const auto actionOpt = ProjectExplorerActions::fromId(actionId);
    if (!actionOpt)
        return;

    const ProjectExplorerActions::Action action = *actionOpt;

    switch (action) {
    case ProjectExplorerActions::Action::Open:
        if (m_service)
            m_service->requestOpenPath(relPath);
        return;
    case ProjectExplorerActions::Action::Reveal:
        m_fs->revealPath(relPath);
        return;
    case ProjectExplorerActions::Action::Rename: {
        const QFileInfo fi(m_fs->rootPath().isEmpty() ? relPath
                                                      : QDir(m_fs->rootPath()).filePath(relPath));
        const QString current = fi.fileName();
        const QString name = promptForName(QStringLiteral("Rename"),
                                            QStringLiteral("New name:"),
                                            current);
        if (!name.isEmpty())
            m_fs->renamePath(relPath, name);
        return;
    }
    case ProjectExplorerActions::Action::Delete: {
        const QFileInfo fi(m_fs->rootPath().isEmpty() ? relPath
                                                      : QDir(m_fs->rootPath()).filePath(relPath));
        if (!fi.exists())
            return;
        if (!m_confirmDeletes || confirmDelete(fi.fileName(), fi.isDir()))
            m_fs->removePath(relPath);
        return;
    }
    case ProjectExplorerActions::Action::Duplicate:
        m_fs->duplicatePath(relPath);
        return;
    case ProjectExplorerActions::Action::NewFolder: {
        const QString name = promptForName(QStringLiteral("New Folder"),
                                            QStringLiteral("Folder name:"),
                                            QString());
        if (!name.isEmpty())
            m_fs->createFolder(relPath, name);
        return;
    }
    case ProjectExplorerActions::Action::NewDesign: {
        const QString name = promptForName(QStringLiteral("New Design"),
                                            QStringLiteral("Design name:"),
                                            QStringLiteral("untitled"));
        if (!name.isEmpty())
            m_fs->createDesign(relPath, name);
        return;
    }
    case ProjectExplorerActions::Action::ImportAsset: {
        const QString initialDir = m_fs->rootPath();
        const QStringList files = QFileDialog::getOpenFileNames(m_dialogParent,
                                                                QStringLiteral("Import Assets"),
                                                                initialDir);
        if (!files.isEmpty())
            m_fs->importAssets(relPath, files);
        return;
    }
    }
}

void ProjectExplorerFileSystemController::showFailure(ProjectExplorerFileSystemService::Operation,
                                                      const QString&,
                                                      const QString& error)
{
    QMessageBox::warning(m_dialogParent, QStringLiteral("Project Explorer"), error);
}

QString ProjectExplorerFileSystemController::promptForName(const QString& title,
                                                           const QString& label,
                                                           const QString& initial) const
{
    bool ok = false;
    const QString text = QInputDialog::getText(m_dialogParent, title, label,
                                               QLineEdit::Normal, initial, &ok);
    if (!ok)
        return {};
    return text.trimmed();
}

bool ProjectExplorerFileSystemController::confirmDelete(const QString& targetName, bool isFolder) const
{
    return Utils::ConfirmationDialog::confirmDelete(m_dialogParent, targetName, isFolder);
}

} // namespace ProjectExplorer::Internal
