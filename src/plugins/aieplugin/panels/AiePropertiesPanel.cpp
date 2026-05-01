// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/AiePropertiesPanel.hpp"

#include "aieplugin/AieService.hpp"
#include "aieplugin/kernels/KernelAssignmentController.hpp"
#include "aieplugin/symbol_table/SymbolsController.hpp"
#include "aieplugin/symbol_table/SymbolTableTypes.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasController.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasItem.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/api/ICanvasDocumentService.hpp"
#include "canvas/api/ICanvasHost.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"

#include <utils/ui/SidebarPanelFrame.hpp>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSet>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtCore/QtGlobal>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QCompleter>
#include <QtWidgets/QFrame>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include "aieplugin/panels/BodyStmtsEditor.hpp"
#include "aieplugin/hlir_sync/TileFifoInfo.hpp"
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
const QString kCoreFunctionLibraryKey = u"coreFunctionLibrary"_s;
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
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
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
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
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
    using CommitCallback  = std::function<void(int, const QString&, const QString&, const QString&, const QString&)>;

    ObjectFifoTypeDelegate(OptionsProvider optionsProvider,
                           CommitCallback commitCallback,
                           QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_optionsProvider(std::move(optionsProvider))
        , m_commitCallback(std::move(commitCallback))
    {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override
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
        const QString currentTypeId    = index.data(kObjectFifoTypeIdRole).toString().trimmed();
        const QString currentValueType = index.data(kObjectFifoTypeValueTypeRole).toString().trimmed();
        const QString currentDimensions = index.data(kObjectFifoTypeDimensionsRole).toString().trimmed();
        bool hasCurrentMatch = false;
        if (!currentTypeId.isEmpty()) {
            hasCurrentMatch = (editor->findData(currentTypeId) >= 0);
        } else {
            for (int i = 0; i < editor->count(); ++i) {
                if (editor->itemData(i, kObjectFifoTypeValueTypeRole).toString().trimmed() == currentValueType &&
                    editor->itemData(i, kObjectFifoTypeDimensionsRole).toString().trimmed() == currentDimensions) {
                    hasCurrentMatch = true; break;
                }
            }
        }
        if (!hasCurrentMatch && (!currentValueType.isEmpty() || !currentDimensions.isEmpty()) &&
            !index.data(Qt::DisplayRole).toString().trimmed().isEmpty()) {
            editor->addItem(index.data(Qt::DisplayRole).toString(), currentTypeId);
            const int itemIndex = editor->count() - 1;
            editor->setItemData(itemIndex, currentValueType, kObjectFifoTypeValueTypeRole);
            editor->setItemData(itemIndex, currentDimensions, kObjectFifoTypeDimensionsRole);
        }
        const int row = index.row();
        connect(editor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, editor, row](int ci) {
            if (ci < 0 || !editor->property("fifoTypeEditorReady").toBool()) return;
            if (m_commitCallback)
                m_commitCallback(row, editor->currentText(),
                                 editor->currentData().toString().trimmed(),
                                 canonicalObjectFifoValueType(editor->currentData(kObjectFifoTypeValueTypeRole).toString()),
                                 editor->currentData(kObjectFifoTypeDimensionsRole).toString().trimmed());
            emit const_cast<ObjectFifoTypeDelegate*>(this)->closeEditor(editor);
        });
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (!combo) return;
        const QString currentTypeId    = index.data(kObjectFifoTypeIdRole).toString().trimmed();
        const QString currentValueType = index.data(kObjectFifoTypeValueTypeRole).toString().trimmed();
        const QString currentDimensions = index.data(kObjectFifoTypeDimensionsRole).toString().trimmed();
        int selectedIndex = currentTypeId.isEmpty() ? -1 : combo->findData(currentTypeId);
        if (selectedIndex < 0) {
            for (int i = 0; i < combo->count(); ++i) {
                if (combo->itemData(i, kObjectFifoTypeValueTypeRole).toString().trimmed() == currentValueType &&
                    combo->itemData(i, kObjectFifoTypeDimensionsRole).toString().trimmed() == currentDimensions) {
                    selectedIndex = i; break;
                }
            }
        }
        combo->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
        combo->setProperty("fifoTypeEditorReady", true);
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (!combo || !model) return;
        model->setData(index, combo->currentText(), Qt::DisplayRole);
        model->setData(index, combo->currentData().toString().trimmed(), kObjectFifoTypeIdRole);
        model->setData(index, canonicalObjectFifoValueType(combo->currentData(kObjectFifoTypeValueTypeRole).toString()), kObjectFifoTypeValueTypeRole);
        model->setData(index, combo->currentData(kObjectFifoTypeDimensionsRole).toString().trimmed(), kObjectFifoTypeDimensionsRole);
    }

private:
    OptionsProvider m_optionsProvider;
    CommitCallback  m_commitCallback;
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

void AiePropertiesPanel::setKernelAssignmentController(KernelAssignmentController* controller)
{
    m_kernelAssignmentController = controller;
}

