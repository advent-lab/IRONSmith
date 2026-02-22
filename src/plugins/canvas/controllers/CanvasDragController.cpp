// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/controllers/CanvasDragController.hpp"

#include "canvas/controllers/CanvasInteractionHelpers.hpp"
#include "canvas/controllers/CanvasSelectionController.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasCommands.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasAutoPorts.hpp"
#include "canvas/utils/CanvasGeometry.hpp"
#include "canvas/utils/CanvasPortUsage.hpp"

#include <QtCore/QLineF>

#include <algorithm>
#include <cmath>

namespace Canvas::Controllers {

CanvasDragController::CanvasDragController(CanvasDocument* doc,
                                           CanvasView* view,
                                           CanvasSelectionController* selection)
    : m_doc(doc)
    , m_view(view)
    , m_selection(selection)
{}

void CanvasDragController::clearTransientState()
{
    m_dragWire = false;
    m_dragWireId = ObjectId{};
    m_dragWireSeg = -1;
    m_dragWirePath.clear();

    m_dragEndpoint = false;
    m_dragEndpointWireId = ObjectId{};
    m_dragEndpointIsA = false;
    m_dragEndpointOriginal = CanvasWire::Endpoint{};
    m_dragEndpointPortDynamic = false;
    m_dragEndpointPortShared = false;
    m_dragEndpointPortPaired = false;
    m_dragEndpointPort = PortRef{};
    m_dragEndpointPortMeta = CanvasPort{};
    m_dragEndpointPortIndex = 0;

    clearPendingEndpoint();

    m_dragBlocks.clear();
    m_dragPrimary = nullptr;
    m_dragPrimaryStartTopLeft = QPointF();
}

bool CanvasDragController::beginPendingEndpoint(const QPointF& scenePos, const QPointF& viewPos)
{
    if (!m_doc || !m_view)
        return false;

    const double tol = Constants::kEndpointHitRadiusPx / std::max(m_view->zoom(), 0.25);
    if (auto candidate = Detail::pickEndpointCandidate(m_doc, m_view, scenePos, tol)) {
        m_pendingEndpoint = true;
        m_pendingEndpointWireId = candidate->wire->id();
        m_pendingEndpointPort = candidate->hit.endpoint.attached;
        m_pendingEndpointPressScene = scenePos;
        m_pendingEndpointPressView = viewPos;
        return true;
    }
    return false;
}

bool CanvasDragController::updatePendingEndpoint(const QPointF& scenePos, Qt::MouseButtons buttons)
{
    if (!m_pendingEndpoint || !m_view || !buttons.testFlag(Qt::LeftButton))
        return false;

    const QPointF viewPos = m_view->sceneToView(scenePos);
    const double dist = QLineF(viewPos, m_pendingEndpointPressView).length();
    if (dist < Constants::kEndpointDragThresholdPx)
        return true;

    CanvasWire* wire = findWire(m_pendingEndpointWireId);
    if (wire && beginEndpointDrag(wire, m_pendingEndpointPressScene)) {
        clearPendingEndpoint();
        updateEndpointDrag(scenePos);
        return true;
    }

    return false;
}

void CanvasDragController::clearPendingEndpoint()
{
    m_pendingEndpoint = false;
    m_pendingEndpointWireId = ObjectId{};
    m_pendingEndpointPort.reset();
    m_pendingEndpointPressScene = QPointF();
    m_pendingEndpointPressView = QPointF();
}

CanvasWire* CanvasDragController::findWire(ObjectId wireId) const
{
    if (!m_doc)
        return nullptr;

    for (const auto& it : m_doc->items()) {
        if (it && it->id() == wireId)
            return dynamic_cast<CanvasWire*>(it.get());
    }
    return nullptr;
}

void CanvasDragController::beginWireSegmentDrag(CanvasWire* wire, const QPointF& scenePos)
{
    if (!m_view || !m_doc || !wire)
        return;

    const CanvasRenderContext ctx = Detail::buildRenderContext(m_doc, m_view);
    const std::vector<QPointF> path = wire->resolvedPathScene(ctx);

    bool horizontal = false;
    const double tol = 6.0 / m_view->zoom();
    const int seg = Detail::pickWireSegment(path, scenePos, tol, horizontal);
    if (seg < 0)
        return;

    m_dragWire = true;
    m_dragWireId = wire->id();
    m_dragWireSeg = seg;
    m_dragWireSegHorizontal = horizontal;

    const double axisCoord = horizontal ? path[seg].y() : path[seg].x();
    m_dragWireOffset = horizontal ? (scenePos.y() - axisCoord) : (scenePos.x() - axisCoord);
    m_dragWirePath = wire->resolvedPathCoords(ctx);

    m_dragBlocks.clear();
    m_dragPrimary = nullptr;
}

void CanvasDragController::updateWireSegmentDrag(const QPointF& scenePos)
{
    if (!m_dragWire || !m_doc || !m_view)
        return;

    const double step = m_doc->fabric().config().step;
    if (step <= 0.0 || m_dragWireSeg < 0 || m_dragWireSeg + 1 >= static_cast<int>(m_dragWirePath.size()))
        return;

    const double raw = (m_dragWireSegHorizontal ? scenePos.y() : scenePos.x()) - m_dragWireOffset;
    int newCoord = static_cast<int>(std::llround(raw / step));

    int spanMin = 0;
    int spanMax = 0;
    if (m_dragWireSegHorizontal) {
        spanMin = std::min(m_dragWirePath[m_dragWireSeg].x, m_dragWirePath[m_dragWireSeg + 1].x);
        spanMax = std::max(m_dragWirePath[m_dragWireSeg].x, m_dragWirePath[m_dragWireSeg + 1].x);
    } else {
        spanMin = std::min(m_dragWirePath[m_dragWireSeg].y, m_dragWirePath[m_dragWireSeg + 1].y);
        spanMax = std::max(m_dragWirePath[m_dragWireSeg].y, m_dragWirePath[m_dragWireSeg + 1].y);
    }

    newCoord = Detail::adjustSegmentCoord(m_doc, m_dragWireSegHorizontal, newCoord, spanMin, spanMax);

    auto next = m_dragWirePath;
    if (m_dragWireSegHorizontal) {
        next[m_dragWireSeg].y = newCoord;
        next[m_dragWireSeg + 1].y = newCoord;
    } else {
        next[m_dragWireSeg].x = newCoord;
        next[m_dragWireSeg + 1].x = newCoord;
    }

    for (auto& it : m_doc->items()) {
        if (it && it->id() == m_dragWireId) {
            if (auto* w = dynamic_cast<CanvasWire*>(it.get())) {
                w->setRouteOverride(std::move(next));
                m_dragWirePath = w->routeOverride();
                m_doc->notifyChanged();
                m_view->update();
            }
            break;
        }
    }
}

void CanvasDragController::endWireSegmentDrag()
{
    m_dragWire = false;
    m_dragWireId = ObjectId{};
    m_dragWireSeg = -1;
    m_dragWirePath.clear();
}

bool CanvasDragController::beginEndpointDrag(CanvasWire* wire, const QPointF& scenePos)
{
    if (!m_view || !m_doc || !wire)
        return false;

    const CanvasRenderContext ctx = Detail::buildRenderContext(m_doc, m_view);
    const double tol = Constants::kEndpointHitRadiusPx / std::max(m_view->zoom(), 0.25);
    const auto hit = Detail::pickWireEndpoint(wire, ctx, scenePos, tol);
    if (!hit)
        return false;

    m_dragEndpoint = true;
    m_dragEndpointWireId = wire->id();
    m_dragEndpointIsA = hit->isA;
    m_dragEndpointOriginal = hit->endpoint;
    m_dragEndpointPortDynamic = false;
    m_dragEndpointPortShared = false;
    m_dragEndpointPort = PortRef{};
    m_dragEndpointPortMeta = CanvasPort{};
    m_dragEndpointPortIndex = 0;

    if (hit->endpoint.attached.has_value()) {
        const PortRef ref = *hit->endpoint.attached;
        CanvasPort meta;
        if (m_doc->getPort(ref.itemId, ref.portId, meta)) {
            m_dragEndpointPort = ref;
            m_dragEndpointPortMeta = meta;
            m_dragEndpointPortDynamic = (meta.role == PortRole::Dynamic);
            m_dragEndpointPortShared = Support::countPortAttachments(*m_doc, ref.itemId, ref.portId, wire->id()) > 0;
            m_dragEndpointPortPaired = Support::isPairedProducerPort(meta);

            if (auto* block = dynamic_cast<CanvasBlock*>(m_doc->findItem(ref.itemId))) {
                Detail::findPortIndex(*block, ref.portId, m_dragEndpointPortIndex);
            }
        }
    }

    CanvasWire::Endpoint next = hit->endpoint;
    next.attached.reset();
    const double step = m_doc->fabric().config().step;
    next.freeScene = Support::snapPointToGrid(scenePos, step);

    if (hit->isA)
        wire->setEndpointA(next);
    else
        wire->setEndpointB(next);

    wire->clearRouteOverride();
    m_doc->notifyChanged();
    m_view->update();
    return true;
}

void CanvasDragController::updateEndpointDrag(const QPointF& scenePos)
{
    if (!m_dragEndpoint || !m_doc || !m_view)
        return;

    CanvasWire* wire = findWire(m_dragEndpointWireId);
    if (!wire)
        return;

    CanvasWire::Endpoint next = m_dragEndpointIsA ? wire->a() : wire->b();
    next.attached.reset();
    const double step = m_doc->fabric().config().step;
    next.freeScene = Support::snapPointToGrid(scenePos, step);

    if (m_dragEndpointIsA)
        wire->setEndpointA(next);
    else
        wire->setEndpointB(next);

    wire->clearRouteOverride();
    if (auto edge = Detail::edgeCandidateAt(m_doc, m_view, scenePos))
        m_view->setHoveredEdge(edge->itemId, edge->side, edge->anchorScene);
    else
        m_view->clearHoveredEdge();
    m_view->update();
}

void CanvasDragController::endEndpointDrag(const QPointF& scenePos)
{
    if (!m_dragEndpoint || !m_doc || !m_view)
        return;

    CanvasWire* wire = findWire(m_dragEndpointWireId);
    if (!wire)
        return;

    const double radiusScene = Constants::kPortHitRadiusPx / std::max(m_view->zoom(), 0.25);
    std::optional<PortRef> target = m_doc->hitTestPort(scenePos, radiusScene);
    std::optional<EdgeCandidate> edge = std::nullopt;
    if (!target)
        edge = Detail::edgeCandidateAt(m_doc, m_view, scenePos);

    bool attached = false;
    bool movedPort = false;
    std::optional<PortRef> attachedRef;

    if (target && !Support::isPortAvailable(*m_doc, target->itemId, target->portId, wire->id())) {
        target.reset();
    }
    if (target && m_dragEndpointPortPaired && m_dragEndpointPort.itemId &&
        target->itemId != m_dragEndpointPort.itemId) {
        target.reset();
    }

    if (target) {
        CanvasWire::Endpoint next;
        next.attached = *target;
        next.freeScene = scenePos;
        if (m_dragEndpointIsA)
            wire->setEndpointA(next);
        else
            wire->setEndpointB(next);
        attached = true;
        attachedRef = *target;
    } else if (edge.has_value()) {
        auto* targetBlock = dynamic_cast<CanvasBlock*>(m_doc->findItem(edge->itemId));
        if (targetBlock) {
            if (m_dragEndpointPortPaired && m_dragEndpointPort.itemId) {
                auto* sourceBlock = dynamic_cast<CanvasBlock*>(m_doc->findItem(m_dragEndpointPort.itemId));
                if (sourceBlock && sourceBlock->id() == targetBlock->id()) {
                    CanvasPort moved = m_dragEndpointPortMeta;
                    moved.side = edge->side;
                    moved.t = Support::clampT(edge->t);
                    sourceBlock->updatePort(m_dragEndpointPort.portId, moved.side, moved.t);
                    CanvasWire::Endpoint next;
                    next.attached = PortRef{sourceBlock->id(), m_dragEndpointPort.portId};
                    next.freeScene = scenePos;
                    if (m_dragEndpointIsA)
                        wire->setEndpointA(next);
                    else
                        wire->setEndpointB(next);
                    attached = true;
                    movedPort = true;
                    attachedRef = PortRef{sourceBlock->id(), m_dragEndpointPort.portId};
                }
            }

            if (m_dragEndpointPortDynamic && !m_dragEndpointPortShared && m_dragEndpointPort.itemId) {
                CanvasPort moved = m_dragEndpointPortMeta;
                moved.side = edge->side;
                moved.t = Support::clampT(edge->t);

                auto* sourceBlock = dynamic_cast<CanvasBlock*>(m_doc->findItem(m_dragEndpointPort.itemId));
                if (sourceBlock) {
                    const bool allowCrossBlock = !sourceBlock->autoOppositeProducerPort();
                    if (sourceBlock->id() == targetBlock->id()) {
                        sourceBlock->updatePort(m_dragEndpointPort.portId, moved.side, moved.t);
                        CanvasWire::Endpoint next;
                        next.attached = PortRef{sourceBlock->id(), m_dragEndpointPort.portId};
                        next.freeScene = scenePos;
                        if (m_dragEndpointIsA)
                            wire->setEndpointA(next);
                        else
                            wire->setEndpointB(next);
                        attached = true;
                        movedPort = true;
                        attachedRef = PortRef{sourceBlock->id(), m_dragEndpointPort.portId};
                    } else if (allowCrossBlock) {
                        const bool removed = sourceBlock->removePort(m_dragEndpointPort.portId).has_value();
                        if (removed) {
                            targetBlock->insertPort(targetBlock->ports().size(), moved);
                            CanvasWire::Endpoint next;
                            next.attached = PortRef{targetBlock->id(), m_dragEndpointPort.portId};
                            next.freeScene = scenePos;
                            if (m_dragEndpointIsA)
                                wire->setEndpointA(next);
                            else
                                wire->setEndpointB(next);
                            attached = true;
                            movedPort = true;
                            attachedRef = PortRef{targetBlock->id(), m_dragEndpointPort.portId};
                        }
                    }
                }
            }

            if (!attached) {
                const PortId newPort = targetBlock->addPort(edge->side, edge->t, PortRole::Dynamic);
                if (newPort) {
                    CanvasWire::Endpoint next;
                    next.attached = PortRef{targetBlock->id(), newPort};
                    next.freeScene = scenePos;
                    if (m_dragEndpointIsA)
                        wire->setEndpointA(next);
                    else
                        wire->setEndpointB(next);
                    attached = true;
                    attachedRef = PortRef{targetBlock->id(), newPort};
                }
            }
        }
    }

    if (!attached) {
        if (m_dragEndpointIsA)
            wire->setEndpointA(m_dragEndpointOriginal);
        else
            wire->setEndpointB(m_dragEndpointOriginal);
    } else if (!movedPort && m_dragEndpointPortDynamic && m_dragEndpointPort.itemId) {
        if (Support::countPortAttachments(*m_doc, m_dragEndpointPort.itemId, m_dragEndpointPort.portId, wire->id()) == 0) {
            if (auto* block = dynamic_cast<CanvasBlock*>(m_doc->findItem(m_dragEndpointPort.itemId))) {
                block->removePort(m_dragEndpointPort.portId);
            }
        }
    }

    wire->clearRouteOverride();
    m_doc->notifyChanged();
    m_view->clearHoveredEdge();

    m_dragEndpoint = false;
    m_dragEndpointWireId = ObjectId{};
    m_dragEndpointIsA = false;
    m_dragEndpointOriginal = CanvasWire::Endpoint{};
    m_dragEndpointPortDynamic = false;
    m_dragEndpointPortShared = false;
    m_dragEndpointPortPaired = false;
    m_dragEndpointPort = PortRef{};
    m_dragEndpointPortMeta = CanvasPort{};
    m_dragEndpointPortIndex = 0;
    m_view->update();
}

void CanvasDragController::beginBlockDrag(CanvasBlock* blk, const QPointF& scenePos)
{
    if (!blk)
        return;

    m_dragBlocks.clear();
    m_dragPrimary = blk;
    m_dragPrimaryStartTopLeft = blk->boundsScene().topLeft();
    m_dragOffset = scenePos - m_dragPrimaryStartTopLeft;

    const bool useSelection = m_selection && m_selection->isSelected(blk->id()) &&
                              m_selection->selectedItems().size() > 1;
    if (useSelection && m_doc && m_selection) {
        for (const auto& id : m_selection->selectedItems()) {
            auto* item = m_doc->findItem(id);
            auto* block = dynamic_cast<CanvasBlock*>(item);
            if (!block || !block->isMovable())
                continue;
            m_dragBlocks.push_back(DragBlockState{block, block->boundsScene().topLeft()});
        }
    }

    if (m_dragBlocks.empty() && blk->isMovable())
        m_dragBlocks.push_back(DragBlockState{blk, blk->boundsScene().topLeft()});
}

void CanvasDragController::updateBlockDrag(const QPointF& scenePos)
{
    if (!m_dragPrimary || m_dragBlocks.empty() || !m_view || !m_doc)
        return;

    const QPointF newTopLeft = scenePos - m_dragOffset;
    const double step = m_doc->fabric().config().step;

    const QPointF snappedPrimary = Support::snapPointToGrid(newTopLeft, step);
    const QPointF delta = snappedPrimary - m_dragPrimaryStartTopLeft;

    for (auto& state : m_dragBlocks) {
        if (!state.block)
            continue;
        QRectF newBounds = state.block->boundsScene();
        newBounds.moveTopLeft(state.startTopLeft + delta);
        state.block->setBoundsScene(newBounds);
    }

    m_view->update();
}

void CanvasDragController::endBlockDrag()
{
    if (!m_doc)
        return;

    auto batch = std::make_unique<CompositeCommand>(QStringLiteral("Move Blocks"));
    bool moved = false;
    for (const auto& state : m_dragBlocks) {
        if (!state.block)
            continue;
        const QPointF endTopLeft = state.block->boundsScene().topLeft();
        if (endTopLeft == state.startTopLeft)
            continue;
        moved = true;
        batch->add(std::make_unique<MoveItemCommand>(state.block->id(), state.startTopLeft, endTopLeft));
    }
    if (!batch->empty())
        m_doc->commands().execute(std::move(batch));
    if (moved)
        m_doc->notifyChanged();

    m_dragBlocks.clear();
    m_dragPrimary = nullptr;
}

} // namespace Canvas::Controllers
