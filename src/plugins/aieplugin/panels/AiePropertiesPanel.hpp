// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
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
class CanvasWire;
namespace Api {
class ICanvasHost;
}
}

namespace Aie::Internal {

class AieService;

class AiePropertiesPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit AiePropertiesPanel(AieService* service, QWidget* parent = nullptr);

private:
    enum class SelectionKind : uint8_t {
        None,
        Tile,
        FifoWire,
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

    QPointer<AieService> m_service;
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

    QPointer<QGroupBox> m_fifoGroup;
    QPointer<QLabel> m_fifoWireIdValue;
    QPointer<QLineEdit> m_fifoNameEdit;
    QPointer<QSpinBox> m_fifoDepthSpin;
    QPointer<QComboBox> m_fifoTypeCombo;
    QPointer<QLineEdit> m_fifoDimensionsEdit;

    bool m_updatingUi = false;
};

} // namespace Aie::Internal