void AiePropertiesPanel::rebuildKernelChips(const QStringList& kernels, const QString& tileSpecId)
{
    if (!m_kernelChipsLayout)
        return;

    // Remove all current items from the layout.
    QLayoutItem* child;
    while ((child = m_kernelChipsLayout->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    if (kernels.isEmpty()) {
        auto* placeholder = new QLabel(QStringLiteral("None"), m_tileKernelRow);
        placeholder->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
        m_kernelChipsLayout->addWidget(placeholder);
        m_kernelChipsLayout->addStretch(1);
        return;
    }

    for (const QString& kernelId : kernels) {
        // Chip: small rounded container with label + × button.
        auto* chip = new QWidget(m_tileKernelRow);
        chip->setObjectName(QStringLiteral("AieKernelChip"));
        auto* chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(6, 2, 4, 2);
        chipLayout->setSpacing(4);

        auto* nameLabel = new QLabel(kernelId, chip);
        nameLabel->setObjectName(QStringLiteral("AieKernelChipLabel"));

        auto* removeBtn = new QPushButton(QStringLiteral("\u00D7"), chip); // ×
        removeBtn->setObjectName(QStringLiteral("AieKernelChipRemoveButton"));
        removeBtn->setFixedSize(16, 16);
        removeBtn->setFlat(true);

        chipLayout->addWidget(nameLabel);
        chipLayout->addWidget(removeBtn);

        const QString capturedKernelId = kernelId;
        const QString capturedSpecId = tileSpecId;
        connect(removeBtn, &QPushButton::clicked, this, [this, capturedKernelId, capturedSpecId]() {
            if (m_kernelAssignmentController)
                m_kernelAssignmentController->removeKernelFromTile(capturedSpecId, capturedKernelId);
        });

        m_kernelChipsLayout->addWidget(chip);
    }
    m_kernelChipsLayout->addStretch(1);
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
        [this](int row, const QString& display, const QString& typeId,
               const QString& valueType, const QString& dimensions) {
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

    m_objectFifosGroup      = objectFifosGroup;
    m_objectFifosTable      = objectFifosTable;
    m_objectFifoDefaultNameEdit  = objectFifoDefaultNameEdit;
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
    // Kernel chips row: dynamically rebuilt in rebuildKernelChips() on every selection refresh.
    auto* kernelRow = new QWidget(tileGroup);
    auto* kernelRowLayout = new QHBoxLayout(kernelRow);
    kernelRowLayout->setContentsMargins(0, 0, 0, 0);
    kernelRowLayout->setSpacing(6);
    kernelRowLayout->addStretch(1); // placeholder — gets cleared and repopulated

    tileForm->addRow(makeKeyLabel(QStringLiteral("Item ID")), tileIdValue);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Spec ID")), tileSpecIdValue);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Bounds")), tileBoundsValue);
    tileForm->addRow(makeKeyLabel(QStringLiteral("Label")), tileLabelEdit);
    auto* kernelRowLabel = makeKeyLabel(QStringLiteral("Kernel"));
    tileForm->addRow(kernelRowLabel, kernelRow);

    // Connected FIFOs rows (read-only, compute tiles only)
    auto* inputFifosLabel = makeKeyLabel(QStringLiteral("In FIFOs"));
    auto* inputFifosValue = new QLabel(QStringLiteral("-"), tileGroup);
    inputFifosValue->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    inputFifosValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    inputFifosValue->setWordWrap(true);
    tileForm->addRow(inputFifosLabel, inputFifosValue);

    auto* outputFifosLabel = makeKeyLabel(QStringLiteral("Out FIFOs"));
    auto* outputFifosValue = new QLabel(QStringLiteral("-"), tileGroup);
    outputFifosValue->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    outputFifosValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outputFifosValue->setWordWrap(true);
    tileForm->addRow(outputFifosLabel, outputFifosValue);

    m_tileGroup = tileGroup;
    m_tileIdValue = tileIdValue;
    m_tileSpecIdValue = tileSpecIdValue;
    m_tileBoundsValue = tileBoundsValue;
    m_tileLabelEdit = tileLabelEdit;
    m_tileKernelRow = kernelRow;
    m_tileKernelRowLabel = kernelRowLabel;
    m_kernelChipsLayout = kernelRowLayout;
    m_tileInputFifosValue  = inputFifosValue;
    m_tileOutputFifosValue = outputFifosValue;

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

    // dims_to_stream (split) and dims_from_stream (join) — hidden by default
    auto* hubDimsToLabel = makeHubKeyLabel(QStringLiteral("dims_to_stream"));
    auto* hubDimsToStreamEdit = new QLineEdit(hubPivotGroup);
    hubDimsToStreamEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    hubDimsToStreamEdit->setPlaceholderText(QStringLiteral("e.g. activation_layout_dims"));
    auto* hubDimsFromLabel = makeHubKeyLabel(QStringLiteral("dims_from_stream"));
    auto* hubDimsFromStreamEdit = new QLineEdit(hubPivotGroup);
    hubDimsFromStreamEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    hubDimsFromStreamEdit->setPlaceholderText(QStringLiteral("e.g. tiled_c_to_rowmajor_dims"));
    auto* hubBranchTypeLabel = makeHubKeyLabel(QStringLiteral("Branch Type"));
    auto* hubBranchTypeEdit = new QLineEdit(hubPivotGroup);
    hubBranchTypeEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    hubBranchTypeEdit->setPlaceholderText(QStringLiteral("e.g. activation_tile_type"));
    auto* hubOffsetsEditLabel = makeHubKeyLabel(QStringLiteral("Offsets"));
    auto* hubOffsetsEdit = new QLineEdit(hubPivotGroup);
    hubOffsetsEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    hubOffsetsEdit->setPlaceholderText(QStringLiteral("auto  (e.g. 0, 1024, 2048, 3072)"));

    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Name")),       hubPivotNameEdit);
    hubPivotForm->addRow(hubPivotFifoLabel,                              hubPivotFifoEdit);
    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Branches")),   hubBranchesValue);
    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Depth")),      hubDepthValue);
    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Value Type")), hubValueTypeValue);
    hubPivotForm->addRow(makeHubKeyLabel(QStringLiteral("Dimensions")), hubDimensionsValue);
    hubPivotForm->addRow(hubBranchTypeLabel,                             hubBranchTypeEdit);
    hubPivotForm->addRow(hubOffsetsEditLabel,                            hubOffsetsEdit);
    hubPivotForm->addRow(hubDimsToLabel,                                 hubDimsToStreamEdit);
    hubPivotForm->addRow(hubDimsFromLabel,                               hubDimsFromStreamEdit);

    m_hubPivotGroup          = hubPivotGroup;
    m_hubPivotNameEdit       = hubPivotNameEdit;
    m_hubPivotFifoLabel      = hubPivotFifoLabel;
    m_hubPivotFifoEdit       = hubPivotFifoEdit;
    m_hubBranchesValue       = hubBranchesValue;
    m_hubOffsetsValue        = hubOffsetsValue;
    m_hubDepthValue          = hubDepthValue;
    m_hubDimsToLabel         = hubDimsToLabel;
    m_hubDimsToStreamEdit    = hubDimsToStreamEdit;
    m_hubDimsFromLabel       = hubDimsFromLabel;
    m_hubDimsFromStreamEdit  = hubDimsFromStreamEdit;
    m_hubBranchTypeLabel     = hubBranchTypeLabel;
    m_hubBranchTypeEdit      = hubBranchTypeEdit;
    m_hubOffsetsEditLabel    = hubOffsetsEditLabel;
    m_hubOffsetsEdit         = hubOffsetsEdit;
    m_hubValueTypeValue  = hubValueTypeValue;
    m_hubDimensionsValue = hubDimensionsValue;

    auto* fifoGroup = new QGroupBox(QStringLiteral("Object FIFO"), fieldsHost);
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
    fifoTypeCombo->addItem(QStringLiteral("i64"));
    fifoTypeCombo->addItem(QStringLiteral("ui8"));
    fifoTypeCombo->addItem(QStringLiteral("ui16"));
    fifoTypeCombo->addItem(QStringLiteral("ui32"));
    fifoTypeCombo->addItem(QStringLiteral("bf16"));
    fifoTypeCombo->addItem(QStringLiteral("f16"));
    fifoTypeCombo->addItem(QStringLiteral("f32"));
    fifoTypeCombo->addItem(QStringLiteral("f64"));
    auto* fifoDimensionsEdit = new QLineEdit(fifoGroup);
    fifoDimensionsEdit->setObjectName(QStringLiteral("AiePropertiesField"));

    // Forward-wire dims rows (hidden by default; shown when a Forward wire is selected)
    auto* fwdDimsFromLabel = makeFifoKeyLabel(QStringLiteral("dims_from_stream"));
    auto* fwdDimsFromStreamEdit = new QLineEdit(fifoGroup);
    fwdDimsFromStreamEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    fwdDimsFromStreamEdit->setPlaceholderText(QStringLiteral("e.g. tiled_c_to_rowmajor_dims"));
    auto* fwdDimsToLabel = makeFifoKeyLabel(QStringLiteral("dims_to_stream"));
    auto* fwdDimsToStreamEdit = new QLineEdit(fifoGroup);
    fwdDimsToStreamEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    fwdDimsToStreamEdit->setPlaceholderText(QStringLiteral("e.g. activation_layout_dims"));

    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Wire ID")),    fifoWireIdValue);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Name")),       fifoNameEdit);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Depth")),      fifoDepthSpin);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Symbol")),     fifoSymbolCombo);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Value Type")), fifoTypeCombo);
    fifoForm->addRow(makeFifoKeyLabel(QStringLiteral("Dimensions")), fifoDimensionsEdit);
    fifoForm->addRow(fwdDimsFromLabel,                               fwdDimsFromStreamEdit);
    fifoForm->addRow(fwdDimsToLabel,                                 fwdDimsToStreamEdit);

    m_fifoGroup              = fifoGroup;
    m_fifoWireIdValue        = fifoWireIdValue;
    m_fifoNameEdit           = fifoNameEdit;
    m_fifoDepthSpin          = fifoDepthSpin;
    m_fifoSymbolCombo        = fifoSymbolCombo;
    m_fifoTypeCombo          = fifoTypeCombo;
    m_fifoDimensionsEdit     = fifoDimensionsEdit;
    m_fwdDimsFromLabel       = fwdDimsFromLabel;
    m_fwdDimsFromStreamEdit  = fwdDimsFromStreamEdit;
    m_fwdDimsToLabel         = fwdDimsToLabel;
    m_fwdDimsToStreamEdit    = fwdDimsToStreamEdit;

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
    auto* ddrGroupLayout = new QVBoxLayout(ddrGroup);
    ddrGroupLayout->setContentsMargins(12, 8, 12, 12);
    ddrGroupLayout->setSpacing(4);
    {
        auto* ddrInputsLabel = new QLabel(QStringLiteral("Inputs"), ddrGroup);
        ddrInputsLabel->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        auto* ddrInputsTable = new QTableWidget(0, 2, ddrGroup);
        ddrInputsTable->setObjectName(QStringLiteral("AiePropertiesObjectFifosTable"));
        ddrInputsTable->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Total Size")});
        ddrInputsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        ddrInputsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        ddrInputsTable->verticalHeader()->setVisible(false);
        ddrInputsTable->verticalHeader()->setDefaultSectionSize(30);
        ddrInputsTable->setEditTriggers(QAbstractItemView::SelectedClicked |
                                        QAbstractItemView::DoubleClicked |
                                        QAbstractItemView::EditKeyPressed);
        ddrInputsTable->setSelectionMode(QAbstractItemView::SingleSelection);
        ddrInputsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ddrInputsTable->setShowGrid(false);
        ddrInputsTable->setAlternatingRowColors(false);
        ddrInputsTable->setFocusPolicy(Qt::StrongFocus);
        ddrInputsTable->setWordWrap(false);
        ddrInputsTable->setFixedHeight(3 * 30 + 30); // header + 3 rows

        auto* ddrOutputsLabel = new QLabel(QStringLiteral("Outputs"), ddrGroup);
        ddrOutputsLabel->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        auto* ddrOutputsTable = new QTableWidget(0, 2, ddrGroup);
        ddrOutputsTable->setObjectName(QStringLiteral("AiePropertiesObjectFifosTable"));
        ddrOutputsTable->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Total Size")});
        ddrOutputsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        ddrOutputsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        ddrOutputsTable->verticalHeader()->setVisible(false);
        ddrOutputsTable->verticalHeader()->setDefaultSectionSize(30);
        ddrOutputsTable->setEditTriggers(QAbstractItemView::SelectedClicked |
                                         QAbstractItemView::DoubleClicked |
                                         QAbstractItemView::EditKeyPressed);
        ddrOutputsTable->setSelectionMode(QAbstractItemView::SingleSelection);
        ddrOutputsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ddrOutputsTable->setShowGrid(false);
        ddrOutputsTable->setAlternatingRowColors(false);
        ddrOutputsTable->setFocusPolicy(Qt::StrongFocus);
        ddrOutputsTable->setWordWrap(false);
        ddrOutputsTable->setFixedHeight(3 * 30 + 30); // header + 3 rows

        // TAP detail editor — shown when a table row is selected
        auto* ddrTapWidget = new QWidget(ddrGroup);
        ddrTapWidget->setVisible(false);
        auto* ddrTapForm = new QFormLayout(ddrTapWidget);
        ddrTapForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        ddrTapForm->setContentsMargins(0, 4, 0, 0);
        ddrTapForm->setHorizontalSpacing(10);
        ddrTapForm->setVerticalSpacing(4);
        ddrTapForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        // TAP source toggle: Custom inline config vs. symbol from symbol table.
        auto* ddrTapSourceCombo = new QComboBox(ddrTapWidget);
        ddrTapSourceCombo->setObjectName(QStringLiteral("AiePropertiesField"));
        ddrTapSourceCombo->addItems({QStringLiteral("Custom"), QStringLiteral("Symbol Table")});
        {
            auto* lbl = new QLabel(QStringLiteral("TAP Source"), ddrTapWidget);
            lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
            ddrTapForm->addRow(lbl, ddrTapSourceCombo);
        }

        // Symbol selector (visible when source = Symbol Table).
        auto* ddrTapSymbolCombo = new QComboBox(ddrTapWidget);
        ddrTapSymbolCombo->setObjectName(QStringLiteral("AiePropertiesField"));
        ddrTapSymbolCombo->setVisible(false);
        {
            auto* lbl = new QLabel(QStringLiteral("TAP Symbol"), ddrTapWidget);
            lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
            ddrTapForm->addRow(lbl, ddrTapSymbolCombo);
        }

        // Custom inline config (visible when source = Custom).
        auto* ddrTapCustomWidget = new QWidget(ddrTapWidget);
        auto* ddrTapCustomForm = new QFormLayout(ddrTapCustomWidget);
        ddrTapCustomForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        ddrTapCustomForm->setContentsMargins(0, 0, 0, 0);
        ddrTapCustomForm->setHorizontalSpacing(10);
        ddrTapCustomForm->setVerticalSpacing(4);
        ddrTapCustomForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        auto* ddrTapModeCombo = new QComboBox(ddrTapCustomWidget);
        ddrTapModeCombo->setObjectName(QStringLiteral("AiePropertiesField"));
        ddrTapModeCombo->addItems({QStringLiteral("Vector"), QStringLiteral("Matrix")});

        auto* ddrTapFieldsWidget = new QWidget(ddrTapCustomWidget);
        auto* ddrTapFieldsForm = new QFormLayout(ddrTapFieldsWidget);
        ddrTapFieldsForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        ddrTapFieldsForm->setContentsMargins(0, 0, 0, 0);
        ddrTapFieldsForm->setHorizontalSpacing(10);
        ddrTapFieldsForm->setVerticalSpacing(4);
        ddrTapFieldsForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        const auto makeDdrTapField = [ddrTapFieldsWidget](const QString& placeholder) {
            auto* e = new QLineEdit(ddrTapFieldsWidget);
            e->setObjectName(QStringLiteral("AiePropertiesField"));
            e->setPlaceholderText(placeholder);
            return e;
        };
        auto* ddrTapTileDimsEdit   = makeDdrTapField(QStringLiteral("e.g. 1 x 512"));
        auto* ddrTapTileCountsEdit = makeDdrTapField(QStringLiteral("e.g. rows x cols // 512"));
        auto* ddrTapRepeatEdit     = makeDdrTapField(QStringLiteral("1"));

        {
            auto* lbl = new QLabel(QStringLiteral("Tile Dimensions"), ddrTapFieldsWidget);
            lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
            ddrTapFieldsForm->addRow(lbl, ddrTapTileDimsEdit);
        }
        {
            auto* lbl = new QLabel(QStringLiteral("Tile Counts"), ddrTapFieldsWidget);
            lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
            ddrTapFieldsForm->addRow(lbl, ddrTapTileCountsEdit);
        }
        {
            auto* lbl = new QLabel(QStringLiteral("Pattern Repeat"), ddrTapFieldsWidget);
            lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
            ddrTapFieldsForm->addRow(lbl, ddrTapRepeatEdit);
        }
        ddrTapFieldsWidget->setVisible(false);

        {
            auto* lbl = new QLabel(QStringLiteral("Mode"), ddrTapCustomWidget);
            lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
            ddrTapCustomForm->addRow(lbl, ddrTapModeCombo);
        }
        ddrTapCustomForm->addRow(ddrTapFieldsWidget);
        ddrTapForm->addRow(ddrTapCustomWidget);

        connect(ddrTapModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                ddrTapFieldsWidget, [ddrTapFieldsWidget](int idx) {
                    ddrTapFieldsWidget->setVisible(idx == 1);
                });
        connect(ddrTapSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [ddrTapCustomWidget, ddrTapSymbolCombo](int idx) {
                    const bool isSymbol = (idx == 1);
                    ddrTapCustomWidget->setVisible(!isSymbol);
                    ddrTapSymbolCombo->setVisible(isSymbol);
                });

        ddrGroupLayout->addWidget(ddrInputsLabel);
        ddrGroupLayout->addWidget(ddrInputsTable);
        ddrGroupLayout->addWidget(ddrOutputsLabel);
        ddrGroupLayout->addWidget(ddrOutputsTable);
        ddrGroupLayout->addWidget(ddrTapWidget);

        m_ddrInputsTable       = ddrInputsTable;
        m_ddrOutputsTable      = ddrOutputsTable;
        m_ddrTapWidget         = ddrTapWidget;
        m_ddrTapSourceCombo    = ddrTapSourceCombo;
        m_ddrTapSymbolCombo    = ddrTapSymbolCombo;
        m_ddrTapCustomWidget   = ddrTapCustomWidget;
        m_ddrTapModeCombo      = ddrTapModeCombo;
        m_ddrTapTileDimsEdit   = ddrTapTileDimsEdit;
        m_ddrTapTileCountsEdit = ddrTapTileCountsEdit;
        m_ddrTapRepeatEdit     = ddrTapRepeatEdit;
    }
    m_ddrGroup = ddrGroup;

    auto* ddrPivotWireGroup = new QGroupBox(QStringLiteral("DDR Pivot Wire"), fieldsHost);
    ddrPivotWireGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    {
        auto* ddrPivotForm = new QFormLayout(ddrPivotWireGroup);
        ddrPivotForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        ddrPivotForm->setContentsMargins(12, 12, 12, 12);
        ddrPivotForm->setHorizontalSpacing(10);
        ddrPivotForm->setVerticalSpacing(8);
        ddrPivotForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* ddrPivotParamEdit = new QLineEdit(ddrPivotWireGroup);
        ddrPivotParamEdit->setObjectName(QStringLiteral("AiePropertiesField"));
        ddrPivotParamEdit->setPlaceholderText(QStringLiteral("e.g. A or buf"));
        auto* ddrPivotLbl = new QLabel(QStringLiteral("Param Name"), ddrPivotWireGroup);
        ddrPivotLbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        ddrPivotForm->addRow(ddrPivotLbl, ddrPivotParamEdit);
        m_ddrPivotParamEdit = ddrPivotParamEdit;
    }
    m_ddrPivotWireGroup = ddrPivotWireGroup;

    auto* armWireGroup = new QGroupBox(QStringLiteral("Arm Wire"), fieldsHost);
    armWireGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    auto* armWireForm = new QFormLayout(armWireGroup);
    armWireForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    armWireForm->setContentsMargins(12, 12, 12, 12);
    armWireForm->setHorizontalSpacing(10);
    armWireForm->setVerticalSpacing(8);
    armWireForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* armFifoEdit = new QLineEdit(armWireGroup);
    armFifoEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    armFifoEdit->setPlaceholderText(QStringLiteral("e.g. inA1"));
    auto* armTapEdit = new QLineEdit(armWireGroup);
    armTapEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    armTapEdit->setPlaceholderText(QStringLiteral("e.g. tap_act"));
    {
        auto* lbl = new QLabel(QStringLiteral("Target FIFO"), armWireGroup);
        lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        armWireForm->addRow(lbl, armFifoEdit);
    }
    {
        auto* lbl = new QLabel(QStringLiteral("TAP"), armWireGroup);
        lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        armWireForm->addRow(lbl, armTapEdit);
    }
    m_armWireGroup = armWireGroup;
    m_armFifoEdit  = armFifoEdit;
    m_armTapEdit   = armTapEdit;

    // --- Direct DDR Wire group (DDR↔SHIM without a hub) ---
    auto* directDdrWireGroup = new QGroupBox(QStringLiteral("Direct DDR Wire"), fieldsHost);
    directDdrWireGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    auto* directDdrWireForm = new QFormLayout(directDdrWireGroup);
    directDdrWireForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    directDdrWireForm->setContentsMargins(12, 12, 12, 12);
    directDdrWireForm->setHorizontalSpacing(10);
    directDdrWireForm->setVerticalSpacing(8);
    directDdrWireForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* directDdrParamValue = new QLabel(directDdrWireGroup);
    directDdrParamValue->setObjectName(QStringLiteral("AiePropertiesValueLabel"));
    directDdrParamValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* directDdrFifoEdit = new QLineEdit(directDdrWireGroup);
    directDdrFifoEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    directDdrFifoEdit->setPlaceholderText(QStringLiteral("e.g. weights_col0_fifo"));
    auto* directDdrTapEdit = new QLineEdit(directDdrWireGroup);
    directDdrTapEdit->setObjectName(QStringLiteral("AiePropertiesField"));
    directDdrTapEdit->setPlaceholderText(QStringLiteral("e.g. tap_act"));
    {
        auto* lbl = new QLabel(QStringLiteral("Buffer"), directDdrWireGroup);
        lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        directDdrWireForm->addRow(lbl, directDdrParamValue);
    }
    {
        auto* lbl = new QLabel(QStringLiteral("Target FIFO"), directDdrWireGroup);
        lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        directDdrWireForm->addRow(lbl, directDdrFifoEdit);
    }
    {
        auto* lbl = new QLabel(QStringLiteral("TAP"), directDdrWireGroup);
        lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        directDdrWireForm->addRow(lbl, directDdrTapEdit);
    }
    m_directDdrWireGroup  = directDdrWireGroup;
    m_directDdrParamValue = directDdrParamValue;
    m_directDdrFifoEdit   = directDdrFifoEdit;
    m_directDdrTapEdit    = directDdrTapEdit;

    // --- Core Function group (compute kernel tiles only) ---
    auto* coreFnGroup = new QGroupBox(QStringLiteral("Core Function"), fieldsHost);
    coreFnGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    auto* coreFnVLayout = new QVBoxLayout(coreFnGroup);
    coreFnVLayout->setContentsMargins(12, 12, 12, 12);
    coreFnVLayout->setSpacing(8);

    auto* coreFnFormHost = new QWidget(coreFnGroup);
    auto* coreFnForm = new QFormLayout(coreFnFormHost);
    coreFnForm->setContentsMargins(0, 0, 0, 0);
    coreFnForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    coreFnForm->setHorizontalSpacing(10);
    coreFnForm->setVerticalSpacing(8);
    coreFnForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* coreFnModeCombo = new QComboBox(coreFnGroup);
    coreFnModeCombo->setObjectName(QStringLiteral("AiePropertiesField"));
    coreFnModeCombo->addItem(QStringLiteral("Default (acquire/call/release)"),
                             QStringLiteral("default"));
    coreFnModeCombo->addItem(QStringLiteral("Body Statements (custom)"),
                             QStringLiteral("bodyStmts"));
    coreFnModeCombo->addItem(QStringLiteral("Shared Function"),
                             QStringLiteral("sharedRef"));
    {
        auto* lbl = new QLabel(QStringLiteral("Mode"), coreFnGroup);
        lbl->setObjectName(QStringLiteral("AiePropertiesKeyLabel"));
        coreFnForm->addRow(lbl, coreFnModeCombo);
    }
    coreFnVLayout->addWidget(coreFnFormHost);

    // fn_args binding table: Argument (read-only param name) | Reference (dropdown)
    auto* argListGroup = new QGroupBox(QStringLiteral("fn_args"), coreFnGroup);
    argListGroup->setObjectName(QStringLiteral("AiePropertiesSectionCard"));
    auto* argListVL = new QVBoxLayout(argListGroup);
    argListVL->setContentsMargins(8, 8, 8, 8);
    argListVL->setSpacing(4);

    auto* argTable = new QTableWidget(0, 2, argListGroup);
    argTable->setHorizontalHeaderLabels({QStringLiteral("Argument"), QStringLiteral("Reference")});
    argTable->horizontalHeader()->setStretchLastSection(true);
    argTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    argTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    argTable->setSelectionMode(QAbstractItemView::SingleSelection);
    argTable->verticalHeader()->setVisible(false);
    argTable->setMinimumHeight(100);
    argTable->setMaximumHeight(200);
    argListVL->addWidget(argTable);

    auto* argBtnRowWidget = new QWidget(argListGroup);
    auto* argBtnLayout = new QHBoxLayout(argBtnRowWidget);
    argBtnLayout->setContentsMargins(0, 0, 0, 0);
    argBtnLayout->setSpacing(4);
    auto* argAutoBtn  = new QPushButton(QStringLiteral("Auto-assign"),  argListGroup);
    auto* argClearBtn = new QPushButton(QStringLiteral("Clear"), argListGroup);
    argAutoBtn->setObjectName(QStringLiteral("AiePropertiesField"));
    argClearBtn->setObjectName(QStringLiteral("AiePropertiesClearBtn"));
    argBtnLayout->addWidget(argAutoBtn);
    argBtnLayout->addStretch(1);
    argBtnLayout->addWidget(argClearBtn);
    argListVL->addWidget(argBtnRowWidget);
    coreFnVLayout->addWidget(argListGroup);

    m_argListGroup = argListGroup;
    m_argListTable = argTable;

    auto* coreFnEditor = new BodyStmtsEditor(coreFnGroup);
    coreFnVLayout->addWidget(coreFnEditor);

    // --- Shared Function picker row (visible when mode == SharedRef) ---
    auto* sharedFnRow = new QWidget(coreFnGroup);
    auto* sharedFnRowLayout = new QHBoxLayout(sharedFnRow);
    sharedFnRowLayout->setContentsMargins(0, 0, 0, 0);
    sharedFnRowLayout->setSpacing(6);
    auto* sharedFnCombo = new QComboBox(sharedFnRow);
    sharedFnCombo->setObjectName(QStringLiteral("AiePropertiesField"));
    sharedFnCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sharedFnRowLayout->addWidget(new QLabel(QStringLiteral("Function:"), sharedFnRow));
    sharedFnRowLayout->addWidget(sharedFnCombo);
    coreFnVLayout->addWidget(sharedFnRow);

    auto* sharedFnPreviewLabel = new QLabel(coreFnGroup);
    sharedFnPreviewLabel->setObjectName(QStringLiteral("AiePropertiesInfoLabel"));
    sharedFnPreviewLabel->setWordWrap(true);
    sharedFnPreviewLabel->setTextFormat(Qt::PlainText);
    coreFnVLayout->addWidget(sharedFnPreviewLabel);

    // --- Bottom buttons row ---
    auto* coreFnBtnRow = new QWidget(coreFnGroup);
    auto* coreFnBtnLayout = new QHBoxLayout(coreFnBtnRow);
    coreFnBtnLayout->setContentsMargins(0, 0, 0, 0);
    coreFnBtnLayout->setSpacing(6);

    auto* coreFnClearBtn = new QPushButton(QStringLiteral("Clear"), coreFnBtnRow);
    coreFnClearBtn->setObjectName(QStringLiteral("AiePropertiesClearBtn"));
    coreFnClearBtn->setToolTip(QStringLiteral("Clear custom body and revert to Default mode"));

    auto* coreFnSaveAsSharedBtn = new QPushButton(QStringLiteral("Save as Shared\u2026"), coreFnBtnRow);
    coreFnSaveAsSharedBtn->setObjectName(QStringLiteral("AiePropertiesField"));
    coreFnSaveAsSharedBtn->setToolTip(QStringLiteral("Save this custom body to the document library so other tiles can reference it"));

    auto* coreFnRemoveSharedBtn = new QPushButton(QStringLiteral("Remove as Shared"), coreFnBtnRow);
    coreFnRemoveSharedBtn->setObjectName(QStringLiteral("AiePropertiesClearBtn"));
    coreFnRemoveSharedBtn->setToolTip(QStringLiteral("Remove this function from the shared library and revert all referencing tiles to Default mode"));
    coreFnRemoveSharedBtn->setVisible(false);

    coreFnBtnLayout->addWidget(coreFnClearBtn);
    coreFnBtnLayout->addWidget(coreFnSaveAsSharedBtn);
    coreFnBtnLayout->addWidget(coreFnRemoveSharedBtn);
    coreFnBtnLayout->addStretch(1);
    coreFnVLayout->addWidget(coreFnBtnRow);

    m_coreFnGroup             = coreFnGroup;
    m_coreFnModeCombo         = coreFnModeCombo;
    m_coreFnEditor            = coreFnEditor;
    m_coreFnClearBtn          = coreFnClearBtn;
    m_coreFnSaveAsSharedBtn   = coreFnSaveAsSharedBtn;
    m_coreFnRemoveSharedBtn   = coreFnRemoveSharedBtn;
    m_sharedFnRow             = sharedFnRow;
    m_sharedFnCombo           = sharedFnCombo;
    m_sharedFnPreviewLabel    = sharedFnPreviewLabel;

    fieldsLayout->addWidget(tileGroup);
    fieldsLayout->addWidget(coreFnGroup);
    fieldsLayout->addWidget(hubPivotGroup);
    fieldsLayout->addWidget(fifoGroup);
    fieldsLayout->addWidget(ddrTransferGroup);
    fieldsLayout->addWidget(ddrGroup);
    fieldsLayout->addWidget(ddrPivotWireGroup);
    fieldsLayout->addWidget(armWireGroup);
    fieldsLayout->addWidget(directDdrWireGroup);
    fieldsLayout->addWidget(objectFifosGroup);
    fieldsLayout->addStretch(1);

    scrollArea->setWidget(fieldsHost);

    contentLayout->addWidget(summaryLabel);
    contentLayout->addWidget(detailLabel);
    contentLayout->addWidget(scrollArea, 1);

    frame->setContentWidget(content);
    rootLayout->addWidget(frame);

    connect(tileLabelEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyTileLabel);

    connect(hubPivotNameEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);
    connect(hubPivotFifoEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);
    connect(hubDimsToStreamEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);
    connect(hubDimsFromStreamEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);
    connect(hubBranchTypeEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);
    connect(hubOffsetsEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyHubPivotProperties);

    connect(fifoNameEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyFifoAnnotation);
    connect(fifoDepthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { applyFifoAnnotation(); });
    connect(fifoSymbolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyFifoAnnotation(); });
    connect(fifoTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyFifoAnnotation(); });
    connect(fifoDimensionsEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyFifoAnnotation);
    connect(fwdDimsFromStreamEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyFifoAnnotation);
    connect(fwdDimsToStreamEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyFifoAnnotation);
    connect(ddrTransferTapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyDdrTransferHubTap(); });

    if (m_ddrInputsTable) {
        connect(m_ddrInputsTable, &QTableWidget::cellChanged,
                this, [this](int row, int) { applyDdrTableRow(true, row); });
        connect(m_ddrInputsTable, &QTableWidget::currentCellChanged,
                this, [this](int row, int, int, int) { onDdrTableRowSelected(true, row); });
    }
    if (m_ddrOutputsTable) {
        connect(m_ddrOutputsTable, &QTableWidget::cellChanged,
                this, [this](int row, int) { applyDdrTableRow(false, row); });
        connect(m_ddrOutputsTable, &QTableWidget::currentCellChanged,
                this, [this](int row, int, int, int) { onDdrTableRowSelected(false, row); });
    }
    if (m_ddrTapSourceCombo) {
        connect(m_ddrTapSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { applyDdrTap(); });
    }
    if (m_ddrTapSymbolCombo) {
        connect(m_ddrTapSymbolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { applyDdrTap(); });
    }
    if (m_ddrTapModeCombo) {
        connect(m_ddrTapModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { applyDdrTap(); });
    }
    if (m_ddrTapTileDimsEdit) {
        connect(m_ddrTapTileDimsEdit,   &QLineEdit::editingFinished, this, &AiePropertiesPanel::applyDdrTap);
        connect(m_ddrTapTileCountsEdit, &QLineEdit::editingFinished, this, &AiePropertiesPanel::applyDdrTap);
        connect(m_ddrTapRepeatEdit,     &QLineEdit::editingFinished, this, &AiePropertiesPanel::applyDdrTap);
    }
    if (m_ddrPivotParamEdit) {
        connect(m_ddrPivotParamEdit, &QLineEdit::editingFinished,
                this, &AiePropertiesPanel::applyDdrPivotParam);
    }

    connect(armFifoEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyArmWireEntry);
    connect(armTapEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyArmWireEntry);

    connect(directDdrFifoEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyDirectDdrFifo);
    connect(directDdrTapEdit, &QLineEdit::editingFinished,
            this, &AiePropertiesPanel::applyDirectDdrFifo);

    connect(coreFnModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiePropertiesPanel::applyCoreFunctionBody);
    connect(coreFnEditor, &BodyStmtsEditor::jsonChanged,
            this, &AiePropertiesPanel::applyCoreFunctionBody);
    connect(coreFnClearBtn, &QPushButton::clicked, this, [this]() {
        if (m_updatingUi || !m_document || !m_coreFnModeCombo || !m_coreFnEditor)
            return;
        auto* block = selectedBlock();
        if (!block) return;
        // Erase bodyStmtsJson and revert to Default — next switch to BodyStmts will repopulate.
        Canvas::CanvasBlock::CoreFunctionConfig cfg;
        cfg.mode          = Canvas::CanvasBlock::CoreFunctionConfig::Mode::Default;
        cfg.bodyStmtsJson = QString{};
        block->setCoreFunctionConfig(std::move(cfg));
        {
            QSignalBlocker blkCombo(m_coreFnModeCombo);
            QSignalBlocker blkEditor(m_coreFnEditor);
            m_coreFnModeCombo->setCurrentIndex(0);
            m_coreFnEditor->setJson(QString{});
            m_coreFnEditor->setVisible(false);
            m_coreFnClearBtn->setVisible(false);
            if (m_sharedFnRow)        m_sharedFnRow->setVisible(false);
            if (m_sharedFnPreviewLabel) m_sharedFnPreviewLabel->setVisible(false);
        }
        m_document->notifyChanged();
    });
    connect(coreFnSaveAsSharedBtn, &QPushButton::clicked,
            this, &AiePropertiesPanel::saveCoreFunctionAsShared);
    connect(coreFnRemoveSharedBtn, &QPushButton::clicked,
            this, &AiePropertiesPanel::removeCoreFunctionShared);
    connect(sharedFnCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiePropertiesPanel::applySharedFunctionSelection);

    // fn_args list buttons
    connect(argAutoBtn, &QPushButton::clicked, this, [this]() {
        if (m_updatingUi) return;
        auto* block = selectedBlock();
        if (block) autoPopulateArgList(block);
    });
    connect(argClearBtn, &QPushButton::clicked, this, [this]() {
        if (m_updatingUi || !m_document) return;
        auto* block = selectedBlock();
        if (!block) return;
        block->clearCoreBodyArgs();
        m_document->notifyChanged();
    });

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
                if (!item) return;
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

    const QString persistedName  = defaultsObject.value(kObjectFifoDefaultNameKey).toString().trimmed();
    const int persistedDepth     = std::max(1, defaultsObject.value(kObjectFifoDefaultDepthKey).toInt(2));
    const QString persistedTypeId = defaultsObject.value(kObjectFifoDefaultTypeIdKey).toString().trimmed();

    Canvas::CanvasController::ObjectFifoDefaults defaults =
        canvasController() ? canvasController()->objectFifoDefaults()
                           : Canvas::CanvasController::ObjectFifoDefaults{};
    if (!persistedName.isEmpty()) defaults.name = persistedName;
    defaults.depth = persistedDepth;
    defaults.type.typeId = persistedTypeId;

    QString selectedTypeId = persistedTypeId;
    if (!selectedTypeId.isEmpty()) {
        for (const FifoTypeOption& option : typeOptions) {
            if (option.id != selectedTypeId) continue;
            defaults.type.valueType  = option.dtype;
            defaults.type.dimensions = option.dimensions;
            break;
        }
    }

    const bool prevUpdatingUi = m_updatingUi;
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
        if (found >= 0) selectedIndex = found;
    } else {
        for (int i = 1; i < m_objectFifoDefaultTypeCombo->count(); ++i) {
            if (canonicalObjectFifoValueType(m_objectFifoDefaultTypeCombo->itemData(i, Qt::UserRole + 1).toString()) ==
                    canonicalObjectFifoValueType(defaults.type.valueType) &&
                m_objectFifoDefaultTypeCombo->itemData(i, Qt::UserRole + 2).toString() == defaults.type.dimensions) {
                selectedIndex = i; break;
            }
        }
    }
    m_objectFifoDefaultTypeCombo->setCurrentIndex(selectedIndex);
    m_updatingUi = prevUpdatingUi;

    if (canvasController())
        canvasController()->setObjectFifoDefaults(defaults);
}

