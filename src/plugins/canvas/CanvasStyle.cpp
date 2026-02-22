// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/CanvasStyle.hpp"

#include "canvas/CanvasConstants.hpp"

#include <QtGui/QPainter>
#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtGui/QPolygonF>
#include <cmath>

namespace Canvas {

static double clamped(const double v, const double lo, const double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void CanvasStyle::drawBlockFrame(QPainter& p, const QRectF& boundsScene, double zoom)
{
    drawBlockFrame(p, boundsScene, zoom,
                   QColor(Constants::kBlockOutlineColor),
                   QColor(Constants::kBlockFillColor),
                   Constants::kBlockCornerRadius);
}

void CanvasStyle::drawBlockFrame(QPainter& p,
                                 const QRectF& boundsScene,
                                 double zoom,
                                 const QColor& outline,
                                 const QColor& fill,
                                 double radius)
{
    const double base = 1.0 / clamped(zoom, 0.25, 8.0);
    const double penW = clamped(base, 0.25, 2.0);

    QPen pen{outline};
    pen.setWidthF(penW);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(fill);

    p.drawRoundedRect(boundsScene, radius, radius);
}

void CanvasStyle::drawBlockSelection(QPainter& p, const QRectF& boundsScene, double zoom)
{
    const double base = 2.0 / clamped(zoom, 0.25, 8.0);
    const double penW = clamped(base, 0.5, 3.0);

    QPen pen{QColor(Constants::kBlockSelectionColor)};
    pen.setWidthF(penW);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const double inset = -2.0 / clamped(zoom, 0.25, 8.0);
    const QRectF r = boundsScene.adjusted(inset, inset, -inset, -inset);
    const double radius = Constants::kBlockCornerRadius;
    p.drawRoundedRect(r, radius, radius);
}

void CanvasStyle::drawBlockLabel(QPainter& p, const QRectF& boundsScene, double zoom, const QString& text)
{
    drawBlockLabel(p, boundsScene, zoom, text, QColor(Constants::kBlockTextColor));
}

void CanvasStyle::drawBlockLabel(QPainter& p,
                                 const QRectF& boundsScene,
                                 double zoom,
                                 const QString& text,
                                 const QColor& color)
{
    Q_UNUSED(zoom);

    QFont f = p.font();
    f.setPointSizeF(Constants::kBlockLabelPointSize);
    f.setBold(true);
    p.setFont(f);

    p.setPen(color);

    const QRectF r = boundsScene.adjusted(Constants::kBlockLabelPadX,
                                          Constants::kBlockLabelPadY,
                                          -Constants::kBlockLabelPadX,
                                          -Constants::kBlockLabelPadY);
    p.drawText(r, Qt::AlignLeft | Qt::AlignTop, text);
}

void CanvasStyle::drawBlockStereotype(QPainter& p,
                                      const QRectF& boundsScene,
                                      double zoom,
                                      const QString& text)
{
    drawBlockStereotype(p, boundsScene, zoom, text, QColor(Constants::kBlockStereotypeColor));
}

void CanvasStyle::drawBlockStereotype(QPainter& p,
                                      const QRectF& boundsScene,
                                      double zoom,
                                      const QString& text,
                                      const QColor& color)
{
    drawBlockStereotype(p, boundsScene, zoom, text, color, false);
}

void CanvasStyle::drawBlockStereotype(QPainter& p,
                                      const QRectF& boundsScene,
                                      double zoom,
                                      const QString& text,
                                      const QColor& color,
                                      bool underline)
{
    Q_UNUSED(zoom);

    const QString normalized = text.trimmed();
    if (normalized.isEmpty())
        return;

    QFont f = p.font();
    f.setPointSizeF(Constants::kBlockStereotypePointSize);
    f.setBold(false);
    f.setItalic(true);
    f.setUnderline(underline);
    p.setFont(f);
    p.setPen(color);

    QFontMetricsF metrics(p.font());
    const QSizeF size = metrics.size(Qt::TextSingleLine, normalized);
    const double x = boundsScene.center().x() - (size.width() * 0.5);
    const double y = boundsScene.top() - Constants::kBlockStereotypeOffsetY - size.height();
    const QRectF textRect(x, y, size.width(), size.height());
    p.drawText(textRect, Qt::AlignCenter, normalized);
}

void CanvasStyle::drawPort(QPainter& p, const QPointF& anchorScene, PortSide side, PortRole role, double zoom, bool hovered)
{
    const double base = 1.0 / clamped(zoom, 0.25, 8.0);
    const double penW = clamped(base, 0.25, 2.0);

    QColor stroke(hovered ? Constants::kBlockSelectionColor : Constants::kBlockOutlineColor);
    QColor fill;
    switch (role) {
        case PortRole::Producer: fill = QColor(Constants::kBlockSelectionColor); break;
        case PortRole::Consumer: fill = QColor(Constants::kBlockTextColor); break;
        case PortRole::Dynamic:  fill = QColor(Constants::kDynamicPortColor); break;
    }
    fill.setAlpha(180);

    QPen pen(stroke);
    pen.setWidthF(penW);
    pen.setCapStyle(Qt::SquareCap);
    p.setPen(pen);
    p.setBrush(fill);

    const double stubLen = hovered ? Constants::kPortStubLengthHover : Constants::kPortStubLength;
    const double half = hovered ? Constants::kPortBoxHalfHover : Constants::kPortBoxHalf;

    QPointF dir(0, 0);
    switch (side) {
        case PortSide::Left:   dir = QPointF(-1, 0); break;
        case PortSide::Right:  dir = QPointF( 1, 0); break;
        case PortSide::Top:    dir = QPointF( 0,-1); break;
        case PortSide::Bottom: dir = QPointF( 0, 1); break;
    }

    const QPointF stubEnd = anchorScene + dir * stubLen;
    p.drawLine(anchorScene, stubEnd);

    const QRectF box(anchorScene.x() - half, anchorScene.y() - half, half * 2.0, half * 2.0);
    p.drawRect(box);
}

void CanvasStyle::drawPortLabel(QPainter& p,
                                const QPointF& anchorScene,
                                PortSide side,
                                double zoom,
                                const QString& text,
                                const QColor& color)
{
    if (text.isEmpty())
        return;

    QFont f = p.font();
    f.setPointSizeF(Constants::kPortLabelPointSize);
    f.setBold(true);
    p.setFont(f);
    p.setPen(color);

    QFontMetricsF fm(p.font());
    const QSizeF size = fm.size(Qt::TextSingleLine, text);

    QPointF dir(0, 0);
    switch (side) {
        case PortSide::Left:   dir = QPointF(-1, 0); break;
        case PortSide::Right:  dir = QPointF( 1, 0); break;
        case PortSide::Top:    dir = QPointF( 0,-1); break;
        case PortSide::Bottom: dir = QPointF( 0, 1); break;
    }

    const double offset = Constants::kPortStubLength + Constants::kPortLabelOffset;
    const QPointF base = anchorScene + dir * offset;

    QPointF topLeft = base;
    if (side == PortSide::Left)
        topLeft = QPointF(base.x() - size.width(), base.y() - size.height() * 0.5);
    else if (side == PortSide::Right)
        topLeft = QPointF(base.x(), base.y() - size.height() * 0.5);
    else if (side == PortSide::Top)
        topLeft = QPointF(base.x() - size.width() * 0.5, base.y() - size.height());
    else
        topLeft = QPointF(base.x() - size.width() * 0.5, base.y());

    p.drawText(QRectF(topLeft, size), Qt::AlignLeft | Qt::AlignTop, text);
}

void CanvasStyle::drawWireAnnotation(QPainter& p,
                                     const QRectF& annotationRect,
                                     double zoom,
                                     const QString& text,
                                     bool selected,
                                     bool forwardObjectFifo,
                                     bool scaleWithZoom)
{
    if (!annotationRect.isValid())
        return;

    const QString normalized = text.trimmed();
    if (normalized.isEmpty())
        return;

    p.save();

    const double base = 1.0 / clamped(zoom, 0.25, 8.0);
    const double penW = clamped(base, 0.25, 1.5);

    const QColor outlineColor = QColor(selected
                                           ? (forwardObjectFifo
                                                  ? Constants::kWireAnnotationForwardSelectedOutlineColor
                                                  : Constants::kWireAnnotationSelectedOutlineColor)
                                           : (forwardObjectFifo
                                                  ? Constants::kWireAnnotationForwardOutlineColor
                                                  : Constants::kWireAnnotationOutlineColor));
    const QColor fillColor = QColor(selected
                                        ? (forwardObjectFifo
                                               ? Constants::kWireAnnotationForwardSelectedFillColor
                                               : Constants::kWireAnnotationSelectedFillColor)
                                        : (forwardObjectFifo
                                               ? Constants::kWireAnnotationForwardFillColor
                                               : Constants::kWireAnnotationFillColor));
    const QColor textColor = QColor(selected
                                        ? (forwardObjectFifo
                                               ? Constants::kWireAnnotationForwardSelectedTextColor
                                               : Constants::kWireAnnotationSelectedTextColor)
                                        : (forwardObjectFifo
                                               ? Constants::kWireAnnotationForwardTextColor
                                               : Constants::kWireAnnotationTextColor));

    QPen pen(outlineColor);
    pen.setWidthF(penW);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(fillColor);

    const double scale = scaleWithZoom ? 1.0 : (1.0 / clamped(zoom, 0.25, 8.0));
    const double radius = Constants::kWireAnnotationCornerRadius * scale;
    p.drawRoundedRect(annotationRect, radius, radius);

    if (selected) {
        QPen accentPen{QColor(Constants::kBlockSelectionColor)};
        accentPen.setWidthF(clamped(penW * 1.8, 1.0, 2.75));
        accentPen.setJoinStyle(Qt::RoundJoin);
        p.setPen(accentPen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(annotationRect, radius, radius);
    }

    QFont font = p.font();
    font.setPointSizeF(Constants::kWireAnnotationPointSize * scale);
    font.setBold(false);
    p.setFont(font);
    p.setPen(textColor);

    const double padX = Constants::kWireAnnotationPadX * scale;
    const double padY = Constants::kWireAnnotationPadY * scale;
    const QRectF textRect = annotationRect.adjusted(padX, padY, -padX, -padY);
    p.drawText(textRect, Qt::AlignCenter, normalized);

    p.restore();
}

void CanvasStyle::drawWire(QPainter& p,
                           const QPointF& aAnchor, const QPointF& aBorder, const QPointF& aFabric,
                           const QPointF& bFabric, const QPointF& bBorder, const QPointF& bAnchor,
                           double zoom, bool selected, WireArrowPolicy arrowPolicy)
{
    drawWirePath(p, aAnchor, aBorder, aFabric, bFabric, bBorder, bAnchor, {}, zoom, selected, arrowPolicy);
}

void CanvasStyle::drawWireColored(QPainter& p,
                           const QPointF& aAnchor, const QPointF& aBorder, const QPointF& aFabric,
                           const QPointF& bFabric, const QPointF& bBorder, const QPointF& bAnchor,
                           const QColor& color,
                           double zoom, bool selected, WireArrowPolicy arrowPolicy)
{
    drawWirePathColored(p, aAnchor, aBorder, aFabric, bFabric, bBorder, bAnchor, {}, color, zoom, selected, arrowPolicy);
}

void CanvasStyle::drawWirePath(QPainter& p,
                           const QPointF& aAnchor, const QPointF& aBorder, const QPointF& aFabric,
                           const QPointF& bFabric, const QPointF& bBorder, const QPointF& bAnchor,
                           const std::vector<QPointF>& pathScene,
                           double zoom, bool selected, WireArrowPolicy arrowPolicy)
{
    const double base = 2.0 / clamped(zoom, 0.25, 8.0);
    const double penW = clamped(base, 0.5, 3.0);

    QColor c(selected ? Constants::kBlockSelectionColor : Constants::kWireColor);
    QPen pen(c);
    pen.setWidthF(penW);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    p.drawLine(aAnchor, aBorder);
    p.drawLine(aBorder, aFabric);

    if (pathScene.size() >= 2) {
        QPolygonF poly;
        poly.reserve(static_cast<int>(pathScene.size()));
        for (const auto& pt : pathScene)
            poly << pt;
        p.drawPolyline(poly);
    } else {
        p.drawLine(aFabric, bFabric);
    }

    p.drawLine(bFabric, bBorder);
    p.drawLine(bBorder, bAnchor);

    if (arrowPolicy != WireArrowPolicy::None) {
        const QPointF tip = (arrowPolicy == WireArrowPolicy::End) ? bBorder : aBorder;
        const QPointF anchor = (arrowPolicy == WireArrowPolicy::End) ? bAnchor : aAnchor;
        const QPointF dir = anchor - tip;
        const double len = std::hypot(dir.x(), dir.y());
        if (len > 1e-6) {
            const QPointF n(dir.x() / len, dir.y() / len);
            const QPointF perp(-n.y(), n.x());
            const double arrowLen = 8.0 / clamped(zoom, 0.25, 8.0);
            const double arrowHalfW = 4.0 / clamped(zoom, 0.25, 8.0);
            const QPointF base = tip - n * arrowLen;
            const QPointF left = base + perp * arrowHalfW;
            const QPointF right = base - perp * arrowHalfW;
            QPolygonF tri;
            tri << tip << left << right;
            p.setBrush(c);
            p.drawPolygon(tri);
            p.setBrush(Qt::NoBrush);
        }
    }
}

void CanvasStyle::drawWirePathColored(QPainter& p,
                           const QPointF& aAnchor, const QPointF& aBorder, const QPointF& aFabric,
                           const QPointF& bFabric, const QPointF& bBorder, const QPointF& bAnchor,
                           const std::vector<QPointF>& pathScene,
                           const QColor& color,
                           double zoom, bool selected, WireArrowPolicy arrowPolicy)
{
    const double base = 2.0 / clamped(zoom, 0.25, 8.0);
    const double penW = clamped(base, 0.5, 3.0);

    QColor c(selected ? Constants::kBlockSelectionColor : color);
    QPen pen(c);
    pen.setWidthF(penW);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    p.drawLine(aAnchor, aBorder);
    p.drawLine(aBorder, aFabric);

    if (pathScene.size() >= 2) {
        QPolygonF poly;
        poly.reserve(static_cast<int>(pathScene.size()));
        for (const auto& pt : pathScene)
            poly << pt;
        p.drawPolyline(poly);
    } else {
        p.drawLine(aFabric, bFabric);
    }

    p.drawLine(bFabric, bBorder);
    p.drawLine(bBorder, bAnchor);

    if (arrowPolicy != WireArrowPolicy::None) {
        const QPointF tip = (arrowPolicy == WireArrowPolicy::End) ? bBorder : aBorder;
        const QPointF anchor = (arrowPolicy == WireArrowPolicy::End) ? bAnchor : aAnchor;
        const QPointF dir = anchor - tip;
        const double len = std::hypot(dir.x(), dir.y());
        if (len > 1e-6) {
            const QPointF n(dir.x() / len, dir.y() / len);
            const QPointF perp(-n.y(), n.x());
            const double arrowLen = 8.0 / clamped(zoom, 0.25, 8.0);
            const double arrowHalfW = 4.0 / clamped(zoom, 0.25, 8.0);
            const QPointF base = tip - n * arrowLen;
            const QPointF left = base + perp * arrowHalfW;
            const QPointF right = base - perp * arrowHalfW;
            QPolygonF tri;
            tri << tip << left << right;
            p.setBrush(c);
            p.drawPolygon(tri);
            p.setBrush(Qt::NoBrush);
        }
    }
}

} // namespace Canvas
