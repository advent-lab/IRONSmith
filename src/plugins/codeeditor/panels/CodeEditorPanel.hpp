// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "codeeditor/CodeEditorGlobal.hpp"
#include "codeeditor/api/CodeEditorTypes.hpp"

#include <utils/Result.hpp>

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

namespace Utils {
class SidebarPanelFrame;
}

QT_BEGIN_NAMESPACE
class QLabel;
class QStackedWidget;
class QTabWidget;
QT_END_NAMESPACE

namespace CodeEditor::Api {
class ICodeEditorService;
}

namespace CodeEditor {
class CodeEditorTextView;
}

namespace CodeEditor::Internal {

class CODEEDITOR_EXPORT CodeEditorPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit CodeEditorPanel(CodeEditor::Api::ICodeEditorService* service, QWidget* parent = nullptr);
    ~CodeEditorPanel() override;

private:
    void handleActionTriggered(const QString& actionId);
    void handleTabCloseRequested(int index);
    void handleCurrentTabChanged(int index);

    void handleFileOpened(const CodeEditor::Api::CodeEditorSessionHandle& handle);
    void handleFileClosed(const CodeEditor::Api::CodeEditorSessionHandle& handle,
                          CodeEditor::Api::CodeEditorCloseReason reason);
    void handleActiveFileChanged(const CodeEditor::Api::CodeEditorSessionHandle& handle);
    void handleDirtyStateChanged(const CodeEditor::Api::CodeEditorSessionHandle& handle, bool dirty);
    void handleFilePathChanged(const CodeEditor::Api::CodeEditorSessionHandle& handle,
                               const QString& oldFilePath,
                               const QString& newFilePath);

    void openFileWithDialog();
    void closeActiveTab();
    void saveActiveFile();
    void saveAllFiles();
    void setupShortcuts();
    CodeEditor::CodeEditorTextView* activeEditorView() const;
    void zoomActiveEditorIn();
    void zoomActiveEditorOut();
    void resetActiveEditorZoom();

    void refreshFromService();
    void ensureTabForHandle(const CodeEditor::Api::CodeEditorSessionHandle& handle);
    void removeTabForSession(const QString& sessionId);
    void refreshTabPresentation(const CodeEditor::Api::CodeEditorSessionHandle& handle);
    void updateFrameSubtitle(const CodeEditor::Api::CodeEditorSessionHandle& handle);
    void updateEmptyState();

    int tabIndexForSession(const QString& sessionId) const;
    CodeEditor::Api::CodeEditorSessionHandle handleForTab(int index) const;

    QString displayNameForHandle(const CodeEditor::Api::CodeEditorSessionHandle& handle) const;

    void showError(const QString& title, const Utils::Result& result);

    QPointer<CodeEditor::Api::ICodeEditorService> m_service;

    Utils::SidebarPanelFrame* m_frame = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QTabWidget* m_tabs = nullptr;

    QHash<QString, CodeEditor::Api::CodeEditorSessionHandle> m_handlesBySessionId;
    int m_syncingUiDepth = 0;
};

} // namespace CodeEditor::Internal