void AiePropertiesPanel::refreshObjectFifoSection()
{
    if (!m_objectFifosTable)
        return;

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
        if (fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Fill    ||
            fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Drain   ||
            fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Split   ||
            fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Join    ||
            fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Forward)
            continue;

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

    auto* nameItem  = m_objectFifosTable->item(row, 0);
    auto* typeItem  = m_objectFifosTable->item(row, 1);
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
        ? QStringLiteral("of") : nameItem->text().trimmed();
    bool depthOk = false;
    int normalizedDepth = depthItem->text().trimmed().toInt(&depthOk);
    if (!depthOk) normalizedDepth = config.depth;
    normalizedDepth = std::max(1, normalizedDepth);

    const QString nextValueType  = canonicalObjectFifoValueType(typeItem->data(kObjectFifoTypeValueTypeRole).toString());
    const QString nextDimensions = typeItem->data(kObjectFifoTypeDimensionsRole).toString().trimmed();
    const QString nextTypeId     = typeItem->data(kObjectFifoTypeIdRole).toString().trimmed();

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
    if (!changed) return;

    config.name              = normalizedName;
    config.depth             = normalizedDepth;
    config.type.typeId       = nextTypeId;
    config.type.valueType    = nextValueType;
    config.type.dimensions   = nextDimensions;
    wire->setObjectFifo(config);
    m_document->notifyChanged();
}

