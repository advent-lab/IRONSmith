// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/services/CanvasLayoutEngine.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasWire.hpp"

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QtGlobal>

#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Canvas::Services {

namespace {

int sideIndex(PortSide side)
{
    switch (side) {
        case PortSide::Left: return 0;
        case PortSide::Right: return 1;
        case PortSide::Top: return 2;
        case PortSide::Bottom: return 3;
    }
    return 0;
}

PortSide sideFromDelta(const QPointF& d)
{
    if (std::abs(d.x()) >= std::abs(d.y()))
        return d.x() >= 0.0 ? PortSide::Right : PortSide::Left;
    return d.y() >= 0.0 ? PortSide::Bottom : PortSide::Top;
}

struct PortConn final {
    PortId id{};
    PortSide side{PortSide::Left};
    double key = 0.0;
};

void addEndpointConnection(const CanvasDocument& doc,
                           CanvasBlock& block,
                           std::array<std::vector<PortConn>, 4>& groups,
                           const QPointF& center,
                           const CanvasWire::Endpoint& endpoint,
                           const CanvasWire::Endpoint& other)
{
    if (!endpoint.attached || endpoint.attached->itemId != block.id())
        return;
    if (other.attached && other.attached->itemId == block.id())
        return;

    QPointF target = other.freeScene;
    if (other.attached) {
        QPointF a, b, f;
        if (doc.computePortTerminal(other.attached->itemId,
                                    other.attached->portId,
                                    a, b, f))
            target = a;
    }

    const QPointF d = target - center;
    const PortSide side = sideFromDelta(d);
    const double key = (side == PortSide::Left || side == PortSide::Right)
                           ? target.y()
                           : target.x();
    groups[sideIndex(side)].push_back(PortConn{endpoint.attached->portId, side, key});
}

void collectPortGroups(const CanvasDocument& doc,
                       CanvasBlock& block,
                       std::array<std::vector<PortConn>, 4>& groups)
{
    const QPointF center = block.boundsScene().center();

    for (const auto& it : doc.items()) {
        if (!it)
            continue;
        auto* wire = dynamic_cast<CanvasWire*>(it.get());
        if (!wire)
            continue;
        addEndpointConnection(doc, block, groups, center, wire->a(), wire->b());
        addEndpointConnection(doc, block, groups, center, wire->b(), wire->a());
    }
}

bool resizeBlockForPorts(const CanvasDocument& doc,
                         CanvasBlock& block,
                         const std::array<std::vector<PortConn>, 4>& groups,
                         double step,
                         QRectF& bounds)
{
    bounds = block.boundsScene();
    const size_t countLeft = groups[sideIndex(PortSide::Left)].size();
    const size_t countRight = groups[sideIndex(PortSide::Right)].size();
    const size_t countTop = groups[sideIndex(PortSide::Top)].size();
    const size_t countBottom = groups[sideIndex(PortSide::Bottom)].size();

    const size_t maxVertical = std::max(countLeft, countRight);
    const size_t maxHorizontal = std::max(countTop, countBottom);
    const double requiredHeight = std::max(bounds.height(),
                                           (static_cast<double>(maxVertical) + 1.0) * step);
    const double requiredWidth = std::max(bounds.width(),
                                          (static_cast<double>(maxHorizontal) + 1.0) * step);
    const double size = std::max(requiredWidth, requiredHeight);

    if (size <= bounds.width() && size <= bounds.height())
        return false;

    const QPointF center = bounds.center();
    bounds = QRectF(center.x() - size * 0.5, center.y() - size * 0.5, size, size);
    block.setBoundsScene(bounds);

    for (const auto& it : doc.items()) {
        auto* wire = dynamic_cast<CanvasWire*>(it.get());
        if (wire && wire->hasRouteOverride() && wire->attachesTo(block.id()))
            wire->clearRouteOverride();
    }
    return true;
}

void layoutPortsOnSide(CanvasBlock& block,
                       const QRectF& bounds,
                       double step,
                       std::vector<PortConn>& list)
{
    if (list.empty())
        return;

    const PortSide side = list.front().side;
    std::sort(list.begin(), list.end(),
              [](const PortConn& a, const PortConn& b) { return a.key < b.key; });

    const double length = (side == PortSide::Left || side == PortSide::Right)
                              ? bounds.height()
                              : bounds.width();
    if (length <= 1e-6)
        return;

    const double start = (side == PortSide::Left || side == PortSide::Right)
                             ? bounds.top()
                             : bounds.left();

    for (size_t i = 0; i < list.size(); ++i) {
        const double pos = start + step * (static_cast<double>(i) + 1.0);
        const double t = (pos - start) / length;
        block.updatePort(list[i].id, side, t);
    }
}

} // namespace

bool CanvasLayoutEngine::arrangeAutoPorts(CanvasDocument& doc, CanvasBlock& block) const
{
    // Link hubs support user-directed port placement and should not be auto-rearranged.
    if (block.isLinkHub() || !block.autoPortLayout() || !block.hasPorts())
        return false;

    const auto beforePorts = block.ports();

    std::array<std::vector<PortConn>, 4> groups;
    collectPortGroups(doc, block, groups);

    const double step = doc.fabric().config().step;
    if (step <= 0.0)
        return false;

    QRectF bounds;
    const bool boundsChanged = resizeBlockForPorts(doc, block, groups, step, bounds);

    for (auto& list : groups)
        layoutPortsOnSide(block, bounds, step, list);

    const auto& afterPorts = block.ports();
    bool portsChanged = beforePorts.size() != afterPorts.size();
    if (!portsChanged) {
        for (size_t i = 0; i < beforePorts.size(); ++i) {
            const auto& a = beforePorts[i];
            const auto& b = afterPorts[i];
            if (a.id != b.id || a.side != b.side || !qFuzzyCompare(a.t, b.t)) {
                portsChanged = true;
                break;
            }
        }
    }

    return boundsChanged || portsChanged;
}

} // namespace Canvas::Services
