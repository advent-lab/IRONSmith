// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/symbol_table/SymbolsPanel.hpp"

#include "aieplugin/symbol_table/SymbolsController.hpp"
#include "aieplugin/symbol_table/SymbolsModel.hpp"
#include "aieplugin/symbol_table/TapPreviewWidget.hpp"

#include <utils/ui/SidebarPanelFrame.hpp>

#include <QtCore/QItemSelectionModel>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStringListModel>
#include <QtCore/QTimer>
#include <QtGui/QFontDatabase>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCompleter>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTableView>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

constexpr int kAllFilterIndex = 0;
constexpr int kConstantsFilterIndex = 1;
constexpr int kTypesFilterIndex = 2;
constexpr int kTapsFilterIndex = 3;

class TapSpinBox final : public QSpinBox
{
public:
    explicit TapSpinBox(QWidget* parent = nullptr)
        : QSpinBox(parent)
    {
    }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (!hasFocus()) {
            event->ignore();
            return;
        }
        QSpinBox::wheelEvent(event);
    }
};

QFont fixedFont()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

void repolish(QWidget* widget)
{
    if (!widget || !widget->style())
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QLabel* makeKeyLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
    return label;
}

QLineEdit* makeField(QWidget* parent, const QString& placeholder = {})
{
    auto* edit = new QLineEdit(parent);
    edit->setObjectName(QStringLiteral("AiePropertiesField"));
    edit->setPlaceholderText(placeholder);
    return edit;
}

QSpinBox* makeTapSpinBox(QWidget* parent, int minimum, int maximum)
{
    auto* spin = new TapSpinBox(parent);
    spin->setObjectName(QStringLiteral("AieTapSpinBox"));
    spin->setRange(minimum, maximum);
    return spin;
}

QToolButton* makeTapPatternButton(const QString& text, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("AieTapPatternButton"));
    button->setText(text);
    button->setAutoRaise(false);
    return button;
}

QString filterSummary(int visibleCount, int totalCount)
{
    if (totalCount <= 0)
        return QStringLiteral("No symbols in this design yet.");
    if (visibleCount == totalCount)
        return QStringLiteral("%1 symbol%2 in this design.")
            .arg(totalCount)
            .arg(totalCount == 1 ? QString() : QStringLiteral("s"));
    return QStringLiteral("Showing %1 of %2 symbols.")
        .arg(visibleCount)
        .arg(totalCount);
}

} // namespace