void AiePropertiesPanel::applyObjectFifoTypeSelection(int row,
                                                      const QString& display,
                                                      const QString& typeId,
                                                      const QString& valueType,
                                                      const QString& dimensions)
{
    if (!m_objectFifosTable) return;
    auto* typeItem = m_objectFifosTable->item(row, 1);
    if (!typeItem) return;
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

    const bool showTile           = (kind == SelectionKind::Tile);
    const bool showHubPivot       = (kind == SelectionKind::HubPivotWire);
    const bool showFifo           = (kind == SelectionKind::FifoWire);
    const bool showDdrTransferHub = (kind == SelectionKind::DdrTransferHub);
    const bool showDdr            = (kind == SelectionKind::DdrBlock);
    const bool showDdrPivot       = (kind == SelectionKind::DdrPivotWire);
    const bool showArmWire        = (kind == SelectionKind::ArmWire);
    const bool showDirectDdrWire  = (kind == SelectionKind::DirectDdrWire);
    if (m_objectFifosGroup) {
        const bool showObjectFifos = (m_canvasHost && m_canvasHost->canvasActive() && m_document) &&
                                     (kind != SelectionKind::Tile && kind != SelectionKind::DdrBlock);
        m_objectFifosGroup->setVisible(showObjectFifos);
    }
    if (m_tileGroup)
        m_tileGroup->setVisible(showTile);
    if (m_coreFnGroup)
        m_coreFnGroup->setVisible(showTile && m_tileIsKernelTile);
    if (m_argListGroup)
        m_argListGroup->setVisible(showTile && m_tileIsKernelTile);
    if (m_hubPivotGroup)
        m_hubPivotGroup->setVisible(showHubPivot);
    if (!showHubPivot) {
        if (m_hubDimsToLabel)        m_hubDimsToLabel->setVisible(false);
        if (m_hubDimsToStreamEdit)   m_hubDimsToStreamEdit->setVisible(false);
        if (m_hubDimsFromLabel)      m_hubDimsFromLabel->setVisible(false);
        if (m_hubDimsFromStreamEdit) m_hubDimsFromStreamEdit->setVisible(false);
        if (m_hubBranchTypeLabel)    m_hubBranchTypeLabel->setVisible(false);
        if (m_hubBranchTypeEdit)     m_hubBranchTypeEdit->setVisible(false);
        if (m_hubOffsetsEditLabel)   m_hubOffsetsEditLabel->setVisible(false);
        if (m_hubOffsetsEdit)        m_hubOffsetsEdit->setVisible(false);
    }
    if (m_fifoGroup)
        m_fifoGroup->setVisible(showFifo);
    // Dims rows are toggled per-wire in refreshSelection; hide them when fifo group is hidden.
    if (!showFifo) {
        if (m_fwdDimsFromLabel)      m_fwdDimsFromLabel->setVisible(false);
        if (m_fwdDimsFromStreamEdit) m_fwdDimsFromStreamEdit->setVisible(false);
        if (m_fwdDimsToLabel)        m_fwdDimsToLabel->setVisible(false);
        if (m_fwdDimsToStreamEdit)   m_fwdDimsToStreamEdit->setVisible(false);
    }
    if (m_ddrTransferGroup)
        m_ddrTransferGroup->setVisible(showDdrTransferHub);
    if (m_ddrGroup)
        m_ddrGroup->setVisible(showDdr);
    if (m_ddrPivotWireGroup)
        m_ddrPivotWireGroup->setVisible(showDdrPivot);
    if (m_armWireGroup)
        m_armWireGroup->setVisible(showArmWire);
    if (m_directDdrWireGroup)
        m_directDdrWireGroup->setVisible(showDirectDdrWire);
}

// Returns the first FIFO wire whose endpoint A or B is attached to blockId.
Canvas::CanvasWire* AiePropertiesPanel::findFifoWireForBlock(Canvas::ObjectId blockId) const
{
    if (!m_document) return nullptr;
    for (const auto& item : m_document->items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire || !wire->hasObjectFifo()) continue;
        if ((wire->a().attached && wire->a().attached->itemId == blockId) ||
            (wire->b().attached && wire->b().attached->itemId == blockId))
            return wire;
    }
    return nullptr;
}

// Returns the FIFO wire whose endpoint matches the given block+port exactly.
Canvas::CanvasWire* AiePropertiesPanel::findFifoWireForPort(Canvas::ObjectId blockId,
                                                             Canvas::PortId portId) const
{
    if (!m_document) return nullptr;
    for (const auto& item : m_document->items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire || !wire->hasObjectFifo()) continue;
        if (wire->a().attached && wire->a().attached->itemId == blockId
                               && wire->a().attached->portId == portId)
            return wire;
        if (wire->b().attached && wire->b().attached->itemId == blockId
                               && wire->b().attached->portId == portId)
            return wire;
    }
    return nullptr;
}

// Returns the pivot (Split/Join/Broadcast) wire whose endpoint B is attached to hubBlockId.
Canvas::CanvasWire* AiePropertiesPanel::findPivotWireForHub(Canvas::ObjectId hubBlockId) const
{
    if (!m_document) return nullptr;
    for (const auto& item : m_document->items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire || !wire->hasObjectFifo()) continue;
        const auto op = wire->objectFifo()->operation;
        const bool isPivot = (op == Canvas::CanvasWire::ObjectFifoOperation::Split ||
                              op == Canvas::CanvasWire::ObjectFifoOperation::Join  ||
                              op == Canvas::CanvasWire::ObjectFifoOperation::Forward);
        if (!isPivot) continue;
        if ((wire->b().attached && wire->b().attached->itemId == hubBlockId) ||
            (wire->a().attached && wire->a().attached->itemId == hubBlockId))
            return wire;
    }
    return nullptr;
}

