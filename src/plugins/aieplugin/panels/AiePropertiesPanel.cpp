// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/AiePropertiesPanel.hpp"

#include "aieplugin/AieService.hpp"
#include "aieplugin/symbol_table/SymbolsController.hpp"
#include "aieplugin/symbol_table/SymbolTableTypes.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasItem.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/api/ICanvasHost.hpp"

#include <utils/ui/SidebarPanelFrame.hpp>

#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QtGlobal>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCompleter>
#include <QtWidgets/QFrame>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace Aie::Internal {

namespace {

QString formatBounds(const QRectF& bounds)
{
    const QRectF normalized = bounds.normalized();
    return QStringLiteral("x:%1 y:%2  w:%3 h:%4")
        .arg(normalized.left(), 0, 'f', 1)
        .arg(normalized.top(), 0, 'f', 1)
        .arg(normalized.width(), 0, 'f', 1)
        .arg(normalized.height(), 0, 'f', 1);
}

// Convert long-form dtype (e.g. "int32", "float32") to the short-form used by type combos.
QString shortValueType(const QString& dtype)
{
    static const QHash<QString, QString> map = {
        { QStringLiteral("int8"),    QStringLiteral("i8")  },
        { QStringLiteral("int16"),   QStringLiteral("i16") },
        { QStringLiteral("int32"),   QStringLiteral("i32") },
        { QStringLiteral("int64"),   QStringLiteral("i64") },
        { QStringLiteral("uint8"),   QStringLiteral("ui8")  },
        { QStringLiteral("uint16"),  QStringLiteral("ui16") },
        { QStringLiteral("uint32"),  QStringLiteral("ui32") },
        { QStringLiteral("bfloat16"),QStringLiteral("bf16") },
        { QStringLiteral("float16"), QStringLiteral("f16")  },
        { QStringLiteral("float32"), QStringLiteral("f32")  },
        { QStringLiteral("float64"), QStringLiteral("f64")  },
    };
    return map.value(dtype.trimmed().toLower(), dtype.trimmed().toLower());
}

} // namespace

