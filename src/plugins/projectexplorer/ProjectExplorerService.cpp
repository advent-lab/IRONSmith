// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "projectexplorer/ProjectExplorerService.hpp"

#include "projectexplorer/ProjectExplorerFilterModel.hpp"
#include "projectexplorer/ProjectExplorerModel.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QModelIndex>
#include <QtCore/QSet>

namespace ProjectExplorer::Internal {

ProjectExplorerService::ProjectExplorerService(QObject* parent)
    : ProjectExplorer::IProjectExplorer(parent)
{
    m_model = new ProjectExplorerModel(this);
    m_filter = new ProjectExplorerFilterModel(this);
    m_filter->setSourceModel(m_model);
    m_searchText.clear();
    m_filter->setFilterText(QString());
}

QAbstractItemModel* ProjectExplorerService::model() const
{
    return m_filter;
}

QString ProjectExplorerService::rootLabel() const
{
    return m_model->rootLabel();
}

void ProjectExplorerService::setRootLabel(const QString& label)
{
    if (!m_model)
        return;
    const QString before = m_model->rootLabel();
    m_model->setRootLabel(label);
    const QString after = m_model->rootLabel();
    if (before != after)
        emit rootLabelChanged(after);
}

void ProjectExplorerService::setEntries(const ProjectExplorer::ProjectEntryList& entries)
{
    const ProjectExplorer::ProjectEntryList previous = m_model->entries();
    m_model->setEntries(entries);
    emit entriesChanged(entries);

    QHash<QString, ProjectExplorer::ProjectEntryKind> previousByPath;
    previousByPath.reserve(previous.size());
    for (const auto& entry : previous)
        previousByPath.insert(entry.path, entry.kind);

    QSet<QString> currentPaths;
    currentPaths.reserve(entries.size());
    for (const auto& entry : entries)
        currentPaths.insert(entry.path);

    for (auto it = previousByPath.cbegin(); it != previousByPath.cend(); ++it) {
        if (currentPaths.contains(it.key()))
            continue;
        emit entryRemoved(absolutizePath(it.key()), it.value());
    }
}

ProjectExplorer::ProjectEntryList ProjectExplorerService::entries() const
{
    return m_model->entries();
}

void ProjectExplorerService::selectPath(const QString& path)
{
    const QString cleaned = path.trimmed();
    if (cleaned.isEmpty())
        return;
    emit selectPathRequested(cleaned);
}

void ProjectExplorerService::revealPath(const QString& path)
{
    const QString cleaned = path.trimmed();
    if (cleaned.isEmpty())
        return;
    emit revealPathRequested(cleaned);
}

void ProjectExplorerService::refresh()
{
    emit refreshRequested();
}

void ProjectExplorerService::openRoot()
{
    emit openRootRequested();
}

void ProjectExplorerService::registerAction(const ProjectExplorer::ProjectExplorerActionSpec& spec)
{
    const QString id = spec.id.trimmed();
    if (id.isEmpty())
        return;

    for (auto& action : m_registeredActions) {
        if (action.id == id) {
            action = spec;
            emit actionsChanged();
            return;
        }
    }

    m_registeredActions.push_back(spec);
    emit actionsChanged();
}

void ProjectExplorerService::unregisterAction(const QString& id)
{
    const QString cleaned = id.trimmed();
    if (cleaned.isEmpty())
        return;

    for (int i = 0; i < m_registeredActions.size(); ++i) {
        if (m_registeredActions.at(i).id == cleaned) {
            m_registeredActions.removeAt(i);
            emit actionsChanged();
            return;
        }
    }
}

ProjectExplorer::ProjectExplorerActionList ProjectExplorerService::registeredActions() const
{
    return m_registeredActions;
}

void ProjectExplorerService::setRootPath(const QString& path, bool userInitiated)
{
    const QString cleaned = path.trimmed();
    if (cleaned == m_rootPath)
        return;

    m_rootPath = cleaned;
    if (m_model)
        m_model->setRootPath(m_rootPath);
    emit rootPathChanged(m_rootPath, userInitiated);
    emit workspaceRootChanged(m_rootPath, userInitiated);
}

QString ProjectExplorerService::rootPath() const
{
    return m_rootPath;
}

void ProjectExplorerService::notifyEntryRemoved(const QString& path, ProjectExplorer::ProjectEntryKind kind)
{
    const QString cleaned = path.trimmed();
    if (cleaned.isEmpty())
        return;
    emit entryRemoved(absolutizePath(cleaned), kind);
}

void ProjectExplorerService::notifyEntryRenamed(const QString& oldPath,
                                                const QString& newPath,
                                                ProjectExplorer::ProjectEntryKind kind)
{
    const QString oldCleaned = oldPath.trimmed();
    const QString newCleaned = newPath.trimmed();
    if (oldCleaned.isEmpty() || newCleaned.isEmpty())
        return;
    emit entryRenamed(absolutizePath(oldCleaned), absolutizePath(newCleaned), kind);
}

void ProjectExplorerService::setSearchText(const QString& text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    m_filter->setFilterText(text);
}

QString ProjectExplorerService::searchText() const
{
    return m_searchText;
}

QString ProjectExplorerService::pathForIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};
    return index.data(ProjectExplorerModel::PathRole).toString();
}