// If wire is an arm wire (one endpoint attaches to a hub tile that has a pivot wire),
// returns that pivot wire. Returns nullptr if wire is not an arm wire.
Canvas::CanvasWire* AiePropertiesPanel::findPivotWireForArmWire(Canvas::CanvasWire* wire) const
{
    if (!m_document || !wire || !wire->hasObjectFifo()) return nullptr;
    if (wire->objectFifo()->operation != Canvas::CanvasWire::ObjectFifoOperation::Fifo)
        return nullptr;

    // Hub blocks carry a BlockContentSymbol (the "S"/"J"/"B" label).
    auto isHubBlock = [&](Canvas::ObjectId blockId) -> bool {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(blockId));
        return block && dynamic_cast<const Canvas::BlockContentSymbol*>(block->content());
    };

    if (wire->a().attached && isHubBlock(wire->a().attached->itemId))
        return findPivotWireForHub(wire->a().attached->itemId);
    if (wire->b().attached && isHubBlock(wire->b().attached->itemId))
        return findPivotWireForHub(wire->b().attached->itemId);
    return nullptr;
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
    if (!m_document)
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
        m_effectiveFifoWireId = Canvas::ObjectId{};
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
                refreshDdrGroup(block);
            showSelectionState(SelectionKind::DdrBlock,
                               QStringLiteral("DDR block selected"),
                               QStringLiteral("Configure runtime inputs and outputs."));
            return;
        }

        // Hub block clicked → redirect to its pivot wire so the wire branch below
        // populates the split/join/broadcast properties panel.
        if (block->isLinkHub()) {
            auto* pivotWire = findPivotWireForHub(block->id());
            if (!pivotWire || !pivotWire->hasObjectFifo()) {
                showSelectionState(SelectionKind::Unsupported,
                                   QStringLiteral("Hub selected"),
                                   QStringLiteral("No FIFO annotation found for this hub."));
                return;
            }
            item = pivotWire;
        } else {
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

            if (isComputeTile && m_tileKernelRow)
                rebuildKernelChips(block->assignedKernels(), block->specId());

            refreshFifoRows(block);

            // Populate core function group (only for compute tiles with a kernel).
            const bool hasKernel = isComputeTile && !block->assignedKernels().isEmpty();
            m_tileIsKernelTile = hasKernel;
            if (hasKernel && m_coreFnModeCombo && m_coreFnEditor) {
                using Mode = Canvas::CanvasBlock::CoreFunctionConfig::Mode;
                const auto& cfg = block->coreFunctionConfig();
                const bool isBodyStmts = cfg.has_value() && cfg->mode == Mode::BodyStmts;
                const bool isSharedRef  = cfg.has_value() && cfg->mode == Mode::SharedRef;
                QSignalBlocker modeBlock(m_coreFnModeCombo);
                QSignalBlocker editorBlock(m_coreFnEditor);
                // Index: 0=default, 1=bodyStmts, 2=sharedRef
                m_coreFnModeCombo->setCurrentIndex(isBodyStmts ? 1 : isSharedRef ? 2 : 0);
                const QString newJson = cfg.has_value() ? cfg->bodyStmtsJson : QString{};
                // Only rebuild the visual editor when content actually changed from outside
                // (avoids resetting widget state on every keystroke cycle-back).
                const QString currentJson = m_coreFnEditor->toJson();
                if (newJson != currentJson && !(newJson.isEmpty() && m_coreFnEditor->isEmpty()))
                    m_coreFnEditor->setJson(newJson);
                m_coreFnEditor->setVisible(isBodyStmts);
                const QString existingSharedName = cfg.has_value() ? cfg->sharedFunctionName : QString{};
                const bool isOwner = isBodyStmts && !existingSharedName.isEmpty();
                if (m_coreFnClearBtn)        m_coreFnClearBtn->setVisible(isBodyStmts || isSharedRef);
                if (m_coreFnSaveAsSharedBtn) {
                    m_coreFnSaveAsSharedBtn->setVisible(isBodyStmts);
                    m_coreFnSaveAsSharedBtn->setText(isOwner
                        ? QStringLiteral("Update Shared")
                        : QStringLiteral("Save as Shared\u2026"));
                }
                if (m_coreFnRemoveSharedBtn) m_coreFnRemoveSharedBtn->setVisible(isOwner);
                if (m_sharedFnRow) {
                    m_sharedFnRow->setVisible(isSharedRef);
                    if (m_sharedFnPreviewLabel) m_sharedFnPreviewLabel->setVisible(isSharedRef);
                    if (isSharedRef) {
                        refreshSharedFunctionCombo();
                        // Restore selected function name.
                        if (m_sharedFnCombo && cfg.has_value()) {
                            QSignalBlocker sb(m_sharedFnCombo);
                            const int idx = m_sharedFnCombo->findData(cfg->sharedFunctionName);
                            m_sharedFnCombo->setCurrentIndex(idx >= 0 ? idx : 0);
                        }
                    }
                }
            }
            if (hasKernel)
                refreshArgList(block);

            m_updatingUi = false;

            showSelectionState(SelectionKind::Tile,
                               QStringLiteral("Tile selected"),
                               QStringLiteral("Update tile label and kernel assignments."));
            return;
        }
    }

    if (auto* wire = dynamic_cast<Canvas::CanvasWire*>(item)) {
        // Check if this is a hub arm wire or DDR pivot wire (no ObjectFifo).
        if (!wire->hasObjectFifo()) {
            const auto& epA = wire->a();
            const auto& epB = wire->b();
            Canvas::CanvasBlock* blkA = nullptr;
            Canvas::CanvasBlock* blkB = nullptr;
            bool aIsDdr = false, bIsDdr = false;
            bool aIsHub = false, bIsHub = false;
            if (epA.attached.has_value() && epB.attached.has_value()) {
                blkA = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(epA.attached->itemId));
                blkB = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(epB.attached->itemId));
                aIsDdr = blkA && blkA->specId().trimmed() == QStringLiteral("ddr");
                bIsDdr = blkB && blkB->specId().trimmed() == QStringLiteral("ddr");
                aIsHub = blkA && blkA->isLinkHub();
                bIsHub = blkB && blkB->isLinkHub();
            }
            // DDR pivot wire: one endpoint DDR, one endpoint hub.
            if ((aIsDdr && bIsHub) || (bIsDdr && aIsHub)) {
                m_ddrPivotWireId = wire->id();
                m_updatingUi = true;
                if (m_ddrPivotParamEdit) {
                    const QString cur = wire->hasFillDrain()
                        ? wire->fillDrain()->paramName : QString{};
                    m_ddrPivotParamEdit->setText(cur);
                }
                m_updatingUi = false;
                showSelectionState(SelectionKind::DdrPivotWire,
                                   QStringLiteral("DDR pivot wire selected"),
                                   QStringLiteral("Type the buffer parameter name."));
                return;
            }
            // Arm wire: one endpoint is a hub, neither is DDR.
            const bool isArmWire = !aIsDdr && !bIsDdr && (aIsHub || bIsHub);
            if (isArmWire) {
                m_armWireId = wire->id();
                m_updatingUi = true;
                if (m_armFifoEdit) {
                    const QString cur = wire->hasFillDrain()
                        ? wire->fillDrain()->fifoName : QString{};
                    m_armFifoEdit->setText(cur);
                }
                if (m_armTapEdit) {
                    const QString cur = (wire->hasFillDrain() && wire->fillDrain()->tapSymbolRef)
                        ? *wire->fillDrain()->tapSymbolRef : QString{};
                    m_armTapEdit->setText(cur);
                }
                m_updatingUi = false;
                showSelectionState(SelectionKind::ArmWire,
                                   QStringLiteral("Arm wire selected"),
                                   QStringLiteral("Type the target FIFO name."));
                return;
            }
            // Direct DDR wire: one endpoint DDR, other is not a hub (e.g. SHIM tile).
            if (aIsDdr || bIsDdr) {
                m_directDdrWireId = wire->id();
                m_updatingUi = true;
                if (m_directDdrParamValue) {
                    const bool isFill = !wire->hasFillDrain() || wire->fillDrain()->isFill;
                    const QString paramName = wire->hasFillDrain()
                        ? wire->fillDrain()->paramName : QString{};
                    const QString prefix = isFill ? QStringLiteral("Input") : QStringLiteral("Output");
                    m_directDdrParamValue->setText(
                        paramName.isEmpty() ? QStringLiteral("(unnamed)") : prefix + QStringLiteral(": ") + paramName);
                }
                if (m_directDdrFifoEdit) {
                    const QString cur = wire->hasFillDrain()
                        ? wire->fillDrain()->fifoName : QString{};
                    m_directDdrFifoEdit->setText(cur);
                }
                if (m_directDdrTapEdit) {
                    const QString cur = (wire->hasFillDrain() && wire->fillDrain()->tapSymbolRef)
                        ? *wire->fillDrain()->tapSymbolRef : QString{};
                    m_directDdrTapEdit->setText(cur);
                }
                m_updatingUi = false;
                showSelectionState(SelectionKind::DirectDdrWire,
                                   QStringLiteral("Direct DDR wire selected"),
                                   QStringLiteral("Type the target FIFO name for fill/drain routing."));
                return;
            }
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

            // Count arm branches: find the hub block (either endpoint), count ports by arm role.
            // Broadcast arms are producer ports (same role as split arms).
            int numBranches = 0;
            auto* hubBlock = [&]() -> Canvas::CanvasBlock* {
                for (const auto& ep : {wire->b(), wire->a()}) {
                    if (!ep.attached.has_value()) continue;
                    auto* blk = dynamic_cast<Canvas::CanvasBlock*>(
                        m_document->findItem(ep.attached->itemId));
                    if (blk && blk->isLinkHub()) return blk;
                }
                return nullptr;
            }();
            if (hubBlock) {
                const Canvas::PortRole armRole = (isSplit || isBroadcast)
                    ? Canvas::PortRole::Producer
                    : Canvas::PortRole::Consumer;
                for (const auto& port : hubBlock->ports()) {
                    if (port.role == armRole)
                        ++numBranches;
                }
            }

            // Resolve the source/destination ObjectFifo wire by name so that
            // depth, value type, and dimensions reflect the actual FIFO, not the pivot wire.
            const auto resolveLinkedFifo =
                [&]() -> std::optional<Canvas::CanvasWire::ObjectFifoConfig> {
                for (const auto& docItem : m_document->items()) {
                    auto* w = dynamic_cast<Canvas::CanvasWire*>(docItem.get());
                    if (!w || !w->hasObjectFifo()) continue;
                    const auto& cfg = w->objectFifo().value();
                    if (cfg.operation == Canvas::CanvasWire::ObjectFifoOperation::Fifo
                            && cfg.name == fifo.name)
                        return cfg;
                }
                return std::nullopt;
            };
            const auto linkedFifo = resolveLinkedFifo();
            const auto& srcType  = linkedFifo.has_value() ? linkedFifo->type  : fifo.type;
            const int   srcDepth = linkedFifo.has_value() ? linkedFifo->depth : fifo.depth;

            // Compute offsets string — not applicable for broadcasts (full FIFO forwarded).
            QString offsetsStr;
            if (!isBroadcast && numBranches > 0) {
                int elemCount = 1024;
                if (!srcType.dimensions.isEmpty()) {
                    int count = 1;
                    for (const QString& d : srcType.dimensions.split(u'x', Qt::SkipEmptyParts))
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

            // dims_to_stream: shown for splits and broadcasts (MM2S reformat)
            const bool showDimsTo = isSplit || isBroadcast;
            if (m_hubDimsToLabel)      m_hubDimsToLabel->setVisible(showDimsTo);
            if (m_hubDimsToStreamEdit) {
                m_hubDimsToStreamEdit->setVisible(showDimsTo);
                if (showDimsTo) m_hubDimsToStreamEdit->setText(fifo.dimsToStream);
            }
            // dims_from_stream: shown for joins and broadcasts (S2MM reformat)
            const bool showDimsFrom = !isSplit;
            if (m_hubDimsFromLabel)      m_hubDimsFromLabel->setVisible(showDimsFrom);
            if (m_hubDimsFromStreamEdit) {
                m_hubDimsFromStreamEdit->setVisible(showDimsFrom);
                if (showDimsFrom) m_hubDimsFromStreamEdit->setText(fifo.dimsFromStream);
            }
            // Branch Type and Offsets overrides: only meaningful for split/join, not broadcast
            const bool showOverrides = !isBroadcast;
            if (m_hubBranchTypeLabel)  m_hubBranchTypeLabel->setVisible(showOverrides);
            if (m_hubBranchTypeEdit) {
                m_hubBranchTypeEdit->setVisible(showOverrides);
                if (showOverrides) m_hubBranchTypeEdit->setText(fifo.branchTypeSymbol);
            }
            if (m_hubOffsetsEditLabel) m_hubOffsetsEditLabel->setVisible(showOverrides);
            if (m_hubOffsetsEdit) {
                m_hubOffsetsEdit->setVisible(showOverrides);
                if (showOverrides) m_hubOffsetsEdit->setText(fifo.offsetsOverride);
            }
            if (m_hubPivotFifoEdit)
                m_hubPivotFifoEdit->setText(fifo.name);
            if (m_hubBranchesValue)
                m_hubBranchesValue->setText(numBranches > 0
                    ? QString::number(numBranches) : QStringLiteral("-"));
            if (m_hubOffsetsValue)
                m_hubOffsetsValue->setText(offsetsStr.isEmpty()
                    ? QStringLiteral("-") : offsetsStr);
            if (m_hubDepthValue)
                m_hubDepthValue->setText(QString::number(srcDepth));
            if (m_hubValueTypeValue)
                m_hubValueTypeValue->setText(srcType.valueType.isEmpty()
                    ? QStringLiteral("i32") : srcType.valueType);
            if (m_hubDimensionsValue)
                m_hubDimensionsValue->setText(srcType.dimensions.isEmpty()
                    ? QStringLiteral("-") : srcType.dimensions);
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

        // Symbol combo — repopulate from metadata on every selection so newly
        // added TypeAbstraction entries appear without restarting the app.
        const QString currentSymbol = fifo.type.symbolRef.value_or(QString{});
        if (m_fifoSymbolCombo) {
            m_fifoSymbolCombo->clear();
            m_fifoSymbolCombo->addItem(QStringLiteral("None"));
            const QJsonObject metadata = m_canvasDocuments
                ? m_canvasDocuments->activeMetadata() : QJsonObject{};
            for (const FifoTypeOption& opt : fifoTypeOptionsFromMetadata(metadata))
                m_fifoSymbolCombo->addItem(opt.name, opt.id);
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

        // dims_from_stream / dims_to_stream — only visible for Forward wires
        const bool isForward = (fifo.operation == Canvas::CanvasWire::ObjectFifoOperation::Forward);
        if (m_fwdDimsFromLabel)      m_fwdDimsFromLabel->setVisible(isForward);
        if (m_fwdDimsFromStreamEdit) { m_fwdDimsFromStreamEdit->setVisible(isForward); m_fwdDimsFromStreamEdit->setText(fifo.dimsFromStream); }
        if (m_fwdDimsToLabel)        m_fwdDimsToLabel->setVisible(isForward);
        if (m_fwdDimsToStreamEdit)   { m_fwdDimsToStreamEdit->setVisible(isForward); m_fwdDimsToStreamEdit->setText(fifo.dimsToStream); }

        m_effectiveFifoWireId = wire->id();
        m_updatingUi = false;

        showSelectionState(SelectionKind::FifoWire,
                           QStringLiteral("Object FIFO selected"),
                           QStringLiteral("Edit FIFO properties above."));
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

/// Build the default template JSON for a kernel tile when switching to BodyStmts mode
/// with no pre-existing body.  Mirrors what HlirSyncService::buildWorkers generates for
/// the default acquire/call/release pattern, using in0/in1.../out0/out1... naming.
static QString buildDefaultBodyJson(const Canvas::CanvasBlock* block,
                                    const Canvas::CanvasDocument* document)
{
    const QStringList& assigned = block->assignedKernels();

    // If the user has explicitly assigned fn_args via coreBodyArgs, derive the template from
    // those rather than from raw wire counts, so param names match the fifo/kernel names.
    const QList<Canvas::CanvasBlock::CoreBodyArgSpec>& coreArgs = block->coreBodyArgs();
    if (!coreArgs.isEmpty()) {
        using Kind = Canvas::CanvasBlock::CoreBodyArgSpec::Kind;
        QStringList kernelParams, inputParams, outputParams;
        for (const auto& arg : coreArgs) {
            // paramName is the function parameter ("in0", "out0", "kernel"); ref is the bound value.
            const QString name = arg.paramName.trimmed().isEmpty() ? arg.ref.trimmed() : arg.paramName.trimmed();
            if (name.isEmpty()) continue;
            switch (arg.kind) {
                case Kind::Kernel:       kernelParams << name; break;
                case Kind::FifoConsumer: inputParams  << name; break;
                case Kind::FifoProducer: outputParams << name; break;
            }
        }
        QJsonArray  params;
        QJsonObject paramRoles;
        auto addPm = [&](const QString& n, int role) { params.append(n); paramRoles[n] = role; };
        for (const QString& n : kernelParams) addPm(n, 0);
        for (const QString& n : inputParams)  addPm(n, 1);
        for (const QString& n : outputParams) addPm(n, 2);

        QJsonArray bufArgs;
        for (const QString& n : inputParams)  bufArgs.append(u"buf_"_s + n);
        for (const QString& n : outputParams) bufArgs.append(u"buf_"_s + n);

        QJsonArray body;
        for (const QString& n : inputParams) {
            QJsonObject acq;
            acq[u"type"_s]       = u"Acquire"_s;
            acq[u"fifo_param"_s] = n;
            acq[u"count"_s]      = 1;
            acq[u"local_var"_s]  = u"buf_"_s + n;
            body.append(acq);
        }
        for (const QString& n : outputParams) {
            QJsonObject acq;
            acq[u"type"_s]       = u"Acquire"_s;
            acq[u"fifo_param"_s] = n;
            acq[u"count"_s]      = 1;
            acq[u"local_var"_s]  = u"buf_"_s + n;
            body.append(acq);
        }
        for (const QString& kp : kernelParams) {
            QJsonObject call;
            call[u"type"_s]         = u"KernelCall"_s;
            call[u"kernel_param"_s] = kp;
            call[u"args"_s]         = bufArgs;
            body.append(call);
        }
        for (const QString& n : inputParams) {
            QJsonObject rel;
            rel[u"type"_s]       = u"Release"_s;
            rel[u"fifo_param"_s] = n;
            rel[u"count"_s]      = 1;
            body.append(rel);
        }
        for (const QString& n : outputParams) {
            QJsonObject rel;
            rel[u"type"_s]       = u"Release"_s;
            rel[u"fifo_param"_s] = n;
            rel[u"count"_s]      = 1;
            body.append(rel);
        }
        QJsonObject root;
        root[u"params"_s]      = params;
        root[u"param_roles"_s] = paramRoles;
        root[u"body"_s]        = body;
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    // Walk all wires connected to this tile and mirror HlirSyncService's classification:
    //
    // Convention: hub is always at endpoint A, tile always at endpoint B for arm wires.
    //   - Hub port at A is Producer (split/broadcast) → tile receives → input
    //   - Hub port at A is Consumer (join)            → tile sends    → output
    // For direct FIFO wires (no hub):
    //   - Tile at B (consumer endpoint) → input
    //   - Tile at A (producer endpoint) → output
    int numInputs = 0;
    int numOutputs = 0;

    // Build a quick id→block lookup for hub classification.
    QHash<Canvas::ObjectId, Canvas::CanvasBlock*> blockIndex;
    for (const auto& wItem : document->items()) {
        auto* blk = dynamic_cast<Canvas::CanvasBlock*>(wItem.get());
        if (blk) blockIndex.insert(blk->id(), blk);
    }

    for (const auto& wItem : document->items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(wItem.get());
        if (!wire || !wire->hasObjectFifo()) continue;
        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached || !epB.attached) continue;

        const bool tileIsA = (epA.attached->itemId == block->id());
        const bool tileIsB = (epB.attached->itemId == block->id());
        if (!tileIsA && !tileIsB) continue;

        if (tileIsB) {
            Canvas::CanvasBlock* blockA = blockIndex.value(epA.attached->itemId, nullptr);
            if (!blockA) continue;

            if (!blockA->isLinkHub()) {
                // Direct FIFO, tile is consumer → input.
                ++numInputs;
            } else {
                // Arm wire: direction determined by hub's port role at endpoint A.
                Canvas::PortRole hubRole = Canvas::PortRole::Dynamic;
                for (const auto& port : blockA->ports()) {
                    if (port.id == epA.attached->portId) {
                        hubRole = port.role;
                        break;
                    }
                }
                if (hubRole == Canvas::PortRole::Producer)
                    ++numInputs;   // split or broadcast → tile receives
                else if (hubRole == Canvas::PortRole::Consumer)
                    ++numOutputs;  // join → tile sends
            }
        } else { // tileIsA
            Canvas::CanvasBlock* blockB = blockIndex.value(epB.attached->itemId, nullptr);
            if (!blockB || blockB->isLinkHub()) continue;
            // Direct FIFO, tile is producer → output.
            ++numOutputs;
        }
    }

    // Build params array and role map: all kernels first, then inputs, then outputs.
    // param_roles: 0=kernel, 1=input, 2=output — read by BodyStmtsEditor::setJson.
    // Kernel names: "kernel" for the first, "kernel2", "kernel3", ... for extras.
    QStringList allKernelParams;
    allKernelParams.append(assigned.isEmpty() ? u"kernel"_s : assigned.first());
    for (int ki = 1; ki < assigned.size(); ++ki)
        allKernelParams.append(QStringLiteral("kernel%1").arg(ki + 1));

    QJsonArray  params;
    QJsonObject paramRoles;
    auto addParam = [&](const QString& name, int role) {
        params.append(name);
        paramRoles.insert(name, role);
    };
    for (const QString& kp : allKernelParams) addParam(kp, 0);
    for (int i = 0; i < numInputs;  ++i) addParam(QString(u"in%1"_s).arg(i),  1);
    for (int i = 0; i < numOutputs; ++i) addParam(QString(u"out%1"_s).arg(i), 2);

    // Build body statements: acquire all FIFOs, one kernel call per kernel, release all FIFOs.
    QJsonArray body;
    for (int i = 0; i < numInputs; ++i) {
        QJsonObject acq;
        acq[u"type"_s]       = u"Acquire"_s;
        acq[u"fifo_param"_s] = QString(u"in%1"_s).arg(i);
        acq[u"count"_s]      = 1;
        acq[u"local_var"_s]  = QString(u"buf_in%1"_s).arg(i);
        body.append(acq);
    }
    for (int i = 0; i < numOutputs; ++i) {
        QJsonObject acq;
        acq[u"type"_s]       = u"Acquire"_s;
        acq[u"fifo_param"_s] = QString(u"out%1"_s).arg(i);
        acq[u"count"_s]      = 1;
        acq[u"local_var"_s]  = QString(u"buf_out%1"_s).arg(i);
        body.append(acq);
    }

    QJsonArray callArgs;
    for (int i = 0; i < numInputs;  ++i) callArgs.append(QString(u"buf_in%1"_s).arg(i));
    for (int i = 0; i < numOutputs; ++i) callArgs.append(QString(u"buf_out%1"_s).arg(i));
    for (const QString& kp : allKernelParams) {
        QJsonObject call;
        call[u"type"_s]         = u"KernelCall"_s;
        call[u"kernel_param"_s] = kp;
        call[u"args"_s]         = callArgs;
        body.append(call);
    }

    for (int i = 0; i < numInputs; ++i) {
        QJsonObject rel;
        rel[u"type"_s]       = u"Release"_s;
        rel[u"fifo_param"_s] = QString(u"in%1"_s).arg(i);
        rel[u"count"_s]      = 1;
        body.append(rel);
    }
    for (int i = 0; i < numOutputs; ++i) {
        QJsonObject rel;
        rel[u"type"_s]       = u"Release"_s;
        rel[u"fifo_param"_s] = QString(u"out%1"_s).arg(i);
        rel[u"count"_s]      = 1;
        body.append(rel);
    }

    QJsonObject root;
    root[u"params"_s]      = params;
    root[u"param_roles"_s] = paramRoles;
    root[u"body"_s]        = body;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// paramsForBlock() — derive the ordered param list for a tile's core function.
//
// Priority:
//  1. If coreBodyArgs is already populated → use it (preserves user assignments).
//  2. If BodyStmts JSON has params + param_roles → parse those.
//  3. Otherwise → default: kernel(s) first, then in0/in1.../out0/out1... from wires.
//
// Returns {paramName, kind} pairs in function-signature order.
// ---------------------------------------------------------------------------
namespace {
using namespace Qt::StringLiterals;
struct ParamInfo {
    QString name;
    Canvas::CanvasBlock::CoreBodyArgSpec::Kind kind;
};

QList<ParamInfo> paramsForBlock(const Canvas::CanvasBlock* block,
                                const Canvas::CanvasDocument* document)
{
    using Kind = Canvas::CanvasBlock::CoreBodyArgSpec::Kind;

    // 1. coreBodyArgs already set → derive from it.
    if (!block->coreBodyArgs().isEmpty()) {
        QList<ParamInfo> result;
        for (const auto& arg : block->coreBodyArgs())
            result.append({arg.paramName, arg.kind});
        return result;
    }

    // 2. BodyStmts JSON with params/param_roles.
    if (block->hasCoreFunctionConfig()) {
        const auto& cfg = block->coreFunctionConfig();
        if (cfg->mode == Canvas::CanvasBlock::CoreFunctionConfig::Mode::BodyStmts
            && !cfg->bodyStmtsJson.isEmpty()) {
            const QJsonDocument jdoc =
                QJsonDocument::fromJson(cfg->bodyStmtsJson.toUtf8());
            const QJsonObject obj   = jdoc.object();
            const QJsonArray  jparams = obj[u"params"].toArray();
            const QJsonObject jroles  = obj[u"param_roles"].toObject();
            if (!jparams.isEmpty()) {
                QList<ParamInfo> result;
                for (const auto& p : jparams) {
                    const QString name = p.toString();
                    const int role = jroles[name].toInt(1); // 0=kernel, 1=in, 2=out
                    Kind kind = (role == 0) ? Kind::Kernel
                              : (role == 2) ? Kind::FifoProducer
                              :               Kind::FifoConsumer;
                    result.append({name, kind});
                }
                return result;
            }
        }
    }

    // 3. Default: one kernel param per assigned kernel, then in/out from wires.
    QList<ParamInfo> result;
    const QStringList& kernels = block->assignedKernels();
    if (kernels.size() == 1)
        result.append({u"kernel"_s, Kind::Kernel});
    else {
        for (int i = 0; i < kernels.size(); ++i)
            result.append({QStringLiteral("Kernel%1").arg(i + 1), Kind::Kernel});
    }
    const QList<TileFifoInfo> fifos = connectedFifosForTile(block, document);
    int inIdx = 0, outIdx = 0;
    for (bool wantInput : {true, false}) {
        for (const auto& f : fifos) {
            if (f.isInput != wantInput) continue;
            if (wantInput) result.append({QStringLiteral("in%1").arg(inIdx++),  Kind::FifoConsumer});
            else           result.append({QStringLiteral("out%1").arg(outIdx++), Kind::FifoProducer});
        }
    }
    return result;
}
} // anonymous namespace

void AiePropertiesPanel::applyCoreFunctionBody()
{
    if (m_updatingUi || !m_document || !m_coreFnModeCombo || !m_coreFnEditor)
        return;

    auto* block = selectedBlock();
    if (!block)
        return;

    using Mode = Canvas::CanvasBlock::CoreFunctionConfig::Mode;
    const QString modeKey = m_coreFnModeCombo->currentData().toString();
    const bool isBodyStmts = (modeKey == QStringLiteral("bodyStmts"));
    const bool isSharedRef = (modeKey == QStringLiteral("sharedRef"));

    const auto& existing = block->coreFunctionConfig();
    const QString existingSharedName = existing.has_value() ? existing->sharedFunctionName : QString{};
    const bool isOwner = isBodyStmts && !existingSharedName.isEmpty();

    // Update widget visibility.
    m_coreFnEditor->setVisible(isBodyStmts);
    if (m_sharedFnRow)           m_sharedFnRow->setVisible(isSharedRef);
    if (m_sharedFnPreviewLabel)  m_sharedFnPreviewLabel->setVisible(isSharedRef);
    if (m_coreFnClearBtn)        m_coreFnClearBtn->setVisible(isBodyStmts || isSharedRef);
    if (m_coreFnSaveAsSharedBtn) {
        m_coreFnSaveAsSharedBtn->setVisible(isBodyStmts);
        m_coreFnSaveAsSharedBtn->setText(isOwner
            ? QStringLiteral("Update Shared")
            : QStringLiteral("Save as Shared\u2026"));
    }
    if (m_coreFnRemoveSharedBtn) m_coreFnRemoveSharedBtn->setVisible(isOwner);
    if (isSharedRef)             refreshSharedFunctionCombo();

    Canvas::CanvasBlock::CoreFunctionConfig cfg;

    if (isBodyStmts) {
        cfg.mode = Mode::BodyStmts;
        // If switching into BodyStmts with no saved body, auto-populate the default template.
        const QString savedJson = existing.has_value() ? existing->bodyStmtsJson : QString{};
        if (savedJson.isEmpty() && m_coreFnEditor->isEmpty()) {
            const QString defaultJson = buildDefaultBodyJson(block, m_document);
            QSignalBlocker blk(m_coreFnEditor);
            m_coreFnEditor->setJson(defaultJson);
        }
        cfg.bodyStmtsJson      = m_coreFnEditor->toJson();
        cfg.sharedFunctionName = existingSharedName;

        // If this tile owns a shared function, auto-update the library entry and
        // refresh fn_args on every consumer tile so their tables stay in sync.
        if (!existingSharedName.isEmpty() && m_canvasDocuments
            && m_canvasDocuments->hasOpenDocument()) {
            QJsonObject metadata = m_canvasDocuments->activeMetadata();
            QJsonArray library = metadata.value(kCoreFunctionLibraryKey).toArray();
            for (int i = 0; i < library.size(); ++i) {
                QJsonObject entry = library[i].toObject();
                if (entry.value(u"name"_s).toString().trimmed() == existingSharedName) {
                    entry.insert(u"bodyStmtsJson"_s, cfg.bodyStmtsJson);
                    library[i] = entry;
                    break;
                }
            }
            metadata.insert(kCoreFunctionLibraryKey, library);
            m_canvasDocuments->updateActiveMetadata(metadata);

            // Re-populate fn_args for all consumer tiles that reference this shared function.
            using Mode = Canvas::CanvasBlock::CoreFunctionConfig::Mode;
            for (const auto& item : m_document->items()) {
                auto* consumerBlk = dynamic_cast<Canvas::CanvasBlock*>(item.get());
                if (!consumerBlk || !consumerBlk->hasCoreFunctionConfig())
                    continue;
                const auto& consumerCfg = consumerBlk->coreFunctionConfig();
                if (consumerCfg->mode == Mode::SharedRef
                        && consumerCfg->sharedFunctionName == existingSharedName) {
                    consumerBlk->clearCoreBodyArgs();
                    autoPopulateArgList(consumerBlk);
                }
            }
        }
    } else if (isSharedRef) {
        cfg.mode = Mode::SharedRef;
        cfg.bodyStmtsJson = existing.has_value() ? existing->bodyStmtsJson : QString{};
        // sharedFunctionName is written by applySharedFunctionSelection; preserve current value.
        cfg.sharedFunctionName = existing.has_value() ? existing->sharedFunctionName : QString{};
    } else {
        cfg.mode = Mode::Default;
        // Preserve both when switching back to Default so they can be restored later.
        cfg.bodyStmtsJson      = existing.has_value() ? existing->bodyStmtsJson      : QString{};
        cfg.sharedFunctionName = existing.has_value() ? existing->sharedFunctionName : QString{};
    }

    block->setCoreFunctionConfig(std::move(cfg));

    // Reconcile coreBodyArgs to match the updated param list so the arg table stays in sync.
    {
        const QList<Canvas::CanvasBlock::CoreBodyArgSpec> oldArgs = block->coreBodyArgs();
        QHash<QString, Canvas::CanvasBlock::CoreBodyArgSpec> oldByParam;
        for (const auto& a : oldArgs)
            oldByParam[a.paramName] = a;

        block->clearCoreBodyArgs(); // bypass priority-1 in paramsForBlock
        const QList<ParamInfo> newParams = paramsForBlock(block, m_document);

        QList<Canvas::CanvasBlock::CoreBodyArgSpec> newArgs;
        for (const auto& p : newParams) {
            const auto it = oldByParam.find(p.name);
            if (it != oldByParam.end()) {
                newArgs.append(*it); // preserve kind + ref
            } else {
                Canvas::CanvasBlock::CoreBodyArgSpec a;
                a.paramName = p.name;
                a.kind      = p.kind;
                newArgs.append(a);
            }
        }
        block->setCoreBodyArgs(std::move(newArgs));
    }

    m_document->notifyChanged();
}


void AiePropertiesPanel::refreshSharedFunctionCombo()
{
    if (!m_sharedFnCombo || !m_canvasDocuments || !m_canvasDocuments->hasOpenDocument())
        return;

    const QJsonObject metadata = m_canvasDocuments->activeMetadata();
    const QJsonArray library = metadata.value(kCoreFunctionLibraryKey).toArray();

    QSignalBlocker blk(m_sharedFnCombo);
    const QString current = m_sharedFnCombo->currentText();
    m_sharedFnCombo->clear();
    m_sharedFnCombo->addItem(QStringLiteral("(select\u2026)"), QString{});
    for (const auto& v : library) {
        const QString name = v.toObject().value(u"name"_s).toString().trimmed();
        if (!name.isEmpty())
            m_sharedFnCombo->addItem(name, name);
    }

    // Restore selection if still present.
    const int idx = m_sharedFnCombo->findData(current);
    m_sharedFnCombo->setCurrentIndex(idx >= 0 ? idx : 0);

    // Update preview label — just show the function name, not the full JSON body.
    const QString selName = m_sharedFnCombo->currentData().toString();
    if (m_sharedFnPreviewLabel) {
        m_sharedFnPreviewLabel->setText(selName.isEmpty()
            ? QString{}
            : QStringLiteral("Shared Function: ") + selName);
    }
}

void AiePropertiesPanel::applySharedFunctionSelection()
{
    if (m_updatingUi || !m_document || !m_sharedFnCombo)
        return;

    auto* block = selectedBlock();
    if (!block)
        return;

    const QString name = m_sharedFnCombo->currentData().toString();

    auto cfg = block->coreFunctionConfig().value_or(Canvas::CanvasBlock::CoreFunctionConfig{});
    cfg.mode               = Canvas::CanvasBlock::CoreFunctionConfig::Mode::SharedRef;
    cfg.sharedFunctionName = name;
    block->setCoreFunctionConfig(std::move(cfg));

    // Update the preview label — show just the function name.
    if (m_sharedFnPreviewLabel) {
        m_sharedFnPreviewLabel->setText(name.isEmpty()
            ? QString{}
            : QStringLiteral("Shared Function: ") + name);
    }

    // Clear stale fn_args from any previous assignment and auto-populate
    // from the new shared function's parameter list so the table updates
    // immediately without the user having to click Clear.
    block->clearCoreBodyArgs();
    autoPopulateArgList(block);  // saves + notifies
}

void AiePropertiesPanel::saveCoreFunctionAsShared()
{
    if (!m_document || !m_coreFnEditor || !m_canvasDocuments
        || !m_canvasDocuments->hasOpenDocument())
        return;

    auto* block = selectedBlock();
    if (!block)
        return;

    const QString bodyJson = m_coreFnEditor->toJson().trimmed();
    if (bodyJson.isEmpty())
        return;

    // Ask for a name via an inline input dialog.
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        QStringLiteral("Save as Shared Function"),
        QStringLiteral("Function name:"),
        QLineEdit::Normal,
        block->label().trimmed().isEmpty()
            ? QStringLiteral("shared_fn") : block->label().trimmed(),
        &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    QJsonObject metadata = m_canvasDocuments->activeMetadata();
    QJsonArray library = metadata.value(kCoreFunctionLibraryKey).toArray();

    // Overwrite entry with same name if it exists, otherwise append.
    bool found = false;
    for (int i = 0; i < library.size(); ++i) {
        QJsonObject entry = library[i].toObject();
        if (entry.value(u"name"_s).toString().trimmed() == name) {
            entry.insert(u"bodyStmtsJson"_s, bodyJson);
            library[i] = entry;
            found = true;
            break;
        }
    }
    if (!found) {
        QJsonObject entry;
        entry.insert(u"name"_s, name);
        entry.insert(u"bodyStmtsJson"_s, bodyJson);
        library.append(entry);
    }

    metadata.insert(kCoreFunctionLibraryKey, library);
    m_canvasDocuments->updateActiveMetadata(metadata);

    // Mark this tile as the owner of the shared function so "Remove as Shared" is shown
    // and so the body auto-updates the library whenever it changes.
    auto cfg = block->coreFunctionConfig().value_or(Canvas::CanvasBlock::CoreFunctionConfig{});
    cfg.sharedFunctionName = name;
    block->setCoreFunctionConfig(std::move(cfg));

    if (m_coreFnSaveAsSharedBtn)   m_coreFnSaveAsSharedBtn->setText(QStringLiteral("Update Shared"));
    if (m_coreFnRemoveSharedBtn)   m_coreFnRemoveSharedBtn->setVisible(true);

    m_document->notifyChanged();

    // Refresh the shared fn combo so the new name appears immediately.
    refreshSharedFunctionCombo();
}

void AiePropertiesPanel::removeCoreFunctionShared()
{
    if (!m_document || !m_canvasDocuments || !m_canvasDocuments->hasOpenDocument())
        return;

    auto* block = selectedBlock();
    if (!block)
        return;

    const auto& cfg = block->coreFunctionConfig();
    if (!cfg.has_value() || cfg->sharedFunctionName.isEmpty())
        return;

    const QString name = cfg->sharedFunctionName;

    // Remove from library.
    QJsonObject metadata = m_canvasDocuments->activeMetadata();
    QJsonArray library = metadata.value(kCoreFunctionLibraryKey).toArray();
    for (int i = 0; i < library.size(); ++i) {
        if (library[i].toObject().value(u"name"_s).toString().trimmed() == name) {
            library.removeAt(i);
            break;
        }
    }
    metadata.insert(kCoreFunctionLibraryKey, library);
    m_canvasDocuments->updateActiveMetadata(metadata);

    // Revert all SharedRef tiles referencing this name back to Default,
    // and clear sharedFunctionName from the owner tile.
    for (const auto& item : m_document->items()) {
        auto* blk = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!blk || !blk->hasCoreFunctionConfig())
            continue;
        const auto& blkCfg = blk->coreFunctionConfig();
        using Mode = Canvas::CanvasBlock::CoreFunctionConfig::Mode;
        if (blkCfg->mode == Mode::SharedRef && blkCfg->sharedFunctionName == name) {
            Canvas::CanvasBlock::CoreFunctionConfig newCfg;
            newCfg.mode = Mode::Default;
            blk->setCoreFunctionConfig(std::move(newCfg));
        } else if (blkCfg->sharedFunctionName == name) {
            // This is the owner tile — clear the sharedFunctionName.
            Canvas::CanvasBlock::CoreFunctionConfig newCfg = *blkCfg;
            newCfg.sharedFunctionName = QString{};
            blk->setCoreFunctionConfig(std::move(newCfg));
        }
    }

    // Update the owner tile's button state.
    if (m_coreFnSaveAsSharedBtn)  m_coreFnSaveAsSharedBtn->setText(QStringLiteral("Save as Shared\u2026"));
    if (m_coreFnRemoveSharedBtn)  m_coreFnRemoveSharedBtn->setVisible(false);

    m_document->notifyChanged();
}

void AiePropertiesPanel::refreshFifoRows(Canvas::CanvasBlock* block)
{
    if (!m_document || !m_tileInputFifosValue || !m_tileOutputFifosValue || !block)
        return;
    const QList<TileFifoInfo> fifos = connectedFifosForTile(block, m_document);
    QStringList inputs, outputs;
    for (const auto& f : fifos)
        (f.isInput ? inputs : outputs) << f.name;
    m_tileInputFifosValue->setText(inputs.isEmpty()  ? QStringLiteral("-") : inputs.join(u'\n'));
    m_tileOutputFifosValue->setText(outputs.isEmpty() ? QStringLiteral("-") : outputs.join(u'\n'));
}

void AiePropertiesPanel::refreshArgList(Canvas::CanvasBlock* block)
{
    if (!m_argListTable || !block || !m_document)
        return;

    using Kind = Canvas::CanvasBlock::CoreBodyArgSpec::Kind;

    // Available refs per kind.
    const QStringList& kernelOptions = block->assignedKernels();
    const QList<TileFifoInfo> fifos  = connectedFifosForTile(block, m_document);
    QStringList inputOptions, outputOptions;
    for (const auto& f : fifos)
        (f.isInput ? inputOptions : outputOptions) << f.name;

    // Build paramName → current ref lookup from existing coreBodyArgs.
    QHash<QString, QString> refByParam;
    for (const auto& arg : block->coreBodyArgs())
        refByParam[arg.paramName] = arg.ref;

    const QList<ParamInfo> params = paramsForBlock(block, m_document);

    // Per-kind counter for auto-assigning refs sequentially when no saved ref exists.
    // Starts at combo index 1 (skipping the "—" placeholder at index 0).
    QHash<Kind, int> autoNextIdx;

    m_argListTable->setRowCount(0);
    for (const auto& param : params) {
        const int row = m_argListTable->rowCount();
        m_argListTable->insertRow(row);

        // Argument column: read-only.
        auto* argItem = new QTableWidgetItem(param.name);
        argItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_argListTable->setItem(row, 0, argItem);

        // Reference column: dropdown filtered by kind.
        auto* refCombo = new QComboBox();
        refCombo->setObjectName(QStringLiteral("AiePropertiesField"));
        refCombo->addItem(QStringLiteral("\u2014")); // "—" unset placeholder

        const QStringList* opts = nullptr;
        switch (param.kind) {
            case Kind::Kernel:       opts = &kernelOptions; break;
            case Kind::FifoConsumer: opts = &inputOptions;  break;
            case Kind::FifoProducer: opts = &outputOptions; break;
        }
        if (opts)
            for (const QString& opt : *opts)
                refCombo->addItem(opt);

        // Select current assignment, or auto-select next available for this kind sequentially.
        const QString cur = refByParam.value(param.name);
        if (!cur.isEmpty()) {
            const int idx = refCombo->findText(cur);
            refCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        } else {
            const int nextIdx = autoNextIdx.value(param.kind, 1);
            if (nextIdx < refCombo->count()) {
                refCombo->setCurrentIndex(nextIdx);
                autoNextIdx[param.kind] = nextIdx + 1;
            }
            // else no more options for this kind — leave at "—" (index 0)
        }

        connect(refCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &AiePropertiesPanel::applyArgList);
        m_argListTable->setCellWidget(row, 1, refCombo);
    }

    // Check if any row has an auto-selected ref that isn't yet persisted to the model.
    // This covers both: (a) coreBodyArgs was completely empty, (b) new params were added
    // with empty refs by applyCoreFunctionBody's reconcile step.
    const bool hasUnpersistedRefs = [&]() {
        QHash<QString, QString> existingRefs;
        for (const auto& a : block->coreBodyArgs())
            existingRefs[a.paramName] = a.ref;
        for (int r = 0; r < m_argListTable->rowCount(); ++r) {
            auto* item = m_argListTable->item(r, 0);
            auto* combo = qobject_cast<QComboBox*>(m_argListTable->cellWidget(r, 1));
            if (!item || !combo) continue;
            const QString paramName = item->text();
            const QString selected  = combo->currentText();
            const QString ref = (selected == QStringLiteral("\u2014")) ? QString() : selected;
            if (ref != existingRefs.value(paramName))
                return true;
        }
        return false;
    }();

    if (hasUnpersistedRefs && m_argListTable->rowCount() > 0) {
        QList<Canvas::CanvasBlock::CoreBodyArgSpec> args;
        for (int row = 0; row < m_argListTable->rowCount(); ++row) {
            auto* argItem  = m_argListTable->item(row, 0);
            auto* refCombo = qobject_cast<QComboBox*>(m_argListTable->cellWidget(row, 1));
            if (!argItem || !refCombo)
                continue;
            Canvas::CanvasBlock::CoreBodyArgSpec arg;
            arg.paramName = argItem->text();
            arg.kind      = (row < params.size()) ? params[row].kind
                          : Canvas::CanvasBlock::CoreBodyArgSpec::Kind::FifoConsumer;
            const QString selected = refCombo->currentText();
            arg.ref = (selected == QStringLiteral("\u2014")) ? QString() : selected;
            args.append(arg);
        }
        block->setCoreBodyArgs(std::move(args));
        // intentionally no m_document->notifyChanged() here
    }
}

void AiePropertiesPanel::applyArgList()
{
    if (m_updatingUi || !m_document || !m_argListTable)
        return;
    auto* block = selectedBlock();
    if (!block)
        return;

    const QList<ParamInfo> params = paramsForBlock(block, m_document);

    QList<Canvas::CanvasBlock::CoreBodyArgSpec> args;
    for (int row = 0; row < m_argListTable->rowCount(); ++row) {
        auto* argItem  = m_argListTable->item(row, 0);
        auto* refCombo = qobject_cast<QComboBox*>(m_argListTable->cellWidget(row, 1));
        if (!argItem || !refCombo)
            continue;

        Canvas::CanvasBlock::CoreBodyArgSpec arg;
        arg.paramName = argItem->text();
        arg.kind      = (row < params.size()) ? params[row].kind
                      : Canvas::CanvasBlock::CoreBodyArgSpec::Kind::FifoConsumer;

        const QString selected = refCombo->currentText();
        arg.ref = (selected == QStringLiteral("\u2014")) ? QString() : selected;

        args.append(arg);
    }
    block->setCoreBodyArgs(std::move(args));
    m_document->notifyChanged();
}

void AiePropertiesPanel::autoPopulateArgList(Canvas::CanvasBlock* block)
{
    if (!m_document || !block)
        return;

    using Kind = Canvas::CanvasBlock::CoreBodyArgSpec::Kind;

    const QList<ParamInfo> params = paramsForBlock(block, m_document);
    const QStringList& kernels    = block->assignedKernels();
    const QList<TileFifoInfo> fifos = connectedFifosForTile(block, m_document);
    QStringList inputFifos, outputFifos;
    for (const auto& f : fifos)
        (f.isInput ? inputFifos : outputFifos) << f.name;

    QList<Canvas::CanvasBlock::CoreBodyArgSpec> args;
    int kIdx = 0, inIdx = 0, outIdx = 0;
    for (const auto& param : params) {
        Canvas::CanvasBlock::CoreBodyArgSpec arg;
        arg.paramName = param.name;
        arg.kind      = param.kind;
        switch (param.kind) {
            case Kind::Kernel:
                arg.ref = (kIdx < kernels.size()) ? kernels[kIdx++] : QString();
                break;
            case Kind::FifoConsumer:
                arg.ref = (inIdx < inputFifos.size()) ? inputFifos[inIdx++] : QString();
                break;
            case Kind::FifoProducer:
                arg.ref = (outIdx < outputFifos.size()) ? outputFifos[outIdx++] : QString();
                break;
        }
        args.append(arg);
    }
    block->setCoreBodyArgs(std::move(args));
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

    const bool isSplit     = (config.operation == Canvas::CanvasWire::ObjectFifoOperation::Split);
    const bool isBroadcast = (config.operation == Canvas::CanvasWire::ObjectFifoOperation::Forward);
    if (isSplit || isBroadcast) {
        if (m_hubDimsToStreamEdit)
            config.dimsToStream = m_hubDimsToStreamEdit->text().trimmed();
    }
    if (!isSplit) {
        if (m_hubDimsFromStreamEdit)
            config.dimsFromStream = m_hubDimsFromStreamEdit->text().trimmed();
    }
    if (!isBroadcast) {
        if (m_hubBranchTypeEdit)
            config.branchTypeSymbol = m_hubBranchTypeEdit->text().trimmed();
        if (m_hubOffsetsEdit)
            config.offsetsOverride = m_hubOffsetsEdit->text().trimmed();
    }

    wire->setObjectFifo(config);
    m_document->notifyChanged();
}

void AiePropertiesPanel::applyFifoAnnotation()
{
    if (m_updatingUi || !m_document || !m_fifoNameEdit || !m_fifoDepthSpin)
        return;

    if (m_effectiveFifoWireId.isNull())
        return;
    auto* wire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(m_effectiveFifoWireId));
    if (!wire || !wire->hasObjectFifo())
        return;

    Canvas::CanvasWire::ObjectFifoConfig config = wire->objectFifo().value();
    config.name  = m_fifoNameEdit->text().trimmed();
    config.depth = m_fifoDepthSpin->value();

    const bool usingSymbol = m_fifoSymbolCombo && m_fifoSymbolCombo->currentIndex() > 0;
    if (usingSymbol) {
        const QString symName = m_fifoSymbolCombo->currentText();
        config.type.symbolRef = symName;
        if (m_symbolsController) {
            for (const auto& sym : m_symbolsController->symbols()) {
                if (sym.name == symName && sym.kind == SymbolKind::TypeAbstraction) {
                    config.type.dimensions = sym.type.shapeTokens.join(u'x');
                    config.type.valueType  = canonicalObjectFifoValueType(sym.type.dtype);
                    break;
                }
            }
        }
    } else {
        config.type.symbolRef  = std::nullopt;
        config.type.valueType  = m_fifoTypeCombo ? m_fifoTypeCombo->currentText().trimmed().toLower() : config.type.valueType;
        config.type.dimensions = m_fifoDimensionsEdit ? m_fifoDimensionsEdit->text().trimmed() : config.type.dimensions;
    }

    // Save dims_from_stream / dims_to_stream for Forward wires
    if (config.operation == Canvas::CanvasWire::ObjectFifoOperation::Forward) {
        if (m_fwdDimsFromStreamEdit)
            config.dimsFromStream = m_fwdDimsFromStreamEdit->text().trimmed();
        if (m_fwdDimsToStreamEdit)
            config.dimsToStream = m_fwdDimsToStreamEdit->text().trimmed();
    }

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

    // FillDrain wires auto-compute TAPs from totalDims/numArms — no tapSymbolId applies.
    if (ddrWire->hasFillDrain())
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

void AiePropertiesPanel::refreshDdrGroup(Canvas::CanvasBlock* ddrBlock)
{
    if (!m_document || !m_ddrInputsTable || !m_ddrOutputsTable)
        return;

    m_ddrTapWireId = {};
    if (m_ddrTapWidget)
        m_ddrTapWidget->setVisible(false);

    struct Entry {
        Canvas::ObjectId wireId;
        QString name;
        QString dims;
    };
    QList<Entry> fillEntries, drainEntries;

    for (const auto& item : m_document->items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire) continue;
        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value()) continue;

        const bool aIsDdr = (epA.attached->itemId == ddrBlock->id());
        const bool bIsDdr = (epB.attached->itemId == ddrBlock->id());
        if (!aIsDdr && !bIsDdr) continue;

        bool isFill;
        if (wire->hasFillDrain()) {
            isFill = wire->fillDrain()->isFill;
        } else {
            const Canvas::PortRef& otherRef = aIsDdr ? epB.attached.value() : epA.attached.value();
            auto* otherBlock = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(otherRef.itemId));
            if (!otherBlock) continue;
            Canvas::PortRole role = Canvas::PortRole::Dynamic;
            for (const auto& port : otherBlock->ports()) {
                if (port.id == otherRef.portId) { role = port.role; break; }
            }
            if (role == Canvas::PortRole::Consumer)
                isFill = true;
            else if (role == Canvas::PortRole::Producer)
                isFill = false;
            else
                continue;
        }

        Entry e;
        e.wireId = wire->id();
        if (wire->hasFillDrain()) {
            e.name = wire->fillDrain()->paramName;
            e.dims = wire->fillDrain()->totalDims;
        }
        if (e.name.isEmpty()) {
            const int idx = isFill ? fillEntries.size() : drainEntries.size();
            e.name = isFill ? QString(QChar(u'A' + idx))
                            : (QStringLiteral("out") + QString::number(idx));
        }

        if (isFill) fillEntries.append(e);
        else        drainEntries.append(e);
    }

    {
        const QSignalBlocker b(m_ddrInputsTable);
        m_ddrInputsTable->setRowCount(fillEntries.size());
        for (int i = 0; i < fillEntries.size(); ++i) {
            const auto& e = fillEntries[i];
            auto* nameItem = new QTableWidgetItem(e.name);
            nameItem->setData(Qt::UserRole, QVariant::fromValue(e.wireId));
            m_ddrInputsTable->setItem(i, 0, nameItem);
            m_ddrInputsTable->setItem(i, 1, new QTableWidgetItem(e.dims));
        }
    }
    {
        const QSignalBlocker b(m_ddrOutputsTable);
        m_ddrOutputsTable->setRowCount(drainEntries.size());
        for (int i = 0; i < drainEntries.size(); ++i) {
            const auto& e = drainEntries[i];
            auto* nameItem = new QTableWidgetItem(e.name);
            nameItem->setData(Qt::UserRole, QVariant::fromValue(e.wireId));
            m_ddrOutputsTable->setItem(i, 0, nameItem);
            m_ddrOutputsTable->setItem(i, 1, new QTableWidgetItem(e.dims));
        }
    }
}

