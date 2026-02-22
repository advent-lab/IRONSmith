// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"
#include "projectexplorer/api/IProjectExplorer.hpp"

#include <QtCore/QPointer>
#include <QtCore/QString>

class QAbstractItemModel;
class QModelIndex;

namespace ProjectExplorer::Internal {

class ProjectExplorerModel;
class ProjectExplorerFilterModel;

class ProjectExplorerService final : public ProjectExplorer::IProjectExplorer
{
    Q_OBJECT

public:
    explicit ProjectExplorerService(QObject* parent = nullptr);

    QAbstractItemModel* model() const override;

    QString rootLabel() const override;
    void setRootLabel(const QString& label) override;

    void setEntries(const ProjectExplorer::ProjectEntryList& entries) override;
    ProjectExplorer::ProjectEntryList entries() const override;

    void selectPath(const QString& path) override;
    void revealPath(const QString& path) override;
    void refresh() override;
    void openRoot() override;

    void registerAction(const ProjectExplorer::ProjectExplorerActionSpec& spec) override;
    void unregisterAction(const QString& id) override;
    ProjectExplorer::ProjectExplorerActionList registeredActions() const override;

    void setRootPath(const QString& path, bool userInitiated = false);
    QString rootPath() const override;

    void notifyEntryRemoved(const QString& path, ProjectExplorer::ProjectEntryKind kind);
    void notifyEntryRenamed(const QString& oldPath,
                            const QString& newPath,
                            ProjectExplorer::ProjectEntryKind kind);

    void setSearchText(const QString& text);
    QString searchText() const;

    QString pathForIndex(const QModelIndex& index) const;
    QModelIndex indexForPath(const QString& path) const;
    ProjectExplorer::ProjectEntryKind entryKindForIndex(const QModelIndex& index) const;
    ProjectExplorer::ProjectEntryKind entryKindForPath(const QString& path) const;

    void requestOpen(const QModelIndex& index);
    void requestOpenPath(const QString& path);
    void requestSelectionChanged(const QModelIndex& index);
    void requestContextAction(const QString& id, const QModelIndex& index);
    void requestOpenRoot();

signals:
    void rootPathChanged(const QString& path, bool userInitiated);
    void rootLabelChanged(const QString& label);
    void entriesChanged(const ProjectExplorer::ProjectEntryList& entries);
    void openRootRequested();
    void revealPathRequested(const QString& path);
    void selectPathRequested(const QString& path);
    void refreshRequested();
    void actionsChanged();

private:
    QString absolutizePath(const QString& path) const;

    ProjectExplorerModel* m_model = nullptr;
    ProjectExplorerFilterModel* m_filter = nullptr;
    QString m_rootPath;
    QString m_searchText;
    ProjectExplorer::ProjectExplorerActionList m_registeredActions;
};

} // namespace ProjectExplorer::Internal
