// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/AiePropertiesPanel.hpp"

#include "aieplugin/AieService.hpp"
#include "aieplugin/symbol_table/SymbolsController.hpp"
#include "aieplugin/symbol_table/SymbolTableTypes.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasController.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasItem.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/api/ICanvasDocumentService.hpp"
#include "canvas/api/ICanvasHost.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"

#include <utils/ui/SidebarPanelFrame.hpp>

#include <QtCore/QSet>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtCore/QtGlobal>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCompleter>
#include <QtWidgets/QFrame>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <functional>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kSymbolsMetadataKey = u"symbols"_s;
const QString kObjectFifoDefaultsKey = u"objectFifoDefaults"_s;
const QString kObjectFifoDefaultNameKey = u"name"_s;
const QString kObjectFifoDefaultDepthKey = u"depth"_s;
const QString kObjectFifoDefaultTypeIdKey = u"typeId"_s;
constexpr int kObjectFifoWireIdRole = Qt::UserRole;
constexpr int kObjectFifoTypeIdRole = Qt::UserRole + 1;
constexpr int kObjectFifoTypeValueTypeRole = Qt::UserRole + 2;
constexpr int kObjectFifoTypeDimensionsRole = Qt::UserRole + 3;

QString formatBounds(const QRectF& bounds)
{
    const QRectF normalized = bounds.normalized();
    return QStringLiteral("x:%1 y:%2  w:%3 h:%4")
        .arg(normalized.left(), 0, 'f', 1)
        .arg(normalized.top(), 0, 'f', 1)
        .arg(normalized.width(), 0, 'f', 1)
        .arg(normalized.height(), 0, 'f', 1);
}

struct FifoTypeOption final {
    QString id;
    QString name;
    QString dtype;
    QString dimensions;
};

struct TapOption final {
    QString id;
    QString name;
};

QString canonicalObjectFifoValueType(QString valueType)
{
    valueType = valueType.trimmed().toLower();
    if (valueType == QStringLiteral("int8"))
        return QStringLiteral("i8");
    if (valueType == QStringLiteral("int16"))
        return QStringLiteral("i16");
    if (valueType == QStringLiteral("int32"))
        return QStringLiteral("i32");
    if (valueType == QStringLiteral("int64"))
        return QStringLiteral("i64");
    if (valueType == QStringLiteral("uint8"))
        return QStringLiteral("ui8");
    if (valueType == QStringLiteral("uint16"))
        return QStringLiteral("ui16");
    if (valueType == QStringLiteral("uint32"))
        return QStringLiteral("ui32");
    if (valueType == QStringLiteral("bfloat16"))
        return QStringLiteral("bf16");
    if (valueType == QStringLiteral("float16"))
        return QStringLiteral("f16");
    if (valueType == QStringLiteral("float32"))
        return QStringLiteral("f32");
    if (valueType == QStringLiteral("float64"))
        return QStringLiteral("f64");
    return valueType.isEmpty() ? QStringLiteral("i32") : valueType;
}

QVector<FifoTypeOption> fifoTypeOptionsFromMetadata(const QJsonObject& metadata)
{
    QVector<SymbolRecord> symbols;
    const Utils::Result parseResult =
        parseSymbolsMetadata(metadata.value(kSymbolsMetadataKey).toObject(), symbols);
    if (!parseResult)
        return {};

    QVector<FifoTypeOption> options;
    for (const SymbolRecord& symbol : symbols) {
        if (symbol.kind != SymbolKind::TypeAbstraction)
            continue;

        FifoTypeOption option;
        option.id = symbol.id;
        option.name = symbol.name;
        option.dtype = canonicalObjectFifoValueType(symbol.type.dtype);
        option.dimensions = symbol.type.shapeTokens.join(QStringLiteral("x"));
        options.push_back(std::move(option));
    }
    return options;
}

QVector<TapOption> tapOptionsFromMetadata(const QJsonObject& metadata)
{
    QVector<SymbolRecord> symbols;
    const Utils::Result parseResult =
        parseSymbolsMetadata(metadata.value(kSymbolsMetadataKey).toObject(), symbols);
    if (!parseResult)
        return {};

    QVector<TapOption> options;
    for (const SymbolRecord& symbol : symbols) {
        if (symbol.kind != SymbolKind::TensorAccessPattern)
            continue;
        options.push_back({symbol.id, symbol.name.trimmed()});
    }
    return options;
}

QString tapLabel(const TapOption& option)
{
    return option.name.trimmed().isEmpty() ? QStringLiteral("Unnamed TAP") : option.name.trimmed();
}

QString objectFifoTypeLabel(const FifoTypeOption& option)
{
    return option.name.trimmed().isEmpty() ? QStringLiteral("Unnamed Type") : option.name.trimmed();
}

QString objectFifoTypeSummary(const Canvas::CanvasWire::ObjectFifoConfig& fifo)
{
    const QString dtype = fifo.type.valueType.trimmed().isEmpty()
        ? QStringLiteral("i32")
        : fifo.type.valueType.trimmed();
    const QString dims = fifo.type.dimensions.trimmed();
    return dims.isEmpty()
        ? dtype
        : QStringLiteral("%1 [%2]").arg(dtype, dims);
}

