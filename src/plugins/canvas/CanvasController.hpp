// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasInteractionTypes.hpp"
#include "canvas/CanvasTypes.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasWire.hpp"

#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QPoint>
#include <QtCore/QSet>
#include <memory>

namespace Canvas {

class CanvasDocument;
class CanvasView;
class CanvasBlock;
class CanvasSelectionModel;
struct PortRef;

namespace Controllers {
class CanvasSelectionController;
class CanvasLinkingController;
class CanvasDragController;
class CanvasContextMenuController;
}

class CANVAS_EXPORT CanvasController final : public QObject
{
	Q_OBJECT

public:
	enum class Mode { Normal, Panning, Linking };
	Q_ENUM(Mode)

	enum class LinkingMode { Normal, Split, Join, Broadcast, Fifo, ForwardFifo };
	Q_ENUM(LinkingMode)

	CanvasController(CanvasDocument* doc, CanvasView* view, CanvasSelectionModel* selection, QObject* parent = nullptr);
	~CanvasController() override;

	Mode mode() const noexcept { return m_mode; }
	LinkingMode linkingMode() const noexcept;
	bool isLinkingInProgress() const noexcept;
	bool isEndpointDragActive() const noexcept;
    bool isBoundProducerPlacementActive() const noexcept;
	ObjectId linkStartItem() const noexcept;
	PortId linkStartPort() const noexcept;
	QPointF linkPreviewScene() const noexcept;
	const QSet<ObjectId>& selectedItems() const noexcept;

public slots:
	void onCanvasMousePressed(const QPointF& scenePos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
	void onCanvasMouseMoved(const QPointF& scenePos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
	void onCanvasMouseReleased(const QPointF& scenePos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
	void onCanvasContextMenuRequested(const QPointF& scenePos, const QPoint& globalPos, Qt::KeyboardModifiers mods);
	void onCanvasWheel(const QPointF& scenePos, const QPoint& angleDelta, const QPoint& pixelDelta, Qt::KeyboardModifiers mods);
	void onCanvasKeyPressed(int key, Qt::KeyboardModifiers mods);
	void setMode(Mode mode);
	void setLinkingMode(LinkingMode mode);

signals:
	void modeChanged(CanvasController::Mode mode);
	void linkingModeChanged(CanvasController::LinkingMode mode);
    void boundProducerPlacementChanged(bool active);

private:
	QPointF sceneToView(const QPointF& scenePos) const;
	void beginPanning(const QPointF& viewPos);
	void updatePanning(const QPointF& viewPos);
	void endPanning();
    void updateHoveredWire(const QPointF& scenePos);
    void clearHoveredWire();

	void clearTransientDragState();
    void syncBoundProducerPlacementUiState();

	CanvasDocument* m_doc = nullptr;
	CanvasView* m_view = nullptr;
	std::unique_ptr<Controllers::CanvasSelectionController> m_selectionController;
	std::unique_ptr<Controllers::CanvasLinkingController> m_linkingController;
	std::unique_ptr<Controllers::CanvasDragController> m_dragController;
	std::unique_ptr<Controllers::CanvasContextMenuController> m_contextMenuController;

	bool    m_panning = false;
	QPointF m_lastViewPos;
	QPointF m_panStart;
	Mode m_mode = Mode::Normal;
	Mode m_modeBeforePan = Mode::Normal;
    bool m_boundProducerPlacementActive = false;
};

} // namespace Canvas
