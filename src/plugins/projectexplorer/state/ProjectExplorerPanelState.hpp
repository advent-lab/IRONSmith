// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QString>

class QTreeView;
class QModelIndex;

namespace Utils {
class SidebarPanelFrame;
}

namespace ProjectExplorer::Internal {

class ProjectExplorerService;

class PROJECTEXPLORER_EXPORT ProjectExplorerPanelState final : public QObject
{
    Q_OBJECT

public:
    explicit ProjectExplorerPanelState(ProjectExplorerService* service, QObject* parent = nullptr);
    ProjectExplorerPanelState(ProjectExplorerService* service, Utils::Environment environment, QObject* parent = nullptr);

    void attach(QTreeView* view, Utils::SidebarPanelFrame* frame);
    void setRootPath(const QString& rootPath);

private slots:
    void handleSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void handleViewSelected(const QString& viewId);
    void handleModelReset();
    void flushSave();

private:
    static Utils::Environment makeEnvironment();

    void loadStateForRoot(const QString& rootPath);
    void applySelection();
    void applyView();
    void scheduleSave();
    void saveState();

    Utils::Environment m_env;
    QPointer<ProjectExplorerService> m_service;
    QPointer<QTreeView> m_view;
    QPointer<Utils::SidebarPanelFrame> m_frame;
    QTimer m_saveTimer;

    QString m_rootPath;
    QString m_selectedPath;
    QString m_pendingSelection;
    QString m_viewId;
    bool m_applying = false;
};

} // namespace ProjectExplorer::Internal