std::optional<FifoTypeOption> findFifoTypeOption(
    const QVector<FifoTypeOption>& options,
    const Canvas::CanvasWire::ObjectFifoTypeAbstraction& type)
{
    const QString typeId = type.typeId.trimmed();
    if (!typeId.isEmpty()) {
        for (const FifoTypeOption& option : options) {
            if (option.id == typeId)
                return option;
        }
    }

    const QString canonicalValueType = canonicalObjectFifoValueType(type.valueType);
    const QString dimensions = type.dimensions.trimmed();
    for (const FifoTypeOption& option : options) {
        if (canonicalObjectFifoValueType(option.dtype) == canonicalValueType &&
            option.dimensions.trimmed() == dimensions) {
            return option;
        }
    }
    return std::nullopt;
}

class ObjectFifoNameDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent,
                          const QStyleOptionViewItem&,
                          const QModelIndex&) const override
    {
        auto* editor = new QLineEdit(parent);
        editor->setObjectName(QStringLiteral("AiePropertiesField"));
        editor->setFrame(false);
        return editor;
    }
};

class ObjectFifoDepthDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent,
                          const QStyleOptionViewItem&,
                          const QModelIndex&) const override
    {
        auto* editor = new QSpinBox(parent);
        editor->setObjectName(QStringLiteral("AiePropertiesField"));
        editor->setFrame(false);
        editor->setRange(1, 4096);
        return editor;
    }
};

class ObjectFifoTypeDelegate final : public QStyledItemDelegate
{
public:
    using OptionsProvider = std::function<QVector<FifoTypeOption>()>;
    using CommitCallback = std::function<void(int, const QString&, const QString&, const QString&, const QString&)>;

    ObjectFifoTypeDelegate(OptionsProvider optionsProvider,
                           CommitCallback commitCallback,
                           QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_optionsProvider(std::move(optionsProvider))
        , m_commitCallback(std::move(commitCallback))
    {}

    QWidget* createEditor(QWidget* parent,
                          const QStyleOptionViewItem&,
                          const QModelIndex& index) const override
    {
        auto* editor = new QComboBox(parent);
        editor->setObjectName(QStringLiteral("AiePropertiesField"));
        editor->setProperty("fifoTypeEditorReady", false);

        const QVector<FifoTypeOption> options = m_optionsProvider ? m_optionsProvider() : QVector<FifoTypeOption>{};
        editor->addItem(QStringLiteral("None"), QString{});
        editor->setItemData(0, QStringLiteral("i32"), kObjectFifoTypeValueTypeRole);
        editor->setItemData(0, QString{}, kObjectFifoTypeDimensionsRole);
        for (const FifoTypeOption& option : options) {
            editor->addItem(objectFifoTypeLabel(option), option.id);
            const int itemIndex = editor->count() - 1;
            editor->setItemData(itemIndex, option.dtype, kObjectFifoTypeValueTypeRole);
            editor->setItemData(itemIndex, option.dimensions, kObjectFifoTypeDimensionsRole);
        }

        const QString currentTypeId = index.data(kObjectFifoTypeIdRole).toString().trimmed();
        const QString currentValueType = index.data(kObjectFifoTypeValueTypeRole).toString().trimmed();
        const QString currentDimensions = index.data(kObjectFifoTypeDimensionsRole).toString().trimmed();
        bool hasCurrentMatch = false;
        if (!currentTypeId.isEmpty()) {
            hasCurrentMatch = (editor->findData(currentTypeId) >= 0);
        } else {
            for (int itemIndex = 0; itemIndex < editor->count(); ++itemIndex) {
                if (editor->itemData(itemIndex, kObjectFifoTypeValueTypeRole).toString().trimmed() == currentValueType &&
                    editor->itemData(itemIndex, kObjectFifoTypeDimensionsRole).toString().trimmed() == currentDimensions) {
                    hasCurrentMatch = true;
                    break;
                }
            }
        }

        if (!hasCurrentMatch &&
            (!currentValueType.isEmpty() || !currentDimensions.isEmpty()) &&
            !index.data(Qt::DisplayRole).toString().trimmed().isEmpty()) {
            editor->addItem(index.data(Qt::DisplayRole).toString(), currentTypeId);
            const int itemIndex = editor->count() - 1;
            editor->setItemData(itemIndex, currentValueType, kObjectFifoTypeValueTypeRole);
            editor->setItemData(itemIndex, currentDimensions, kObjectFifoTypeDimensionsRole);
        }

        const int row = index.row();
        connect(editor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, editor, row](int currentIndex) {
            if (currentIndex < 0 || !editor->property("fifoTypeEditorReady").toBool())
                return;
            const QString display = editor->currentText();
            const QString typeId = editor->currentData().toString().trimmed();
            const QString valueType = canonicalObjectFifoValueType(
                editor->currentData(kObjectFifoTypeValueTypeRole).toString());
            const QString dimensions =
                editor->currentData(kObjectFifoTypeDimensionsRole).toString().trimmed();
            if (m_commitCallback)
                m_commitCallback(row, display, typeId, valueType, dimensions);
            auto* delegate = const_cast<ObjectFifoTypeDelegate*>(this);
            emit delegate->closeEditor(editor);
        });
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (!combo)
            return;

        const QString currentTypeId = index.data(kObjectFifoTypeIdRole).toString().trimmed();
        const QString currentValueType = index.data(kObjectFifoTypeValueTypeRole).toString().trimmed();
        const QString currentDimensions = index.data(kObjectFifoTypeDimensionsRole).toString().trimmed();

        int selectedIndex = currentTypeId.isEmpty() ? -1 : combo->findData(currentTypeId);
        if (selectedIndex < 0) {
            for (int itemIndex = 0; itemIndex < combo->count(); ++itemIndex) {
                if (combo->itemData(itemIndex, kObjectFifoTypeValueTypeRole).toString().trimmed() == currentValueType &&
                    combo->itemData(itemIndex, kObjectFifoTypeDimensionsRole).toString().trimmed() == currentDimensions) {
                    selectedIndex = itemIndex;
                    break;
                }
            }
        }

        combo->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
        combo->setProperty("fifoTypeEditorReady", true);
    }