void AiePropertiesPanel::applyDdrTableRow(bool isFill, int row)
{
    if (m_updatingUi || !m_document) return;
    auto* table = isFill ? m_ddrInputsTable.data() : m_ddrOutputsTable.data();
    if (!table || row < 0 || row >= table->rowCount()) return;

    auto* nameItem = table->item(row, 0);
    auto* dimsItem = table->item(row, 1);
    if (!nameItem) return;

    const Canvas::ObjectId wireId = qvariant_cast<Canvas::ObjectId>(nameItem->data(Qt::UserRole));
    if (wireId.isNull()) return;
    auto* wire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(wireId));
    if (!wire) return;

    Canvas::CanvasWire::FillDrainConfig fd;
    if (wire->hasFillDrain()) {
        fd = wire->fillDrain().value();
    } else {
        fd.isFill = isFill;
    }
    fd.paramName = nameItem->text().trimmed();
    fd.totalDims = dimsItem ? dimsItem->text().trimmed() : fd.totalDims;

    wire->setFillDrain(fd);

    m_updatingUi = true;
    m_document->notifyChanged();
    m_updatingUi = false;
}

void AiePropertiesPanel::onDdrTableRowSelected(bool isFill, int row)
{
    if (m_updatingUi || !m_document || !m_ddrTapWidget) return;

    auto* table = isFill ? m_ddrInputsTable.data() : m_ddrOutputsTable.data();
    if (!table || row < 0 || row >= table->rowCount()) {
        m_ddrTapWireId = {};
        m_ddrTapWidget->setVisible(false);
        return;
    }

    // Deselect the other table
    auto* other = isFill ? m_ddrOutputsTable.data() : m_ddrInputsTable.data();
    if (other) {
        const QSignalBlocker b(other);
        other->clearSelection();
        other->setCurrentItem(nullptr);
    }

    auto* nameItem = table->item(row, 0);
    if (!nameItem) { m_ddrTapWidget->setVisible(false); return; }

    const Canvas::ObjectId wireId = qvariant_cast<Canvas::ObjectId>(nameItem->data(Qt::UserRole));
    if (wireId.isNull()) { m_ddrTapWidget->setVisible(false); return; }

    auto* wire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(wireId));
    if (!wire) { m_ddrTapWidget->setVisible(false); return; }

    m_ddrTapWireId = wireId;

    m_updatingUi = true;

    // Populate TAP symbol combo from the symbol table.
    if (m_ddrTapSymbolCombo) {
        m_ddrTapSymbolCombo->clear();
        m_ddrTapSymbolCombo->addItem(QStringLiteral("— select TAP —"), QString{});
        if (m_symbolsController) {
            for (const auto& sym : m_symbolsController->symbols()) {
                if (sym.kind != SymbolKind::TensorAccessPattern || sym.name.isEmpty())
                    continue;
                m_ddrTapSymbolCombo->addItem(sym.name, sym.name);
            }
        }
    }

    const bool hasTapSymRef = wire->hasFillDrain()
        && wire->fillDrain()->tapSymbolRef.has_value()
        && !wire->fillDrain()->tapSymbolRef->isEmpty();

    // Set source combo.
    if (m_ddrTapSourceCombo)
        m_ddrTapSourceCombo->setCurrentIndex(hasTapSymRef ? 1 : 0);
    if (m_ddrTapCustomWidget)
        m_ddrTapCustomWidget->setVisible(!hasTapSymRef);
    if (m_ddrTapSymbolCombo)
        m_ddrTapSymbolCombo->setVisible(hasTapSymRef);

    if (hasTapSymRef) {
        // Select the referenced symbol in the combo.
        const QString& ref = *wire->fillDrain()->tapSymbolRef;
        const int idx = m_ddrTapSymbolCombo ? m_ddrTapSymbolCombo->findData(ref) : -1;
        if (m_ddrTapSymbolCombo)
            m_ddrTapSymbolCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    } else {
        const bool isMatrix = wire->hasFillDrain()
            && wire->fillDrain()->mode == Canvas::CanvasWire::DimensionMode::Matrix;
        if (m_ddrTapModeCombo)
            m_ddrTapModeCombo->setCurrentIndex(isMatrix ? 1 : 0);

        if (wire->hasFillDrain() && wire->fillDrain()->tap.has_value()) {
            const auto& tap = *wire->fillDrain()->tap;
            if (m_ddrTapTileDimsEdit)   m_ddrTapTileDimsEdit->setText(tap.tileDims);
            if (m_ddrTapTileCountsEdit) m_ddrTapTileCountsEdit->setText(tap.tileCounts);
            if (m_ddrTapRepeatEdit)     m_ddrTapRepeatEdit->setText(tap.patternRepeat);
        } else {
            if (m_ddrTapTileDimsEdit)   m_ddrTapTileDimsEdit->clear();
            if (m_ddrTapTileCountsEdit) m_ddrTapTileCountsEdit->clear();
            if (m_ddrTapRepeatEdit)     m_ddrTapRepeatEdit->clear();
        }
    }

    m_updatingUi = false;

    m_ddrTapWidget->setVisible(true);
}

