// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasTypes.hpp"
#include "canvas/CanvasPorts.hpp"

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>

#include <algorithm>
#include <cmath>
#include <optional>

namespace Canvas::Support {

inline double snapCoord(double v, double step)
{
    if (step <= 0.0)
        return v;
    return std::llround(v / step) * step;
}

inline QPointF snapPointToGrid(const QPointF& p, double step)
{
    return QPointF(snapCoord(p.x(), step), snapCoord(p.y(), step));
}

inline double snapSizeUp(double v, double step)
{
    if (step <= 0.0)
        return v;
    return std::ceil(v / step) * step;
}

inline QRectF snapBoundsToGrid(const QRectF& r, double step)
{
    if (step <= 0.0)
        return r;

    QRectF out = r;
    const QPointF tl = r.topLeft();
    const double w = r.width();
    const double h = r.height();

    const QPointF snappedTopLeft(snapCoord(tl.x(), step), snapCoord(tl.y(), step));
    const double snappedW = snapSizeUp(w, step);
    const double snappedH = snapSizeUp(h, step);
    out.setTopLeft(snappedTopLeft);
    out.setSize(QSizeF(snappedW, snappedH));
    return out;
}

inline double clampT(double t, double lo = 0.0, double hi = 1.0)
{
    return t < lo ? lo : (t > hi ? hi : t);
}

struct EdgeHit final {
    PortSide side = PortSide::Left;
    double t = 0.5;
    QPointF anchorScene;
};

inline std::optional<EdgeHit> edgeHitForRect(const QRectF& boundsScene,
                                             const QPointF& scenePos,
                                             double threshold,
                                             double snapStep)
{
    if (boundsScene.width() <= 1e-6 || boundsScene.height() <= 1e-6)
        return std::nullopt;

    const QRectF expanded = boundsScene.adjusted(-threshold, -threshold, threshold, threshold);
    if (!expanded.contains(scenePos))
        return std::nullopt;

    const double dLeft = std::abs(scenePos.x() - boundsScene.left());
    const double dRight = std::abs(scenePos.x() - boundsScene.right());
    const double dTop = std::abs(scenePos.y() - boundsScene.top());
    const double dBottom = std::abs(scenePos.y() - boundsScene.bottom());

    double best = dLeft;
    PortSide side = PortSide::Left;
    if (dRight < best) { best = dRight; side = PortSide::Right; }
    if (dTop < best) { best = dTop; side = PortSide::Top; }
    if (dBottom < best) { best = dBottom; side = PortSide::Bottom; }

    if (best > threshold)
        return std::nullopt;

    EdgeHit hit;
    hit.side = side;

    if (side == PortSide::Left || side == PortSide::Right) {
        double y = std::clamp(scenePos.y(), boundsScene.top(), boundsScene.bottom());
        y = snapCoord(y, snapStep);
        y = std::clamp(y, boundsScene.top(), boundsScene.bottom());
        hit.anchorScene = QPointF(side == PortSide::Left ? boundsScene.left() : boundsScene.right(), y);
        hit.t = (hit.anchorScene.y() - boundsScene.top()) / boundsScene.height();
    } else {
        double x = std::clamp(scenePos.x(), boundsScene.left(), boundsScene.right());
        x = snapCoord(x, snapStep);
        x = std::clamp(x, boundsScene.left(), boundsScene.right());
        hit.anchorScene = QPointF(x, side == PortSide::Top ? boundsScene.top() : boundsScene.bottom());
        hit.t = (hit.anchorScene.x() - boundsScene.left()) / boundsScene.width();
    }

    return hit;
}

inline FabricCoord toFabricCoord(const QPointF& s, double step)
{
    const int ix = static_cast<int>(std::llround(s.x() / step));
    const int iy = static_cast<int>(std::llround(s.y() / step));
    return FabricCoord{ix, iy};
}

inline QPointF toScenePoint(const FabricCoord& c, double step)
{
    return QPointF(c.x * step, c.y * step);
}

} // namespace Canvas::Support