    void setModelData(QWidget* editor,
                      QAbstractItemModel* model,
                      const QModelIndex& index) const override
    {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (!combo || !model)
            return;

        model->setData(index, combo->currentText(), Qt::DisplayRole);
        model->setData(index, combo->currentData().toString().trimmed(), kObjectFifoTypeIdRole);
        model->setData(index,
                       canonicalObjectFifoValueType(
                           combo->currentData(kObjectFifoTypeValueTypeRole).toString()),
                       kObjectFifoTypeValueTypeRole);
        model->setData(index,
                       combo->currentData(kObjectFifoTypeDimensionsRole).toString().trimmed(),
                       kObjectFifoTypeDimensionsRole);
    }

private:
    OptionsProvider m_optionsProvider;
    CommitCallback m_commitCallback;
};

QString shortValueType(const QString& dtype)
{
    static const QHash<QString, QString> map = {
        { QStringLiteral("int8"),    QStringLiteral("i8")  },
        { QStringLiteral("int16"),   QStringLiteral("i16") },
        { QStringLiteral("int32"),   QStringLiteral("i32") },
        { QStringLiteral("int64"),   QStringLiteral("i64") },
        { QStringLiteral("uint8"),   QStringLiteral("ui8") },
        { QStringLiteral("uint16"),  QStringLiteral("ui16") },
        { QStringLiteral("uint32"),  QStringLiteral("ui32") },
        { QStringLiteral("bfloat16"),QStringLiteral("bf16") },
        { QStringLiteral("float16"), QStringLiteral("f16") },
        { QStringLiteral("float32"), QStringLiteral("f32") },
        { QStringLiteral("float64"), QStringLiteral("f64") },
    };
    return map.value(dtype.trimmed().toLower(), dtype.trimmed().toLower());
}
} // namespace