SymbolsPanel::SymbolsPanel(SymbolsController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    setObjectName(QStringLiteral("AieSymbolsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(0);
    connect(&m_commitTimer, &QTimer::timeout, this, &SymbolsPanel::flushPendingCommit);

    buildUi();
    bindController();
    refreshPanelState();
}

void SymbolsPanel::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* frame = new Utils::SidebarPanelFrame(this);
    frame->setTitle(QStringLiteral("Symbols"));
    frame->setSubtitle(QStringLiteral("Constants, type abstractions, and TAPs"));
    frame->setSearchEnabled(true);
    frame->setSearchPlaceholder(QStringLiteral("Search symbols"));
    frame->setHeaderDividerVisible(true);
    m_frame = frame;

    auto* content = new QWidget(frame);
    content->setObjectName(QStringLiteral("AieSymbolsPanelContent"));
    content->setMinimumWidth(420);
    content->setMinimumHeight(760);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(6);

    auto* summaryLabel = new QLabel(content);
    summaryLabel->setWordWrap(true);
    summaryLabel->setObjectName(QStringLiteral("AieSymbolsSummaryLabel"));
    m_summaryLabel = summaryLabel;

    auto* detailLabel = new QLabel(content);
    detailLabel->setWordWrap(true);
    detailLabel->setObjectName(QStringLiteral("AieSymbolsDetailLabel"));
    detailLabel->setProperty("severity", QStringLiteral("normal"));
    m_detailLabel = detailLabel;

    auto* toolbarCard = new QFrame(content);
    toolbarCard->setObjectName(QStringLiteral("AieSymbolsToolbarRow"));
    auto* toolbarLayout = new QHBoxLayout(toolbarCard);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    auto* addConstantButton = new QPushButton(QStringLiteral("New Constant"), toolbarCard);
    addConstantButton->setObjectName(QStringLiteral("AieSymbolsPrimaryButton"));
    m_addConstantButton = addConstantButton;

    auto* addTypeButton = new QPushButton(QStringLiteral("New Type"), toolbarCard);
    addTypeButton->setObjectName(QStringLiteral("AieSymbolsSecondaryButton"));
    m_addTypeButton = addTypeButton;

    auto* addTapButton = new QPushButton(QStringLiteral("New TAP"), toolbarCard);
    addTapButton->setObjectName(QStringLiteral("AieSymbolsSecondaryButton"));
    m_addTapButton = addTapButton;

    auto* deleteButton = new QPushButton(QStringLiteral("Delete"), toolbarCard);
    deleteButton->setObjectName(QStringLiteral("AieSymbolsDangerButton"));
    m_deleteButton = deleteButton;

    auto* filterCombo = new QComboBox(toolbarCard);
    filterCombo->setObjectName(QStringLiteral("AieSymbolsFilterField"));
    filterCombo->addItem(QStringLiteral("All Symbols"), static_cast<int>(SymbolFilterKind::All));
    filterCombo->addItem(QStringLiteral("Constants"), static_cast<int>(SymbolFilterKind::Constants));
    filterCombo->addItem(QStringLiteral("Types"), static_cast<int>(SymbolFilterKind::Types));
    filterCombo->addItem(QStringLiteral("TAPs"), static_cast<int>(SymbolFilterKind::TensorAccessPatterns));
    filterCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_filterCombo = filterCombo;

    toolbarLayout->addWidget(addConstantButton);
    toolbarLayout->addWidget(addTypeButton);
    toolbarLayout->addWidget(addTapButton);
    toolbarLayout->addWidget(deleteButton);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(filterCombo, 0);

    auto* listCard = new QFrame(content);
    listCard->setObjectName(QStringLiteral("AieSymbolsListSurface"));
    listCard->setMinimumHeight(220);
    auto* listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    m_model = new SymbolsModel(m_controller, this);
    m_filterModel = new SymbolsFilterModel(this);
    m_filterModel->setSourceModel(m_model);

    auto* tableView = new QTableView(listCard);
    tableView->setObjectName(QStringLiteral("AieSymbolsTable"));
    tableView->setModel(m_filterModel);
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView->setAlternatingRowColors(false);
    tableView->setShowGrid(false);
    tableView->setSortingEnabled(false);
    tableView->verticalHeader()->setVisible(false);
    tableView->verticalHeader()->setDefaultSectionSize(30);
    tableView->horizontalHeader()->setStretchLastSection(false);
    tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tableView->setFocusPolicy(Qt::StrongFocus);
    tableView->setWordWrap(false);
    tableView->setMinimumHeight(180);
    m_tableView = tableView;

    listLayout->addWidget(tableView, 1);

    auto* editorHost = new QWidget(content);
    editorHost->setObjectName(QStringLiteral("AieSymbolsEditorHost"));
    auto* editorHostLayout = new QVBoxLayout(editorHost);
    editorHostLayout->setContentsMargins(0, 0, 0, 0);
    editorHostLayout->setSpacing(0);

    auto* editorStack = new QStackedWidget(editorHost);
    editorStack->setObjectName(QStringLiteral("AieSymbolsEditorStack"));
    m_editorStack = editorStack;

    buildEditorPages();
    editorHostLayout->addWidget(editorStack, 1);

    contentLayout->addWidget(summaryLabel);
    contentLayout->addWidget(detailLabel);
    contentLayout->addWidget(toolbarCard);
    contentLayout->addWidget(listCard);
    contentLayout->addWidget(editorHost);
    contentLayout->addStretch(1);

    auto* scrollArea = new QScrollArea(frame);
    scrollArea->setObjectName(QStringLiteral("AieSymbolsScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setWidget(content);

    frame->setContentWidget(scrollArea);
    rootLayout->addWidget(frame);

    if (auto* search = frame->searchField()) {
        connect(search, &QLineEdit::textChanged, this, [this](const QString& text) {
            if (m_filterModel)
                m_filterModel->setSearchText(text);
            updateSummaryText();
        });
    }

    connect(addConstantButton, &QPushButton::clicked, this, [this]() {
        if (!m_controller)
            return;
        QString newId;
        const Utils::Result result = m_controller->createConstant(&newId);
        refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
        if (result)
            refreshSelection();
    });
    connect(addTypeButton, &QPushButton::clicked, this, [this]() {
        if (!m_controller)
            return;
        QString newId;
        const Utils::Result result = m_controller->createTypeAbstraction(&newId);
        refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
        if (result)
            refreshSelection();
    });
    connect(addTapButton, &QPushButton::clicked, this, [this]() {
        if (!m_controller)
            return;
        QString newId;
        const Utils::Result result = m_controller->createTensorAccessPattern(&newId);
        refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
        if (result)
            refreshSelection();
    });
    connect(deleteButton, &QPushButton::clicked, this, &SymbolsPanel::deleteSelectedSymbol);
    connect(filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_filterModel)
            return;

        SymbolFilterKind kind = SymbolFilterKind::All;
        switch (index) {
            case kConstantsFilterIndex:
                kind = SymbolFilterKind::Constants;
                break;
            case kTypesFilterIndex:
                kind = SymbolFilterKind::Types;
                break;
            case kTapsFilterIndex:
                kind = SymbolFilterKind::TensorAccessPatterns;
                break;
            case kAllFilterIndex:
            default:
                break;
        }

        m_filterModel->setFilterKind(kind);
        updateSummaryText();
        refreshSelection();
    });
}

void SymbolsPanel::buildEditorPages()
{
    auto* emptyPage = new QWidget(m_editorStack);
    emptyPage->setObjectName(QStringLiteral("AieSymbolsEmptyEditorPage"));
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(8);

    auto* emptyCard = new QGroupBox(QStringLiteral("Selection"), emptyPage);
    emptyCard->setObjectName(QStringLiteral("AieSymbolsSection"));
    auto* emptyCardLayout = new QVBoxLayout(emptyCard);
    emptyCardLayout->setContentsMargins(12, 12, 12, 12);
    emptyCardLayout->setSpacing(8);

    auto* emptyLabel = new QLabel(QStringLiteral("Create a constant, type, or TAP, or select an existing symbol to inspect and edit it."), emptyCard);
    emptyLabel->setWordWrap(true);
    emptyLabel->setObjectName(QStringLiteral("AieSymbolsEmptyStateLabel"));

    auto* exampleLabel = new QLabel(QStringLiteral("Examples:\nN = 1024\nin_ty = np.ndarray[(N,), np.dtype[np.int32]]\ntap = TensorAccessPattern((16, 16), offset=0, sizes=[4, 4], strides=[16, 1])"), emptyCard);
    exampleLabel->setWordWrap(true);
    exampleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    exampleLabel->setFont(fixedFont());
    exampleLabel->setObjectName(QStringLiteral("AieSymbolsPreviewLabel"));

    emptyCardLayout->addWidget(emptyLabel);
    emptyCardLayout->addWidget(exampleLabel);
    emptyCardLayout->addStretch(1);
    emptyLayout->addWidget(emptyCard);
    emptyLayout->addStretch(1);

    m_emptyEditorPage = emptyPage;
    m_editorStack->addWidget(emptyPage);

    auto* constantCard = new QGroupBox(QStringLiteral("Constant"), m_editorStack);
    constantCard->setObjectName(QStringLiteral("AieSymbolsSection"));
    auto* constantForm = new QFormLayout(constantCard);
    constantForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    constantForm->setContentsMargins(12, 12, 12, 12);
    constantForm->setHorizontalSpacing(10);
    constantForm->setVerticalSpacing(8);
    constantForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* constantNameEdit = makeField(constantCard, QStringLiteral("Identifier"));
    auto* constantValueEdit = makeField(constantCard, QStringLiteral("Integral value"));
    auto* constantReferencesLabel = new QLabel(constantCard);
    constantReferencesLabel->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    constantReferencesLabel->setWordWrap(true);
    auto* constantPreviewLabel = new QLabel(constantCard);
    constantPreviewLabel->setObjectName(QStringLiteral("AieSymbolsPreviewLabel"));
    constantPreviewLabel->setFont(fixedFont());
    constantPreviewLabel->setWordWrap(true);
    constantPreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    constantForm->addRow(makeKeyLabel(QStringLiteral("Name"), constantCard), constantNameEdit);
    constantForm->addRow(makeKeyLabel(QStringLiteral("Value"), constantCard), constantValueEdit);
    constantForm->addRow(makeKeyLabel(QStringLiteral("Used By"), constantCard), constantReferencesLabel);
    constantForm->addRow(makeKeyLabel(QStringLiteral("Preview"), constantCard), constantPreviewLabel);

    m_constantEditorCard = constantCard;
    m_constantNameEdit = constantNameEdit;
    m_constantValueEdit = constantValueEdit;
    m_constantReferencesLabel = constantReferencesLabel;
    m_constantPreviewLabel = constantPreviewLabel;
    m_editorStack->addWidget(constantCard);

    auto* typePage = new QWidget(m_editorStack);
    typePage->setObjectName(QStringLiteral("AieSymbolsEditorPage"));
    auto* typeHostLayout = new QVBoxLayout(typePage);
    typeHostLayout->setContentsMargins(0, 0, 0, 0);
    typeHostLayout->setSpacing(0);

    auto* typeCard = new QGroupBox(QStringLiteral("Type Abstraction"), typePage);
    typeCard->setObjectName(QStringLiteral("AieSymbolsSection"));
    auto* typeForm = new QFormLayout(typeCard);
    typeForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    typeForm->setContentsMargins(12, 12, 12, 12);
    typeForm->setHorizontalSpacing(10);
    typeForm->setVerticalSpacing(8);
    typeForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto* typeNameEdit = makeField(typeCard, QStringLiteral("Identifier"));
    auto* typeContainerValue = new QLabel(QStringLiteral("np.ndarray"), typeCard);
    typeContainerValue->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    auto* typeRankSpin = new QSpinBox(typeCard);
    typeRankSpin->setObjectName(QStringLiteral("AiePropertiesField"));
    typeRankSpin->setRange(1, 8);
    typeRankSpin->setValue(1);

    auto* dimensionsHost = new QWidget(typeCard);
    auto* dimensionsForm = new QFormLayout(dimensionsHost);
    dimensionsForm->setContentsMargins(0, 0, 0, 0);
    dimensionsForm->setHorizontalSpacing(10);
    dimensionsForm->setVerticalSpacing(6);
    dimensionsForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    dimensionsForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* dtypeCombo = new QComboBox(typeCard);
    dtypeCombo->setObjectName(QStringLiteral("AiePropertiesField"));
    dtypeCombo->addItems(supportedSymbolDtypes());

    auto* typePreviewLabel = new QLabel(typeCard);
    typePreviewLabel->setObjectName(QStringLiteral("AieSymbolsPreviewLabel"));
    typePreviewLabel->setFont(fixedFont());
    typePreviewLabel->setWordWrap(true);
    typePreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    typeForm->addRow(makeKeyLabel(QStringLiteral("Name"), typeCard), typeNameEdit);
    typeForm->addRow(makeKeyLabel(QStringLiteral("Container"), typeCard), typeContainerValue);
    typeForm->addRow(makeKeyLabel(QStringLiteral("Rank"), typeCard), typeRankSpin);
    typeForm->addRow(makeKeyLabel(QStringLiteral("Dimensions"), typeCard), dimensionsHost);
    typeForm->addRow(makeKeyLabel(QStringLiteral("DType"), typeCard), dtypeCombo);
    typeForm->addRow(makeKeyLabel(QStringLiteral("Preview"), typeCard), typePreviewLabel);

    typeHostLayout->addWidget(typeCard);
    typeHostLayout->addStretch(1);

    m_typeEditorCard = typeCard;
    m_typeNameEdit = typeNameEdit;
    m_typeContainerValue = typeContainerValue;
    m_typeRankSpin = typeRankSpin;
    m_typeDimensionsForm = dimensionsForm;
    m_typeDTypeCombo = dtypeCombo;
    m_typePreviewLabel = typePreviewLabel;
    m_editorStack->addWidget(typePage);

    auto* tapPage = new QWidget(m_editorStack);
    tapPage->setObjectName(QStringLiteral("AieSymbolsEditorPage"));
    auto* tapHostLayout = new QVBoxLayout(tapPage);
    tapHostLayout->setContentsMargins(0, 0, 0, 0);
    tapHostLayout->setSpacing(10);

    auto* tapCard = new QGroupBox(QStringLiteral("Tensor Access Pattern"), tapPage);
    tapCard->setObjectName(QStringLiteral("AieSymbolsSection"));
    auto* tapCardLayout = new QVBoxLayout(tapCard);
    tapCardLayout->setContentsMargins(12, 12, 12, 12);
    tapCardLayout->setSpacing(10);

    auto* tapNameForm = new QFormLayout();
    tapNameForm->setContentsMargins(0, 0, 0, 0);
    tapNameForm->setHorizontalSpacing(10);
    tapNameForm->setVerticalSpacing(8);
    tapNameForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto* tapNameEdit = makeField(tapCard, QStringLiteral("Identifier"));
    tapNameForm->addRow(makeKeyLabel(QStringLiteral("Name"), tapCard), tapNameEdit);
    
    // Add format selector
    auto* tapFormatCombo = new QComboBox(tapCard);
    tapFormatCombo->setObjectName(QStringLiteral("AiePropertiesField"));
    tapFormatCombo->addItem(QStringLiteral("TensorAccessPattern"));
    tapFormatCombo->addItem(QStringLiteral("TensorTiler2D"));
    tapNameForm->addRow(makeKeyLabel(QStringLiteral("Format"), tapCard), tapFormatCombo);
    
    tapCardLayout->addLayout(tapNameForm);

    // Create stacked widget for format-specific editors
    auto* tapFormatStack = new QStackedWidget(tapCard);
    tapFormatStack->setObjectName(QStringLiteral("AieTapFormatStack"));

    auto* tapPatternPage = new QWidget(tapFormatStack);
    tapPatternPage->setObjectName(QStringLiteral("AieTapPatternPage"));
    auto* tapPatternLayout = new QVBoxLayout(tapPatternPage);
    tapPatternLayout->setContentsMargins(0, 0, 0, 0);
    tapPatternLayout->setSpacing(10);

    auto* tapConfigCard = new QFrame(tapPatternPage);
    tapConfigCard->setObjectName(QStringLiteral("AieTapEditorCard"));
    auto* tapConfigLayout = new QGridLayout(tapConfigCard);
    tapConfigLayout->setContentsMargins(12, 10, 12, 10);
    tapConfigLayout->setHorizontalSpacing(8);
    tapConfigLayout->setVerticalSpacing(8);

    auto* tapRowsSpin = makeTapSpinBox(tapConfigCard, 1, 4096);
    auto* tapColsSpin = makeTapSpinBox(tapConfigCard, 1, 4096);
    auto* tapOffsetSpin = makeTapSpinBox(tapConfigCard, 0, 1000000);
    auto* tapShowRepetitions = new QCheckBox(QStringLiteral("Show repetitions"), tapConfigCard);
    tapShowRepetitions->setObjectName(QStringLiteral("AieTapShowRepetitionsCheck"));

    tapConfigLayout->addWidget(makeKeyLabel(QStringLiteral("Rows"), tapConfigCard), 0, 0);
    tapConfigLayout->addWidget(tapRowsSpin, 0, 1);
    tapConfigLayout->addWidget(makeKeyLabel(QStringLiteral("Columns"), tapConfigCard), 0, 2);
    tapConfigLayout->addWidget(tapColsSpin, 0, 3);
    tapConfigLayout->addWidget(makeKeyLabel(QStringLiteral("Offset"), tapConfigCard), 1, 0);
    tapConfigLayout->addWidget(tapOffsetSpin, 1, 1, 1, 3);
    tapConfigLayout->addWidget(tapShowRepetitions, 2, 0, 1, 4);
    tapPatternLayout->addWidget(tapConfigCard);

    auto* tapPatternsCard = new QFrame(tapPatternPage);
    tapPatternsCard->setObjectName(QStringLiteral("AieTapEditorCard"));
    auto* tapPatternsLayout = new QVBoxLayout(tapPatternsCard);
    tapPatternsLayout->setContentsMargins(12, 10, 12, 10);
    tapPatternsLayout->setSpacing(8);

    auto* tapPatternsHeader = new QHBoxLayout();
    tapPatternsHeader->setContentsMargins(0, 0, 0, 0);
    tapPatternsHeader->setSpacing(8);
    auto* sizesLabel = makeKeyLabel(QStringLiteral("Sizes"), tapPatternsCard);
    auto* stridesLabel = makeKeyLabel(QStringLiteral("Strides"), tapPatternsCard);
    auto* addPatternButton = makeTapPatternButton(QStringLiteral("+"), tapPatternsCard);
    tapPatternsHeader->addWidget(sizesLabel);
    tapPatternsHeader->addSpacing(72);
    tapPatternsHeader->addWidget(stridesLabel);
    tapPatternsHeader->addStretch(1);
    tapPatternsHeader->addWidget(addPatternButton);

    auto* tapPatternsHost = new QWidget(tapPatternsCard);
    tapPatternsHost->setObjectName(QStringLiteral("AieTapPatternsHost"));
    auto* tapPatternsGrid = new QGridLayout(tapPatternsHost);
    tapPatternsGrid->setContentsMargins(0, 0, 0, 0);
    tapPatternsGrid->setHorizontalSpacing(8);
    tapPatternsGrid->setVerticalSpacing(6);

    tapPatternsLayout->addLayout(tapPatternsHeader);
    tapPatternsLayout->addWidget(tapPatternsHost);
    tapPatternLayout->addWidget(tapPatternsCard);

    tapFormatStack->addWidget(tapPatternPage);

    // ===== TensorTiler2D Page =====
    auto* tapTiler2DPage = new QWidget(tapFormatStack);
    tapTiler2DPage->setObjectName(QStringLiteral("AieTapTiler2DPage"));
    auto* tapTiler2DLayout = new QVBoxLayout(tapTiler2DPage);
    tapTiler2DLayout->setContentsMargins(0, 0, 0, 0);
    tapTiler2DLayout->setSpacing(10);

    auto* tapTiler2DConfigCard = new QFrame(tapTiler2DPage);
    tapTiler2DConfigCard->setObjectName(QStringLiteral("AieTapEditorCard"));
    auto* tapTiler2DConfigLayout = new QGridLayout(tapTiler2DConfigCard);
    tapTiler2DConfigLayout->setContentsMargins(12, 10, 12, 10);
    tapTiler2DConfigLayout->setHorizontalSpacing(8);
    tapTiler2DConfigLayout->setVerticalSpacing(8);

    auto makeTiler2DField = [tapTiler2DConfigCard](const QString& placeholder) {
        auto* e = new QLineEdit(tapTiler2DConfigCard);
        e->setObjectName(QStringLiteral("AiePropertiesField"));
        e->setPlaceholderText(placeholder);
        return e;
    };
    auto* tapTiler2DArrayDimsEdit   = makeTiler2DField(QStringLiteral("e.g. 256 x 256"));
    auto* tapTiler2DTileDimsEdit    = makeTiler2DField(QStringLiteral("e.g. 32 x 32"));
    auto* tapTiler2DTileCountsEdit  = makeTiler2DField(QStringLiteral("e.g. 8 x 8"));
    auto* tapTiler2DPatternRepeatEdit = makeTiler2DField(QStringLiteral("optional, e.g. 1"));

    tapTiler2DConfigLayout->addWidget(makeKeyLabel(QStringLiteral("Array Dimensions"), tapTiler2DConfigCard), 0, 0);
    tapTiler2DConfigLayout->addWidget(tapTiler2DArrayDimsEdit, 0, 1, 1, 3);
    tapTiler2DConfigLayout->addWidget(makeKeyLabel(QStringLiteral("Tile Dimensions"), tapTiler2DConfigCard), 1, 0);
    tapTiler2DConfigLayout->addWidget(tapTiler2DTileDimsEdit, 1, 1, 1, 3);
    tapTiler2DConfigLayout->addWidget(makeKeyLabel(QStringLiteral("Tile Counts"), tapTiler2DConfigCard), 2, 0);
    tapTiler2DConfigLayout->addWidget(tapTiler2DTileCountsEdit, 2, 1, 1, 3);
    tapTiler2DConfigLayout->addWidget(makeKeyLabel(QStringLiteral("Pattern Repeat"), tapTiler2DConfigCard), 3, 0);
    tapTiler2DConfigLayout->addWidget(tapTiler2DPatternRepeatEdit, 3, 1, 1, 3);

    auto* tiler2DNote = new QLabel(
        QStringLiteral("TensorTiler2D automatically calculates strides for optimal 2D tiling patterns."),
        tapTiler2DConfigCard);
    tiler2DNote->setObjectName(QStringLiteral("AieSymbolsDetailLabel"));
    tiler2DNote->setWordWrap(true);
    tapTiler2DConfigLayout->addWidget(tiler2DNote, 4, 0, 1, 4);

    tapTiler2DLayout->addWidget(tapTiler2DConfigCard);

    tapFormatStack->addWidget(tapTiler2DPage);

    tapCardLayout->addWidget(tapFormatStack);

    // Preview widget (shared between both formats)
    auto* tapPreviewCard = new QFrame(tapCard);
    tapPreviewCard->setObjectName(QStringLiteral("AieTapEditorCard"));
    tapPreviewCard->setMinimumHeight(320);
    auto* tapPreviewLayout = new QVBoxLayout(tapPreviewCard);
    tapPreviewLayout->setContentsMargins(12, 10, 12, 10);
    tapPreviewLayout->setSpacing(8);
    auto* tapPreviewTitle = makeKeyLabel(QStringLiteral("Preview"), tapPreviewCard);
    auto* tapPreviewWidget = new TapPreviewWidget(tapPreviewCard);
    tapPreviewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tapPreviewWidget->setMinimumHeight(260);
    tapPreviewLayout->addWidget(tapPreviewTitle);
    tapPreviewLayout->addWidget(tapPreviewWidget, 1);
    tapCardLayout->addWidget(tapPreviewCard);

    tapHostLayout->addWidget(tapCard);
    tapHostLayout->addStretch(1);

    m_tapEditorPage = tapPage;
    m_tapEditorCard = tapCard;
    m_tapNameEdit = tapNameEdit;
    m_tapFormatCombo = tapFormatCombo;
    m_tapFormatStack = tapFormatStack;
    m_tapPatternPage = tapPatternPage;
    m_tapTiler2DPage = tapTiler2DPage;
    m_tapRowsSpin = tapRowsSpin;
    m_tapColsSpin = tapColsSpin;
    m_tapOffsetSpin = tapOffsetSpin;
    m_tapShowRepetitionsCheck = tapShowRepetitions;
    m_tapPatternsHost = tapPatternsHost;
    m_tapPatternsGrid = tapPatternsGrid;
    m_tapAddPatternButton = addPatternButton;
    m_tapTiler2DArrayDimsEdit = tapTiler2DArrayDimsEdit;
    m_tapTiler2DTileDimsEdit = tapTiler2DTileDimsEdit;
    m_tapTiler2DTileCountsEdit = tapTiler2DTileCountsEdit;
    m_tapTiler2DPatternRepeatEdit = tapTiler2DPatternRepeatEdit;
    m_tapPreviewWidget = tapPreviewWidget;
    m_editorStack->addWidget(tapPage);

    rebuildDimensionEditors(1);
    rebuildTapPatternEditors();

    connect(constantNameEdit, &QLineEdit::textChanged, this, &SymbolsPanel::refreshEditorPreview);
    connect(constantValueEdit, &QLineEdit::textChanged, this, &SymbolsPanel::refreshEditorPreview);
    connect(constantNameEdit, &QLineEdit::editingFinished, this, &SymbolsPanel::requestConstantCommit);
    connect(constantValueEdit, &QLineEdit::editingFinished, this, &SymbolsPanel::requestConstantCommit);

    connect(typeNameEdit, &QLineEdit::textChanged, this, &SymbolsPanel::refreshEditorPreview);
    connect(typeNameEdit, &QLineEdit::editingFinished, this, &SymbolsPanel::requestTypeCommit);
    connect(typeRankSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int rank) {
        if (m_updatingUi)
            return;
        QStringList existingTokens;
        existingTokens.reserve(m_dimensionEdits.size());
        for (const auto& edit : m_dimensionEdits)
            existingTokens.push_back(edit ? edit->text().trimmed() : QString());
        rebuildDimensionEditors(rank);
        for (int i = 0; i < m_dimensionEdits.size() && i < existingTokens.size(); ++i) {
            if (m_dimensionEdits[i] && !existingTokens.at(i).isEmpty())
                m_dimensionEdits[i]->setText(existingTokens.at(i));
        }
        refreshEditorPreview();
        requestTypeCommit();
    });
    connect(dtypeCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (m_updatingUi)
            return;
        refreshEditorPreview();
        requestTypeCommit();
    });

    connect(tapNameEdit, &QLineEdit::textChanged, this, &SymbolsPanel::refreshEditorPreview);
    connect(tapNameEdit, &QLineEdit::editingFinished, this, &SymbolsPanel::requestTapCommit);
    connect(tapFormatCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updatingUi || !m_tapFormatStack)
            return;
        m_tapFormatStack->setCurrentIndex(index);
        refreshEditorPreview();
        requestTapCommit();
    });
    connect(tapRowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        if (m_updatingUi)
            return;
        refreshEditorPreview();
        requestTapCommit();
    });
    connect(tapColsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        if (m_updatingUi)
            return;
        refreshEditorPreview();
        requestTapCommit();
    });
    connect(tapOffsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        if (m_updatingUi)
            return;
        refreshEditorPreview();
        requestTapCommit();
    });
    connect(tapShowRepetitions, &QCheckBox::toggled, this, [this](bool) {
        if (m_updatingUi)
            return;
        refreshEditorPreview();
        requestTapCommit();
    });
    connect(addPatternButton, &QToolButton::clicked, this, &SymbolsPanel::addTapPatternRow);
    connect(tapTiler2DArrayDimsEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_updatingUi) return;
        refreshEditorPreview();
        requestTapCommit();
    });
    connect(tapTiler2DTileDimsEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_updatingUi) return;
        refreshEditorPreview();
        requestTapCommit();
    });
    connect(tapTiler2DTileCountsEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_updatingUi) return;
        refreshEditorPreview();
        requestTapCommit();
    });
    connect(tapTiler2DPatternRepeatEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_updatingUi) return;
        refreshEditorPreview();
        requestTapCommit();
    });
}

