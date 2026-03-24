// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/AieKernelsPanel.hpp"

#include "aieplugin/hlir_sync/AieOutputLog.hpp"
#include "aieplugin/kernels/KernelAssignmentController.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"
#include "aieplugin/panels/KernelPreviewDialog.hpp"
#include "aieplugin/panels/KernelPreviewUtils.hpp"

#include "codeeditor/api/CodeEditorTypes.hpp"
#include "codeeditor/api/ICodeEditorService.hpp"

#include <utils/Result.hpp>
#include <utils/ui/SidebarPanelFrame.hpp>

#include <QtCore/QSize>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtCore/QtGlobal>

#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <QtGui/QShortcut>

namespace Aie::Internal {

namespace {

QString cleanFilter(const QString& text)
{
    return text.trimmed().toLower();
}

QString fallbackSnippet(const KernelAsset& kernel)
{
    if (!kernel.signature.trimmed().isEmpty())
        return kernel.signature.trimmed();
    if (!kernel.description.trimmed().isEmpty())
        return kernel.description.trimmed();
    return QStringLiteral("No signature available.");
}

} // namespace

AieKernelsPanel::AieKernelsPanel(KernelRegistryService* registry,
                                 KernelAssignmentController* assignments,
                                 CodeEditor::Api::ICodeEditorService* codeEditorService,
                                 AieOutputLog* outputLog,
                                 QWidget* parent)
    : QWidget(parent)
    , m_registry(registry)
    , m_assignments(assignments)
    , m_outputLog(outputLog)
    , m_codeEditorService(codeEditorService)
{
    setObjectName(QStringLiteral("AieKernelsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    buildUi();

    if (m_registry) {
        connect(m_registry, &KernelRegistryService::kernelsChanged,
                this, &AieKernelsPanel::loadPersistedState);
        connect(m_registry, &KernelRegistryService::warningsUpdated, this,
                [this](const QStringList& warnings) {
                    if (warnings.isEmpty())
                        return;
                    QMessageBox::warning(this,
                                         QStringLiteral("Kernels Catalog"),
                                         warnings.join(QStringLiteral("\n")));
                });
    }

    if (m_assignments) {
        connect(m_assignments, &KernelAssignmentController::selectedKernelChanged,
                this, [this](const QString&) {
                    syncSelectionFromAssignments();
                    savePersistedState();
                });
        connect(m_assignments, &KernelAssignmentController::assignmentFailed, this,
                [this](const QString& message) {
                    if (!message.trimmed().isEmpty() && m_outputLog)
                        m_outputLog->addEntry(false, message);
                });
    }

    if (m_registry)
        m_registry->reload();
    loadPersistedState();
}

void AieKernelsPanel::setCodeEditorService(CodeEditor::Api::ICodeEditorService* service)
{
    m_codeEditorService = service;
}

void AieKernelsPanel::setOutputLog(AieOutputLog* log)
{
    m_outputLog = log;
}

void AieKernelsPanel::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* frame = new Utils::SidebarPanelFrame(this);
    frame->setTitle(QStringLiteral("Kernels"));
    frame->setSubtitle(QString());
    frame->setSearchEnabled(true);
    frame->setSearchPlaceholder(QStringLiteral("Search kernels"));
    frame->setHeaderDividerVisible(true);
    m_searchField = frame->searchField();

    QIcon listIcon(QStringLiteral(":/ui/icons/svg/list_view_icon.svg"));
    if (listIcon.isNull())
        listIcon = style()->standardIcon(QStyle::SP_FileDialogListView);
    frame->addAction(QStringLiteral("toggleListView"), listIcon, QStringLiteral("Toggle list view"));
    if (auto* btn = frame->actionButton(QStringLiteral("toggleListView"))) {
        btn->setCheckable(true);
        connect(btn, &QToolButton::toggled, this, [this](bool checked) {
            m_listViewMode = checked;
            refreshCards();
        });
    }

    auto* content = new QWidget(frame);
    content->setObjectName(QStringLiteral("AieKernelsPanelContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(8);

    auto* scrollArea = new QScrollArea(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setObjectName(QStringLiteral("AieKernelsScrollArea"));

    auto* cardsHost = new QWidget(scrollArea);
    cardsHost->setObjectName(QStringLiteral("AieKernelCardsHost"));
    m_cardsLayout = new QVBoxLayout(cardsHost);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(8);
    m_cardsLayout->addStretch(1);

    scrollArea->setWidget(cardsHost);

    auto* clearSelectionButton = new QPushButton(QStringLiteral("Clear Active Kernel"), content);
    clearSelectionButton->setObjectName(QStringLiteral("AieKernelClearSelectionButton"));
    connect(clearSelectionButton, &QPushButton::clicked, this, [this]() {
        if (m_assignments)
            m_assignments->clearSelectedKernel();
    });
    auto* clearShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    clearShortcut->setContext(Qt::WindowShortcut);
    connect(clearShortcut, &QShortcut::activated, this, [this]() {
        if (m_assignments)
            m_assignments->clearSelectedKernel();
    });

    contentLayout->addWidget(scrollArea, 1);
    contentLayout->addWidget(clearSelectionButton);
    frame->setContentWidget(content);
    rootLayout->addWidget(frame);

    m_selectionGroup = new QButtonGroup(this);
    m_selectionGroup->setExclusive(true);

    if (m_searchField) {
        connect(m_searchField, &QLineEdit::textChanged,
                this, [this](const QString&) {
                    refreshCards();
                    savePersistedState();
                });
    }
    if (auto* verticalBar = scrollArea->verticalScrollBar()) {
        connect(verticalBar, &QScrollBar::valueChanged, this, [this](int) {
            savePersistedState();
        });
    }
}

void AieKernelsPanel::refreshCards()
{
    if (!m_cardsLayout || !m_selectionGroup)
        return;

    while (m_cardsLayout->count() > 0) {
        QLayoutItem* item = m_cardsLayout->takeAt(0);
        if (!item)
            continue;
        if (auto* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    const auto buttons = m_selectionGroup->buttons();
    for (QAbstractButton* button : buttons)
        m_selectionGroup->removeButton(button);

    m_cards.clear();
    m_cardsLayout->setSpacing(m_listViewMode ? 2 : 8);

    const QString filterText = cleanFilter(m_searchField ? m_searchField->text() : QString());
    const QVector<KernelAsset> kernels = m_registry ? m_registry->kernels() : QVector<KernelAsset>{};

    for (const KernelAsset& kernel : kernels) {
        if (!kernelMatchesFilter(kernel, filterText))
            continue;

        CardRefs refs = m_listViewMode ? createListItem(kernel) : createCard(kernel);
        m_cards.push_back(refs);
        if (refs.frame)
            m_cardsLayout->addWidget(refs.frame);
    }

    m_cardsLayout->addStretch(1);
    syncSelectionFromAssignments();
}

bool AieKernelsPanel::kernelMatchesFilter(const KernelAsset& kernel, const QString& filterText) const
{
    if (filterText.isEmpty())
        return true;

    if (kernel.id.toLower().contains(filterText))
        return true;
    if (kernel.name.toLower().contains(filterText))
        return true;
    if (kernel.signature.toLower().contains(filterText))
        return true;

    for (const QString& tag : kernel.tags) {
        if (tag.toLower().contains(filterText))
            return true;
    }

    return false;
}

AieKernelsPanel::CardRefs AieKernelsPanel::createCard(const KernelAsset& kernel)
{
    CardRefs refs;
    refs.kernelId = kernel.id;

    QWidget* cardsHost = m_cardsLayout ? m_cardsLayout->parentWidget() : this;
    auto* frame = new QFrame(cardsHost);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setObjectName(QStringLiteral("AieKernelCard"));
    frame->setProperty("selected", false);

    auto* cardLayout = new QVBoxLayout(frame);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(7);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);

    auto* selectButton = new QRadioButton(kernel.name, frame);
    selectButton->setObjectName(QStringLiteral("AieKernelSelectButton"));
    selectButton->setProperty("kernelId", kernel.id);

    auto* scopeChip = new QLabel(kernelScopeName(kernel.scope), frame);
    scopeChip->setObjectName(QStringLiteral("AieKernelScopeChip"));

    auto* previewButton = new QToolButton(frame);
    previewButton->setObjectName(QStringLiteral("AieKernelPreviewButton"));
    QIcon previewIcon(QStringLiteral(":/ui/icons/svg/eye_icon.svg"));
    if (previewIcon.isNull())
        previewIcon = QIcon::fromTheme(QStringLiteral("view-preview"));
    if (previewIcon.isNull())
        previewIcon = style()->standardIcon(QStyle::SP_DialogOpenButton);
    previewButton->setIcon(previewIcon);
    previewButton->setToolTip(QStringLiteral("Preview"));
    previewButton->setText(QString());
    previewButton->setAutoRaise(true);
    previewButton->setIconSize(QSize(15, 15));

    auto* menuButton = new QToolButton(frame);
    menuButton->setObjectName(QStringLiteral("AieKernelMenuButton"));
    menuButton->setText(QStringLiteral("⋮"));
    menuButton->setAutoRaise(true);
    menuButton->setPopupMode(QToolButton::InstantPopup);

    topRow->addWidget(selectButton, 1);
    topRow->addWidget(scopeChip);
    topRow->addWidget(previewButton);
    topRow->addWidget(menuButton);

    auto* signatureLabel = new QLabel(fallbackSnippet(kernel), frame);
    signatureLabel->setObjectName(QStringLiteral("AieKernelSignatureLabel"));
    signatureLabel->setWordWrap(true);

    auto* snippetLabel = new QLabel(KernelPreview::loadCodePreview(kernel), frame);
    snippetLabel->setObjectName(QStringLiteral("AieKernelSnippetLabel"));
    snippetLabel->setWordWrap(true);

    auto* headerDivider = new QFrame(frame);
    headerDivider->setObjectName(QStringLiteral("AieKernelCardHeaderDivider"));
    headerDivider->setFrameShape(QFrame::HLine);
    headerDivider->setFrameShadow(QFrame::Plain);
    headerDivider->setLineWidth(1);

    cardLayout->addLayout(topRow);
    cardLayout->addWidget(headerDivider);
    cardLayout->addWidget(signatureLabel);
    cardLayout->addWidget(snippetLabel);

    m_selectionGroup->addButton(selectButton);

    connect(selectButton, &QRadioButton::toggled, this,
            [this, kernelId = kernel.id](bool checked) {
                if (checked)
                    setSelectedKernel(kernelId);
            });
    connect(previewButton, &QToolButton::clicked, this,
            [this, kernelId = kernel.id]() { showPreviewDialog(kernelId); });

    auto* menu = new QMenu(menuButton);
    menu->setObjectName(QStringLiteral("AieKernelCardMenu"));
    QAction* previewAction = menu->addAction(QStringLiteral("Preview"));
    previewAction->setIcon(previewIcon);
    QAction* metadataAction = menu->addAction(QStringLiteral("View Metadata"));
    QAction* openEditorAction = menu->addAction(QStringLiteral("Open In Code Editor"));
    menu->addSeparator();
    QAction* copyWorkspaceAction = menu->addAction(QStringLiteral("Create Workspace Copy"));
    QAction* copyGlobalAction = menu->addAction(QStringLiteral("Create Global Copy"));

    if (kernel.scope == KernelSourceScope::Workspace)
        copyWorkspaceAction->setEnabled(false);
    if (kernel.scope == KernelSourceScope::Global)
        copyGlobalAction->setEnabled(false);

    connect(previewAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() { showPreviewDialog(kernelId); });
    connect(metadataAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() { showPreviewDialog(kernelId, true); });
    connect(openEditorAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() { openKernelInEditor(kernelId, false); });
    connect(copyWorkspaceAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() {
                copyKernelToScope(kernelId, KernelSourceScope::Workspace, true);
            });
    connect(copyGlobalAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() {
                copyKernelToScope(kernelId, KernelSourceScope::Global, true);
            });

    menuButton->setMenu(menu);

    refs.frame = frame;
    refs.selectButton = selectButton;
    refs.previewButton = previewButton;
    refs.menuButton = menuButton;
    return refs;
}

AieKernelsPanel::CardRefs AieKernelsPanel::createListItem(const KernelAsset& kernel)
{
    CardRefs refs;
    refs.kernelId = kernel.id;

    QWidget* cardsHost = m_cardsLayout ? m_cardsLayout->parentWidget() : this;
    auto* frame = new QFrame(cardsHost);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setObjectName(QStringLiteral("AieKernelCard"));
    frame->setProperty("selected", false);

    auto* rowLayout = new QHBoxLayout(frame);
    rowLayout->setContentsMargins(10, 4, 6, 4);
    rowLayout->setSpacing(4);

    auto* selectButton = new QRadioButton(kernel.name, frame);
    selectButton->setObjectName(QStringLiteral("AieKernelSelectButton"));
    selectButton->setProperty("kernelId", kernel.id);

    auto* menuButton = new QToolButton(frame);
    menuButton->setObjectName(QStringLiteral("AieKernelMenuButton"));
    menuButton->setText(QStringLiteral("⋮"));
    menuButton->setAutoRaise(true);
    menuButton->setPopupMode(QToolButton::InstantPopup);

    rowLayout->addWidget(selectButton, 1);
    rowLayout->addWidget(menuButton);

    m_selectionGroup->addButton(selectButton);

    connect(selectButton, &QRadioButton::toggled, this,
            [this, kernelId = kernel.id](bool checked) {
                if (checked)
                    setSelectedKernel(kernelId);
            });

    QIcon previewIcon(QStringLiteral(":/ui/icons/svg/eye_icon.svg"));
    if (previewIcon.isNull())
        previewIcon = style()->standardIcon(QStyle::SP_DialogOpenButton);

    auto* menu = new QMenu(menuButton);
    menu->setObjectName(QStringLiteral("AieKernelCardMenu"));
    QAction* previewAction = menu->addAction(QStringLiteral("Preview"));
    previewAction->setIcon(previewIcon);
    QAction* metadataAction = menu->addAction(QStringLiteral("View Metadata"));
    QAction* openEditorAction = menu->addAction(QStringLiteral("Open In Code Editor"));
    menu->addSeparator();
    QAction* copyWorkspaceAction = menu->addAction(QStringLiteral("Create Workspace Copy"));
    QAction* copyGlobalAction = menu->addAction(QStringLiteral("Create Global Copy"));

    if (kernel.scope == KernelSourceScope::Workspace)
        copyWorkspaceAction->setEnabled(false);
    if (kernel.scope == KernelSourceScope::Global)
        copyGlobalAction->setEnabled(false);

    connect(previewAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() { showPreviewDialog(kernelId); });
    connect(metadataAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() { showPreviewDialog(kernelId, true); });
    connect(openEditorAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() { openKernelInEditor(kernelId, false); });
    connect(copyWorkspaceAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() {
                copyKernelToScope(kernelId, KernelSourceScope::Workspace, true);
            });
    connect(copyGlobalAction, &QAction::triggered, this,
            [this, kernelId = kernel.id]() {
                copyKernelToScope(kernelId, KernelSourceScope::Global, true);
            });

    menuButton->setMenu(menu);

    refs.frame = frame;
    refs.selectButton = selectButton;
    refs.menuButton = menuButton;
    return refs;
}

void AieKernelsPanel::showPreviewDialog(const QString& kernelId, bool openMetadataTab)
{
    const KernelAsset* kernel = kernelById(kernelId);
    if (!kernel)
        return;

    KernelPreviewDialog dialog(*kernel, this);
    KernelPreview::initializeDialogContent(dialog, *kernel, m_codeEditorService);
    dialog.setActiveTab(openMetadataTab
                            ? KernelPreviewDialog::Tab::Metadata
                            : KernelPreviewDialog::Tab::Code);

    connect(dialog.openInEditorButton(), &QPushButton::clicked, &dialog,
            [this, &dialog, kernelId]() {
                dialog.accept();
                QTimer::singleShot(0, this, [this, kernelId]() {
                    openKernelInEditor(kernelId, false);
                });
            });
    connect(dialog.copyToWorkspaceButton(), &QPushButton::clicked, &dialog,
            [this, kernelId]() { copyKernelToScope(kernelId, KernelSourceScope::Workspace, true); });
    connect(dialog.copyToGlobalButton(), &QPushButton::clicked, &dialog,
            [this, kernelId]() { copyKernelToScope(kernelId, KernelSourceScope::Global, true); });

    dialog.exec();
}

void AieKernelsPanel::setSelectedKernel(const QString& kernelId)
{
    if (!m_assignments)
        return;

    m_assignments->setSelectedKernelId(kernelId);
}

void AieKernelsPanel::syncSelectionFromAssignments()
{
    const QString selectedKernel = m_assignments ? m_assignments->selectedKernelId() : QString();
    if (selectedKernel.isEmpty()) {
        const bool wasExclusive = m_selectionGroup && m_selectionGroup->exclusive();
        if (m_selectionGroup)
            m_selectionGroup->setExclusive(false);

        for (const CardRefs& refs : m_cards) {
            if (!refs.selectButton || !refs.selectButton->isChecked())
                continue;

            QSignalBlocker blocker(refs.selectButton);
            refs.selectButton->setChecked(false);
        }

        if (m_selectionGroup)
            m_selectionGroup->setExclusive(wasExclusive);
        syncCardHighlightState();
        return;
    }

    for (const CardRefs& refs : m_cards) {
        if (!refs.selectButton)
            continue;

        const bool shouldCheck = (!selectedKernel.isEmpty() && refs.kernelId == selectedKernel);
        if (refs.selectButton->isChecked() == shouldCheck)
            continue;

        QSignalBlocker blocker(refs.selectButton);
        refs.selectButton->setChecked(shouldCheck);
    }
    syncCardHighlightState();
}

void AieKernelsPanel::syncCardHighlightState()
{
    for (const CardRefs& refs : m_cards) {
        if (!refs.frame || !refs.selectButton)
            continue;

        const bool selected = refs.selectButton->isChecked();
        if (refs.frame->property("selected").toBool() == selected)
            continue;

        refs.frame->setProperty("selected", selected);
        refs.frame->style()->unpolish(refs.frame);
        refs.frame->style()->polish(refs.frame);
        refs.frame->update();
    }
}

void AieKernelsPanel::loadPersistedState()
{
    const AieKernelsPanelState::Snapshot snapshot =
        m_persistedState.stateForWorkspaceRoot(workspaceRootForPersistence());

    if (m_searchField) {
        const QString persistedSearchText = snapshot.searchText;
        if (m_searchField->text() != persistedSearchText) {
            QSignalBlocker blocker(m_searchField);
            m_searchField->setText(persistedSearchText);
        }
    }

    refreshCards();

    if (m_assignments) {
        const QString selectedKernelId = snapshot.selectedKernelId.trimmed();
        const QString currentSelectedKernelId = m_assignments->selectedKernelId().trimmed();

        if (!selectedKernelId.isEmpty() && (!m_registry || m_registry->kernelById(selectedKernelId))) {
            m_assignments->setSelectedKernelId(selectedKernelId);
        } else if (snapshot.hasPersistedValue) {
            m_assignments->clearSelectedKernel();
        } else if (!currentSelectedKernelId.isEmpty()
                   && m_registry
                   && !m_registry->kernelById(currentSelectedKernelId)) {
            m_assignments->clearSelectedKernel();
        }
    }
    syncSelectionFromAssignments();

    if (QScrollArea* scrollArea = cardsScrollArea()) {
        const int scrollValue = snapshot.scrollValue;
        QPointer<QScrollArea> guardedScrollArea(scrollArea);
        QTimer::singleShot(0, this, [guardedScrollArea, scrollValue]() {
            if (!guardedScrollArea)
                return;
            if (auto* scrollBar = guardedScrollArea->verticalScrollBar())
                scrollBar->setValue(qMax(0, scrollValue));
        });
    }
}

void AieKernelsPanel::savePersistedState()
{
    AieKernelsPanelState::Snapshot snapshot;
    snapshot.searchText = m_searchField ? m_searchField->text() : QString();
    snapshot.selectedKernelId = m_assignments ? m_assignments->selectedKernelId() : QString();

    if (QScrollArea* scrollArea = cardsScrollArea()) {
        if (QScrollBar* scrollBar = scrollArea->verticalScrollBar())
            snapshot.scrollValue = scrollBar->value();
    }

    m_persistedState.setStateForWorkspaceRoot(workspaceRootForPersistence(), snapshot);
}

void AieKernelsPanel::openKernelInEditor(const QString& kernelId, bool forceReadOnly)
{
    const KernelAsset* kernel = kernelById(kernelId);
    if (!kernel)
        return;

    if (!m_codeEditorService) {
        QMessageBox::warning(this,
                             QStringLiteral("Code Editor"),
                             QStringLiteral("Code editor service is not available."));
        return;
    }

    CodeEditor::Api::CodeEditorOpenRequest request;
    request.filePath = kernel->absoluteEntryPath();
    request.activate = true;
    request.readOnly = forceReadOnly || kernel->scope == KernelSourceScope::BuiltIn;

    CodeEditor::Api::CodeEditorSessionHandle handle;
    const Utils::Result openResult = m_codeEditorService->openFile(request, handle);
    if (!openResult) {
        QMessageBox::warning(this,
                             QStringLiteral("Open Kernel"),
                             openResult.errors.join(QStringLiteral("\n")));
    }
}

void AieKernelsPanel::copyKernelToScope(const QString& kernelId,
                                        KernelSourceScope scope,
                                        bool openCopiedKernelInEditor)
{
    if (!m_registry)
        return;

    KernelAsset copiedKernel;
    const Utils::Result copyResult = m_registry->copyKernelToScope(kernelId, scope, &copiedKernel);
    if (!copyResult) {
        QMessageBox::warning(this,
                             QStringLiteral("Kernel Copy"),
                             copyResult.errors.join(QStringLiteral("\n")));
        return;
    }

    refreshCards();
    setSelectedKernel(copiedKernel.id);

    if (openCopiedKernelInEditor)
        openKernelInEditor(copiedKernel.id, false);
}

const KernelAsset* AieKernelsPanel::kernelById(const QString& kernelId) const
{
    if (!m_registry)
        return nullptr;
    return m_registry->kernelById(kernelId);
}

QString AieKernelsPanel::workspaceRootForPersistence() const
{
    return m_registry ? m_registry->workspaceRoot() : QString();
}

QScrollArea* AieKernelsPanel::cardsScrollArea() const
{
    return findChild<QScrollArea*>(QStringLiteral("AieKernelsScrollArea"));
}

} // namespace Aie::Internal
