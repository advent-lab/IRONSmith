// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"

#include "projectexplorer/filesystem/ProjectExplorerFileSystemService.hpp"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

class QWidget;

namespace ProjectExplorer::Internal {

class ProjectExplorerService;

class ProjectExplorerFileSystemController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool confirmDeletes READ confirmDeletes WRITE setConfirmDeletes NOTIFY confirmDeletesChanged)

public:
    explicit ProjectExplorerFileSystemController(ProjectExplorerService* service,
                                                 ProjectExplorerFileSystemService* fs,
                                                 QObject* parent = nullptr);

    void setDialogParent(QWidget* parent);

    bool confirmDeletes() const;
    void setConfirmDeletes(bool enabled);

public slots:
    void handleContextAction(const QString& actionId, const QString& relPath);
    void handleRevealPath(const QString& relPath);

signals:
    void confirmDeletesChanged(bool enabled);

private slots:
    void showFailure(ProjectExplorerFileSystemService::Operation op, const QString& path, const QString& error);

private:
    QString promptForName(const QString& title, const QString& label, const QString& initial) const;
    bool confirmDelete(const QString& targetName, bool isFolder) const;

    QPointer<ProjectExplorerService> m_service;
    QPointer<ProjectExplorerFileSystemService> m_fs;
    QPointer<QWidget> m_dialogParent;
    bool m_confirmDeletes = true;
};

} // namespace ProjectExplorer::Internal