void SymbolsPanel::rebuildDimensionEditors(int rank)
{
    if (!m_typeDimensionsForm)
        return;

    while (m_typeDimensionsForm->rowCount() > 0)
        m_typeDimensionsForm->removeRow(0);

    m_dimensionEdits.clear();
    m_dimensionCompleters.clear();

    const QStringList suggestions = m_controller
        ? m_controller->dimensionReferenceCandidates()
        : QStringList{QStringLiteral("1")};

    for (int axis = 0; axis < rank; ++axis) {
        auto* field = makeField(m_typeDimensionsForm->parentWidget(),
                                QStringLiteral("Literal or constant"));
        field->setText(QStringLiteral("1"));
        auto* completerModel = new QStringListModel(suggestions, field);
        auto* completer = new QCompleter(completerModel, field);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        field->setCompleter(completer);

        connect(field, &QLineEdit::textChanged, this, &SymbolsPanel::refreshEditorPreview);
        connect(field, &QLineEdit::editingFinished, this, &SymbolsPanel::requestTypeCommit);

        m_typeDimensionsForm->addRow(makeKeyLabel(QStringLiteral("Axis %1").arg(axis), field->parentWidget()),
                                     field);
        m_dimensionEdits.push_back(field);
        m_dimensionCompleters.push_back(completer);
    }
}

