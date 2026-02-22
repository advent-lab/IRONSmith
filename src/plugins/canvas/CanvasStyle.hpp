// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasPorts.hpp"

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <vector>

class QPainter;

namespace Canvas {

struct CANVAS_EXPORT CanvasStyle final
{
    static void drawBlockFrame(QPainter& p, const QRectF& boundsScene, double zoom);
    static void drawBlockFrame(QPainter& p,
                               const QRectF& boundsScene,
                               double zoom,
                               const QColor& outline,
                               const QColor& fill,
                               double radius);
    static void drawBlockSelection(QPainter& p, const QRectF& boundsScene, double zoom);
    static void drawBlockLabel(QPainter& p, const QRectF& boundsScene, double zoom, const QString& text);
    static void drawBlockLabel(QPainter& p,
                               const QRectF& boundsScene,
                               double zoom,
                               const QString& text,
                               const QColor& color);
    static void drawBlockStereotype(QPainter& p,
                                    const QRectF& boundsScene,
                                    double zoom,
                                    const QString& text);
    static void drawBlockStereotype(QPainter& p,
                                    const QRectF& boundsScene,
                                    double zoom,
                                    const QString& text,
                                    const QColor& color);
    static void drawBlockStereotype(QPainter& p,
                                    const QRectF& boundsScene,
                                    double zoom,
                                    const QString& text,
                                    const QColor& color,
                                    bool underline);
    static void drawPort(QPainter& p, const QPointF& anchorScene, PortSide side, PortRole role, double zoom, bool hovered);
    static void drawPortLabel(QPainter& p,
                              const QPointF& anchorScene,
                              PortSide side,
                              double zoom,
                              const QString& text,
                              const QColor& color);
    static void drawWireAnnotation(QPainter& p,
                                   const QRectF& annotationRect,
                                   double zoom,
                                   const QString& text,
                                   bool selected,
                                   bool forwardObjectFifo,
                                   bool scaleWithZoom = true);

    static void drawWirePath(QPainter& p,
                             const QPointF& aAnchor, const QPointF& aBorder, const QPointF& aFabric,
                             const QPointF& bFabric, const QPointF& bBorder, const QPointF& bAnchor,
                             const std::vector<QPointF>& pathScene,
                             double zoom, bool selected,
                             WireArrowPolicy arrowPolicy = WireArrowPolicy::End);
    static void drawWirePathColored(QPainter& p,
                             const QPointF& aAnchor, const QPointF& aBorder, const QPointF& aFabric,
                             const QPointF& bFabric, const QPointF& bBorder, const QPointF& bAnchor,
                             const std::vector<QPointF>& pathScene,
                             const QColor& color,
                             double zoom, bool selected,
                             WireArrowPolicy arrowPolicy = WireArrowPolicy::End);

    static void drawWire(QPainter& p,
                         const QPointF& aAnchor, const QPointF& aBorder, const QPointF& aFabric,
                         const QPointF& bFabric, const QPointF& bBorder, const QPointF& bAnchor,
                         double zoom, bool selected,
                         WireArrowPolicy arrowPolicy = WireArrowPolicy::End);
    static void drawWireColored(QPainter& p,
                         const QPointF& aAnchor, const QPointF& aBorder, const QPointF& aFabric,
                         const QPointF& bFabric, const QPointF& bBorder, const QPointF& bAnchor,
                         const QColor& color,
                         double zoom, bool selected,
                         WireArrowPolicy arrowPolicy = WireArrowPolicy::End);
};

} // namespace Canvas