void AiePropertiesPanel::applyDdrTap()
{
    if (m_updatingUi || !m_document || m_ddrTapWireId.isNull()) return;

    auto* wire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(m_ddrTapWireId));
    if (!wire) return;

    Canvas::CanvasWire::FillDrainConfig fd = wire->hasFillDrain()
        ? wire->fillDrain().value()
        : Canvas::CanvasWire::FillDrainConfig{};

    const bool useSymbol = m_ddrTapSourceCombo && m_ddrTapSourceCombo->currentIndex() == 1;
    if (useSymbol) {
        const QString symName = m_ddrTapSymbolCombo
            ? m_ddrTapSymbolCombo->currentData().toString()
            : QString{};
        fd.tapSymbolRef = symName.isEmpty() ? std::nullopt : std::optional<QString>{symName};
        fd.tap          = std::nullopt;
        fd.mode         = Canvas::CanvasWire::DimensionMode::Vector;
    } else {
        if (!m_ddrTapModeCombo || !m_ddrTapTileDimsEdit) return;
        fd.tapSymbolRef = std::nullopt;
        const bool isMatrix = (m_ddrTapModeCombo->currentIndex() == 1);
        fd.mode = isMatrix ? Canvas::CanvasWire::DimensionMode::Matrix
                           : Canvas::CanvasWire::DimensionMode::Vector;
        if (isMatrix) {
            Canvas::CanvasWire::TensorTilerConfig tap;
            tap.tileDims      = m_ddrTapTileDimsEdit->text().trimmed();
            tap.tileCounts    = m_ddrTapTileCountsEdit->text().trimmed();
            tap.patternRepeat = m_ddrTapRepeatEdit->text().trimmed();
            tap.pruneStep     = false;
            tap.index         = 0;
            fd.tap = tap;
        } else {
            fd.tap = std::nullopt;
        }
    }

    wire->setFillDrain(fd);
    m_updatingUi = true;
    m_document->notifyChanged();
    m_updatingUi = false;
}