AiePropertiesPanel::AiePropertiesPanel(AieService* service, QWidget* parent)
    : QWidget(parent)
    , m_service(service)
{
    setObjectName(QStringLiteral("AiePropertiesPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    buildUi();
    bindCanvasSignalsIfNeeded();
    refreshSelection();

    QTimer::singleShot(0, this, [this]() { refreshSelection(); });
}

void AiePropertiesPanel::setSymbolsController(SymbolsController* controller)
{
    if (m_symbolsController)
        disconnect(m_symbolsController, &SymbolsController::symbolsChanged,
                   this, &AiePropertiesPanel::populateFifoSymbolCombo);
    m_symbolsController = controller;
    if (m_symbolsController)
        connect(m_symbolsController, &SymbolsController::symbolsChanged,
                this, &AiePropertiesPanel::populateFifoSymbolCombo);
    populateFifoSymbolCombo();
}

void AiePropertiesPanel::populateFifoSymbolCombo()
{
    if (!m_fifoSymbolCombo)
        return;

    const QString current = m_fifoSymbolCombo->currentText();
    m_fifoSymbolCombo->blockSignals(true);
    m_fifoSymbolCombo->clear();
    m_fifoSymbolCombo->addItem(QStringLiteral("None"));

    if (m_symbolsController) {
        for (const auto& sym : m_symbolsController->symbols()) {
            if (sym.kind == SymbolKind::TypeAbstraction)
                m_fifoSymbolCombo->addItem(sym.name);
        }
    }

    const int idx = m_fifoSymbolCombo->findText(current);
    m_fifoSymbolCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_fifoSymbolCombo->blockSignals(false);

    // Update FIFO dimensions completer with current constant names.
    if (m_fifoDimensionsEdit && m_symbolsController) {
        const QStringList candidates = m_symbolsController->dimensionReferenceCandidates();
        auto* completer = new QCompleter(candidates, m_fifoDimensionsEdit);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_fifoDimensionsEdit->setCompleter(completer);
    }
}

void AiePropertiesPanel::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* frame = new Utils::SidebarPanelFrame(this);
    frame->setTitle(QStringLiteral("Properties"));
    frame->setSubtitle(QStringLiteral("Selection"));
    frame->setSearchEnabled(false);
    frame->setHeaderDividerVisible(true);
    m_frame = frame;

    auto* content = new QWidget(frame);
    content->setObjectName(QStringLiteral("AiePropertiesPanelContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(8);

    auto* summaryLabel = new QLabel(content);
    summaryLabel->setWordWrap(true);
    summaryLabel->setObjectName(QStringLiteral("AiePropertiesSummaryLabel"));
    m_summaryLabel = summaryLabel;

    auto* detailLabel = new QLabel(content);
    detailLabel->setWordWrap(true);
    detailLabel->setObjectName(QStringLiteral("AiePropertiesDetailLabel"));
    detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailLabel = detailLabel;

    auto* scrollArea = new QScrollArea(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setObjectName(QStringLiteral("AiePropertiesScrollArea"));
    if (scrollArea->viewport())
        scrollArea->viewport()->setObjectName(QStringLiteral("AiePropertiesScrollViewport"));

    auto* fieldsHost = new QWidget(scrollArea);
    fieldsHost->setObjectName(QStringLiteral("AiePropertiesFieldsHost"));
    auto* fieldsLayout = new QVBoxLayout(fieldsHost);
    fieldsLayout->setContentsMargins(0, 0, 0, 0);
    fieldsLayout->setSpacing(10);

    auto* tileGroup = new QGroupBox(QStringLiteral("Tile"), fieldsHost);
    tileGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    auto* tileForm = new QFormLayout(tileGroup);
    tileForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    tileForm->setContentsMargins(12, 12, 12, 12);
    tileForm->setHorizontalSpacing(10);
    tileForm->setVerticalSpacing(8);
    tileForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    const auto makeKeyLabel = [tileGroup](const QString& text) -> QLabel* {
        auto* label = new QLabel(text, tileGroup);
        label->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        return label;
    };

    auto* tileIdValue = new QLabel(tileGroup);
    tileIdValue->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    tileIdValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* tileSpecIdValue = new QLabel(tileGroup);
    tileSpecIdValue->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    tileSpecIdValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* tileBoundsValue = new QLabel(tileGroup);
    tileBoundsValue->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    tileBoundsValue->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* tileLabelEdit = new QLineEdit(tileGroup);
    tileLabelEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    auto* tileStereotypeEdit = new QLineEdit(tileGroup);
    tileStereotypeEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    tileStereotypeEdit->setReadOnly(true);
    tileStereotypeEdit->setPlaceholderText(QStringLiteral("None"));

    auto* tileStereotypeClearBtn = new QPushButton(QStringLiteral("Clear"), tileGroup);
    tileStereotypeClearBtn->setObjectName(QStringLiteral("AiePropertiesClearButton"));
    tileStereotypeClearBtn->setEnabled(false);

    auto* kernelRow = new QWidget(tileGroup);
    auto* kernelRowLayout = new QHBoxLayout(kernelRow);
    kernelRowLayout->setContentsMargins(0, 0, 0, 0);
    kernelRowLayout->setSpacing(6);
    kernelRowLayout->addWidget(tileStereotypeEdit, 1);
    kernelRowLayout->addWidget(tileStereotypeClearBtn);

    tileForm->addRow(makeKeyLabel(QStringLiteral("Item ID")), tileIdValue);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Spec ID")), tileSpecIdValue);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Bounds")), tileBoundsValue);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Label")), tileLabelEdit);
    auto* kernelRowLabel = makeKeyLabel(QStringLiteral("Kernel"));
    tileForm->addRow(kernelRowLabel, kernelRow);

    m_tileGroup = tileGroup;
    m_tileIdValue = tileIdValue;
    m_tileSpecIdValue = tileSpecIdValue;
    m_tileBoundsValue = tileBoundsValue;
    m_tileLabelEdit = tileLabelEdit;
    m_tileStereotypeEdit = tileStereotypeEdit;
    m_tileStereotypeClearBtn = tileStereotypeClearBtn;
    m_tileKernelRow = kernelRow;
    m_tileKernelRowLabel = kernelRowLabel;

    auto* fifoGroup = new QGroupBox(QStringLiteral("FIFO Annotation"), fieldsHost);
    fifoGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    auto* fifoForm = new QFormLayout(fifoGroup);
    fifoForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    fifoForm->setContentsMargins(12, 12, 12, 12);
    fifoForm->setHorizontalSpacing(10);
    fifoForm->setVerticalSpacing(8);
    fifoForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    const auto makeFifoKeyLabel = [fifoGroup](const QString& text) -> QLabel* {
        auto* label = new QLabel(text, fifoGroup);
        label->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        return label;
    };

    auto* fifoWireIdValue = new QLabel(fifoGroup);
    fifoWireIdValue->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    fifoWireIdValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* fifoNameEdit = new QLineEdit(fifoGroup);
    fifoNameEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    auto* fifoDepthSpin = new QSpinBox(fifoGroup);
    fifoDepthSpin->setObjectName(QStringLiteral("AiePropertiesField"));
    fifoDepthSpin->setRange(1, 4096);
    auto* fifoSymbolCombo = new QComboBox(fifoGroup);
    fifoSymbolCombo->setObjectName(QStringLiteral("AiePropertiesField"));
    fifoSymbolCombo->addItem(QStringLiteral("None"));
    auto* fifoTypeCombo = new QComboBox(fifoGroup);
    fifoTypeCombo->setObjectName(QStringLiteral("AiePropertiesField"));
    fifoTypeCombo->addItem(QStringLiteral("i8"));
    fifoTypeCombo->addItem(QStringLiteral("i16"));
    fifoTypeCombo->addItem(QStringLiteral("i32"));
    auto* fifoDimensionsEdit = new QLineEdit(fifoGroup);
    fifoDimensionsEdit->setObjectName(QStringLiteral("AiePropertiesField"));

    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Wire ID")), fifoWireIdValue);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Name")), fifoNameEdit);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Depth")), fifoDepthSpin);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Symbol")), fifoSymbolCombo);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Value Type")), fifoTypeCombo);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Dimensions")), fifoDimensionsEdit);

    m_fifoGroup = fifoGroup;
    m_fifoWireIdValue = fifoWireIdValue;
    m_fifoNameEdit = fifoNameEdit;
    m_fifoDepthSpin = fifoDepthSpin;
    m_fifoSymbolCombo = fifoSymbolCombo;
    m_fifoTypeCombo = fifoTypeCombo;
    m_fifoDimensionsEdit = fifoDimensionsEdit;

    // --- Hub Pivot group (Split / Join wires) ---
    auto* hubPivotGroup = new QGroupBox(QStringLiteral("Split / Join"), fieldsHost);
    hubPivotGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    auto* hubPivotForm = new QFormLayout(hubPivotGroup);
    hubPivotForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    hubPivotForm->setContentsMargins(12, 12, 12, 12);
    hubPivotForm->setHorizontalSpacing(10);
    hubPivotForm->setVerticalSpacing(8);
    hubPivotForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    const auto makeHubKeyLabel = [hubPivotGroup](const QString& text) -> QLabel* {
        auto* label = new QLabel(text, hubPivotGroup);
        label->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        return label;
    };
    const auto makeHubValueLabel = [hubPivotGroup]() -> QLabel* {
        auto* label = new QLabel(hubPivotGroup);
        label->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };

    auto* hubPivotNameEdit = new QLineEdit(hubPivotGroup);
    hubPivotNameEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    hubPivotNameEdit->setPlaceholderText(QStringLiteral("e.g. split1"));

    auto* hubPivotFifoLabel = new QLabel(QStringLiteral("Source FIFO"), hubPivotGroup);
    hubPivotFifoLabel->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
    auto* hubPivotFifoEdit = new QLineEdit(hubPivotGroup);
    hubPivotFifoEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    hubPivotFifoEdit->setPlaceholderText(QStringLiteral("e.g. A"));

    auto* hubBranchesValue  = makeHubValueLabel();
    auto* hubOffsetsValue   = makeHubValueLabel();
    auto* hubDepthValue     = makeHubValueLabel();
    auto* hubValueTypeValue = makeHubValueLabel();
    auto* hubDimensionsValue = makeHubValueLabel();

    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Name")),       hubPivotNameEdit);
    hubPivotForm->addRow(hubPivotFifoLabel,                              hubPivotFifoEdit);
    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Branches")),   hubBranchesValue);
    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Offsets")),    hubOffsetsValue);
    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Depth")),      hubDepthValue);
    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Value Type")), hubValueTypeValue);
    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Dimensions")), hubDimensionsValue);

    m_hubPivotGroup      = hubPivotGroup;
    m_hubPivotNameEdit   = hubPivotNameEdit;
    m_hubPivotFifoLabel  = hubPivotFifoLabel;
    m_hubPivotFifoEdit   = hubPivotFifoEdit;
    m_hubBranchesValue   = hubBranchesValue;
    m_hubOffsetsValue    = hubOffsetsValue;
    m_hubDepthValue      = hubDepthValue;
    m_hubValueTypeValue  = hubValueTypeValue;
    m_hubDimensionsValue = hubDimensionsValue;

    auto* ddrGroup = new QGroupBox(QStringLiteral("DDR Runtime"), fieldsHost);
    ddrGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    new QVBoxLayout(ddrGroup);
    m_ddrGroup = ddrGroup;

    fieldsLayout->addWidget(tileGroup);
    fieldsLayout->addWidget(fifoGroup);
    fieldsLayout->addWidget(hubPivotGroup);
    fieldsLayout->addWidget(ddrGroup);
    fieldsLayout->addStretch(1);

    scrollArea->setWidget(fieldsHost);

    contentLayout->addWidget(summaryLabel);
    contentLayout->addWidget(detailLabel);
    contentLayout->addWidget(scrollArea, 1);

    frame->setContentWidget(content);
    rootLayout->addWidget(frame);

    connect(tileLabelEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyTileLabel);
    connect(tileStereotypeClearBtn, &QPushButton::clicked, this,
            &AiePropertiesPanel::applyTileStereotype);

    connect(fifoNameEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyFifoProperties);
    connect(fifoDepthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { applyFifoProperties(); });
    connect(fifoSymbolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, fifoSymbolCombo, fifoTypeCombo, fifoDimensionsEdit](int idx) {
                const bool isSymbol = (idx > 0);
                fifoTypeCombo->setEnabled(!isSymbol);
                fifoDimensionsEdit->setEnabled(!isSymbol);
                if (isSymbol && m_symbolsController) {
                    const QString symName = fifoSymbolCombo->currentText();
                    for (const auto& s : m_symbolsController->symbols()) {
                        if (s.name == symName && s.kind == SymbolKind::TypeAbstraction) {
                            fifoDimensionsEdit->setText(s.type.shapeTokens.join(u'x'));
                            const int ti = fifoTypeCombo->findText(shortValueType(s.type.dtype));
                            if (ti >= 0) fifoTypeCombo->setCurrentIndex(ti);
                            break;
                        }
                    }
                }
                applyFifoProperties();
            });
    connect(fifoTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyFifoProperties(); });
    connect(fifoDimensionsEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyFifoProperties);

    connect(hubPivotNameEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);
    connect(hubPivotFifoEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);
}

