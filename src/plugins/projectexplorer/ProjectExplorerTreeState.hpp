// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QTimer>

class QModelIndex;
class QTreeView;

namespace ProjectExplorer::Internal {

class ProjectExplorerService;

class PROJECTEXPLORER_EXPORT ProjectExplorerTreeState final : public QObject
{
    Q_OBJECT

public:
    explicit ProjectExplorerTreeState(ProjectExplorerService* service, QObject* parent = nullptr);
    ProjectExplorerTreeState(ProjectExplorerService* service, Utils::Environment environment, QObject* parent = nullptr);

    void attach(QTreeView* view);
    void setRootPath(const QString& rootPath, bool userInitiated);
    void setSuspended(bool suspended);
    bool isSuspended() const;

private slots:
    void handleExpanded(const QModelIndex& index);
    void handleCollapsed(const QModelIndex& index);
    void handleModelReset();
    void flushSave();

private:
    static Utils::Environment makeEnvironment();

    void loadStateForRoot(const QString& rootPath);
    void apply();
    void scheduleSave();
    void saveState();

    QString pathForIndex(const QModelIndex& index) const;

    Utils::Environment m_env;
    QPointer<ProjectExplorerService> m_service;
    QPointer<QTreeView> m_view;
    QSet<QString> m_expanded;
    QString m_rootPath;
    bool m_rootExpanded = true;
    bool m_applying = false;
    bool m_suspended = false;
    QTimer m_saveTimer;
};

} // namespace ProjectExplorer::Internal
