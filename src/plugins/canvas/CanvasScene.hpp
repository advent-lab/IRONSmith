// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasRenderContext.hpp"
#include "canvas/CanvasTypes.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasController.hpp"
#include "canvas/CanvasSelectionModel.hpp"

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSet>
#include <QtCore/QSizeF>

#include <QtCore/QObject>
#include <qnamespace.h>

#include <vector>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace Canvas {

class CANVAS_EXPORT CanvasScene final : public QObject
{
    Q_OBJECT

public:
    struct ViewState final {
        QSizeF size;
        QPointF pan;
        double zoom = 1.0;
    };

    explicit CanvasScene(QObject* parent = nullptr);

    void setDocument(CanvasDocument* doc);
    void setController(CanvasController* controller);
    void setSelectionModel(CanvasSelectionModel* model);

    CanvasDocument* document() const noexcept { return m_document; }
    CanvasController* controller() const noexcept { return m_controller; }
    CanvasSelectionModel* selectionModel() const noexcept { return m_selectionModel; }

    ObjectId selectedItem() const noexcept;
    const QSet<ObjectId>& selectedItems() const noexcept;
    bool isSelected(ObjectId id) const noexcept;
    bool isPortSelected(ObjectId itemId, PortId portId) const noexcept;
    // True if `id` names a currently-selected link-hub block.
    bool isHubHighlighted(ObjectId id) const noexcept;
    void setSelectedItem(ObjectId id);
    void setSelectedItems(const QSet<ObjectId>& items);
    void clearSelectedItems();
    void setSelectedPort(ObjectId itemId, PortId portId);
    void clearSelectedPort();

    // Bumps the wire-geometry cache generation without going through
    // CanvasDocument::changed() — for live/transient position updates (block drag,
    // endpoint drag) that intentionally skip the heavier changed() listeners (e.g. HLIR
    // resync) until the drag commits, but still need wires to visually track the item
    // every frame during the drag itself.
    void invalidateWireGeometryCache() noexcept { ++m_wireGeometryGeneration; }

    void setHoveredPort(ObjectId itemId, PortId portId);
    void clearHoveredPort();
    void setHoveredWire(ObjectId itemId);
    void clearHoveredWire();
    bool hasHoveredWire() const noexcept { return m_hasHoveredWire; }
    ObjectId hoveredWire() const noexcept { return m_hoveredWireItem; }
    void setHoveredStereotype(ObjectId itemId);
    void clearHoveredStereotype();
    void setHoveredEdge(ObjectId itemId, PortSide side, const QPointF& anchorScene);
    void clearHoveredEdge();
    void setMarqueeRect(const QRectF& sceneRect);
    void clearMarqueeRect();
    void setWireAnnotationVisibilityMode(WireAnnotationVisibilityMode mode);
    WireAnnotationVisibilityMode wireAnnotationVisibilityMode() const noexcept { return m_wireAnnotationVisibilityMode; }
    void setWireAnnotationDetailMode(WireAnnotationDetailMode mode);
    WireAnnotationDetailMode wireAnnotationDetailMode() const noexcept { return m_wireAnnotationDetailMode; }
    void setWireAnnotationsScaleWithZoom(bool enabled);
    bool wireAnnotationsScaleWithZoom() const noexcept { return m_wireAnnotationsScaleWithZoom; }
    void setShowAllWireAnnotations(bool enabled);
    bool showAllWireAnnotations() const noexcept { return m_wireAnnotationVisibilityMode == WireAnnotationVisibilityMode::ShowAll; }

    void paint(QPainter& p, const ViewState& view) const;
    QRectF sceneRect(const ViewState& view) const;

signals:
    void requestUpdate();
    void selectedItemChanged(Canvas::ObjectId id);
    void selectedItemsChanged();
    void hoveredPortChanged(Canvas::ObjectId itemId, Canvas::PortId portId);
    void hoveredPortCleared();

private:
    void drawBackgroundLayer(QPainter& p) const;
    void applyViewTransform(QPainter& p, const ViewState& view) const;
    void drawGridFabric(QPainter& p, const QRectF& visibleScene) const;
    void drawContentLayer(QPainter& p, const QRectF& visibleScene, double zoom) const;
    void drawOverlayLayer(QPainter& p, const QRectF& visibleScene, double zoom) const;
    CanvasRenderContext buildRenderContext(const QRectF& sceneRect, bool includeHover, double zoom) const;

    QPointer<CanvasDocument> m_document;
    QPointer<CanvasController> m_controller;
    QPointer<CanvasSelectionModel> m_selectionModel;

    bool m_hasHoveredPort = false;
    ObjectId m_hoveredItem{};
    PortId m_hoveredPort{};
    bool m_hasHoveredWire = false;
    ObjectId m_hoveredWireItem{};
    bool m_hasHoveredStereotype = false;
    ObjectId m_hoveredStereotypeItem{};
    bool m_hasHoveredEdge = false;
    ObjectId m_hoveredEdgeItem{};
    PortSide m_hoveredEdgeSide = PortSide::Left;
    QPointF m_hoveredEdgeAnchor{};
    bool m_hasMarquee = false;
    QRectF m_marqueeSceneRect;
    WireAnnotationVisibilityMode m_wireAnnotationVisibilityMode = WireAnnotationVisibilityMode::Auto;
    WireAnnotationDetailMode m_wireAnnotationDetailMode = WireAnnotationDetailMode::Adaptive;
    bool m_wireAnnotationsScaleWithZoom = true;

    // Cross-repaint cache for the wire-overlap-avoidance pre-pass (see
    // Support::buildRenderContext). Repaints happen far more often than the document
    // actually changes (e.g. every hover tick), and the pre-pass reroutes every wire in
    // the document, so it's only worth rerunning when CanvasDocument::changed() fires.
    quint64 m_wireGeometryGeneration = 0;
    mutable quint64 m_cachedWireGeometryGeneration = ~quint64{0};
    mutable QHash<ObjectId, std::vector<QPointF>> m_cachedResolvedWirePaths;
};

} // namespace Canvas
