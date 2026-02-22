// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"
#include "projectexplorer/api/ProjectExplorerTypes.hpp"

#include <QtCore/QObject>

class QAbstractItemModel;

namespace ProjectExplorer {

class PROJECTEXPLORER_EXPORT IProjectExplorer : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~IProjectExplorer() override = default;

    virtual QAbstractItemModel* model() const = 0;
    virtual QString rootLabel() const = 0;
    virtual void setRootLabel(const QString& label) = 0;
    virtual void setEntries(const ProjectEntryList& entries) = 0;
    virtual ProjectEntryList entries() const = 0;

    virtual void selectPath(const QString& path) = 0;
    virtual void revealPath(const QString& path) = 0;
    virtual void refresh() = 0;
    virtual void openRoot() = 0;

    virtual void registerAction(const ProjectExplorerActionSpec& spec) = 0;
    virtual void unregisterAction(const QString& id) = 0;
    virtual ProjectExplorerActionList registeredActions() const = 0;
    virtual QString rootPath() const = 0;

signals:
    void workspaceRootChanged(const QString& rootPath, bool userInitiated);
    void entryRemoved(const QString& absolutePath, ProjectExplorer::ProjectEntryKind kind);
    void entryRenamed(const QString& oldAbsolutePath,
                      const QString& newAbsolutePath,
                      ProjectExplorer::ProjectEntryKind kind);
    void openRequested(const QString& path, ProjectExplorer::ProjectEntryKind kind);
    void entryActivated(const QString& path);
    void selectionChanged(const QString& path);
    void contextActionRequested(const QString& actionId, const QString& path);
    void selectPathRequested(const QString& path);
    void revealPathRequested(const QString& path);
    void refreshRequested();
    void openRootRequested();
    void actionsChanged();
};

} // namespace ProjectExplorer