AiePropertiesPanel::AiePropertiesPanel(AieService* service,
                                       Canvas::Api::ICanvasDocumentService* canvasDocuments,
                                       QWidget* parent)
    : QWidget(parent)
    , m_service(service)
    , m_canvasDocuments(canvasDocuments)
{
    setObjectName(QStringLiteral("AiePropertiesPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    buildUi();

    if (m_canvasDocuments) {
        connect(m_canvasDocuments, &Canvas::Api::ICanvasDocumentService::documentOpened,
                this, [this](const Canvas::Api::CanvasDocumentHandle&) { refreshSelection(); });
        connect(m_canvasDocuments, &Canvas::Api::ICanvasDocumentService::documentClosed,
                this, [this](const Canvas::Api::CanvasDocumentHandle&,
                             Canvas::Api::CanvasDocumentCloseReason) { refreshSelection(); });
        connect(m_canvasDocuments, &Canvas::Api::ICanvasDocumentService::documentDirtyChanged,
                this, [this](const Canvas::Api::CanvasDocumentHandle&, bool) {
                    refreshObjectFifoSection();
                });
    }

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
    refreshObjectFifoSection();
    refreshSelection();
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

    const auto makeSectionHeading = [](const QString& text, QWidget* parent) -> QLabel* {
        auto* label = new QLabel(text, parent);
        label->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        return label;
    };

    auto* objectFifosGroup = new QGroupBox(QStringLiteral("Object FIFOs"), fieldsHost);
    objectFifosGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    auto* objectFifosLayout = new QVBoxLayout(objectFifosGroup);
    objectFifosLayout->setContentsMargins(12, 12, 12, 12);
    objectFifosLayout->setSpacing(8);

    auto* fifoInventoryHeading = makeSectionHeading(QStringLiteral("Inventory"), objectFifosGroup);
    objectFifosLayout->addWidget(fifoInventoryHeading);

    auto* objectFifoListSurface = new QFrame(objectFifosGroup);
    objectFifoListSurface->setObjectName(QStringLiteral("AieSymbolsListSurface"));
    auto* objectFifoListLayout = new QVBoxLayout(objectFifoListSurface);
    objectFifoListLayout->setContentsMargins(0, 0, 0, 0);
    objectFifoListLayout->setSpacing(0);

    auto* objectFifosTable = new QTableWidget(objectFifoListSurface);
    objectFifosTable->setObjectName(QStringLiteral("AiePropertiesObjectFifosTable"));
    objectFifosTable->setColumnCount(3);
    objectFifosTable->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Type"), QStringLiteral("Depth")});
    objectFifosTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    objectFifosTable->setSelectionMode(QAbstractItemView::SingleSelection);
    objectFifosTable->setEditTriggers(QAbstractItemView::SelectedClicked |
                                      QAbstractItemView::DoubleClicked |
                                      QAbstractItemView::EditKeyPressed);
    objectFifosTable->setShowGrid(false);
    objectFifosTable->setAlternatingRowColors(false);
    objectFifosTable->setFocusPolicy(Qt::StrongFocus);
    objectFifosTable->setWordWrap(false);
    objectFifosTable->verticalHeader()->setVisible(false);
    objectFifosTable->verticalHeader()->setDefaultSectionSize(30);
    objectFifosTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    objectFifosTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    objectFifosTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    objectFifosTable->setMinimumHeight(176);
    objectFifosTable->setItemDelegateForColumn(0, new ObjectFifoNameDelegate(objectFifosTable));
    objectFifosTable->setItemDelegateForColumn(1, new ObjectFifoTypeDelegate(
        [this]() {
            return fifoTypeOptionsFromMetadata(
                m_canvasDocuments ? m_canvasDocuments->activeMetadata() : QJsonObject{});
        },
        [this](int row,
               const QString& display,
               const QString& typeId,
               const QString& valueType,
               const QString& dimensions) {
            applyObjectFifoTypeSelection(row, display, typeId, valueType, dimensions);
        },
        objectFifosTable));
    objectFifosTable->setItemDelegateForColumn(2, new ObjectFifoDepthDelegate(objectFifosTable));
    objectFifoListLayout->addWidget(objectFifosTable, 1);
    objectFifosLayout->addWidget(objectFifoListSurface, 1);

    auto* fifoDefaultsHeading = makeSectionHeading(QStringLiteral("Defaults"), objectFifosGroup);
    objectFifosLayout->addWidget(fifoDefaultsHeading);

    auto* defaultsHost = new QWidget(objectFifosGroup);
    auto* defaultsForm = new QFormLayout(defaultsHost);
    defaultsForm->setContentsMargins(0, 0, 0, 0);
    defaultsForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    defaultsForm->setHorizontalSpacing(10);
    defaultsForm->setVerticalSpacing(8);
    defaultsForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* objectFifoDefaultNameEdit = new QLineEdit(defaultsHost);
    objectFifoDefaultNameEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    auto* objectFifoDefaultDepthSpin = new QSpinBox(defaultsHost);
    objectFifoDefaultDepthSpin->setObjectName(QStringLiteral("AiePropertiesField"));
    objectFifoDefaultDepthSpin->setRange(1, 4096);
    auto* objectFifoDefaultTypeCombo = new QComboBox(defaultsHost);
    objectFifoDefaultTypeCombo->setObjectName(QStringLiteral("AiePropertiesField"));

    defaultsForm->addRow(makeSectionHeading(QStringLiteral("Name"), defaultsHost), objectFifoDefaultNameEdit);
    defaultsForm->addRow(makeSectionHeading(QStringLiteral("Type Abstraction"), defaultsHost), objectFifoDefaultTypeCombo);
    defaultsForm->addRow(makeSectionHeading(QStringLiteral("Depth"), defaultsHost), objectFifoDefaultDepthSpin);
    objectFifosLayout->addWidget(defaultsHost);

    m_objectFifosGroup = objectFifosGroup;
    m_objectFifosTable = objectFifosTable;
    m_objectFifoDefaultNameEdit = objectFifoDefaultNameEdit;
    m_objectFifoDefaultDepthSpin = objectFifoDefaultDepthSpin;
    m_objectFifoDefaultTypeCombo = objectFifoDefaultTypeCombo;

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

    auto* ddrTransferGroup = new QGroupBox(QStringLiteral("DDR Transfer"), fieldsHost);
    ddrTransferGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    auto* ddrTransferForm = new QFormLayout(ddrTransferGroup);
    ddrTransferForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    ddrTransferForm->setContentsMargins(12, 12, 12, 12);
    ddrTransferForm->setHorizontalSpacing(10);
    ddrTransferForm->setVerticalSpacing(8);
    ddrTransferForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* ddrTransferModeValue = new QLabel(ddrTransferGroup);
    ddrTransferModeValue->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    auto* ddrTransferTapCombo = new QComboBox(ddrTransferGroup);
    ddrTransferTapCombo->setObjectName(QStringLiteral("AiePropertiesField"));

    ddrTransferForm->addRow(makeKeyLabel(QStringLiteral("Mode")), ddrTransferModeValue);
    ddrTransferForm->addRow(makeKeyLabel(QStringLiteral("Tensor Access Pattern")), ddrTransferTapCombo);

    m_ddrTransferGroup = ddrTransferGroup;
    m_ddrTransferModeValue = ddrTransferModeValue;
    m_ddrTransferTapCombo = ddrTransferTapCombo;

    auto* ddrGroup = new QGroupBox(QStringLiteral("DDR Runtime"), fieldsHost);
    ddrGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    new QVBoxLayout(ddrGroup);
    m_ddrGroup = ddrGroup;

    fieldsLayout->addWidget(objectFifosGroup);
    fieldsLayout->addWidget(tileGroup);
    fieldsLayout->addWidget(hubPivotGroup);
    fieldsLayout->addWidget(ddrTransferGroup);
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

    connect(hubPivotNameEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);
    connect(hubPivotFifoEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);
    connect(ddrTransferTapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyDdrTransferHubTap(); });

    connect(objectFifoDefaultNameEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyObjectFifoDefaults);
    connect(objectFifoDefaultDepthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { applyObjectFifoDefaults(); });
    connect(objectFifoDefaultTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyObjectFifoDefaults(); });
    connect(objectFifosTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (m_updatingObjectFifoTable || !m_objectFifosTable || !m_canvasView)
            return;
        const auto items = m_objectFifosTable->selectedItems();
        if (items.isEmpty())
            return;
        if (auto* item = m_objectFifosTable->item(items.front()->row(), 0)) {
            const QString idText = item->data(Qt::UserRole).toString();
            if (const auto parsed = Canvas::ObjectId::fromString(idText); parsed.has_value())
                m_canvasView->setSelectedItem(*parsed);
        }
    });
    connect(objectFifosTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem* item) {
                if (!item)
                    return;
                applyObjectFifoRowEdits(item->row());
            });
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

