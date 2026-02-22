// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace ProjectExplorer {
class IProjectExplorer;
}

namespace CodeEditor::Api {
class ICodeEditorService;
}

namespace Aie::Internal {

class KernelAssignmentController;
class KernelRegistryService;

class KernelToolboxController final : public QObject
{
    Q_OBJECT

public:
    explicit KernelToolboxController(QObject* parent = nullptr);
    ~KernelToolboxController() override;

    void setRegistry(KernelRegistryService* registry);
    void setAssignments(KernelAssignmentController* assignments);
    void setProjectExplorer(ProjectExplorer::IProjectExplorer* explorer);
    void setCodeEditorService(CodeEditor::Api::ICodeEditorService* codeEditorService);
    void setDialogParent(QWidget* parent);

private:
    void registerProjectAction();
    void unregisterProjectAction();
    void handleCreateToolboxAction();
    void openKernelInEditor(const QString& filePath);

    QPointer<KernelRegistryService> m_registry;
    QPointer<KernelAssignmentController> m_assignments;
    QPointer<ProjectExplorer::IProjectExplorer> m_projectExplorer;
    QPointer<CodeEditor::Api::ICodeEditorService> m_codeEditorService;
    QPointer<QWidget> m_dialogParent;

    QMetaObject::Connection m_contextActionConnection;
    bool m_projectActionRegistered = false;
};

} // namespace Aie::Internal