void SymbolsPanel::rebuildTapPatternEditors()
{
    if (!m_tapPatternsGrid || !m_tapPatternsHost)
        return;

    while (QLayoutItem* item = m_tapPatternsGrid->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    m_tapSizeSpins.clear();
    m_tapStrideSpins.clear();
    m_tapMoveUpButtons.clear();
    m_tapMoveDownButtons.clear();
    m_tapRemoveButtons.clear();

    const SymbolRecord* current = m_controller ? m_controller->symbolById(m_controller->selectedSymbolId()) : nullptr;
    const QVector<int> sizes = current && current->kind == SymbolKind::TensorAccessPattern
        ? current->tap.sizes
        : QVector<int>{4, 4};
    const QVector<int> strides = current && current->kind == SymbolKind::TensorAccessPattern
        ? current->tap.strides
        : QVector<int>{16, 1};

    const int rowCount = qMin(sizes.size(), strides.size());
    for (int row = 0; row < rowCount; ++row) {
        auto* sizeSpin = makeTapSpinBox(m_tapPatternsHost, 1, 1000000);
        auto* strideSpin = makeTapSpinBox(m_tapPatternsHost, 1, 1000000);
        auto* upButton = makeTapPatternButton(QStringLiteral("↑"), m_tapPatternsHost);
        auto* downButton = makeTapPatternButton(QStringLiteral("↓"), m_tapPatternsHost);
        auto* removeButton = makeTapPatternButton(QStringLiteral("-"), m_tapPatternsHost);

        sizeSpin->setValue(sizes.value(row, 1));
        strideSpin->setValue(strides.value(row, 1));

        connect(sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            if (m_updatingUi)
                return;
            refreshEditorPreview();
            requestTapCommit();
        });
        connect(strideSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            if (m_updatingUi)
                return;
            refreshEditorPreview();
            requestTapCommit();
        });
        connect(upButton, &QToolButton::clicked, this, [this, row]() { moveTapPatternRow(row, -1); });
        connect(downButton, &QToolButton::clicked, this, [this, row]() { moveTapPatternRow(row, 1); });
        connect(removeButton, &QToolButton::clicked, this, [this, row]() { removeTapPatternRow(row); });

        m_tapPatternsGrid->addWidget(sizeSpin, row, 0);
        m_tapPatternsGrid->addWidget(strideSpin, row, 1);
        m_tapPatternsGrid->addWidget(upButton, row, 2);
        m_tapPatternsGrid->addWidget(downButton, row, 3);
        m_tapPatternsGrid->addWidget(removeButton, row, 4);

        m_tapSizeSpins.push_back(sizeSpin);
        m_tapStrideSpins.push_back(strideSpin);
        m_tapMoveUpButtons.push_back(upButton);
        m_tapMoveDownButtons.push_back(downButton);
        m_tapRemoveButtons.push_back(removeButton);
    }

    for (int row = 0; row < m_tapMoveUpButtons.size(); ++row) {
        if (m_tapMoveUpButtons[row])
            m_tapMoveUpButtons[row]->setEnabled(row > 0);
        if (m_tapMoveDownButtons[row])
            m_tapMoveDownButtons[row]->setEnabled(row + 1 < m_tapMoveDownButtons.size());
        if (m_tapRemoveButtons[row])
            m_tapRemoveButtons[row]->setEnabled(m_tapRemoveButtons.size() > 1);
    }
}

