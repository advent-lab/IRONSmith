// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/utils/CanvasRenderContextBuilder.hpp"

#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/internal/CanvasWireRouting.hpp"
#include "canvas/utils/CanvasGeometry.hpp"

#include <algorithm>

namespace Canvas::Support {

CanvasRenderContext buildRenderContext(const CanvasDocument* doc,
                                       const QRectF& visibleSceneRect,
                                       double zoom,
                                       const RenderContextSelection& selection,
                                       const RenderContextPortState& ports,
                                       const RenderContextAnnotationState& annotations,
                                       bool computeWirePaths)
{
    CanvasRenderContext ctx;
    ctx.zoom = zoom;
    ctx.visibleSceneRect = visibleSceneRect;
    ctx.isSelected = selection.isSelected;
    ctx.isSelectedUser = selection.user;
    ctx.hasHoveredItem = selection.hasHoveredItem;
    ctx.hoveredItem = selection.hoveredItem;
    ctx.isHubHighlighted = selection.isHubHighlighted;
    ctx.isHubHighlightedUser = selection.user;

    if (doc) {
        ctx.computePortTerminal = &CanvasDocument::computePortTerminalThunk;
        ctx.computePortTerminalUser = const_cast<CanvasDocument*>(doc);
        ctx.resolveObjectFifoNameForEndpoint = &CanvasDocument::resolveObjectFifoNameForEndpointThunk;
        ctx.resolveObjectFifoNameForEndpointUser = const_cast<CanvasDocument*>(doc);
        ctx.resolveConsumerHandleLabelForEndpoint = &CanvasDocument::resolveConsumerHandleLabelForEndpointThunk;
        ctx.resolveConsumerHandleLabelForEndpointUser = const_cast<CanvasDocument*>(doc);
        ctx.resolveHubArmLabelForEndpoint = &CanvasDocument::resolveHubArmLabelForEndpointThunk;
        ctx.resolveHubArmLabelForEndpointUser = const_cast<CanvasDocument*>(doc);
        ctx.resolveItemSpecId = &CanvasDocument::resolveItemSpecIdThunk;
        ctx.resolveItemSpecIdUser = const_cast<CanvasDocument*>(doc);
        ctx.resolveItemSymbol = &CanvasDocument::resolveItemSymbolThunk;
        ctx.resolveItemSymbolUser = const_cast<CanvasDocument*>(doc);
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

    // Collect FIFO names that are Join-hub pivot targets so that BCAST pivot
    // annotation can suppress the hub name when its source was already joined.
    if (doc) {
        for (const auto& item : doc->items()) {
            const auto* wire = dynamic_cast<const CanvasWire*>(item.get());
            if (!wire || !wire->hasObjectFifo()) continue;
            const auto& cfg = wire->objectFifo().value();
            if (cfg.operation == CanvasWire::ObjectFifoOperation::Join
                    && !cfg.hubName.trimmed().isEmpty()
                    && !cfg.name.trimmed().isEmpty())
                ctx.joinTargetFifoNames.insert(cfg.name.trimmed());
        }
    }

    // Sequential, document-order pre-pass: route every wire once, each wire penalized
    // (not hard-blocked) by edges wires earlier in this same pass already claimed, so
    // overlapping wires prefer a free parallel lane when one exists and only actually
    // overlap when tiles leave no room. Hub-trunk arm wires (the auto routeOverride set
    // by the shared-trunk feature) are exempted from *receiving* the penalty — their
    // overlap with siblings on the trunk is intentional — but still contribute their
    // claimed edges so unrelated wires steer around a hub's trunk when possible.
    if (computeWirePaths && doc && ctx.fabricStep > 0.0) {
        Internal::EdgeOccupancy occupancy;

        // Bounds derived only from each wire's own endpoints, not the live viewport —
        // otherwise the occupancy outcome (and everything routed after a given wire in
        // this pass) would shift as the user scrolls or zooms.
        CanvasRenderContext occCtx = ctx;
        occCtx.visibleSceneRect = QRectF();

        for (const auto& item : doc->items()) {
            const auto* wire = dynamic_cast<const CanvasWire*>(item.get());
            if (!wire)
                continue;

            occCtx.wireEdgeOccupancy = wire->hasRouteOverride() ? nullptr : &occupancy;
            const std::vector<QPointF> path = wire->resolvedPathScene(occCtx);
            ctx.resolvedWirePaths.insert(wire->id(), path);

            std::vector<FabricCoord> coords;
            coords.reserve(path.size());
            for (const auto& pt : path)
                coords.push_back(toFabricCoord(pt, ctx.fabricStep));
            Internal::addPathEdges(occupancy, coords);
        }
    }

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
