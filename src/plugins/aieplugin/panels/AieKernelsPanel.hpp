// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/kernels/KernelCatalog.hpp"
#include "aieplugin/state/AieKernelsPanelState.hpp"

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QString>

#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QFrame;
class QLineEdit;
class QRadioButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QWidget;
QT_END_NAMESPACE

namespace Utils {
class SidebarPanelFrame;
}

namespace CodeEditor::Api {
class ICodeEditorService;
}

namespace Aie::Internal {

class AieOutputLog;
class KernelAssignmentController;
class KernelRegistryService;

class AieKernelsPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit AieKernelsPanel(KernelRegistryService* registry,
                             KernelAssignmentController* assignments,
                             CodeEditor::Api::ICodeEditorService* codeEditorService,
                             AieOutputLog* outputLog,
                             QWidget* parent = nullptr);

    void setCodeEditorService(CodeEditor::Api::ICodeEditorService* service);
    void setOutputLog(AieOutputLog* log);

private:
    struct CardRefs final {
        QString kernelId;
        QPointer<QFrame> frame;
        QPointer<QRadioButton> selectButton;
        QPointer<QToolButton> previewButton;
        QPointer<QToolButton> menuButton;
    };

    void buildUi();
    void refreshCards();
    bool kernelMatchesFilter(const KernelAsset& kernel, const QString& filterText) const;

    CardRefs createCard(const KernelAsset& kernel);
    void showPreviewDialog(const QString& kernelId, bool openMetadataTab = false);

    void setSelectedKernel(const QString& kernelId);
    void syncSelectionFromAssignments();
    void syncCardHighlightState();
    void loadPersistedState();
    void savePersistedState();

    void openKernelInEditor(const QString& kernelId, bool forceReadOnly);
    void copyKernelToScope(const QString& kernelId,
                           KernelSourceScope scope,
                           bool openCopiedKernelInEditor);

    const KernelAsset* kernelById(const QString& kernelId) const;
    QString workspaceRootForPersistence() const;
    QScrollArea* cardsScrollArea() const;

    QPointer<KernelRegistryService> m_registry;
    QPointer<KernelAssignmentController> m_assignments;
    QPointer<CodeEditor::Api::ICodeEditorService> m_codeEditorService;
    QPointer<AieOutputLog> m_outputLog;
    AieKernelsPanelState m_persistedState;

    QPointer<QLineEdit> m_searchField;
    QPointer<QVBoxLayout> m_cardsLayout;
    QPointer<QButtonGroup> m_selectionGroup;

    QVector<CardRefs> m_cards;
};

} // namespace Aie::Internal
