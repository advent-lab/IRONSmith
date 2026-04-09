// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasTypes.hpp"
#include "canvas/CanvasWire.hpp"

#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE
class QHBoxLayout;
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
class BodyStmtsEditor;
class KernelAssignmentController;
class SymbolsController;

class AiePropertiesPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit AiePropertiesPanel(AieService* service,
                                Canvas::Api::ICanvasDocumentService* canvasDocuments,
                                QWidget* parent = nullptr);

    void setSymbolsController(SymbolsController* controller);
    void setKernelAssignmentController(KernelAssignmentController* controller);

private:
    enum class SelectionKind : uint8_t {
        None,
        Tile,
        FifoWire,
        HubPivotWire,
        DdrTransferHub,
        DdrBlock,
        DdrPivotWire,
        ArmWire,
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

    // Find a FIFO or pivot wire relevant to the current selection (block or port).
    Canvas::CanvasWire* findFifoWireForBlock(Canvas::ObjectId blockId) const;
    Canvas::CanvasWire* findFifoWireForPort(Canvas::ObjectId blockId, Canvas::PortId portId) const;
    Canvas::CanvasWire* findPivotWireForHub(Canvas::ObjectId hubBlockId) const;
    Canvas::CanvasWire* findPivotWireForArmWire(Canvas::CanvasWire* wire) const;

    void applyTileLabel();
    void applyCoreFunctionBody();
    void rebuildKernelChips(const QStringList& kernels, const QString& tileSpecId);
    void refreshFifoRows(Canvas::CanvasBlock* block);
    void refreshArgList(Canvas::CanvasBlock* block);
    void applyArgList();
    void autoPopulateArgList(Canvas::CanvasBlock* block);
    void applyFifoAnnotation();
    void applyHubPivotProperties();
    void applyDdrTransferHubTap();
    void applyObjectFifoDefaults();
    void refreshDdrGroup(Canvas::CanvasBlock* ddrBlock);
    void applyDdrTableRow(bool isFill, int row);
    void onDdrTableRowSelected(bool isFill, int row);
    void applyDdrTap();
    void applyDdrPivotParam();
    void applyArmWireEntry();

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

    QPointer<KernelAssignmentController> m_kernelAssignmentController;

    QPointer<QGroupBox> m_tileGroup;
    QPointer<QLabel> m_tileIdValue;
    QPointer<QLabel> m_tileSpecIdValue;
    QPointer<QLabel> m_tileBoundsValue;
    QPointer<QLineEdit> m_tileLabelEdit;
    QPointer<QWidget> m_tileKernelRow;
    QPointer<QLabel> m_tileKernelRowLabel;
    QHBoxLayout* m_kernelChipsLayout = nullptr; // layout inside m_tileKernelRow; not owned via QPointer
    QPointer<QLabel>       m_tileInputFifosValue;
    QPointer<QLabel>       m_tileOutputFifosValue;

    QPointer<QGroupBox>    m_argListGroup;
    QPointer<QTableWidget> m_argListTable;

    QPointer<QGroupBox> m_hubPivotGroup;
    QPointer<QLineEdit> m_hubPivotNameEdit;
    QPointer<QLabel>    m_hubPivotFifoLabel;
    QPointer<QLineEdit> m_hubPivotFifoEdit;
    QPointer<QLabel>    m_hubBranchesValue;
    QPointer<QLabel>    m_hubOffsetsValue;
    QPointer<QLabel>    m_hubDepthValue;
    QPointer<QLabel>    m_hubValueTypeValue;
    QPointer<QLabel>    m_hubDimensionsValue;

    QPointer<QGroupBox> m_fifoGroup;
    QPointer<QLabel>    m_fifoWireIdValue;
    QPointer<QLineEdit> m_fifoNameEdit;
    QPointer<QSpinBox>  m_fifoDepthSpin;
    QPointer<QComboBox> m_fifoSymbolCombo;
    QPointer<QComboBox> m_fifoTypeCombo;
    QPointer<QLineEdit> m_fifoDimensionsEdit;

    QPointer<QGroupBox> m_ddrTransferGroup;
    QPointer<QLabel>    m_ddrTransferModeValue;
    QPointer<QComboBox> m_ddrTransferTapCombo;

    QPointer<QGroupBox>      m_ddrGroup;
    QPointer<QTableWidget>   m_ddrInputsTable;
    QPointer<QTableWidget>   m_ddrOutputsTable;
    QPointer<QWidget>        m_ddrTapWidget;
    QPointer<QComboBox>      m_ddrTapSourceCombo;   // "Custom" or "Symbol Table"
    QPointer<QComboBox>      m_ddrTapSymbolCombo;   // TAP symbols (visible when source=Symbol)
    QPointer<QWidget>        m_ddrTapCustomWidget;  // custom inline fields (visible when source=Custom)
    QPointer<QComboBox>      m_ddrTapModeCombo;
    QPointer<QLineEdit>      m_ddrTapTileDimsEdit;
    QPointer<QLineEdit>      m_ddrTapTileCountsEdit;
    QPointer<QLineEdit>      m_ddrTapRepeatEdit;

    QPointer<QGroupBox> m_ddrPivotWireGroup;
    QPointer<QLineEdit> m_ddrPivotParamEdit;

    QPointer<QGroupBox>      m_armWireGroup;
    QPointer<QLineEdit>      m_armFifoEdit;   // typed FIFO name for arm wire

    QPointer<QGroupBox>       m_coreFnGroup;
    QPointer<QComboBox>       m_coreFnModeCombo;
    QPointer<BodyStmtsEditor> m_coreFnEditor;
    QPointer<QPushButton>     m_coreFnClearBtn;

    Canvas::ObjectId m_effectiveFifoWireId{};
    bool m_tileIsKernelTile = false; // set in refreshSelection(), used by showSelectionState()
    Canvas::ObjectId m_armWireId{};      // currently-selected arm wire
    Canvas::ObjectId m_ddrPivotWireId{}; // currently-selected DDR pivot wire
    Canvas::ObjectId m_ddrTapWireId{};   // wire whose TAP is being edited
    bool m_updatingUi = false;
    bool m_updatingObjectFifoTable = false;
};

} // namespace Aie::Internal
