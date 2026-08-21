// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/services/CanvasGeometryService.hpp"

#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasItem.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/utils/CanvasGeometry.hpp"

#include <QtCore/QRectF>

#include <optional>

namespace Canvas::Services {

namespace {

void stepOutCoord(FabricCoord& coord, PortSide side)
{
    switch (side) {
        case PortSide::Left:   coord.x -= 1; break;
        case PortSide::Right:  coord.x += 1; break;
        case PortSide::Top:    coord.y -= 1; break;
        case PortSide::Bottom: coord.y += 1; break;
    }
}

} // namespace

bool CanvasGeometryService::isFabricPointBlocked(const CanvasDocument& doc, const FabricCoord& coord)
{
    const double step = doc.fabric().config().step;
    const QPointF p(coord.x * step, coord.y * step);

    for (const auto& it : doc.items()) {
        if (!it || !it->blocksFabric())
            continue;
        if (it->keepoutSceneRect().contains(p))
            return true;
    }
    return false;
}

bool CanvasGeometryService::isRectBlocked(const CanvasDocument& doc, const QRectF& sceneRect)
{
    for (const auto& it : doc.items()) {
        if (!it || !it->blocksFabric())
            continue;
        if (it->keepoutSceneRect().intersects(sceneRect))
            return true;
    }
    return false;
}

bool CanvasGeometryService::computePortTerminal(const CanvasDocument& doc,
                                                ObjectId itemId,
                                                PortId portId,
                                                QPointF& outAnchorScene,
                                                QPointF& outBorderScene,
                                                QPointF& outFabricScene)
{
    const CanvasItem* item = doc.findItem(itemId);
    if (!item || !item->hasPorts())
        return false;

    std::optional<CanvasPort> meta;
    for (const auto& p : item->ports()) {
        if (p.id == portId) { meta = p; break; }
    }
    if (!meta.has_value())
        return false;

    outAnchorScene = item->portAnchorScene(portId);

    const QRectF keepout = item->blocksFabric() ? item->keepoutSceneRect() : item->boundsScene();

    outBorderScene = outAnchorScene;
    if (meta->side == PortSide::Left)   outBorderScene.setX(keepout.left());
    if (meta->side == PortSide::Right)  outBorderScene.setX(keepout.right());
    if (meta->side == PortSide::Top)    outBorderScene.setY(keepout.top());
    if (meta->side == PortSide::Bottom) outBorderScene.setY(keepout.bottom());

    const auto& cfg = doc.fabric().config();
    const double step = cfg.step;
    if (step <= 0.0)
        return false;

    FabricCoord c = Support::toFabricCoord(outBorderScene, step);
    int guard = 0;
    while (isFabricPointBlocked(doc, c) && guard++ < 64) {
        stepOutCoord(c, meta->side);
    }

    outFabricScene = Support::toScenePoint(c, step);
    return true;
}

bool CanvasGeometryService::computePortTerminalThunk(void* user,
                                                     ObjectId itemId,
                                                     PortId portId,
                                                     QPointF& outAnchorScene,
                                                     QPointF& outBorderScene,
                                                     QPointF& outFabricScene)
{
    auto* doc = static_cast<const CanvasDocument*>(user);
    return doc && computePortTerminal(*doc, itemId, portId, outAnchorScene, outBorderScene, outFabricScene);
}

bool CanvasGeometryService::isFabricPointBlockedThunk(const FabricCoord& coord, void* user)
{
    auto* doc = static_cast<const CanvasDocument*>(user);
    return doc ? isFabricPointBlocked(*doc, coord) : false;
}

} // namespace Canvas::Services
