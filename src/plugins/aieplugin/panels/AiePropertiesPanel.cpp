// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/AiePropertiesPanel.hpp"

#include "aieplugin/AieService.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasItem.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/api/ICanvasHost.hpp"

#include <utils/ui/SidebarPanelFrame.hpp>

#include <QtCore/QTimer>
#include <QtCore/QtGlobal>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
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

    tileForm->addRow(makeKeyLabel(QStringLiteral("Item ID")), tileIdValue);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Spec ID")), tileSpecIdValue);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Bounds")), tileBoundsValue);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Label")), tileLabelEdit);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Stereotype")), tileStereotypeEdit);

    m_tileGroup = tileGroup;
    m_tileIdValue = tileIdValue;
    m_tileSpecIdValue = tileSpecIdValue;
    m_tileBoundsValue = tileBoundsValue;
    m_tileLabelEdit = tileLabelEdit;
    m_tileStereotypeEdit = tileStereotypeEdit;

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
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Value Type")), fifoTypeCombo);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Dimensions")), fifoDimensionsEdit);

    m_fifoGroup = fifoGroup;
    m_fifoWireIdValue = fifoWireIdValue;
    m_fifoNameEdit = fifoNameEdit;
    m_fifoDepthSpin = fifoDepthSpin;
    m_fifoTypeCombo = fifoTypeCombo;
    m_fifoDimensionsEdit = fifoDimensionsEdit;

    fieldsLayout->addWidget(tileGroup);
    fieldsLayout->addWidget(fifoGroup);
    fieldsLayout->addStretch(1);

    scrollArea->setWidget(fieldsHost);

    contentLayout->addWidget(summaryLabel);
    contentLayout->addWidget(detailLabel);
    contentLayout->addWidget(scrollArea, 1);

    frame->setContentWidget(content);
    rootLayout->addWidget(frame);

    connect(tileLabelEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyTileLabel);
    connect(tileStereotypeEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyTileStereotype);

    connect(fifoNameEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyFifoProperties);
    connect(fifoDepthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { applyFifoProperties(); });
    connect(fifoTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyFifoProperties(); });
    connect(fifoDimensionsEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyFifoProperties);
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

    const bool showTile = (kind == SelectionKind::Tile);
    const bool showFifo = (kind == SelectionKind::FifoWire);
    if (m_tileGroup)
        m_tileGroup->setVisible(showTile);
    if (m_fifoGroup)
        m_fifoGroup->setVisible(showFifo);
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
        if (m_tileStereotypeEdit)
            m_tileStereotypeEdit->setText(block->stereotype());
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
        m_updatingUi = true;
        if (m_fifoWireIdValue)
            m_fifoWireIdValue->setText(wire->id().toString());
        if (m_fifoNameEdit)
            m_fifoNameEdit->setText(fifo.name);
        if (m_fifoDepthSpin)
            m_fifoDepthSpin->setValue(fifo.depth);
        if (m_fifoTypeCombo) {
            const QString valueType = fifo.type.valueType.trimmed().toLower();
            const int index = m_fifoTypeCombo->findText(valueType);
            m_fifoTypeCombo->setCurrentIndex(index >= 0 ? index : m_fifoTypeCombo->findText(QStringLiteral("i32")));
        }
        if (m_fifoDimensionsEdit)
            m_fifoDimensionsEdit->setText(fifo.type.dimensions);
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
    if (!block)
        return;

    const QString next = m_tileStereotypeEdit->text().trimmed();
    if (block->stereotype() == next)
        return;

    block->setStereotype(next);
    m_document->notifyChanged();
}

void AiePropertiesPanel::applyFifoProperties()
{
    if (m_updatingUi || !m_document || !m_fifoNameEdit || !m_fifoDepthSpin ||
        !m_fifoTypeCombo || !m_fifoDimensionsEdit) {
        return;
    }

    auto* wire = selectedFifoWire();
    if (!wire)
        return;

    Canvas::CanvasWire::ObjectFifoConfig config = wire->objectFifo().value();
    config.name = m_fifoNameEdit->text().trimmed();
    config.depth = m_fifoDepthSpin->value();
    config.type.valueType = m_fifoTypeCombo->currentText().trimmed().toLower();
    config.type.dimensions = m_fifoDimensionsEdit->text().trimmed();

    wire->setObjectFifo(config);
    m_document->notifyChanged();
}

} // namespace Aie::Internal