void SymbolsPanel::bindController()
{
    if (!m_controller)
        return;

    connect(m_controller, &SymbolsController::symbolsChanged,
            this, &SymbolsPanel::refreshPanelState);
    connect(m_controller, &SymbolsController::selectedSymbolChanged,
            this, &SymbolsPanel::refreshSelection);
    connect(m_controller, &SymbolsController::activeDocumentChanged,
            this, &SymbolsPanel::refreshPanelState);

    if (m_tableView && m_tableView->selectionModel()) {
        connect(m_tableView->selectionModel(), &QItemSelectionModel::currentRowChanged,
                this, [this](const QModelIndex& current) {
                    if (m_updatingUi || !m_controller)
                        return;
                    flushPendingCommit();
                    m_controller->setSelectedSymbolId(current.data(SymbolsModel::SymbolIdRole).toString());
                });
    }
}

void SymbolsPanel::refreshPanelState()
{
    updateActionState();
    updateSummaryText();
    refreshSelection();

    if (!m_controller || !m_controller->hasActiveDocument()) {
        refreshStatusMessage(QStringLiteral("Open a design to manage symbols."));
        if (m_editorStack && m_emptyEditorPage)
            m_editorStack->setCurrentWidget(m_emptyEditorPage);
        return;
    }

    if (m_controller->symbols().isEmpty())
        refreshStatusMessage(QStringLiteral("Create constants, reusable ndarray types, and TAPs for this design."));
    else
        refreshStatusMessage();

    refreshEditor();
}

