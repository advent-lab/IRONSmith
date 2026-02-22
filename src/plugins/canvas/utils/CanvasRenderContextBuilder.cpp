// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/utils/CanvasRenderContextBuilder.hpp"

#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"

#include <algorithm>

namespace Canvas::Support {

CanvasRenderContext buildRenderContext(const CanvasDocument* doc,
                                       const QRectF& visibleSceneRect,
                                       double zoom,
                                       const RenderContextSelection& selection,
                                       const RenderContextPortState& ports,
                                       const RenderContextAnnotationState& annotations)
{
    CanvasRenderContext ctx;
    ctx.zoom = zoom;
    ctx.visibleSceneRect = visibleSceneRect;
    ctx.isSelected = selection.isSelected;
    ctx.isSelectedUser = selection.user;
    ctx.hasHoveredItem = selection.hasHoveredItem;
    ctx.hoveredItem = selection.hoveredItem;

    if (doc) {
        ctx.computePortTerminal = &CanvasDocument::computePortTerminalThunk;
        ctx.computePortTerminalUser = const_cast<CanvasDocument*>(doc);
        ctx.resolveObjectFifoNameForEndpoint = &CanvasDocument::resolveObjectFifoNameForEndpointThunk;
        ctx.resolveObjectFifoNameForEndpointUser = const_cast<CanvasDocument*>(doc);
        ctx.resolveConsumerHandleLabelForEndpoint = &CanvasDocument::resolveConsumerHandleLabelForEndpointThunk;
        ctx.resolveConsumerHandleLabelForEndpointUser = const_cast<CanvasDocument*>(doc);
        ctx.isFabricBlocked = &CanvasDocument::isFabricPointBlockedThunk;
        ctx.isFabricBlockedUser = const_cast<CanvasDocument*>(doc);
        ctx.fabricStep = doc->fabric().config().step;
    } else {
        ctx.fabricStep = 0.0;
    }

    ctx.hasHoveredPort = ports.hasHoveredPort;
    ctx.hoveredPortItem = ports.hoveredPortItem;
    ctx.hoveredPortId = ports.hoveredPortId;

    ctx.hasSelectedPort = ports.hasSelectedPort;
    ctx.selectedPortItem = ports.selectedPortItem;
    ctx.selectedPortId = ports.selectedPortId;
    ctx.isPortSelected = ports.isPortSelected;
    ctx.isPortSelectedUser = ports.isPortSelectedUser;

    ctx.wireAnnotationVisibilityMode = annotations.wireAnnotationVisibilityMode;
    ctx.wireAnnotationDetailMode = annotations.wireAnnotationDetailMode;
    ctx.wireAnnotationsScaleWithZoom = annotations.wireAnnotationsScaleWithZoom;
    ctx.showAllWireAnnotations = (annotations.wireAnnotationVisibilityMode == WireAnnotationVisibilityMode::ShowAll);

    return ctx;
}

QRectF computeVisibleSceneRect(const CanvasView& view)
{
    const QPointF tl = view.viewToScene(QPointF(0.0, 0.0));
    const QPointF br = view.viewToScene(QPointF(view.width(), view.height()));
    const double left   = std::min(tl.x(), br.x());
    const double right  = std::max(tl.x(), br.x());
    const double top    = std::min(tl.y(), br.y());
    const double bottom = std::max(tl.y(), br.y());
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

} // namespace Canvas::Support