void AiePropertiesPanel::bindCanvasSignalsIfNeeded()
{
    auto* nextHost = m_service ? m_service->canvasHost() : nullptr;
    if (m_canvasHost == nextHost)
        return;

    if (m_canvasHost)
        disconnect(m_canvasHost, nullptr, this, nullptr);
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    if (m_canvasView)
        disconnect(m_canvasView, nullptr, this, nullptr);

    m_canvasHost = nextHost;
    m_document = m_canvasHost ? m_canvasHost->document() : nullptr;
    m_canvasView = m_canvasHost ? qobject_cast<Canvas::CanvasView*>(m_canvasHost->viewWidget()) : nullptr;

    if (m_canvasHost) {
        connect(m_canvasHost, &Canvas::Api::ICanvasHost::canvasActiveChanged,
                this, [this](bool) { refreshSelection(); });
    }
    if (m_document) {
        connect(m_document, &Canvas::CanvasDocument::changed,
                this, &AiePropertiesPanel::refreshSelection);
    }
    if (m_canvasView) {
        connect(m_canvasView, &Canvas::CanvasView::selectedItemsChanged,
                this, &AiePropertiesPanel::refreshSelection);
        connect(m_canvasView, &Canvas::CanvasView::selectedItemChanged,
                this, [this](Canvas::ObjectId) { refreshSelection(); });
    }
}

void AiePropertiesPanel::showSelectionState(SelectionKind kind,
                                            const QString& summary,
                                            const QString& detail)
{
    if (m_summaryLabel)
        m_summaryLabel->setText(summary);
    if (m_detailLabel) {
        m_detailLabel->setVisible(!detail.trimmed().isEmpty());
        m_detailLabel->setText(detail);
    }

    const bool showTile     = (kind == SelectionKind::Tile);
    const bool showFifo     = (kind == SelectionKind::FifoWire);
    const bool showHubPivot = (kind == SelectionKind::HubPivotWire);
    const bool showDdr      = (kind == SelectionKind::DdrBlock);
    if (m_tileGroup)
        m_tileGroup->setVisible(showTile);
    if (m_fifoGroup)
        m_fifoGroup->setVisible(showFifo);
    if (m_hubPivotGroup)
        m_hubPivotGroup->setVisible(showHubPivot);
    if (m_ddrGroup)
        m_ddrGroup->setVisible(showDdr);
}

Canvas::CanvasBlock* AiePropertiesPanel::selectedBlock() const
{
    if (!m_canvasView || !m_document)
        return nullptr;
    const Canvas::ObjectId itemId = m_canvasView->selectedItem();
    if (itemId.isNull())
        return nullptr;
    return dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(itemId));
}

Canvas::CanvasWire* AiePropertiesPanel::selectedFifoWire() const
{
    if (!m_canvasView || !m_document)
        return nullptr;
    const Canvas::ObjectId itemId = m_canvasView->selectedItem();
    if (itemId.isNull())
        return nullptr;
    auto* wire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(itemId));
    if (!wire || !wire->hasObjectFifo())
        return nullptr;
    return wire;
}

