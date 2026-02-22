// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/kernels/KernelToolboxController.hpp"

#include "aieplugin/kernels/KernelAssignmentController.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"
#include "aieplugin/panels/KernelCreateDialog.hpp"

#include "codeeditor/api/CodeEditorTypes.hpp"
#include "codeeditor/api/ICodeEditorService.hpp"
#include "projectexplorer/api/IProjectExplorer.hpp"
#include "projectexplorer/api/ProjectExplorerTypes.hpp"

#include <QtCore/QDir>
#include <QtWidgets/QMessageBox>

namespace Aie::Internal {

namespace {

const QString kCreateKernelToolboxActionId = QStringLiteral("IRONSmith.Aie.CreateKernelToolboxEntry");

} // namespace

KernelToolboxController::KernelToolboxController(QObject* parent)
    : QObject(parent)
{
}

KernelToolboxController::~KernelToolboxController()
{
    unregisterProjectAction();
}

void KernelToolboxController::setRegistry(KernelRegistryService* registry)
{
    m_registry = registry;
}

void KernelToolboxController::setAssignments(KernelAssignmentController* assignments)
{
    m_assignments = assignments;
}

void KernelToolboxController::setProjectExplorer(ProjectExplorer::IProjectExplorer* explorer)
{
    if (m_projectExplorer == explorer)
        return;

    unregisterProjectAction();

    if (m_contextActionConnection)
        disconnect(m_contextActionConnection);

    m_projectExplorer = explorer;
    registerProjectAction();

    if (m_projectExplorer) {
        m_contextActionConnection = connect(m_projectExplorer,
                                            &ProjectExplorer::IProjectExplorer::contextActionRequested,
                                            this,
                                            [this](const QString& actionId, const QString&) {
                                                if (actionId != kCreateKernelToolboxActionId)
                                                    return;
                                                handleCreateToolboxAction();
                                            });
    }
}

void KernelToolboxController::setCodeEditorService(CodeEditor::Api::ICodeEditorService* codeEditorService)
{
    m_codeEditorService = codeEditorService;
}

void KernelToolboxController::setDialogParent(QWidget* parent)
{
    m_dialogParent = parent;
}

void KernelToolboxController::registerProjectAction()
{
    if (!m_projectExplorer || m_projectActionRegistered)
        return;

    ProjectExplorer::ProjectExplorerActionSpec action;
    action.id = kCreateKernelToolboxActionId;
    action.text = QStringLiteral("Create Kernel Toolbox Entry");
    action.section = ProjectExplorer::ProjectExplorerActionSection::Create;
    action.requiresItem = false;
    action.disallowRoot = false;

    m_projectExplorer->registerAction(action);
    m_projectActionRegistered = true;
}

void KernelToolboxController::unregisterProjectAction()
{
    if (!m_projectExplorer || !m_projectActionRegistered)
        return;

    m_projectExplorer->unregisterAction(kCreateKernelToolboxActionId);
    m_projectActionRegistered = false;
}

void KernelToolboxController::handleCreateToolboxAction()
{
    if (!m_registry)
        return;

    const bool workspaceAvailable = !m_registry->workspaceUserRoot().trimmed().isEmpty();
    KernelCreateDialog dialog(workspaceAvailable,
                              m_registry->workspaceUserRoot(),
                              m_registry->globalUserRoot(),
                              m_dialogParent);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const KernelCreateDialogResult dialogResult = dialog.result();

    KernelRegistryService::KernelCreateRequest request;
    request.id = dialogResult.id;
    request.name = dialogResult.name;
    request.signature = dialogResult.signature;
    request.description = dialogResult.description;
    request.tags = QStringList{QStringLiteral("custom")};

    KernelAsset createdKernel;
    const Utils::Result createResult = m_registry->createKernelInScope(request,
                                                                       dialogResult.scope,
                                                                       &createdKernel);
    if (!createResult) {
        QMessageBox::warning(m_dialogParent,
                             QStringLiteral("Create Kernel"),
                             createResult.errors.join(QStringLiteral("\n")));
        return;
    }

    if (m_assignments)
        m_assignments->setSelectedKernelId(createdKernel.id);

    if (m_projectExplorer) {
        m_projectExplorer->refresh();

        const QString workspaceRoot = m_registry->workspaceRoot().trimmed();
        if (dialogResult.scope == KernelSourceScope::Workspace
            && !workspaceRoot.isEmpty()
            && createdKernel.directoryPath.startsWith(workspaceRoot)) {
            const QString relPath = QDir(workspaceRoot).relativeFilePath(createdKernel.directoryPath);
            if (!relPath.startsWith(QStringLiteral("..")))
                m_projectExplorer->selectPath(relPath);
        }
    }

    if (dialogResult.openInEditor)
        openKernelInEditor(createdKernel.absoluteEntryPath());
}

void KernelToolboxController::openKernelInEditor(const QString& filePath)
{
    if (!m_codeEditorService || filePath.trimmed().isEmpty())
        return;

    CodeEditor::Api::CodeEditorOpenRequest request;
    request.filePath = filePath;
    request.activate = true;
    request.readOnly = false;

    CodeEditor::Api::CodeEditorSessionHandle handle;
    const Utils::Result openResult = m_codeEditorService->openFile(request, handle);
    if (!openResult) {
        QMessageBox::warning(m_dialogParent,
                             QStringLiteral("Code Editor"),
                             openResult.errors.join(QStringLiteral("\n")));
    }
}

} // namespace Aie::Internal

