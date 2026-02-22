// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "codeeditor/panels/CodeEditorPanel.hpp"

#include "codeeditor/CodeEditorTextView.hpp"
#include "codeeditor/api/ICodeEditorService.hpp"

#include <utils/ui/SidebarPanelFrame.hpp>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtGui/QIcon>
#include <QtGui/QKeySequence>
#include <QtGui/QShortcut>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>

#include <functional>

namespace CodeEditor::Internal {

namespace {

const QString kActionOpenFile = QStringLiteral("codeEditor.openFile");
const QString kActionSaveFile = QStringLiteral("codeEditor.saveFile");
const QString kActionSaveAllFiles = QStringLiteral("codeEditor.saveAllFiles");
const QString kActionCloseFile = QStringLiteral("codeEditor.closeFile");

const char kSessionIdProperty[] = "codeEditorSessionId";

class UiSyncScope final
{
public:
    explicit UiSyncScope(int& depth)
        : m_depth(depth)
    {
        ++m_depth;
    }

    ~UiSyncScope()
    {
        --m_depth;
    }

private:
    int& m_depth;
};

} // namespace

CodeEditorPanel::CodeEditorPanel(CodeEditor::Api::ICodeEditorService* service, QWidget* parent)
    : QWidget(parent)
    , m_service(service)
{
    setObjectName(QStringLiteral("CodeEditorPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_frame = new Utils::SidebarPanelFrame(this);
    if (auto* frameLayout = qobject_cast<QVBoxLayout*>(m_frame->layout())) {
        frameLayout->setContentsMargins(0, 0, 0, 0);
        frameLayout->setSpacing(0);
    }

    m_frame->setTitle(QStringLiteral("Code"));
    m_frame->setSubtitle(QString());
    m_frame->setSearchEnabled(false);
    m_frame->setHeaderDividerVisible(true);
    m_frame->addAction(kActionOpenFile,
                       QIcon(QStringLiteral(":/ui/icons/64x64/folder-new.png")),
                       QStringLiteral("Open File"));
    m_frame->addAction(kActionSaveFile,
                       QIcon(QStringLiteral(":/ui/icons/64x64/file-save.png")),
                       QStringLiteral("Save"));
    m_frame->addAction(kActionSaveAllFiles,
                       QIcon(QStringLiteral(":/ui/icons/64x64/file-save-as.png")),
                       QStringLiteral("Save All"));
    m_frame->addAction(kActionCloseFile,
                       QIcon(QStringLiteral(":/ui/icons/svg/close_icon.svg")),
                       QStringLiteral("Close"));
    connect(m_frame, &Utils::SidebarPanelFrame::actionTriggered,
            this, &CodeEditorPanel::handleActionTriggered);

    m_contentStack = new QStackedWidget(m_frame);
    m_contentStack->setObjectName(QStringLiteral("CodeEditorContentStack"));
    m_emptyLabel = new QLabel(QStringLiteral("No files open.\nOpen from Project Explorer or use Open File."),
                              m_contentStack);
    m_emptyLabel->setObjectName(QStringLiteral("CodeEditorEmptyState"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);

    m_tabs = new QTabWidget(m_contentStack);
    m_tabs->setObjectName(QStringLiteral("CodeEditorTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setElideMode(Qt::ElideMiddle);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setMovable(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setTabBarAutoHide(false);
    if (QTabBar* tabBar = m_tabs->tabBar()) {
        tabBar->setObjectName(QStringLiteral("CodeEditorTabBar"));
        tabBar->setExpanding(false);
    }

    connect(m_tabs, &QTabWidget::tabCloseRequested,
            this, &CodeEditorPanel::handleTabCloseRequested);
    connect(m_tabs, &QTabWidget::currentChanged,
            this, &CodeEditorPanel::handleCurrentTabChanged);

    m_contentStack->addWidget(m_emptyLabel);
    m_contentStack->addWidget(m_tabs);
    m_frame->setContentWidget(m_contentStack);

    rootLayout->addWidget(m_frame);

    if (m_service) {
        connect(m_service, &CodeEditor::Api::ICodeEditorService::fileOpened,
                this, &CodeEditorPanel::handleFileOpened);
        connect(m_service, &CodeEditor::Api::ICodeEditorService::fileClosed,
                this, &CodeEditorPanel::handleFileClosed);
        connect(m_service, &CodeEditor::Api::ICodeEditorService::activeFileChanged,
                this, &CodeEditorPanel::handleActiveFileChanged);
        connect(m_service, &CodeEditor::Api::ICodeEditorService::fileDirtyStateChanged,
                this, &CodeEditorPanel::handleDirtyStateChanged);
        connect(m_service, &CodeEditor::Api::ICodeEditorService::filePathChanged,
                this, &CodeEditorPanel::handleFilePathChanged);
    }

    setupShortcuts();
    refreshFromService();
}

CodeEditorPanel::~CodeEditorPanel()
{
    if (!m_tabs)
        return;

    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        QWidget* editor = m_tabs->widget(i);
        m_tabs->removeTab(i);
        if (editor)
            editor->setParent(nullptr);
    }
}

void CodeEditorPanel::handleActionTriggered(const QString& actionId)
{
    if (actionId == kActionOpenFile) {
        openFileWithDialog();
    } else if (actionId == kActionSaveFile) {
        saveActiveFile();
    } else if (actionId == kActionSaveAllFiles) {
        saveAllFiles();
    } else if (actionId == kActionCloseFile) {
        closeActiveTab();
    }
}

void CodeEditorPanel::handleTabCloseRequested(int index)
{
    if (!m_service)
        return;

    const auto handle = handleForTab(index);
    if (!handle.isValid())
        return;

    const Utils::Result result = m_service->closeFile(handle, CodeEditor::Api::CodeEditorCloseReason::UserClosed);
    if (!result)
        showError(QStringLiteral("Close File"), result);
}

void CodeEditorPanel::handleCurrentTabChanged(int index)
{
    if (m_syncingUiDepth > 0 || !m_service)
        return;

    const auto handle = handleForTab(index);
    if (!handle.isValid())
        return;

    const Utils::Result result = m_service->setActiveFile(handle);
    if (!result)
        showError(QStringLiteral("Set Active File"), result);
}

void CodeEditorPanel::handleFileOpened(const CodeEditor::Api::CodeEditorSessionHandle& handle)
{
    m_handlesBySessionId.insert(handle.id, handle);
    ensureTabForHandle(handle);
    refreshTabPresentation(handle);
    updateEmptyState();
}

void CodeEditorPanel::handleFileClosed(const CodeEditor::Api::CodeEditorSessionHandle& handle,
                                       CodeEditor::Api::CodeEditorCloseReason reason)
{
    Q_UNUSED(reason);
    removeTabForSession(handle.id);
    m_handlesBySessionId.remove(handle.id);
    updateEmptyState();
}

void CodeEditorPanel::handleActiveFileChanged(const CodeEditor::Api::CodeEditorSessionHandle& handle)
{
    if (handle.isValid())
        m_handlesBySessionId.insert(handle.id, handle);

    const int index = tabIndexForSession(handle.id);
    if (index >= 0) {
        UiSyncScope syncScope(m_syncingUiDepth);
        m_tabs->setCurrentIndex(index);

        if (QWidget* editor = m_tabs->widget(index)) {
            QTimer::singleShot(0, editor, [editor]() {
                if (editor->isVisible())
                    editor->setFocus(Qt::OtherFocusReason);
            });
        }
    }

    updateFrameSubtitle(handle);
}

void CodeEditorPanel::handleDirtyStateChanged(const CodeEditor::Api::CodeEditorSessionHandle& handle, bool dirty)
{
    Q_UNUSED(dirty);
    refreshTabPresentation(handle);

    if (!m_service)
        return;

    const auto active = m_service->activeFile();
    if (active.isValid() && active.id == handle.id)
        updateFrameSubtitle(active);
}

void CodeEditorPanel::handleFilePathChanged(const CodeEditor::Api::CodeEditorSessionHandle& handle,
                                            const QString& oldFilePath,
                                            const QString& newFilePath)
{
    Q_UNUSED(oldFilePath);
    Q_UNUSED(newFilePath);

    m_handlesBySessionId.insert(handle.id, handle);
    refreshTabPresentation(handle);

    if (!m_service)
        return;

    const auto active = m_service->activeFile();
    if (active.isValid() && active.id == handle.id)
        updateFrameSubtitle(active);
}

void CodeEditorPanel::openFileWithDialog()
{
    if (!m_service)
        return;

    QString initialDirectory = QDir::currentPath();
    if (const auto active = m_service->activeFile(); active.isValid())
        initialDirectory = QFileInfo(active.filePath).absolutePath();

    const QString selectedPath = QFileDialog::getOpenFileName(this,
                                                              QStringLiteral("Open File"),
                                                              initialDirectory);
    if (selectedPath.isEmpty())
        return;

    CodeEditor::Api::CodeEditorOpenRequest request;
    request.filePath = selectedPath;
    request.activate = true;

    CodeEditor::Api::CodeEditorSessionHandle handle;
    const Utils::Result result = m_service->openFile(request, handle);
    if (!result)
        showError(QStringLiteral("Open File"), result);
}

void CodeEditorPanel::closeActiveTab()
{
    if (!m_service)
        return;

    const auto active = m_service->activeFile();
    if (!active.isValid())
        return;

    const Utils::Result result = m_service->closeFile(active, CodeEditor::Api::CodeEditorCloseReason::UserClosed);
    if (!result)
        showError(QStringLiteral("Close File"), result);
}

void CodeEditorPanel::saveActiveFile()
{
    if (!m_service)
        return;

    const auto active = m_service->activeFile();
    if (!active.isValid())
        return;

    const Utils::Result result = m_service->saveFile(active);
    if (!result)
        showError(QStringLiteral("Save File"), result);
}

void CodeEditorPanel::saveAllFiles()
{
    if (!m_service)
        return;

    const Utils::Result result = m_service->saveAllFiles();
    if (!result)
        showError(QStringLiteral("Save All Files"), result);
}

void CodeEditorPanel::setupShortcuts()
{
    auto bindShortcuts = [this](const QList<QKeySequence>& sequences,
                                const std::function<void()>& handler) {
        QSet<QString> seen;
        for (const QKeySequence& sequence : sequences) {
            if (sequence.isEmpty())
                continue;

            const QString key = sequence.toString(QKeySequence::PortableText);
            if (key.isEmpty() || seen.contains(key))
                continue;
            seen.insert(key);

            auto* shortcut = new QShortcut(sequence, this);
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(shortcut, &QShortcut::activated, this, handler);
        }
    };

    QList<QKeySequence> openSequences = QKeySequence::keyBindings(QKeySequence::Open);
    if (openSequences.isEmpty())
        openSequences.push_back(QKeySequence(QStringLiteral("Ctrl+O")));
    bindShortcuts(openSequences, [this]() { openFileWithDialog(); });

    QList<QKeySequence> saveSequences = QKeySequence::keyBindings(QKeySequence::Save);
    if (saveSequences.isEmpty())
        saveSequences.push_back(QKeySequence(QStringLiteral("Ctrl+S")));
    bindShortcuts(saveSequences, [this]() { saveActiveFile(); });

    bindShortcuts({ QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                    QKeySequence(QStringLiteral("Meta+Shift+S")) },
                  [this]() { saveAllFiles(); });

    QList<QKeySequence> closeSequences = QKeySequence::keyBindings(QKeySequence::Close);
    if (closeSequences.isEmpty())
        closeSequences.push_back(QKeySequence(QStringLiteral("Ctrl+W")));
    bindShortcuts(closeSequences, [this]() { closeActiveTab(); });

    bindShortcuts({ QKeySequence(QStringLiteral("Ctrl+0")),
                    QKeySequence(QStringLiteral("Meta+0")) },
                  [this]() { resetActiveEditorZoom(); });
}

CodeEditor::CodeEditorTextView* CodeEditorPanel::activeEditorView() const
{
    if (!m_service)
        return nullptr;

    const auto active = m_service->activeFile();
    if (!active.isValid())
        return nullptr;

    return qobject_cast<CodeEditor::CodeEditorTextView*>(m_service->widgetForSession(active));
}

void CodeEditorPanel::zoomActiveEditorIn()
{
    if (auto* view = activeEditorView())
        view->zoomInEditor();
}

void CodeEditorPanel::zoomActiveEditorOut()
{
    if (auto* view = activeEditorView())
        view->zoomOutEditor();
}

void CodeEditorPanel::resetActiveEditorZoom()
{
    if (auto* view = activeEditorView())
        view->resetZoom();
}

void CodeEditorPanel::refreshFromService()
{
    UiSyncScope syncScope(m_syncingUiDepth);

    if (!m_service) {
        updateEmptyState();
        return;
    }

    const QVector<CodeEditor::Api::CodeEditorSessionHandle> sessions = m_service->openFiles();
    QSet<QString> activeIds;
    activeIds.reserve(sessions.size());

    for (const auto& handle : sessions) {
        if (!handle.isValid())
            continue;

        activeIds.insert(handle.id);
        m_handlesBySessionId.insert(handle.id, handle);
        ensureTabForHandle(handle);
        refreshTabPresentation(handle);
    }

    const QList<QString> knownIds = m_handlesBySessionId.keys();
    for (const QString& knownId : knownIds) {
        if (activeIds.contains(knownId))
            continue;

        removeTabForSession(knownId);
        m_handlesBySessionId.remove(knownId);
    }

    handleActiveFileChanged(m_service->activeFile());
    updateEmptyState();
}

void CodeEditorPanel::ensureTabForHandle(const CodeEditor::Api::CodeEditorSessionHandle& handle)
{
    if (!m_service || !handle.isValid())
        return;

    int index = tabIndexForSession(handle.id);
    if (index >= 0)
        return;

    QWidget* editorWidget = m_service->widgetForSession(handle);
    if (!editorWidget)
        return;

    if (editorWidget->parentWidget() && editorWidget->parentWidget() != m_tabs)
        editorWidget->setParent(nullptr);

    editorWidget->setProperty(kSessionIdProperty, handle.id);
    UiSyncScope syncScope(m_syncingUiDepth);
    index = m_tabs->addTab(editorWidget, displayNameForHandle(handle));
    m_tabs->setTabToolTip(index, handle.filePath);
}

void CodeEditorPanel::removeTabForSession(const QString& sessionId)
{
    const int index = tabIndexForSession(sessionId);
    if (index < 0)
        return;

    QWidget* editorWidget = m_tabs->widget(index);
    UiSyncScope syncScope(m_syncingUiDepth);
    m_tabs->removeTab(index);
    if (editorWidget)
        editorWidget->setParent(nullptr);
}

void CodeEditorPanel::refreshTabPresentation(const CodeEditor::Api::CodeEditorSessionHandle& handle)
{
    if (!m_service || !handle.isValid())
        return;

    const int index = tabIndexForSession(handle.id);
    if (index < 0)
        return;

    QString title = displayNameForHandle(handle);
    if (m_service->isDirty(handle))
        title.append(QStringLiteral(" *"));

    m_tabs->setTabText(index, title);

    QString tooltip = handle.filePath;
    if (handle.readOnly)
        tooltip.append(QStringLiteral("\nRead-only"));
    m_tabs->setTabToolTip(index, tooltip);
}

void CodeEditorPanel::updateFrameSubtitle(const CodeEditor::Api::CodeEditorSessionHandle& handle)
{
    if (!m_frame)
        return;

    if (!handle.isValid()) {
        m_frame->setSubtitle(QString());
        return;
    }

    QString subtitle = handle.filePath;
    if (m_service && m_service->isDirty(handle))
        subtitle.append(QStringLiteral(" (modified)"));
    m_frame->setSubtitle(subtitle);
}

void CodeEditorPanel::updateEmptyState()
{
    if (!m_contentStack || !m_tabs)
        return;

    if (m_tabs->count() == 0)
        m_contentStack->setCurrentWidget(m_emptyLabel);
    else
        m_contentStack->setCurrentWidget(m_tabs);
}

int CodeEditorPanel::tabIndexForSession(const QString& sessionId) const
{
    if (!m_tabs || sessionId.isEmpty())
        return -1;

    for (int i = 0; i < m_tabs->count(); ++i) {
        QWidget* editor = m_tabs->widget(i);
        if (!editor)
            continue;

        if (editor->property(kSessionIdProperty).toString() == sessionId)
            return i;
    }

    return -1;
}

CodeEditor::Api::CodeEditorSessionHandle CodeEditorPanel::handleForTab(int index) const
{
    if (!m_tabs || index < 0 || index >= m_tabs->count())
        return {};

    QWidget* editor = m_tabs->widget(index);
    if (!editor)
        return {};

    const QString sessionId = editor->property(kSessionIdProperty).toString();
    if (sessionId.isEmpty())
        return {};

    return m_handlesBySessionId.value(sessionId);
}

QString CodeEditorPanel::displayNameForHandle(const CodeEditor::Api::CodeEditorSessionHandle& handle) const
{
    const QFileInfo info(handle.filePath);
    const QString fileName = info.fileName();
    if (!fileName.isEmpty())
        return fileName;
    return handle.filePath;
}

void CodeEditorPanel::showError(const QString& title, const Utils::Result& result)
{
    const QString message = result.errors.isEmpty()
                                ? QStringLiteral("Unknown error.")
                                : result.errors.join(QStringLiteral("\n"));
    QMessageBox::warning(this, title, message);
}

} // namespace CodeEditor::Internal