void AiePropertiesPanel::refreshSelection()
{
    bindCanvasSignalsIfNeeded();

    if (!m_canvasHost || !m_document || !m_canvasView || !m_canvasHost->canvasActive()) {
        showSelectionState(SelectionKind::None,
                           QStringLiteral("Open an AIE design to edit properties."),
                           QString());
        return;
    }

    const Canvas::ObjectId selectedItemId = m_canvasView->selectedItem();
    if (selectedItemId.isNull()) {
        showSelectionState(SelectionKind::None,
                           QStringLiteral("Select a tile or FIFO annotation."),
                           QStringLiteral("Click a tile block or FIFO annotation label on canvas."));
        return;
    }

    auto* item = m_document->findItem(selectedItemId);
    if (auto* block = dynamic_cast<Canvas::CanvasBlock*>(item)) {
        if (block->specId().trimmed() == QStringLiteral("ddr")) {
            if (!m_updatingUi)
                rebuildDdrGroup(block);
            showSelectionState(SelectionKind::DdrBlock,
                               QStringLiteral("DDR block selected"),
                               QStringLiteral("Configure runtime inputs and outputs."));
            return;
        }

        m_updatingUi = true;
        if (m_tileIdValue)
            m_tileIdValue->setText(block->id().toString());
        if (m_tileSpecIdValue)
            m_tileSpecIdValue->setText(block->specId().trimmed().isEmpty()
                                           ? QStringLiteral("-")
                                           : block->specId().trimmed());
        if (m_tileBoundsValue)
            m_tileBoundsValue->setText(formatBounds(block->boundsScene()));
        if (m_tileLabelEdit)
            m_tileLabelEdit->setText(block->label());
        const bool isComputeTile = block->specId().trimmed().startsWith(u"aie");
        if (m_tileKernelRow)
            m_tileKernelRow->setVisible(isComputeTile);
        if (m_tileKernelRowLabel)
            m_tileKernelRowLabel->setVisible(isComputeTile);

        if (isComputeTile && m_tileStereotypeEdit) {
            // Strip UML stereotype decorators: <<kernel: name>> → name
            QString kernelDisplay = block->stereotype();
            if (kernelDisplay.startsWith(u"<<") && kernelDisplay.endsWith(u">>"))
                kernelDisplay = kernelDisplay.sliced(2, kernelDisplay.size() - 4).trimmed();
            if (kernelDisplay.startsWith(u"kernel:"))
                kernelDisplay = kernelDisplay.sliced(7).trimmed();
            m_tileStereotypeEdit->setText(kernelDisplay);
            if (m_tileStereotypeClearBtn)
                m_tileStereotypeClearBtn->setEnabled(!block->stereotype().isEmpty());
        }
        m_updatingUi = false;

        showSelectionState(SelectionKind::Tile,
                           QStringLiteral("Tile selected"),
                           QStringLiteral("Update tile label and stereotype."));
        return;
    }

    if (auto* wire = dynamic_cast<Canvas::CanvasWire*>(item)) {
        if (!wire->hasObjectFifo()) {
            showSelectionState(SelectionKind::Unsupported,
                               QStringLiteral("Wire selected"),
                               QStringLiteral("Only FIFO annotation wires are editable in this panel."));
            return;
        }

        const auto fifo = wire->objectFifo().value();
        const bool isPivot = (fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Split ||
                               fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Join  ||
                               fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Forward);

        if (isPivot) {
            const bool isSplit    = (fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Split);
            const bool isBroadcast = (fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Forward);

            // Count arm branches: find the hub block at endpoint B, count ports by arm role.
            // Broadcast arms are producer ports (same role as split arms).
            int numBranches = 0;
            if (wire->b().attached.has_value()) {
                auto* hubBlock = dynamic_cast<Canvas::CanvasBlock*>(
                    m_document->findItem(wire->b().attached->itemId));
                if (hubBlock) {
                    const Canvas::PortRole armRole = (isSplit || isBroadcast)
                        ? Canvas::PortRole::Producer
                        : Canvas::PortRole::Consumer;
                    for (const auto& port : hubBlock->ports()) {
                        if (port.role == armRole)
                            ++numBranches;
                    }
                }
            }

            // Compute offsets string — not applicable for broadcasts (full FIFO forwarded).
            QString offsetsStr;
            if (!isBroadcast && numBranches > 0) {
                int elemCount = 1024;
                if (!fifo.type.dimensions.isEmpty()) {
                    int count = 1;
                    for (const QString& d : fifo.type.dimensions.split(u'x', Qt::SkipEmptyParts))
                        count *= d.trimmed().toInt();
                    if (count > 0)
                        elemCount = count;
                }
                const int stride = elemCount / numBranches;
                QStringList parts;
                parts.reserve(numBranches);
                for (int i = 0; i < numBranches; ++i)
                    parts.append(QString::number(stride * i));
                offsetsStr = parts.join(QStringLiteral(", "));
            }

            m_updatingUi = true;
            if (m_hubPivotGroup)
                m_hubPivotGroup->setTitle(isBroadcast
                    ? QStringLiteral("Broadcast")
                    : isSplit ? QStringLiteral("Split")
                              : QStringLiteral("Join"));
            if (m_hubPivotFifoLabel)
                m_hubPivotFifoLabel->setText(isSplit || isBroadcast
                    ? QStringLiteral("Source FIFO")
                    : QStringLiteral("Destination FIFO"));
            if (m_hubPivotNameEdit)
                m_hubPivotNameEdit->setText(fifo.hubName);
            if (m_hubPivotFifoEdit)
                m_hubPivotFifoEdit->setText(fifo.name);
            if (m_hubBranchesValue)
                m_hubBranchesValue->setText(numBranches > 0
                    ? QString::number(numBranches) : QStringLiteral("-"));
            if (m_hubOffsetsValue)
                m_hubOffsetsValue->setText(offsetsStr.isEmpty()
                    ? QStringLiteral("-") : offsetsStr);
            if (m_hubDepthValue)
                m_hubDepthValue->setText(QString::number(fifo.depth));
            if (m_hubValueTypeValue)
                m_hubValueTypeValue->setText(fifo.type.valueType.isEmpty()
                    ? QStringLiteral("i32") : fifo.type.valueType);
            if (m_hubDimensionsValue)
                m_hubDimensionsValue->setText(fifo.type.dimensions.isEmpty()
                    ? QStringLiteral("-") : fifo.type.dimensions);
            m_updatingUi = false;

            showSelectionState(SelectionKind::HubPivotWire,
                               isBroadcast ? QStringLiteral("Broadcast hub wire selected")
                               : isSplit   ? QStringLiteral("Split hub wire selected")
                                           : QStringLiteral("Join hub wire selected"),
                               QStringLiteral("Edit hub name and source/destination FIFO."));
            return;
        }

        m_updatingUi = true;
        if (m_fifoWireIdValue)
            m_fifoWireIdValue->setText(wire->id().toString());
        if (m_fifoNameEdit)
            m_fifoNameEdit->setText(fifo.name);
        if (m_fifoDepthSpin)
            m_fifoDepthSpin->setValue(fifo.depth);

        // Symbol combo
        const QString currentSymbol = fifo.type.symbolRef.value_or(QString{});
        if (m_fifoSymbolCombo) {
            const int symIdx = currentSymbol.isEmpty() ? 0 : m_fifoSymbolCombo->findText(currentSymbol);
            m_fifoSymbolCombo->setCurrentIndex(symIdx >= 0 ? symIdx : 0);
        }
        const bool usingSymbol = !currentSymbol.isEmpty() && (m_fifoSymbolCombo && m_fifoSymbolCombo->currentIndex() > 0);

        if (m_fifoTypeCombo) {
            m_fifoTypeCombo->setEnabled(!usingSymbol);
            const QString valueType = fifo.type.valueType.trimmed().toLower();
            const int index = m_fifoTypeCombo->findText(valueType);
            m_fifoTypeCombo->setCurrentIndex(index >= 0 ? index : m_fifoTypeCombo->findText(QStringLiteral("i32")));
        }
        if (m_fifoDimensionsEdit) {
            m_fifoDimensionsEdit->setEnabled(!usingSymbol);
            m_fifoDimensionsEdit->setText(fifo.type.dimensions);
        }
        m_updatingUi = false;

        showSelectionState(SelectionKind::FifoWire,
                           QStringLiteral("FIFO annotation selected"),
                           QStringLiteral("Edit name, depth, dimensions, and value type."));
        return;
    }

    showSelectionState(SelectionKind::Unsupported,
                       QStringLiteral("Selection not supported"),
                       QStringLiteral("This panel currently supports tiles and FIFO annotations."));
}