Canvas::CanvasController* AiePropertiesPanel::canvasController() const
{
    return m_canvasHost ? m_canvasHost->controller() : nullptr;
}

void AiePropertiesPanel::refreshObjectFifoDefaultsUi()
{
    if (!m_objectFifoDefaultNameEdit || !m_objectFifoDefaultDepthSpin || !m_objectFifoDefaultTypeCombo)
        return;

    const QJsonObject metadata = m_canvasDocuments ? m_canvasDocuments->activeMetadata() : QJsonObject{};
    const QJsonObject defaultsObject = metadata.value(kObjectFifoDefaultsKey).toObject();
    const QVector<FifoTypeOption> typeOptions = fifoTypeOptionsFromMetadata(metadata);

    const QString persistedName = defaultsObject.value(kObjectFifoDefaultNameKey).toString().trimmed();
    const int persistedDepth = std::max(1, defaultsObject.value(kObjectFifoDefaultDepthKey).toInt(2));
    const QString persistedTypeId = defaultsObject.value(kObjectFifoDefaultTypeIdKey).toString().trimmed();

    Canvas::CanvasController::ObjectFifoDefaults defaults =
        canvasController() ? canvasController()->objectFifoDefaults()
                           : Canvas::CanvasController::ObjectFifoDefaults{};

    if (!persistedName.isEmpty())
        defaults.name = persistedName;
    defaults.depth = persistedDepth;
    defaults.type.typeId = persistedTypeId;

    QString selectedTypeId = persistedTypeId;
    if (!selectedTypeId.isEmpty()) {
        for (const FifoTypeOption& option : typeOptions) {
            if (option.id != selectedTypeId)
                continue;
            defaults.type.valueType = option.dtype;
            defaults.type.dimensions = option.dimensions;
            break;
        }
    }

    m_updatingUi = true;
    QSignalBlocker nameBlock(m_objectFifoDefaultNameEdit);
    QSignalBlocker depthBlock(m_objectFifoDefaultDepthSpin);
    QSignalBlocker typeBlock(m_objectFifoDefaultTypeCombo);

    m_objectFifoDefaultNameEdit->setText(defaults.name);
    m_objectFifoDefaultDepthSpin->setValue(std::max(1, defaults.depth));

    m_objectFifoDefaultTypeCombo->clear();
    m_objectFifoDefaultTypeCombo->addItem(QStringLiteral("None"), QString{});
    m_objectFifoDefaultTypeCombo->setItemData(0, QStringLiteral("i32"), Qt::UserRole + 1);
    m_objectFifoDefaultTypeCombo->setItemData(0, QString{}, Qt::UserRole + 2);
    for (const FifoTypeOption& option : typeOptions) {
        m_objectFifoDefaultTypeCombo->addItem(objectFifoTypeLabel(option), option.id);
        const int index = m_objectFifoDefaultTypeCombo->count() - 1;
        m_objectFifoDefaultTypeCombo->setItemData(index, option.dtype, Qt::UserRole + 1);
        m_objectFifoDefaultTypeCombo->setItemData(index, option.dimensions, Qt::UserRole + 2);
    }

    int selectedIndex = 0;
    if (!selectedTypeId.isEmpty()) {
        const int found = m_objectFifoDefaultTypeCombo->findData(selectedTypeId);
        if (found >= 0)
            selectedIndex = found;
    } else {
        for (int index = 1; index < m_objectFifoDefaultTypeCombo->count(); ++index) {
            if (canonicalObjectFifoValueType(
                    m_objectFifoDefaultTypeCombo->itemData(index, Qt::UserRole + 1).toString()) ==
                    canonicalObjectFifoValueType(defaults.type.valueType) &&
                m_objectFifoDefaultTypeCombo->itemData(index, Qt::UserRole + 2).toString() == defaults.type.dimensions) {
                selectedIndex = index;
                break;
            }
        }
    }
    m_objectFifoDefaultTypeCombo->setCurrentIndex(selectedIndex);
    m_updatingUi = false;

    if (canvasController())
        canvasController()->setObjectFifoDefaults(defaults);
}

