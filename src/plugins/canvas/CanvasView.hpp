// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasRenderContext.hpp"
#include "canvas/CanvasTypes.hpp"
#include "canvas/CanvasPorts.hpp"

#include <QtCore/QPointF>
#include <QtCore/QPoint>
#include <QtCore/QRectF>
#include <QtCore/QSet>
#include <QtCore/QString>

#include <QtWidgets/QWidget>
#include <qnamespace.h>

QT_BEGIN_NAMESPACE
class QPainter;
class QContextMenuEvent;
QT_END_NAMESPACE

namespace Canvas {

class CanvasDocument;
class CanvasController;
class CanvasSelectionModel;
class CanvasScene;
class CanvasViewport;

class CANVAS_EXPORT CanvasView final : public QWidget
{
	Q_OBJECT

	Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
	Q_PROPERTY(QPointF pan READ pan WRITE setPan NOTIFY panChanged)

public:
	explicit CanvasView(QWidget* parent = nullptr);

	void setDocument(CanvasDocument* doc);
	void setController(CanvasController* controller);
	void setSelectionModel(CanvasSelectionModel* model);

	double zoom() const noexcept;
	double displayZoom() const noexcept;
	double displayZoomBaseline() const noexcept;
	void setDisplayZoomBaseline(double baseline);
	void setZoom(double zoom);

	QPointF pan() const noexcept;
	void setPan(const QPointF& pan);

	QPointF viewToScene(const QPointF& viewPos) const;
	QPointF sceneToView(const QPointF& scenePos) const;

	CanvasViewport* viewport() const noexcept { return m_viewport; }

	ObjectId selectedItem() const noexcept;
	const QSet<ObjectId>& selectedItems() const noexcept;
	bool isSelected(ObjectId id) const noexcept;
	void setSelectedItem(ObjectId id);
	void setSelectedItems(const QSet<ObjectId>& items);
	void clearSelectedItems();
	void setSelectedPort(ObjectId itemId, PortId portId);
	void clearSelectedPort();

	void setHoveredPort(ObjectId itemId, PortId portId);
	void clearHoveredPort();
    void setHoveredWire(ObjectId itemId);
    void clearHoveredWire();
    bool hasHoveredWire() const noexcept;
    ObjectId hoveredWire() const noexcept;
    void setHoveredStereotype(ObjectId itemId);
    void clearHoveredStereotype();
	void setHoveredEdge(ObjectId itemId, PortSide side, const QPointF& anchorScene);
	void clearHoveredEdge();
	void setMarqueeRect(const QRectF& sceneRect);
	void clearMarqueeRect();
    void setWireAnnotationVisibilityMode(WireAnnotationVisibilityMode mode);
    WireAnnotationVisibilityMode wireAnnotationVisibilityMode() const noexcept;
    void setWireAnnotationDetailMode(WireAnnotationDetailMode mode);
    WireAnnotationDetailMode wireAnnotationDetailMode() const noexcept;
    void setWireAnnotationsScaleWithZoom(bool enabled);
    bool wireAnnotationsScaleWithZoom() const noexcept;
    void setShowAllWireAnnotations(bool enabled);
    bool showAllWireAnnotations() const noexcept;

    void setEmptyStateVisible(bool visible);
    bool emptyStateVisible() const noexcept { return m_emptyStateVisible; }
    void setEmptyStateText(QString title, QString message);

signals:
	void zoomChanged(double zoom);
	void panChanged(QPointF pan);
	void selectedItemChanged(Canvas::ObjectId id);
	void selectedItemsChanged();
	void hoveredPortChanged(Canvas::ObjectId itemId, Canvas::PortId portId);
	void hoveredPortCleared();
	void canvasMousePressed(const QPointF& scenePos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
	void canvasMouseMoved(const QPointF& scenePos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
	void canvasMouseReleased(const QPointF& scenePos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
    void canvasContextMenuRequested(const QPointF& scenePos, const QPoint& globalPos, Qt::KeyboardModifiers mods);
	void canvasWheel(const QPointF& scenePos, const QPoint& angleDelta, const QPoint& pixelDelta, Qt::KeyboardModifiers mods);
	void canvasKeyPressed(int key, Qt::KeyboardModifiers mods);

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
    void drawEmptyState(QPainter& painter);

	CanvasScene* m_scene = nullptr;
	CanvasViewport* m_viewport = nullptr;
    bool m_emptyStateVisible = false;
    QString m_emptyTitle;
    QString m_emptyMessage;
};

} // namespace Canvas