void AiePropertiesPanel::applyTileLabel()
{
    if (m_updatingUi || !m_document || !m_tileLabelEdit)
        return;

    auto* block = selectedBlock();
    if (!block)
        return;

    const QString next = m_tileLabelEdit->text().trimmed();
    if (block->label() == next)
        return;

    block->setLabel(next);
    m_document->notifyChanged();
}

void AiePropertiesPanel::applyTileStereotype()
{
    if (m_updatingUi || !m_document || !m_tileStereotypeEdit)
        return;

    auto* block = selectedBlock();
    if (!block || block->stereotype().isEmpty())
        return;

    block->setStereotype(QString{});
    m_document->notifyChanged();

    m_updatingUi = true;
    m_tileStereotypeEdit->clear();
    if (m_tileStereotypeClearBtn)
        m_tileStereotypeClearBtn->setEnabled(false);
    m_updatingUi = false;
}

void AiePropertiesPanel::applyFifoProperties()
{
    if (m_updatingUi || !m_document || !m_fifoNameEdit || !m_fifoDepthSpin ||
        !m_fifoSymbolCombo || !m_fifoTypeCombo || !m_fifoDimensionsEdit) {
        return;
    }

    auto* wire = selectedFifoWire();
    if (!wire)
        return;

    Canvas::CanvasWire::ObjectFifoConfig config = wire->objectFifo().value();
    config.name  = m_fifoNameEdit->text().trimmed();
    config.depth = m_fifoDepthSpin->value();

    // Symbol reference takes precedence over literal type/dims
    const bool usingSymbol = m_fifoSymbolCombo && m_fifoSymbolCombo->currentIndex() > 0;
    if (usingSymbol) {
        const QString symName = m_fifoSymbolCombo->currentText();
        config.type.symbolRef = symName;
        // Resolve and store concrete dims/valueType from the symbol so other code still works.
        if (m_symbolsController) {
            for (const auto& sym : m_symbolsController->symbols()) {
                if (sym.name == symName && sym.kind == SymbolKind::TypeAbstraction) {
                    config.type.dimensions = sym.type.shapeTokens.join(u'x');
                    config.type.valueType  = sym.type.dtype;
                    break;
                }
            }
        }
    } else {
        config.type.symbolRef = std::nullopt;
        config.type.valueType = m_fifoTypeCombo->currentText().trimmed().toLower();
    }

    // Validate new dimensions against the DDR total buffer size.
    // The FIFO size must be a divisor of the total (element counts).
    const QString newDims = m_fifoDimensionsEdit->text().trimmed();
    if (!newDims.isEmpty()) {
        // Find which endpoint is a SHIM, then find the DDR↔SHIM wire for it.
        const auto findShimDdrWire = [&]() -> Canvas::CanvasWire* {
            if (!wire->a().attached.has_value() || !wire->b().attached.has_value())
                return nullptr;
            auto* bkA = dynamic_cast<Canvas::CanvasBlock*>(
                m_document->findItem(wire->a().attached->itemId));
            auto* bkB = dynamic_cast<Canvas::CanvasBlock*>(
                m_document->findItem(wire->b().attached->itemId));
            if (!bkA || !bkB) return nullptr;
            Canvas::CanvasBlock* shimBlock =
                bkA->specId().startsWith(QLatin1StringView("shim")) ? bkA :
                bkB->specId().startsWith(QLatin1StringView("shim")) ? bkB : nullptr;
            if (!shimBlock) return nullptr;
            for (const auto& item : m_document->items()) {
                auto* w = dynamic_cast<Canvas::CanvasWire*>(item.get());
                if (!w || !w->a().attached.has_value() || !w->b().attached.has_value())
                    continue;
                auto* wA = dynamic_cast<Canvas::CanvasBlock*>(
                    m_document->findItem(w->a().attached->itemId));
                auto* wB = dynamic_cast<Canvas::CanvasBlock*>(
                    m_document->findItem(w->b().attached->itemId));
                if (!wA || !wB) continue;
                const bool aDdr = wA->specId() == QLatin1StringView("ddr");
                const bool bDdr = wB->specId() == QLatin1StringView("ddr");
                if ((aDdr && wB->id() == shimBlock->id()) ||
                    (bDdr && wA->id() == shimBlock->id()))
                    return w;
            }
            return nullptr;
        };

        // Helper: product of all 'x'-separated integer parts
        const auto elemCount = [](const QString& dims) -> int {
            int n = 1;
            for (const QString& part : dims.split(u'x', Qt::SkipEmptyParts))
                n *= part.trimmed().toInt();
            return n;
        };

        auto* ddrWire = findShimDdrWire();
        if (ddrWire && ddrWire->hasObjectFifo()) {
            const QString totalDims = ddrWire->objectFifo().value().type.dimensions.trimmed();
            if (!totalDims.isEmpty()) {
                const int total = elemCount(totalDims);
                const int fifo  = elemCount(newDims);
                if (total > 0 && fifo > 0 && total % fifo != 0) {
                    // Not a divisor — revert the field and bail
                    m_updatingUi = true;
                    m_fifoDimensionsEdit->setText(config.type.dimensions);
                    m_updatingUi = false;
                    return;
                }
            }
        }
    }

    if (!usingSymbol)
        config.type.dimensions = newDims;

    wire->setObjectFifo(config);
    m_document->notifyChanged();
}