void AiePropertiesPanel::refreshObjectFifoSection()
{
    if (!m_objectFifosTable)
        return;

    if (m_objectFifosGroup)
        m_objectFifosGroup->setVisible(m_canvasHost && m_canvasHost->canvasActive() && m_document);

    refreshObjectFifoDefaultsUi();

    m_updatingObjectFifoTable = true;
    QSignalBlocker tableBlock(m_objectFifosTable);
    m_objectFifosTable->clearContents();
    m_objectFifosTable->setRowCount(0);

    if (!m_document || !m_canvasHost || !m_canvasHost->canvasActive()) {
        m_updatingObjectFifoTable = false;
        return;
    }

    const QVector<FifoTypeOption> typeOptions = fifoTypeOptionsFromMetadata(
        m_canvasDocuments ? m_canvasDocuments->activeMetadata() : QJsonObject{});

    int row = 0;
    for (const auto& item : m_document->items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire || !wire->hasObjectFifo())
            continue;

        const auto fifo = wire->objectFifo().value();
        if (fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Fill ||
            fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Drain) {
            continue;
        }
        m_objectFifosTable->insertRow(row);

        auto* nameItem = new QTableWidgetItem(fifo.name);
        nameItem->setData(kObjectFifoWireIdRole, wire->id().toString());
        nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);

        auto* typeItem = new QTableWidgetItem;
        typeItem->setFlags(typeItem->flags() | Qt::ItemIsEditable);
        if (const auto matchedType = findFifoTypeOption(typeOptions, fifo.type); matchedType.has_value()) {
            typeItem->setText(objectFifoTypeLabel(*matchedType));
            typeItem->setData(kObjectFifoTypeIdRole, matchedType->id);
            typeItem->setData(kObjectFifoTypeValueTypeRole, matchedType->dtype);
            typeItem->setData(kObjectFifoTypeDimensionsRole, matchedType->dimensions);
        } else {
            typeItem->setText(objectFifoTypeSummary(fifo));
            typeItem->setData(kObjectFifoTypeIdRole, QString{});
            typeItem->setData(kObjectFifoTypeValueTypeRole, fifo.type.valueType.trimmed());
            typeItem->setData(kObjectFifoTypeDimensionsRole, fifo.type.dimensions.trimmed());
        }

        auto* depthItem = new QTableWidgetItem(QString::number(fifo.depth));
        depthItem->setFlags(depthItem->flags() | Qt::ItemIsEditable);

        m_objectFifosTable->setItem(row, 0, nameItem);
        m_objectFifosTable->setItem(row, 1, typeItem);
        m_objectFifosTable->setItem(row, 2, depthItem);
        ++row;
    }

    const Canvas::ObjectId selectedId = m_canvasView ? m_canvasView->selectedItem() : Canvas::ObjectId{};
    if (!selectedId.isNull()) {
        const QString selectedText = selectedId.toString();
        for (int index = 0; index < m_objectFifosTable->rowCount(); ++index) {
            auto* item = m_objectFifosTable->item(index, 0);
            if (!item || item->data(Qt::UserRole).toString() != selectedText)
                continue;
            m_objectFifosTable->selectRow(index);
            m_objectFifosTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            break;
        }
    }

    m_updatingObjectFifoTable = false;
}

