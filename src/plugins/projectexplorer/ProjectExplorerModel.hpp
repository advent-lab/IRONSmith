// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"
#include "projectexplorer/api/ProjectExplorerTypes.hpp"
#include "projectexplorer/ProjectExplorerIconProvider.hpp"

#include <utils/TreeIndex.hpp>
#include <utils/VirtualPath.hpp>

#include <QtCore/QAbstractItemModel>
#include <QtCore/QMap>
#include <QtCore/QString>

namespace ProjectExplorer::Internal {

class PROJECTEXPLORER_EXPORT ProjectExplorerModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        KindRole,
        IsFolderRole,
        RootPathRole
    };

    enum class NodeKind : unsigned char {
        Root,
        Folder,
        Design,
        Asset,
        Meta,
        Cache,
        File
    };

    struct NodeData final {
        QString name;
        Utils::VirtualPath path;
        NodeKind kind = NodeKind::File;
    };

    explicit ProjectExplorerModel(QObject* parent = nullptr);

    void setRootLabel(const QString& label);
    QString rootLabel() const;

    void setRootPath(const QString& path);
    QString rootPath() const;

    void setEntries(const ProjectExplorer::ProjectEntryList& entries);
    ProjectExplorer::ProjectEntryList entries() const;

    QModelIndex indexForPath(const QString& path) const;

    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    void rebuildTree();
    static NodeKind mapEntryKind(const ProjectExplorer::ProjectEntry& entry, const Utils::VirtualPath& path);
    const Utils::TreeIndex<NodeData>::Node* nodeFromIndex(const QModelIndex& index) const;

    Utils::TreeIndex<NodeData> m_tree;
    QMap<QString, Utils::TreeNodeId> m_pathIndex;
    QString m_rootLabel;
    QString m_rootPath;
    ProjectExplorer::ProjectEntryList m_entries;
    ProjectExplorerIconProvider m_iconProvider;
};

} // namespace ProjectExplorer::Internal