void AiePropertiesPanel::applyHubPivotProperties()
{
    if (m_updatingUi || !m_document || !m_hubPivotNameEdit || !m_hubPivotFifoEdit)
        return;

    auto* wire = selectedFifoWire();
    if (!wire)
        return;

    Canvas::CanvasWire::ObjectFifoConfig config = wire->objectFifo().value();
    config.hubName = m_hubPivotNameEdit->text().trimmed();
    config.name    = m_hubPivotFifoEdit->text().trimmed();

    wire->setObjectFifo(config);
    m_document->notifyChanged();
}

void AiePropertiesPanel::rebuildDdrGroup(Canvas::CanvasBlock* ddrBlock)
{
    if (!m_document || !m_ddrGroup)
        return;

    // Remove old dynamic content
    if (m_ddrContent) {
        delete m_ddrContent;
        m_ddrContent = nullptr;
    }

    const auto& items = m_document->items();

    // Pass 1: find fill SHIMs (DDR→SHIM) and drain SHIMs (SHIM→DDR).
    // Also keep the DDR↔SHIM wire itself — its ObjectFifo dimensions = total DDR buffer size.
    QSet<Canvas::ObjectId> fillShimIds;
    QSet<Canvas::ObjectId> drainShimIds;
    QHash<Canvas::ObjectId, Canvas::CanvasWire*> fillShimDdrWires;
    QHash<Canvas::ObjectId, Canvas::CanvasWire*> drainShimDdrWires;

    for (const auto& item : items) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire) continue;
        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value()) continue;

        auto* blockA = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(epA.attached->itemId));
        auto* blockB = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(epB.attached->itemId));
        if (!blockA || !blockB) continue;

        if (blockA->id() == ddrBlock->id()
                && blockB->specId().startsWith(QLatin1StringView("shim"))) {
            fillShimIds.insert(blockB->id());
            fillShimDdrWires.insert(blockB->id(), wire);
        } else if (blockB->id() == ddrBlock->id()
                && blockA->specId().startsWith(QLatin1StringView("shim"))) {
            drainShimIds.insert(blockA->id());
            drainShimDdrWires.insert(blockA->id(), wire);
        }
    }

    // Pass 2: find FIFO wires where the SHIM is producer (fill) or consumer (drain).
    struct FifoEntry {
        Canvas::CanvasWire* fifoWire;  // SHIM→compute (transfer size)
        Canvas::CanvasWire* ddrWire;   // DDR→SHIM (total buffer size)
    };
    QList<FifoEntry> fillEntries;
    QList<FifoEntry> drainEntries;

    for (const auto& item : items) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire) continue;
        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value()) continue;

        auto* blockA = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(epA.attached->itemId));
        auto* blockB = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(epB.attached->itemId));
        if (!blockA || !blockB) continue;
        if (blockA->specId() == QStringLiteral("ddr") || blockB->specId() == QStringLiteral("ddr"))
            continue;

        if (fillShimIds.contains(blockA->id()))
            fillEntries.append(FifoEntry{wire, fillShimDdrWires.value(blockA->id())});
        else if (drainShimIds.contains(blockB->id()))
            drainEntries.append(FifoEntry{wire, drainShimDdrWires.value(blockB->id())});
    }

    // Build the dynamic content widget
    auto* content = new QWidget(m_ddrGroup);
    content->setObjectName(QStringLiteral("AieDdrContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 4, 0, 0);
    contentLayout->setSpacing(4);
    m_ddrContent = content;

    // Helper: section header label
    const auto makeHeader = [content](const QString& title) -> QLabel* {
        auto* lbl = new QLabel(title, content);
        lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        return lbl;
    };

    // Collect TypeAbstraction symbol names for DDR symbol combos.
    QStringList typeSymbolNames;
    QStringList dimsCandidates;
    if (m_symbolsController) {
        for (const auto& sym : m_symbolsController->symbols()) {
            if (sym.kind == SymbolKind::TypeAbstraction)
                typeSymbolNames.append(sym.name);
        }
        dimsCandidates = m_symbolsController->dimensionReferenceCandidates();
    }

    // Helper: one row of [name][totalDims][type] for a fill/drain param.
    // fifoWire: SHIM→compute wire (name and valueType)
    // ddrWire:  DDR→SHIM wire (total buffer dimensions for main())
    const auto makeRow = [this, content, contentLayout, typeSymbolNames, dimsCandidates](
            Canvas::CanvasWire* fifoWire, Canvas::CanvasWire* ddrWire, const QString& defaultName)
    {
        QString name = defaultName;
        QString dims = QStringLiteral("1024");
        QString type = QStringLiteral("i32");
        bool isMatrix = false;
        QString ddrSymbol;
        Canvas::CanvasWire::TensorTilerConfig tapCfg;
        if (fifoWire->hasObjectFifo()) {
            const auto& cfg = fifoWire->objectFifo().value();
            if (!cfg.name.isEmpty())           name = cfg.name;
            if (!cfg.type.valueType.isEmpty()) type = cfg.type.valueType;
            ddrSymbol = cfg.type.symbolRef.value_or(QString{});
        }
        // Total buffer size and TAP config live on the DDR→SHIM wire
        if (ddrWire && ddrWire->hasObjectFifo()) {
            const auto& ddrCfg = ddrWire->objectFifo().value();
            const QString d = ddrCfg.type.dimensions.trimmed();
            if (!d.isEmpty()) dims = d;
            isMatrix = (ddrCfg.type.mode == Canvas::CanvasWire::DimensionMode::Matrix);
            if (ddrCfg.type.tap.has_value())
                tapCfg = *ddrCfg.type.tap;
        }

        // ── Symbol row ───────────────────────────────────────────────────────
        auto* symbolRow = new QWidget(content);
        auto* symbolRowLayout = new QHBoxLayout(symbolRow);
        symbolRowLayout->setContentsMargins(0, 0, 0, 0);
        symbolRowLayout->setSpacing(4);
        auto* symLbl = new QLabel(QStringLiteral("Symbol:"), symbolRow);
        symLbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        auto* ddrSymbolCombo = new QComboBox(symbolRow);
        ddrSymbolCombo->setObjectName(QStringLiteral("AiePropertiesField"));
        ddrSymbolCombo->addItem(QStringLiteral("None"));
        for (const QString& sn : typeSymbolNames)
            ddrSymbolCombo->addItem(sn);
        {
            const int si = ddrSymbol.isEmpty() ? 0 : ddrSymbolCombo->findText(ddrSymbol);
            ddrSymbolCombo->setCurrentIndex(si >= 0 ? si : 0);
        }
        symbolRowLayout->addWidget(symLbl);
        symbolRowLayout->addWidget(ddrSymbolCombo, 1);
        contentLayout->addWidget(symbolRow);

        // ── Top row: Name | Dims | Type | Mode ──────────────────────────────
        auto* row = new QWidget(content);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* nameEdit = new QLineEdit(name, row);
        nameEdit->setObjectName(QStringLiteral("AiePropertiesField"));
        nameEdit->setFixedWidth(55);
        nameEdit->setPlaceholderText(QStringLiteral("name"));

        auto* dimsEdit = new QLineEdit(dims, row);
        dimsEdit->setObjectName(QStringLiteral("AiePropertiesField"));
        dimsEdit->setPlaceholderText(QStringLiteral("e.g. 1024 or MxN"));
        if (!dimsCandidates.isEmpty()) {
            auto* dimsCompleter = new QCompleter(dimsCandidates, dimsEdit);
            dimsCompleter->setCaseSensitivity(Qt::CaseInsensitive);
            dimsEdit->setCompleter(dimsCompleter);
        }

        auto* typeCombo = new QComboBox(row);
        typeCombo->setObjectName(QStringLiteral("AiePropertiesField"));
        typeCombo->addItems({QStringLiteral("i8"), QStringLiteral("i16"), QStringLiteral("i32")});
        const int tidx = typeCombo->findText(type.trimmed().toLower());
        typeCombo->setCurrentIndex(tidx >= 0 ? tidx : typeCombo->findText(QStringLiteral("i32")));

        auto* modeCombo = new QComboBox(row);
        modeCombo->setObjectName(QStringLiteral("AiePropertiesField"));
        modeCombo->addItems({QStringLiteral("Vector"), QStringLiteral("Matrix")});
        modeCombo->setCurrentIndex(isMatrix ? 1 : 0);

        const bool ddrUsingSymbol = (ddrSymbolCombo->currentIndex() > 0);
        dimsEdit->setEnabled(!ddrUsingSymbol);
        typeCombo->setEnabled(!ddrUsingSymbol);

        rowLayout->addWidget(nameEdit);
        rowLayout->addWidget(dimsEdit, 1);
        rowLayout->addWidget(typeCombo);
        rowLayout->addWidget(modeCombo);
        contentLayout->addWidget(row);

        // ── TAP section (only shown in Matrix mode) ──────────────────────────
        auto* tapWidget = new QWidget(content);
        tapWidget->setVisible(isMatrix);
        auto* tapForm = new QFormLayout(tapWidget);
        tapForm->setContentsMargins(16, 2, 0, 4);
        tapForm->setSpacing(3);
        tapForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        const auto makeTapField = [tapWidget](const QString& val, const QString& placeholder) {
            auto* e = new QLineEdit(val, tapWidget);
            e->setObjectName(QStringLiteral("AiePropertiesField"));
            e->setPlaceholderText(placeholder);
            return e;
        };
        auto* tileDimsEdit   = makeTapField(tapCfg.tileDims,      QStringLiteral("e.g. 1 x 512"));
        auto* tileCountsEdit = makeTapField(tapCfg.tileCounts,    QStringLiteral("e.g. rows x cols // 512"));
        auto* repeatEdit     = makeTapField(tapCfg.patternRepeat, QStringLiteral("1"));

        tapForm->addRow(QStringLiteral("Tile Dimensions"), tileDimsEdit);
        tapForm->addRow(QStringLiteral("Tile Counts"),     tileCountsEdit);
        tapForm->addRow(QStringLiteral("Pattern Repeat"),  repeatEdit);
        contentLayout->addWidget(tapWidget);

        connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                tapWidget, [tapWidget](int idx) { tapWidget->setVisible(idx == 1); });

        // ── Apply function ───────────────────────────────────────────────────
        const Canvas::ObjectId fifoWireId = fifoWire->id();
        const Canvas::ObjectId ddrWireId  = ddrWire ? ddrWire->id() : Canvas::ObjectId{};
        const auto applyFn = [this, fifoWireId, ddrWireId,
                               nameEdit, dimsEdit, typeCombo, modeCombo,
                               ddrSymbolCombo,
                               tileDimsEdit, tileCountsEdit, repeatEdit]() {
            Canvas::CanvasWire::TensorTilerConfig tap;
            tap.tileDims      = tileDimsEdit->text().trimmed();
            tap.tileCounts    = tileCountsEdit->text().trimmed();
            tap.patternRepeat = repeatEdit->text().trimmed();
            tap.pruneStep     = false;
            tap.index         = 0;
            const QString symName = (ddrSymbolCombo && ddrSymbolCombo->currentIndex() > 0)
                ? ddrSymbolCombo->currentText() : QString{};
            applyDdrEntry(fifoWireId, ddrWireId,
                          nameEdit->text(), dimsEdit->text(), typeCombo->currentText(),
                          modeCombo->currentIndex() == 1, tap, symName);
        };
        connect(nameEdit,       &QLineEdit::editingFinished, this, applyFn);
        connect(dimsEdit,       &QLineEdit::editingFinished, this, applyFn);
        connect(tileDimsEdit,   &QLineEdit::editingFinished, this, applyFn);
        connect(tileCountsEdit, &QLineEdit::editingFinished, this, applyFn);
        connect(repeatEdit,     &QLineEdit::editingFinished, this, applyFn);
        connect(typeCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [applyFn](int) { applyFn(); });
        connect(modeCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [applyFn](int) { applyFn(); });
        connect(ddrSymbolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, ddrSymbolCombo, dimsEdit, typeCombo, applyFn](int idx) {
                    const bool sym = (idx > 0);
                    dimsEdit->setEnabled(!sym);
                    typeCombo->setEnabled(!sym);
                    if (sym && m_symbolsController) {
                        const QString symName = ddrSymbolCombo->currentText();
                        for (const auto& s : m_symbolsController->symbols()) {
                            if (s.name == symName && s.kind == SymbolKind::TypeAbstraction) {
                                dimsEdit->setText(s.type.shapeTokens.join(u'x'));
                                const int ti = typeCombo->findText(shortValueType(s.type.dtype));
                                if (ti >= 0) typeCombo->setCurrentIndex(ti);
                                break;
                            }
                        }
                    }
                    applyFn();
                });
    };

    // Inputs section
    contentLayout->addWidget(makeHeader(QStringLiteral("Inputs")));
    if (fillEntries.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("No fill connections"), content);
        none->setObjectName(QStringLiteral("AiePropertiesDetailLabel"));
        contentLayout->addWidget(none);
    } else {
        for (int i = 0; i < fillEntries.size(); ++i)
            makeRow(fillEntries[i].fifoWire, fillEntries[i].ddrWire, QString(QChar(u'A' + i)));
    }

    // Outputs section
    contentLayout->addWidget(makeHeader(QStringLiteral("Outputs")));
    if (drainEntries.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("No drain connections"), content);
        none->setObjectName(QStringLiteral("AiePropertiesDetailLabel"));
        contentLayout->addWidget(none);
    } else {
        for (int i = 0; i < drainEntries.size(); ++i)
            makeRow(drainEntries[i].fifoWire, drainEntries[i].ddrWire, QStringLiteral("out") + QString::number(i));
    }

    qobject_cast<QVBoxLayout*>(m_ddrGroup->layout())->addWidget(content);
}