void AiePropertiesPanel::applyObjectFifoRowEdits(int row)
{
    if (m_updatingObjectFifoTable || !m_document || !m_objectFifosTable)
        return;

    auto* nameItem = m_objectFifosTable->item(row, 0);
    auto* typeItem = m_objectFifosTable->item(row, 1);
    auto* depthItem = m_objectFifosTable->item(row, 2);
    if (!nameItem || !typeItem || !depthItem)
        return;

    const QString wireIdText = nameItem->data(kObjectFifoWireIdRole).toString();
    const auto wireId = Canvas::ObjectId::fromString(wireIdText);
    if (!wireId.has_value())
        return;

    auto* wire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(*wireId));
    if (!wire || !wire->hasObjectFifo())
        return;

    Canvas::CanvasWire::ObjectFifoConfig config = wire->objectFifo().value();
    const QString normalizedName = nameItem->text().trimmed().isEmpty()
        ? QStringLiteral("of")
        : nameItem->text().trimmed();

    bool depthOk = false;
    int normalizedDepth = depthItem->text().trimmed().toInt(&depthOk);
    if (!depthOk)
        normalizedDepth = config.depth;
    normalizedDepth = std::max(1, normalizedDepth);

    QString nextValueType = canonicalObjectFifoValueType(
        typeItem->data(kObjectFifoTypeValueTypeRole).toString());
    QString nextDimensions = typeItem->data(kObjectFifoTypeDimensionsRole).toString().trimmed();
    const QString nextTypeId = typeItem->data(kObjectFifoTypeIdRole).toString().trimmed();

    const bool changed = (config.name != normalizedName) ||
                         (config.depth != normalizedDepth) ||
                         (config.type.typeId.trimmed() != nextTypeId) ||
                         (canonicalObjectFifoValueType(config.type.valueType) != nextValueType) ||
                         (config.type.dimensions.trimmed() != nextDimensions);

    {
        QSignalBlocker blocker(m_objectFifosTable);
        m_updatingObjectFifoTable = true;
        nameItem->setText(normalizedName);
        depthItem->setText(QString::number(normalizedDepth));
        m_updatingObjectFifoTable = false;
    }

    if (!changed)
        return;

    config.name = normalizedName;
    config.depth = normalizedDepth;
    config.type.typeId = nextTypeId;
    config.type.valueType = nextValueType;
    config.type.dimensions = nextDimensions;

    wire->setObjectFifo(config);
    m_document->notifyChanged();
}

void AiePropertiesPanel::applyObjectFifoTypeSelection(int row,
                                                      const QString& display,
                                                      const QString& typeId,
                                                      const QString& valueType,
                                                      const QString& dimensions)
{
    if (!m_objectFifosTable)
        return;

    auto* typeItem = m_objectFifosTable->item(row, 1);
    if (!typeItem)
        return;

    {
        QSignalBlocker blocker(m_objectFifosTable);
        m_updatingObjectFifoTable = true;
        typeItem->setText(display.trimmed());
        typeItem->setData(kObjectFifoTypeIdRole, typeId.trimmed());
        typeItem->setData(kObjectFifoTypeValueTypeRole, canonicalObjectFifoValueType(valueType));
        typeItem->setData(kObjectFifoTypeDimensionsRole, dimensions.trimmed());
        m_updatingObjectFifoTable = false;
    }

    applyObjectFifoRowEdits(row);
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
    const bool showHubPivot = (kind == SelectionKind::HubPivotWire);
    const bool showDdrTransferHub = (kind == SelectionKind::DdrTransferHub);
    const bool showDdr      = (kind == SelectionKind::DdrBlock);
    if (m_objectFifosGroup)
        m_objectFifosGroup->setVisible(m_canvasHost && m_canvasHost->canvasActive() && m_document);
    if (m_tileGroup)
        m_tileGroup->setVisible(showTile);
    if (m_hubPivotGroup)
        m_hubPivotGroup->setVisible(showHubPivot);
    if (m_ddrTransferGroup)
        m_ddrTransferGroup->setVisible(showDdrTransferHub);
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

Canvas::CanvasWire* AiePropertiesPanel::selectedDdrTransferWire() const
{
    auto* block = selectedBlock();
    if (!block || !block->isLinkHub() || !m_document)
        return nullptr;

    for (const auto& item : m_document->items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire)
            continue;

        const bool touchesHub =
            (wire->a().attached.has_value() && wire->a().attached->itemId == block->id()) ||
            (wire->b().attached.has_value() && wire->b().attached->itemId == block->id());
        if (!touchesHub)
            continue;

        auto endpointBlock = [&](const Canvas::CanvasWire::Endpoint& endpoint) -> Canvas::CanvasBlock* {
            if (!endpoint.attached.has_value())
                return nullptr;
            return dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(endpoint.attached->itemId));
        };

        auto* blockA = endpointBlock(wire->a());
        auto* blockB = endpointBlock(wire->b());
        if ((blockA && blockA->specId().trimmed() == QStringLiteral("ddr")) ||
            (blockB && blockB->specId().trimmed() == QStringLiteral("ddr"))) {
            return wire;
        }
    }

    return nullptr;
}

