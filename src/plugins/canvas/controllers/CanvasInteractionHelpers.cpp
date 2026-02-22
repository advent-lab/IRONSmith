// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/controllers/CanvasInteractionHelpers.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasGeometry.hpp"
#include "canvas/utils/CanvasPortUsage.hpp"
#include "canvas/utils/CanvasRenderContextBuilder.hpp"

#include <QtCore/QLineF>

#include <algorithm>
#include <cmath>

namespace Canvas::Controllers::Detail {

CanvasRenderContext buildRenderContext(const CanvasDocument* doc,
                                       const CanvasView* view)
{
    if (!doc)
        return {};

    const double zoom = view ? view->zoom() : 1.0;
    const QRectF visible = view ? Support::computeVisibleSceneRect(*view) : QRectF();

    Support::RenderContextSelection selection;
    if (view && view->hasHoveredWire()) {
        selection.hasHoveredItem = true;
        selection.hoveredItem = view->hoveredWire();
    }

    Support::RenderContextAnnotationState annotations;
    if (view) {
        annotations.wireAnnotationVisibilityMode = view->wireAnnotationVisibilityMode();
        annotations.wireAnnotationDetailMode = view->wireAnnotationDetailMode();
        annotations.wireAnnotationsScaleWithZoom = view->wireAnnotationsScaleWithZoom();
    }

    return Support::buildRenderContext(doc, visible, zoom, selection, Support::RenderContextPortState{}, annotations);
}

std::optional<WireEndpointHit> pickWireEndpoint(CanvasWire* wire,
                                                const CanvasRenderContext& ctx,
                                                const QPointF& scenePos,
                                                double tol)
{
    if (!wire)
        return std::nullopt;

    auto resolveAnchor = [&](const CanvasWire::Endpoint& e) -> QPointF {
        if (e.attached.has_value()) {
            const auto ref = *e.attached;
            QPointF anchor, border, fabric;
            if (ctx.portTerminal(ref.itemId, ref.portId, anchor, border, fabric))
                return anchor;
        }
        return e.freeScene;
    };

    const QPointF aAnchor = resolveAnchor(wire->a());
    const QPointF bAnchor = resolveAnchor(wire->b());

    const double distA = QLineF(scenePos, aAnchor).length();
    const double distB = QLineF(scenePos, bAnchor).length();

    if (distA > tol && distB > tol)
        return std::nullopt;

    WireEndpointHit hit;
    if (distA <= distB) {
        hit.isA = true;
        hit.endpoint = wire->a();
    } else {
        hit.isA = false;
        hit.endpoint = wire->b();
    }
    return hit;
}

std::optional<EndpointCandidate> pickEndpointCandidate(CanvasDocument* doc,
                                                       const CanvasView* view,
                                                       const QPointF& scenePos,
                                                       double tol)
{
    if (!doc || !view)
        return std::nullopt;

    const CanvasRenderContext ctx = buildRenderContext(doc, view);
    for (auto it = doc->items().rbegin(); it != doc->items().rend(); ++it) {
        if (!*it)
            continue;
        auto* wire = dynamic_cast<CanvasWire*>(it->get());
        if (!wire)
            continue;
        if (auto hit = pickWireEndpoint(wire, ctx, scenePos, tol))
            return EndpointCandidate{wire, *hit};
    }
    return std::nullopt;
}

std::optional<EdgeCandidate> edgeCandidateAt(CanvasDocument* doc,
                                             CanvasView* view,
                                             const QPointF& scenePos)
{
    if (!doc)
        return std::nullopt;

    const double zoom = view ? view->zoom() : 1.0;
    const double threshold = Canvas::Constants::kPortActivationBandPx / std::max(zoom, 0.25);
    const double step = doc->fabric().config().step;

    for (auto it = doc->items().rbegin(); it != doc->items().rend(); ++it) {
        auto* block = dynamic_cast<CanvasBlock*>(it->get());
        if (!block)
            continue;

        const QRectF expanded = block->boundsScene().adjusted(-threshold, -threshold, threshold, threshold);
        if (!expanded.contains(scenePos))
            continue;

        const auto hit = Support::edgeHitForRect(block->boundsScene(), scenePos, threshold, step);
        if (!hit)
            continue;

        EdgeCandidate out;
        out.itemId = block->id();
        out.side = hit->side;
        out.t = hit->t;
        out.anchorScene = hit->anchorScene;
        return out;
    }

    return std::nullopt;
}

std::optional<PortRef> ensureEdgePort(CanvasDocument* doc,
                                      const EdgeCandidate& candidate)
{
    if (!doc)
        return std::nullopt;

    auto* block = dynamic_cast<CanvasBlock*>(doc->findItem(candidate.itemId));
    if (!block)
        return std::nullopt;

    const double tol = 0.05;
    const double baseT = Support::clampT(candidate.t);

    auto isPortNear = [&](const CanvasPort& port, double t) {
        return port.side == candidate.side && std::abs(port.t - t) <= tol;
    };

    for (const auto& port : block->ports()) {
        if (!isPortNear(port, baseT))
            continue;
        if (Support::isPortAvailable(*doc, block->id(), port.id))
            return PortRef{block->id(), port.id};
        if (!block->allowMultiplePorts())
            return PortRef{block->id(), port.id};
        break;
    }

    double chosenT = baseT;
    if (block->allowMultiplePorts()) {
        const double step = 0.08;
        const double minT = 0.05;
        const double maxT = 0.95;

        auto isFreeT = [&](double t) {
            for (const auto& port : block->ports()) {
                if (isPortNear(port, t))
                    return false;
            }
            return true;
        };

        if (!isFreeT(chosenT)) {
            bool found = false;
            for (int i = 1; i <= 8 && !found; ++i) {
                const double offset = step * i;
                const double tPlus = chosenT + offset;
                if (tPlus <= maxT && isFreeT(tPlus)) {
                    chosenT = tPlus;
                    found = true;
                    break;
                }
                const double tMinus = chosenT - offset;
                if (tMinus >= minT && isFreeT(tMinus)) {
                    chosenT = tMinus;
                    found = true;
                    break;
                }
            }
        }
    }

    const Canvas::PortRole role = block->hasAutoPortRole()
        ? block->autoPortRole()
        : Canvas::PortRole::Dynamic;
    const Canvas::PortId portId = block->addPort(candidate.side, chosenT, role);
    if (portId.isNull())
        return std::nullopt;

    doc->notifyChanged();
    return PortRef{block->id(), portId};
}

bool findPortIndex(const CanvasBlock& block, PortId portId, size_t& outIndex)
{
    const auto& ports = block.ports();
    for (size_t i = 0; i < ports.size(); ++i) {
        if (ports[i].id == portId) {
            outIndex = i;
            return true;
        }
    }
    return false;
}

int pickWireSegment(const std::vector<QPointF>& path,
                    const QPointF& scenePos,
                    double tol,
                    bool& outHorizontal)
{
    if (path.size() < 2)
        return -1;

    int bestIdx = -1;
    double bestDist = tol;
    for (size_t i = 1; i < path.size(); ++i) {
        const QPointF a = path[i - 1];
        const QPointF b = path[i];
        const QPointF ab = b - a;
        const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
        if (len2 <= 1e-6)
            continue;

        const QPointF ap = scenePos - a;
        double t = (ap.x() * ab.x() + ap.y() * ab.y()) / len2;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        const QPointF proj(a.x() + t * ab.x(), a.y() + t * ab.y());
        const double d = QLineF(scenePos, proj).length();
        if (d <= bestDist) {
            bestDist = d;
            bestIdx = static_cast<int>(i - 1);
            outHorizontal = std::abs(ab.y()) <= std::abs(ab.x());
        }
    }
    return bestIdx;
}

static bool isSegmentBlocked(const CanvasDocument* doc,
                             bool horizontal,
                             int coord,
                             int spanMin,
                             int spanMax)
{
    if (!doc)
        return false;

    if (spanMin > spanMax)
        std::swap(spanMin, spanMax);

    for (int v = spanMin; v <= spanMax; ++v) {
        const int x = horizontal ? v : coord;
        const int y = horizontal ? coord : v;
        if (doc->isFabricPointBlocked(Canvas::FabricCoord{x, y}))
            return true;
    }
    return false;
}

int adjustSegmentCoord(const CanvasDocument* doc,
                       bool horizontal,
                       int desired,
                       int spanMin,
                       int spanMax)
{
    if (!doc)
        return desired;

    if (!isSegmentBlocked(doc, horizontal, desired, spanMin, spanMax))
        return desired;

    for (int dist = 1; dist < 64; ++dist) {
        if (!isSegmentBlocked(doc, horizontal, desired - dist, spanMin, spanMax))
            return desired - dist;
        if (!isSegmentBlocked(doc, horizontal, desired + dist, spanMin, spanMax))
            return desired + dist;
    }
    return desired;
}

} // namespace Canvas::Controllers::Detail
