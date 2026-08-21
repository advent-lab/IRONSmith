// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasTypes.hpp"

#include <QtCore/QPointF>
#include <QtCore/QRectF>

namespace Canvas {
class CanvasDocument;
}

namespace Canvas::Services {

class CANVAS_EXPORT CanvasGeometryService final
{
public:
    static bool isFabricPointBlocked(const CanvasDocument& doc, const FabricCoord& coord);

    // True if any fabric-blocking item's keepout rect intersects sceneRect.
    static bool isRectBlocked(const CanvasDocument& doc, const QRectF& sceneRect);

    static bool computePortTerminal(const CanvasDocument& doc,
                                    ObjectId itemId,
                                    PortId portId,
                                    QPointF& outAnchorScene,
                                    QPointF& outBorderScene,
                                    QPointF& outFabricScene);

    static bool computePortTerminalThunk(void* user,
                                         ObjectId itemId,
                                         PortId portId,
                                         QPointF& outAnchorScene,
                                         QPointF& outBorderScene,
                                         QPointF& outFabricScene);

    static bool isFabricPointBlockedThunk(const FabricCoord& coord, void* user);
};

} // namespace Canvas::Services