void SymbolsPanel::refreshSelection()
{
    if (!m_tableView || !m_filterModel || !m_controller)
        return;

    const QString selectedId = m_controller->selectedSymbolId();
    QModelIndex targetIndex;
    for (int row = 0; row < m_filterModel->rowCount(); ++row) {
        const QModelIndex index = m_filterModel->index(row, 0);
        if (index.data(SymbolsModel::SymbolIdRole).toString() == selectedId) {
            targetIndex = index;
            break;
        }
    }

    m_updatingUi = true;
    if (auto* selectionModel = m_tableView->selectionModel()) {
        selectionModel->clearSelection();
        if (targetIndex.isValid()) {
            selectionModel->setCurrentIndex(targetIndex,
                                            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
    m_updatingUi = false;

    refreshEditor();
    updateActionState();
}

void SymbolsPanel::refreshEditor()
{
    if (!m_editorStack || !m_controller) {
        return;
    }

    const SymbolRecord* symbol = m_controller->symbolById(m_controller->selectedSymbolId());
    if (!symbol) {
        if (m_emptyEditorPage)
            m_editorStack->setCurrentWidget(m_emptyEditorPage);
        m_commitTimer.stop();
        m_pendingCommit = PendingCommitKind::None;
        refreshEditorPreview();
        return;
    }

    m_updatingUi = true;

    if (symbol->kind == SymbolKind::Constant) {
        if (m_constantNameEdit)
            m_constantNameEdit->setText(symbol->name);
        if (m_constantValueEdit)
            m_constantValueEdit->setText(QString::number(symbol->constant.value));
        if (m_constantReferencesLabel) {
            const QStringList references = m_controller->referencesForSymbol(symbol->id);
            m_constantReferencesLabel->setText(references.isEmpty()
                                                   ? QStringLiteral("Not referenced yet.")
                                                   : references.join(QStringLiteral(", ")));
        }
        if (m_editorStack && m_constantEditorCard)
            m_editorStack->setCurrentWidget(m_constantEditorCard);
    } else if (symbol->kind == SymbolKind::TypeAbstraction) {
        if (m_typeNameEdit)
            m_typeNameEdit->setText(symbol->name);
        const int newRank = qMax(1, symbol->type.shapeTokens.size());
        if (m_typeRankSpin)
            m_typeRankSpin->setValue(newRank);
        // Only rebuild the dimension editors if the rank changed; otherwise
        // just update the field values in place to avoid destroying live
        // widgets that callers may still hold raw pointers to.
        if (m_dimensionEdits.size() != newRank)
            rebuildDimensionEditors(newRank);
        for (int i = 0; i < m_dimensionEdits.size() && i < symbol->type.shapeTokens.size(); ++i) {
            if (m_dimensionEdits[i])
                m_dimensionEdits[i]->setText(symbol->type.shapeTokens.at(i));
        }
        if (m_typeDTypeCombo) {
            const int dtypeIndex = m_typeDTypeCombo->findText(symbol->type.dtype);
            if (dtypeIndex >= 0)
                m_typeDTypeCombo->setCurrentIndex(dtypeIndex);
        }
        if (m_editorStack)
            m_editorStack->setCurrentIndex(2);
    } else {
        if (m_tapNameEdit)
            m_tapNameEdit->setText(symbol->name);
        
        // Set format combo and show appropriate page
        const bool useTiler2D = symbol->tap.useTiler2D;
        if (m_tapFormatCombo) {
            m_tapFormatCombo->setCurrentIndex(useTiler2D ? 1 : 0);
        }
        if (m_tapFormatStack) {
            m_tapFormatStack->setCurrentIndex(useTiler2D ? 1 : 0);
        }
        
        // Update TensorAccessPattern fields
        if (m_tapRowsSpin)
            m_tapRowsSpin->setValue(symbol->tap.rows);
        if (m_tapColsSpin)
            m_tapColsSpin->setValue(symbol->tap.cols);
        if (m_tapOffsetSpin)
            m_tapOffsetSpin->setValue(symbol->tap.offset);
        if (m_tapShowRepetitionsCheck)
            m_tapShowRepetitionsCheck->setChecked(symbol->tap.showRepetitions);
        rebuildTapPatternEditors();
        
        // Update TensorTiler2D fields
        if (m_tapTiler2DArrayDimsEdit)
            m_tapTiler2DArrayDimsEdit->setText(symbol->tap.tensorDims);
        if (m_tapTiler2DTileDimsEdit)
            m_tapTiler2DTileDimsEdit->setText(symbol->tap.tileDims);
        if (m_tapTiler2DTileCountsEdit)
            m_tapTiler2DTileCountsEdit->setText(symbol->tap.tileCounts);
        if (m_tapTiler2DPatternRepeatEdit)
            m_tapTiler2DPatternRepeatEdit->setText(symbol->tap.patternRepeat);
        
        if (m_editorStack && m_tapEditorPage)
            m_editorStack->setCurrentWidget(m_tapEditorPage);
    }

    m_updatingUi = false;
    refreshEditorPreview();
}

void SymbolsPanel::refreshEditorPreview()
{
    if (!m_controller || !m_editorStack)
        return;

    const SymbolRecord* symbol = m_controller->symbolById(m_controller->selectedSymbolId());
    if (!symbol)
        return;

    if (symbol->kind == SymbolKind::Constant) {
        if (m_constantPreviewLabel)
            m_constantPreviewLabel->setText(currentConstantPreview());
    } else if (symbol->kind == SymbolKind::TypeAbstraction) {
        if (m_typePreviewLabel)
            m_typePreviewLabel->setText(currentTypePreview());
    } else if (m_tapPreviewWidget) {
        TensorAccessPatternSymbolData tapData;
        
        // Determine which format is active
        const bool useTiler2D = m_tapFormatCombo && m_tapFormatCombo->currentIndex() == 1;
        tapData.useTiler2D = useTiler2D;
        
        if (useTiler2D) {
            // TensorTiler2D format
            tapData.tensorDims = m_tapTiler2DArrayDimsEdit ? m_tapTiler2DArrayDimsEdit->text().trimmed() : QString();
            tapData.tileDims = m_tapTiler2DTileDimsEdit ? m_tapTiler2DTileDimsEdit->text().trimmed() : QString();
            tapData.tileCounts = m_tapTiler2DTileCountsEdit ? m_tapTiler2DTileCountsEdit->text().trimmed() : QString();
            tapData.patternRepeat = m_tapTiler2DPatternRepeatEdit ? m_tapTiler2DPatternRepeatEdit->text().trimmed() : QString();
            tapData.showRepetitions = false;
            tapData.sizes.clear();
            tapData.strides.clear();
        } else {
            // TensorAccessPattern format
            tapData.rows = m_tapRowsSpin ? m_tapRowsSpin->value() : 16;
            tapData.cols = m_tapColsSpin ? m_tapColsSpin->value() : 16;
            tapData.offset = m_tapOffsetSpin ? m_tapOffsetSpin->value() : 0;
            tapData.showRepetitions = m_tapShowRepetitionsCheck && m_tapShowRepetitionsCheck->isChecked();
            tapData.sizes.clear();
            tapData.strides.clear();
            for (const auto& spin : m_tapSizeSpins)
                tapData.sizes.push_back(spin ? spin->value() : 1);
            for (const auto& spin : m_tapStrideSpins)
                tapData.strides.push_back(spin ? spin->value() : 1);
        }
        
        m_tapPreviewWidget->setTapData(tapData);
    }
}

void SymbolsPanel::refreshStatusMessage(const QString& message, bool error)
{
    if (!m_detailLabel)
        return;

    const QString text = message.trimmed().isEmpty()
        ? QStringLiteral("Stored with the active design document.")
        : message.trimmed();
    m_detailLabel->setText(text);
    m_detailLabel->setProperty("severity", error ? QStringLiteral("error") : QStringLiteral("normal"));
    repolish(m_detailLabel);
}

void SymbolsPanel::updateSummaryText()
{
    if (!m_summaryLabel)
        return;

    if (!m_controller || !m_controller->hasActiveDocument()) {
        m_summaryLabel->setText(QStringLiteral("No design open."));
        return;
    }

    const int totalCount = m_controller->symbols().size();
    const int visibleCount = m_filterModel ? m_filterModel->rowCount() : totalCount;
    m_summaryLabel->setText(filterSummary(visibleCount, totalCount));
}

void SymbolsPanel::updateActionState()
{
    const bool hasDocument = m_controller && m_controller->hasActiveDocument();
    const bool hasSelection = m_controller && m_controller->symbolById(m_controller->selectedSymbolId());

    if (m_addConstantButton)
        m_addConstantButton->setEnabled(hasDocument);
    if (m_addTypeButton)
        m_addTypeButton->setEnabled(hasDocument);
    if (m_addTapButton)
        m_addTapButton->setEnabled(hasDocument);
    if (m_deleteButton)
        m_deleteButton->setEnabled(hasDocument && hasSelection);
}

void SymbolsPanel::requestConstantCommit()
{
    if (m_updatingUi)
        return;

    m_pendingCommit = PendingCommitKind::Constant;
    m_commitTimer.start();
}

void SymbolsPanel::requestTypeCommit()
{
    if (m_updatingUi)
        return;

    m_pendingCommit = PendingCommitKind::Type;
    m_commitTimer.start();
}

void SymbolsPanel::requestTapCommit()
{
    if (m_updatingUi)
        return;

    m_pendingCommit = PendingCommitKind::Tap;
    m_commitTimer.start();
}

void SymbolsPanel::flushPendingCommit()
{
    const PendingCommitKind commit = m_pendingCommit;
    m_pendingCommit = PendingCommitKind::None;

    switch (commit) {
        case PendingCommitKind::Constant:
            commitConstantEdits();
            break;
        case PendingCommitKind::Type:
            commitTypeEdits();
            break;
        case PendingCommitKind::Tap:
            commitTapEdits();
            break;
        case PendingCommitKind::None:
            break;
    }
}

void SymbolsPanel::commitConstantEdits()
{
    if (m_updatingUi || !m_controller)
        return;

    const SymbolRecord* current = m_controller->symbolById(m_controller->selectedSymbolId());
    if (!current || current->kind != SymbolKind::Constant)
        return;

    bool ok = false;
    const qint64 value = m_constantValueEdit ? m_constantValueEdit->text().trimmed().toLongLong(&ok) : 0;
    if (!ok) {
        refreshStatusMessage(QStringLiteral("Constants must use an integral value."), true);
        return;
    }

    SymbolRecord updated = *current;
    updated.name = currentEditorName();
    updated.constant.value = value;

    const Utils::Result result = m_controller->updateSymbol(updated);
    refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
}

void SymbolsPanel::commitTypeEdits()
{
    if (m_updatingUi || !m_controller)
        return;

    const SymbolRecord* current = m_controller->symbolById(m_controller->selectedSymbolId());
    if (!current || current->kind != SymbolKind::TypeAbstraction)
        return;

    SymbolRecord updated = *current;
    updated.name = currentEditorName();
    updated.type.dtype = m_typeDTypeCombo ? m_typeDTypeCombo->currentText().trimmed() : QStringLiteral("int32");
    updated.type.shapeTokens.clear();
    updated.type.shapeTokens.reserve(m_dimensionEdits.size());
    for (const auto& edit : m_dimensionEdits)
        updated.type.shapeTokens.push_back(edit ? edit->text().trimmed() : QString());

    const Utils::Result result = m_controller->updateSymbol(updated);
    refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
}

void SymbolsPanel::commitTapEdits()
{
    if (m_updatingUi || !m_controller)
        return;

    const SymbolRecord* current = m_controller->symbolById(m_controller->selectedSymbolId());
    if (!current || current->kind != SymbolKind::TensorAccessPattern)
        return;

    SymbolRecord updated = *current;
    updated.name = currentEditorName();
    
    // Determine which format is active
    const bool useTiler2D = m_tapFormatCombo && m_tapFormatCombo->currentIndex() == 1;
    updated.tap.useTiler2D = useTiler2D;
    
    if (useTiler2D) {
        // TensorTiler2D format
        updated.tap.tensorDims = m_tapTiler2DArrayDimsEdit ? m_tapTiler2DArrayDimsEdit->text().trimmed() : QString();
        updated.tap.tileDims = m_tapTiler2DTileDimsEdit ? m_tapTiler2DTileDimsEdit->text().trimmed() : QString();
        updated.tap.tileCounts = m_tapTiler2DTileCountsEdit ? m_tapTiler2DTileCountsEdit->text().trimmed() : QString();
        updated.tap.patternRepeat = m_tapTiler2DPatternRepeatEdit ? m_tapTiler2DPatternRepeatEdit->text().trimmed() : QString();
        updated.tap.showRepetitions = false;
        updated.tap.sizes.clear();
        updated.tap.strides.clear();
    } else {
        // TensorAccessPattern format
        updated.tap.rows = m_tapRowsSpin ? m_tapRowsSpin->value() : 16;
        updated.tap.cols = m_tapColsSpin ? m_tapColsSpin->value() : 16;
        updated.tap.offset = m_tapOffsetSpin ? m_tapOffsetSpin->value() : 0;
        updated.tap.showRepetitions = m_tapShowRepetitionsCheck && m_tapShowRepetitionsCheck->isChecked();
        updated.tap.sizes.clear();
        updated.tap.strides.clear();
        updated.tap.sizes.reserve(m_tapSizeSpins.size());
        updated.tap.strides.reserve(m_tapStrideSpins.size());
        for (const auto& spin : m_tapSizeSpins)
            updated.tap.sizes.push_back(spin ? spin->value() : 1);
        for (const auto& spin : m_tapStrideSpins)
            updated.tap.strides.push_back(spin ? spin->value() : 1);
    }

    const Utils::Result result = m_controller->updateSymbol(updated);
    refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
}

void SymbolsPanel::deleteSelectedSymbol()
{
    if (!m_controller)
        return;

    const QString symbolId = m_controller->selectedSymbolId();
    if (symbolId.isEmpty())
        return;

    const Utils::Result result = m_controller->removeSymbol(symbolId);
    refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
}

QString SymbolsPanel::selectedSymbolIdFromView() const
{
    if (!m_tableView || !m_tableView->selectionModel())
        return {};
    return m_tableView->selectionModel()->currentIndex().data(SymbolsModel::SymbolIdRole).toString();
}

QString SymbolsPanel::currentEditorName() const
{
    const SymbolRecord* current = m_controller ? m_controller->symbolById(m_controller->selectedSymbolId()) : nullptr;
    if (!current)
        return {};

    if (current->kind == SymbolKind::Constant && m_constantNameEdit)
        return m_constantNameEdit->text().trimmed();
    if (current->kind == SymbolKind::TypeAbstraction && m_typeNameEdit)
        return m_typeNameEdit->text().trimmed();
    if (current->kind == SymbolKind::TensorAccessPattern && m_tapNameEdit)
        return m_tapNameEdit->text().trimmed();
    return current->name;
}

QString SymbolsPanel::currentConstantPreview() const
{
    const QString name = m_constantNameEdit ? m_constantNameEdit->text().trimmed() : QString();
    const QString value = m_constantValueEdit ? m_constantValueEdit->text().trimmed() : QString();
    return QStringLiteral("%1 = %2").arg(name, value);
}

QString SymbolsPanel::currentTypePreview() const
{
    TypeAbstractionSymbolData typeData;
    typeData.dtype = m_typeDTypeCombo ? m_typeDTypeCombo->currentText().trimmed() : QStringLiteral("int32");
    for (const auto& edit : m_dimensionEdits)
        typeData.shapeTokens.push_back(edit ? edit->text().trimmed() : QString());
    return typeAbstractionPreview(m_typeNameEdit ? m_typeNameEdit->text().trimmed() : QString(), typeData);
}

QString SymbolsPanel::currentTapPreview() const
{
    TensorAccessPatternSymbolData tapData;
    tapData.rows = m_tapRowsSpin ? m_tapRowsSpin->value() : 16;
    tapData.cols = m_tapColsSpin ? m_tapColsSpin->value() : 16;
    tapData.offset = m_tapOffsetSpin ? m_tapOffsetSpin->value() : 0;
    tapData.showRepetitions = m_tapShowRepetitionsCheck && m_tapShowRepetitionsCheck->isChecked();
    for (const auto& spin : m_tapSizeSpins)
        tapData.sizes.push_back(spin ? spin->value() : 1);
    for (const auto& spin : m_tapStrideSpins)
        tapData.strides.push_back(spin ? spin->value() : 1);
    return tensorAccessPatternPreview(m_tapNameEdit ? m_tapNameEdit->text().trimmed() : QString(), tapData);
}

void SymbolsPanel::addTapPatternRow()
{
    flushPendingCommit();
    const SymbolRecord* current = m_controller ? m_controller->symbolById(m_controller->selectedSymbolId()) : nullptr;
    if (!current || current->kind != SymbolKind::TensorAccessPattern)
        return;

    SymbolRecord updated = *current;
    const int nextSize = updated.tap.sizes.isEmpty() ? 4 : updated.tap.sizes.back();
    const int nextStride = updated.tap.strides.isEmpty() ? 1 : updated.tap.strides.back();
    updated.tap.sizes.push_back(nextSize);
    updated.tap.strides.push_back(nextStride);

    const Utils::Result result = m_controller->updateSymbol(updated);
    refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
}

void SymbolsPanel::moveTapPatternRow(int row, int delta)
{
    flushPendingCommit();
    const SymbolRecord* current = m_controller ? m_controller->symbolById(m_controller->selectedSymbolId()) : nullptr;
    if (!current || current->kind != SymbolKind::TensorAccessPattern)
        return;

    const int target = row + delta;
    if (row < 0 || row >= current->tap.sizes.size() || target < 0 || target >= current->tap.sizes.size())
        return;

    SymbolRecord updated = *current;
    updated.tap.sizes.move(row, target);
    updated.tap.strides.move(row, target);

    const Utils::Result result = m_controller->updateSymbol(updated);
    refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
}

void SymbolsPanel::removeTapPatternRow(int row)
{
    flushPendingCommit();
    const SymbolRecord* current = m_controller ? m_controller->symbolById(m_controller->selectedSymbolId()) : nullptr;
    if (!current || current->kind != SymbolKind::TensorAccessPattern || current->tap.sizes.size() <= 1)
        return;

    SymbolRecord updated = *current;
    if (row < 0 || row >= updated.tap.sizes.size())
        return;

    updated.tap.sizes.removeAt(row);
    updated.tap.strides.removeAt(row);

    const Utils::Result result = m_controller->updateSymbol(updated);
    refreshStatusMessage(result ? QString() : result.errors.join(QStringLiteral("\n")), !result.ok);
}

} // namespace Aie::Internal