void AiePropertiesPanel::applyDdrEntry(Canvas::ObjectId fifoWireId,
                                        Canvas::ObjectId ddrWireId,
                                        const QString& name,
                                        const QString& dims,
                                        const QString& type,
                                        bool isMatrix,
                                        const Canvas::CanvasWire::TensorTilerConfig& tap,
                                        const QString& symbolRef)
{
    if (m_updatingUi || !m_document)
        return;

    // Write name and value type to the FIFO wire (SHIM→compute)
    auto* fifoWire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(fifoWireId));
    if (fifoWire) {
        Canvas::CanvasWire::ObjectFifoConfig cfg;
        if (fifoWire->hasObjectFifo())
            cfg = fifoWire->objectFifo().value();
        else
            cfg.depth = 2;
        cfg.name           = name.trimmed();
        cfg.type.valueType = type.trimmed().toLower();
        cfg.type.symbolRef = symbolRef.isEmpty() ? std::nullopt : std::optional<QString>(symbolRef);
        fifoWire->setObjectFifo(cfg);
    }

    // Write total buffer dimensions, mode, TAP to the DDR→SHIM wire
    auto* ddrWire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(ddrWireId));
    if (ddrWire) {
        Canvas::CanvasWire::ObjectFifoConfig cfg;
        if (ddrWire->hasObjectFifo())
            cfg = ddrWire->objectFifo().value();
        else
            cfg.depth = 1;
        if (!symbolRef.isEmpty()) {
            // Resolve concrete dims/type from the symbol for downstream code.
            if (m_symbolsController) {
                for (const auto& sym : m_symbolsController->symbols()) {
                    if (sym.name == symbolRef && sym.kind == SymbolKind::TypeAbstraction) {
                        cfg.type.dimensions = sym.type.shapeTokens.join(u'x');
                        cfg.type.valueType  = sym.type.dtype;
                        break;
                    }
                }
            }
        } else {
            cfg.type.dimensions = dims.trimmed();
        }
        cfg.type.mode = isMatrix
            ? Canvas::CanvasWire::DimensionMode::Matrix
            : Canvas::CanvasWire::DimensionMode::Vector;
        if (isMatrix && !tap.tileDims.isEmpty())
            cfg.type.tap = tap;
        else
            cfg.type.tap.reset();
        ddrWire->setObjectFifo(cfg);
    }

    m_updatingUi = true;  // prevent rebuildDdrGroup re-entry during notifyChanged
    m_document->notifyChanged();
    m_updatingUi = false;
}

} // namespace Aie::Internal