void AiePropertiesPanel::refreshSelection()
{
    bindCanvasSignalsIfNeeded();
    refreshObjectFifoSection();

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

        if (block->isLinkHub()) {
            const auto* symbolContent = dynamic_cast<const Canvas::BlockContentSymbol*>(block->content());
            const QString symbol = symbolContent ? symbolContent->symbol().trimmed() : QString();
            const bool isDistribute = symbol == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Distribute).symbol;
            const bool isCollect = symbol == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Collect).symbol;
            if (isDistribute || isCollect) {
                const QVector<TapOption> tapOptions = tapOptionsFromMetadata(
                    m_canvasDocuments ? m_canvasDocuments->activeMetadata() : QJsonObject{});
                Canvas::CanvasWire* ddrWire = selectedDdrTransferWire();
                const QString selectedTapId =
                    ddrWire && ddrWire->hasObjectFifo()
                        ? ddrWire->objectFifo()->type.tapSymbolId.trimmed()
                        : QString();

                m_updatingUi = true;
                if (m_ddrTransferModeValue)
                    m_ddrTransferModeValue->setText(isDistribute ? QStringLiteral("Distribute") : QStringLiteral("Collect"));
                if (m_ddrTransferTapCombo) {
                    QSignalBlocker blocker(m_ddrTransferTapCombo);
                    m_ddrTransferTapCombo->clear();
                    m_ddrTransferTapCombo->addItem(QStringLiteral("None"), QString{});
                    for (const TapOption& option : tapOptions)
                        m_ddrTransferTapCombo->addItem(tapLabel(option), option.id);
                    const int tapIndex = selectedTapId.isEmpty() ? 0 : m_ddrTransferTapCombo->findData(selectedTapId);
                    m_ddrTransferTapCombo->setCurrentIndex(tapIndex >= 0 ? tapIndex : 0);
                    m_ddrTransferTapCombo->setEnabled(ddrWire != nullptr);
                }
                m_updatingUi = false;

                showSelectionState(SelectionKind::DdrTransferHub,
                                   isDistribute ? QStringLiteral("Distribute hub selected")
                                                : QStringLiteral("Collect hub selected"),
                                   ddrWire
                                       ? QStringLiteral("Assign a Tensor Access Pattern from the Symbols panel.")
                                       : QStringLiteral("Connect this hub to DDR before assigning a Tensor Access Pattern."));
                return;
            }
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

        showSelectionState(SelectionKind::FifoWire,
                           QStringLiteral("Object FIFO selected"),
                           QStringLiteral("Edit name, type abstraction, and depth in the Object FIFOs table."));
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

void AiePropertiesPanel::applyDdrTransferHubTap()
{
    if (m_updatingUi || !m_document || !m_ddrTransferTapCombo)
        return;

    auto* ddrWire = selectedDdrTransferWire();
    auto* block = selectedBlock();
    if (!ddrWire || !block)
        return;

    Canvas::CanvasWire::ObjectFifoConfig config;
    if (ddrWire->hasObjectFifo()) {
        config = ddrWire->objectFifo().value();
    } else {
        config.depth = 1;
        const auto* symbolContent = dynamic_cast<const Canvas::BlockContentSymbol*>(block->content());
        const QString symbol = symbolContent ? symbolContent->symbol().trimmed() : QString();
        if (symbol == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Distribute).symbol)
            config.operation = Canvas::CanvasWire::ObjectFifoOperation::Fill;
        else if (symbol == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Collect).symbol)
            config.operation = Canvas::CanvasWire::ObjectFifoOperation::Drain;
    }

    const QString nextTapId = m_ddrTransferTapCombo->currentData().toString().trimmed();
    if (config.type.tapSymbolId.trimmed() == nextTapId)
        return;

    config.type.tapSymbolId = nextTapId;
    ddrWire->setObjectFifo(config);
    m_document->notifyChanged();
}

void AiePropertiesPanel::applyObjectFifoDefaults()
{
    if (m_updatingUi || !m_objectFifoDefaultNameEdit || !m_objectFifoDefaultDepthSpin || !m_objectFifoDefaultTypeCombo)
        return;

    Canvas::CanvasController::ObjectFifoDefaults defaults;
    defaults.name = m_objectFifoDefaultNameEdit->text().trimmed();
    if (defaults.name.isEmpty())
        defaults.name = QStringLiteral("of");
    defaults.depth = m_objectFifoDefaultDepthSpin->value();
    defaults.type.valueType = canonicalObjectFifoValueType(
        m_objectFifoDefaultTypeCombo->currentData(Qt::UserRole + 1).toString());
    defaults.type.typeId = m_objectFifoDefaultTypeCombo->currentData().toString().trimmed();
    defaults.type.dimensions =
        m_objectFifoDefaultTypeCombo->currentData(Qt::UserRole + 2).toString().trimmed();

    if (auto* controller = canvasController())
        controller->setObjectFifoDefaults(defaults);

    if (!m_canvasDocuments || !m_canvasDocuments->hasOpenDocument())
        return;

    QJsonObject metadata = m_canvasDocuments->activeMetadata();
    QJsonObject defaultsObject = metadata.value(kObjectFifoDefaultsKey).toObject();
    defaultsObject.insert(kObjectFifoDefaultNameKey, defaults.name);
    defaultsObject.insert(kObjectFifoDefaultDepthKey, defaults.depth);

    const QString typeId = m_objectFifoDefaultTypeCombo->currentData().toString().trimmed();
    if (typeId.isEmpty())
        defaultsObject.remove(kObjectFifoDefaultTypeIdKey);
    else
        defaultsObject.insert(kObjectFifoDefaultTypeIdKey, typeId);

    metadata.insert(kObjectFifoDefaultsKey, defaultsObject);
    m_canvasDocuments->updateActiveMetadata(metadata);
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
