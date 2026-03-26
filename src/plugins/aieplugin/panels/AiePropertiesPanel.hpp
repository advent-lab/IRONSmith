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
QT_END_NAMESPACE

namespace Utils {
class SidebarPanelFrame;
}

namespace Canvas {
class CanvasBlock;
class CanvasDocument;
class CanvasView;
namespace Api {
class ICanvasHost;
}
}

namespace Aie::Internal {

class AieService;
class SymbolsController;

class AiePropertiesPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit AiePropertiesPanel(AieService* service, QWidget* parent = nullptr);

    void setSymbolsController(SymbolsController* controller);

private:
    enum class SelectionKind : uint8_t {
        None,
        Tile,
        FifoWire,
        HubPivotWire,
        DdrBlock,
        Unsupported
    };

    void buildUi();
    void bindCanvasSignalsIfNeeded();
    void refreshSelection();
    void showSelectionState(SelectionKind kind, const QString& summary, const QString& detail = QString());

    Canvas::CanvasBlock* selectedBlock() const;
    Canvas::CanvasWire* selectedFifoWire() const;

    void applyTileLabel();
    void applyTileStereotype();
    void applyFifoProperties();
    void applyHubPivotProperties();
    void rebuildDdrGroup(Canvas::CanvasBlock* ddrBlock);
    void applyDdrEntry(Canvas::ObjectId fifoWireId, Canvas::ObjectId ddrWireId,
                       const QString& name, const QString& dims, const QString& type,
                       bool isMatrix = false,
                       const Canvas::CanvasWire::TensorTilerConfig& tap = {},
                       const QString& symbolRef = {});

    void populateFifoSymbolCombo();

    QPointer<AieService> m_service;
    QPointer<SymbolsController> m_symbolsController;
    QPointer<Canvas::Api::ICanvasHost> m_canvasHost;
    QPointer<Canvas::CanvasDocument> m_document;
    QPointer<Canvas::CanvasView> m_canvasView;

    QPointer<Utils::SidebarPanelFrame> m_frame;
    QPointer<QLabel> m_summaryLabel;
    QPointer<QLabel> m_detailLabel;

    QPointer<QGroupBox> m_tileGroup;
    QPointer<QLabel> m_tileIdValue;
    QPointer<QLabel> m_tileSpecIdValue;
    QPointer<QLabel> m_tileBoundsValue;
    QPointer<QLineEdit> m_tileLabelEdit;
    QPointer<QLineEdit> m_tileStereotypeEdit;
    QPointer<QPushButton> m_tileStereotypeClearBtn;
    QPointer<QWidget> m_tileKernelRow;
    QPointer<QLabel> m_tileKernelRowLabel;

    QPointer<QGroupBox> m_fifoGroup;
    QPointer<QLabel> m_fifoWireIdValue;
    QPointer<QLineEdit> m_fifoNameEdit;
    QPointer<QSpinBox> m_fifoDepthSpin;
    QPointer<QComboBox> m_fifoSymbolCombo;
    QPointer<QComboBox> m_fifoTypeCombo;
    QPointer<QLineEdit> m_fifoDimensionsEdit;

    QPointer<QGroupBox> m_hubPivotGroup;
    QPointer<QLineEdit> m_hubPivotNameEdit;
    QPointer<QLabel>    m_hubPivotFifoLabel;
    QPointer<QLineEdit> m_hubPivotFifoEdit;
    QPointer<QLabel>    m_hubBranchesValue;
    QPointer<QLabel>    m_hubOffsetsValue;
    QPointer<QLabel>    m_hubDepthValue;
    QPointer<QLabel>    m_hubValueTypeValue;
    QPointer<QLabel>    m_hubDimensionsValue;

    QPointer<QGroupBox> m_ddrGroup;
    QPointer<QWidget>   m_ddrContent;

    bool m_updatingUi = false;
};

} // namespace Aie::Internal
