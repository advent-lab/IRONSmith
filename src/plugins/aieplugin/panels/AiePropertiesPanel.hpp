// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasTypes.hpp"
#include "canvas/CanvasWire.hpp"

#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QComboBox;
class QGroupBox;
class QTableWidget;
QT_END_NAMESPACE

namespace Utils {
class SidebarPanelFrame;
}

namespace Canvas {
class CanvasBlock;
class CanvasDocument;
class CanvasView;
class CanvasController;
namespace Api {
class ICanvasHost;
class ICanvasDocumentService;
}
}

namespace Aie::Internal {

class AieService;
class SymbolsController;

class AiePropertiesPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit AiePropertiesPanel(AieService* service,
                                Canvas::Api::ICanvasDocumentService* canvasDocuments,
                                QWidget* parent = nullptr);

    void setSymbolsController(SymbolsController* controller);

private:
    enum class SelectionKind : uint8_t {
        None,
        Tile,
        FifoWire,
        HubPivotWire,
        DdrTransferHub,
        DdrBlock,
        Unsupported
    };

    void buildUi();
    void bindCanvasSignalsIfNeeded();
    void refreshSelection();
    void refreshObjectFifoSection();
    void refreshObjectFifoDefaultsUi();
    void applyObjectFifoRowEdits(int row);
    void applyObjectFifoTypeSelection(int row,
                                      const QString& display,
                                      const QString& typeId,
                                      const QString& valueType,
                                      const QString& dimensions);
    void showSelectionState(SelectionKind kind, const QString& summary, const QString& detail = QString());

    Canvas::CanvasBlock* selectedBlock() const;
    Canvas::CanvasWire* selectedFifoWire() const;
    Canvas::CanvasController* canvasController() const;

    void applyTileLabel();
    void applyTileStereotype();
    void applyHubPivotProperties();
    void applyDdrTransferHubTap();
    void applyObjectFifoDefaults();
    void rebuildDdrGroup(Canvas::CanvasBlock* ddrBlock);
    void applyDdrEntry(Canvas::ObjectId fifoWireId, Canvas::ObjectId ddrWireId,
                       const QString& name, const QString& dims, const QString& type,
                       bool isMatrix = false,
                       const Canvas::CanvasWire::TensorTilerConfig& tap = {},
                       const QString& symbolRef = {});

    void populateFifoSymbolCombo();
    Canvas::CanvasWire* selectedDdrTransferWire() const;

    QPointer<AieService> m_service;
    QPointer<SymbolsController> m_symbolsController;
    QPointer<Canvas::Api::ICanvasHost> m_canvasHost;
    QPointer<Canvas::Api::ICanvasDocumentService> m_canvasDocuments;
    QPointer<Canvas::CanvasDocument> m_document;
    QPointer<Canvas::CanvasView> m_canvasView;

    QPointer<Utils::SidebarPanelFrame> m_frame;
    QPointer<QLabel> m_summaryLabel;
    QPointer<QLabel> m_detailLabel;
    QPointer<QGroupBox> m_objectFifosGroup;
    QPointer<QTableWidget> m_objectFifosTable;
    QPointer<QLineEdit> m_objectFifoDefaultNameEdit;
    QPointer<QSpinBox> m_objectFifoDefaultDepthSpin;
    QPointer<QComboBox> m_objectFifoDefaultTypeCombo;

    QPointer<QGroupBox> m_tileGroup;
    QPointer<QLabel> m_tileIdValue;
    QPointer<QLabel> m_tileSpecIdValue;
    QPointer<QLabel> m_tileBoundsValue;
    QPointer<QLineEdit> m_tileLabelEdit;
    QPointer<QLineEdit> m_tileStereotypeEdit;
    QPointer<QPushButton> m_tileStereotypeClearBtn;
    QPointer<QWidget> m_tileKernelRow;
    QPointer<QLabel> m_tileKernelRowLabel;

    QPointer<QGroupBox> m_hubPivotGroup;
    QPointer<QLineEdit> m_hubPivotNameEdit;
    QPointer<QLabel>    m_hubPivotFifoLabel;
    QPointer<QLineEdit> m_hubPivotFifoEdit;
    QPointer<QLabel>    m_hubBranchesValue;
    QPointer<QLabel>    m_hubOffsetsValue;
    QPointer<QLabel>    m_hubDepthValue;
    QPointer<QLabel>    m_hubValueTypeValue;
    QPointer<QLabel>    m_hubDimensionsValue;

    QPointer<QGroupBox> m_ddrTransferGroup;
    QPointer<QLabel>    m_ddrTransferModeValue;
    QPointer<QComboBox> m_ddrTransferTapCombo;

    QPointer<QGroupBox> m_ddrGroup;
    QPointer<QWidget>   m_ddrContent;

    bool m_updatingUi = false;
    bool m_updatingObjectFifoTable = false;
};

} // namespace Aie::Internal