QModelIndex ProjectExplorerService::indexForPath(const QString& path) const
{
    if (!m_model || !m_filter)
        return {};

    const QModelIndex source = m_model->indexForPath(path);
    if (!source.isValid())
        return {};
    return m_filter->mapFromSource(source);
}

ProjectExplorer::ProjectEntryKind ProjectExplorerService::entryKindForIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return ProjectExplorer::ProjectEntryKind::Unknown;

    const int kind = index.data(ProjectExplorerModel::KindRole).toInt();
    switch (static_cast<ProjectExplorerModel::NodeKind>(kind)) {
        case ProjectExplorerModel::NodeKind::Root:
        case ProjectExplorerModel::NodeKind::Folder:
            return ProjectExplorer::ProjectEntryKind::Folder;
        case ProjectExplorerModel::NodeKind::Design:
            return ProjectExplorer::ProjectEntryKind::Design;
        case ProjectExplorerModel::NodeKind::Asset:
            return ProjectExplorer::ProjectEntryKind::Asset;
        default:
            return ProjectExplorer::ProjectEntryKind::Unknown;
    }
}

ProjectExplorer::ProjectEntryKind ProjectExplorerService::entryKindForPath(const QString& path) const
{
    const QModelIndex idx = indexForPath(path);
    return entryKindForIndex(idx);
}

void ProjectExplorerService::requestOpen(const QModelIndex& index)
{
    const QString path = pathForIndex(index);
    if (path.isEmpty())
        return;

    const auto kind = entryKindForIndex(index);
    emit openRequested(path, kind);
    if (kind != ProjectExplorer::ProjectEntryKind::Folder)
        emit entryActivated(path);
}

void ProjectExplorerService::requestOpenPath(const QString& path)
{
    const QString cleaned = path.trimmed();
    if (cleaned.isEmpty())
        return;

    const auto kind = entryKindForPath(cleaned);
    emit openRequested(cleaned, kind);
    if (kind != ProjectExplorer::ProjectEntryKind::Folder)
        emit entryActivated(cleaned);
}

void ProjectExplorerService::requestSelectionChanged(const QModelIndex& index)
{
    const QString path = pathForIndex(index);
    emit selectionChanged(path);
}

void ProjectExplorerService::requestContextAction(const QString& id, const QModelIndex& index)
{
    emit contextActionRequested(id, pathForIndex(index));
}

void ProjectExplorerService::requestOpenRoot()
{
    openRoot();
}

QString ProjectExplorerService::absolutizePath(const QString& path) const
{
    const QString cleaned = path.trimmed();
    if (cleaned.isEmpty())
        return {};

    const QFileInfo info(cleaned);
    if (info.isAbsolute())
        return QDir::cleanPath(info.absoluteFilePath());
    if (m_rootPath.isEmpty())
        return QDir::cleanPath(cleaned);
    return QDir::cleanPath(QDir(m_rootPath).filePath(cleaned));
}

} // namespace ProjectExplorer::Internal