void AiePropertiesPanel::applyDdrPivotParam()
{
    if (m_updatingUi || !m_document || !m_ddrPivotParamEdit || m_ddrPivotWireId.isNull()) return;
    auto* wire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(m_ddrPivotWireId));
    if (!wire) return;

    Canvas::CanvasWire::FillDrainConfig fd;
    if (wire->hasFillDrain()) {
        fd = wire->fillDrain().value();
    } else {
        bool isFill = true;
        for (const auto* ep : {&wire->b(), &wire->a()}) {
            if (!ep->attached.has_value()) continue;
            auto* blk = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(ep->attached->itemId));
            if (!blk || blk->specId().trimmed() == QStringLiteral("ddr")) continue;
            for (const auto& port : blk->ports()) {
                if (port.id == ep->attached->portId) {
                    isFill = (port.role == Canvas::PortRole::Consumer);
                    break;
                }
            }
            break;
        }
        fd.isFill = isFill;
    }
    fd.paramName = m_ddrPivotParamEdit->text().trimmed();
    wire->setFillDrain(fd);

    m_updatingUi = true;
    m_document->notifyChanged();
    m_updatingUi = false;
}

void AiePropertiesPanel::applyArmWireEntry()
{
    if (m_updatingUi || !m_document || !m_armFifoEdit || m_armWireId.isNull())
        return;

    auto* armWire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(m_armWireId));
    if (!armWire)
        return;

    const QString typed = m_armFifoEdit->text().trimmed();

    // Validate: check that a FIFO wire with this name exists in the document.
    bool valid = false;
    if (!typed.isEmpty()) {
        for (const auto& item : m_document->items()) {
            auto* w = dynamic_cast<Canvas::CanvasWire*>(item.get());
            if (!w || !w->hasObjectFifo()) continue;
            if (w->objectFifo()->operation != Canvas::CanvasWire::ObjectFifoOperation::Fifo) continue;
            if (w->objectFifo()->name == typed) { valid = true; break; }
        }
    }
    // If invalid, revert the edit field to the previously stored value (or clear it).
    if (!valid && !typed.isEmpty()) {
        m_updatingUi = true;
        const QString prev = armWire->hasFillDrain() ? armWire->fillDrain()->fifoName : QString{};
        m_armFifoEdit->setText(prev);
        m_updatingUi = false;
        return;
    }

    Canvas::CanvasWire::FillDrainConfig fd;
    if (armWire->hasFillDrain()) {
        fd = armWire->fillDrain().value();
    } else {
        // Infer isFill from hub port role.
        const auto& epA = armWire->a();
        const auto& epB = armWire->b();
        for (const Canvas::CanvasWire::Endpoint* ep : {&epA, &epB}) {
            if (!ep->attached.has_value()) continue;
            auto* blk = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(ep->attached->itemId));
            if (!blk || !blk->isLinkHub()) continue;
            for (const auto& port : blk->ports()) {
                if (port.id == ep->attached->portId) {
                    fd.isFill = (port.role == Canvas::PortRole::Producer);
                    break;
                }
            }
            break;
        }
    }
    fd.fifoName = typed;
    if (m_armTapEdit) {
        const QString tap = m_armTapEdit->text().trimmed();
        fd.tapSymbolRef = tap.isEmpty() ? std::nullopt : std::optional<QString>(tap);
    }

    armWire->setFillDrain(fd);

    m_updatingUi = true;
    m_document->notifyChanged();
    m_updatingUi = false;
}

void AiePropertiesPanel::applyDirectDdrFifo()
{
    if (m_updatingUi || !m_document || !m_directDdrFifoEdit || m_directDdrWireId.isNull())
        return;

    auto* wire = dynamic_cast<Canvas::CanvasWire*>(m_document->findItem(m_directDdrWireId));
    if (!wire)
        return;

    const QString typed = m_directDdrFifoEdit->text().trimmed();

    Canvas::CanvasWire::FillDrainConfig fd;
    if (wire->hasFillDrain()) {
        fd = wire->fillDrain().value();
    } else {
        // Infer isFill from SHIM port role: Consumer port means DDR→SHIM (fill).
        bool isFill = true;
        for (const Canvas::CanvasWire::Endpoint* ep : {&wire->a(), &wire->b()}) {
            if (!ep->attached.has_value()) continue;
            auto* blk = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(ep->attached->itemId));
            if (!blk || blk->specId().trimmed() == QStringLiteral("ddr")) continue;
            for (const auto& port : blk->ports()) {
                if (port.id == ep->attached->portId) {
                    isFill = (port.role == Canvas::PortRole::Consumer);
                    break;
                }
            }
            break;
        }
        fd.isFill = isFill;
    }
    fd.fifoName = typed;
    if (m_directDdrTapEdit) {
        const QString tap = m_directDdrTapEdit->text().trimmed();
        fd.tapSymbolRef = tap.isEmpty() ? std::nullopt : std::optional<QString>(tap);
    }

    wire->setFillDrain(fd);

    m_updatingUi = true;
    m_document->notifyChanged();
    m_updatingUi = false;
}

} // namespace Aie::Internal
