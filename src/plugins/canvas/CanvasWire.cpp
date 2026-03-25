// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/CanvasWire.hpp"

#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasStyle.hpp"
#include "canvas/internal/CanvasWireRouting.hpp"
#include "canvas/utils/CanvasGeometry.hpp"

#include <QtCore/QHashFunctions>
#include <QtCore/QLineF>
#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Canvas {

namespace {

constexpr double kHitTestTolerance = 6.0;
constexpr int kEscapeMaxSteps = 8;

int signum(double v)
{
    return (v > 0.0) ? 1 : (v < 0.0) ? -1 : 0;
}

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= 1e-6)
        return QLineF(p, a).length();

    const QPointF ap = p - a;
    double t = (ap.x() * ab.x() + ap.y() * ab.y()) / len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const QPointF proj(a.x() + t * ab.x(), a.y() + t * ab.y());
    return QLineF(p, proj).length();
}

static QPointF endpointScene(const CanvasWire::Endpoint& e, const CanvasRenderContext& ctx,
                             QPointF* outAnchor, QPointF* outBorder, QPointF* outFabric)
{
    if (e.attached.has_value()) {
        const auto ref = *e.attached;
        QPointF a, b, f;
        if (ctx.portTerminal(ref.itemId, ref.portId, a, b, f)) {
            if (outAnchor) *outAnchor = a;
            if (outBorder) *outBorder = b;
            if (outFabric) *outFabric = f;
            return f;
        }
    }
    if (outAnchor) *outAnchor = e.freeScene;
    if (outBorder) *outBorder = e.freeScene;
    if (outFabric) *outFabric = e.freeScene;
    return e.freeScene;
}

struct EndpointPositions final {
    QPointF aAnchor;
    QPointF aBorder;
    QPointF aFabric;
    QPointF bAnchor;
    QPointF bBorder;
    QPointF bFabric;
};

EndpointPositions resolveEndpoints(const CanvasWire& wire, const CanvasRenderContext& ctx)
{
    EndpointPositions out;
    endpointScene(wire.a(), ctx, &out.aAnchor, &out.aBorder, &out.aFabric);
    endpointScene(wire.b(), ctx, &out.bAnchor, &out.bBorder, &out.bFabric);
    return out;
}

struct EscapeInfo final {
    FabricCoord coord{};
    bool hasEscape = false;
};

EscapeInfo computeEscape(const QPointF& borderScene,
                         const QPointF& fabricScene,
                         const FabricCoord& start,
                         const CanvasRenderContext& ctx)
{
    const int dirX = signum(fabricScene.x() - borderScene.x());
    const int dirY = signum(fabricScene.y() - borderScene.y());
    if (dirX == 0 && dirY == 0)
        return {start, false};

    FabricCoord cur = start;
    for (int step = 0; step < kEscapeMaxSteps; ++step) {
        FabricCoord next{cur.x + dirX, cur.y + dirY};
        if (!ctx.fabricBlocked(next))
            return {next, true};
        cur = next;
    }
    return {start, false};
}

void appendCoord(std::vector<FabricCoord>& path, const FabricCoord& coord)
{
    if (!path.empty() && path.back().x == coord.x && path.back().y == coord.y)
        return;
    path.push_back(coord);
}

QString shortStrongId(const QString& idText)
{
    const QString normalized = idText.trimmed();
    if (normalized.isEmpty())
        return QStringLiteral("?");

    return normalized.left(6).toUpper();
}

QString normalizeObjectFifoType(QString valueType)
{
    valueType = valueType.trimmed().toLower();
    if (valueType == QStringLiteral("i8") ||
        valueType == QStringLiteral("i16") ||
        valueType == QStringLiteral("i32")) {
        return valueType;
    }
    return QStringLiteral("i32");
}

QString normalizedObjectFifoName(QString name)
{
    name = name.trimmed();
    if (name.isEmpty())
        return QStringLiteral("of");
    return name;
}

int normalizedObjectFifoDepth(int depth)
{
    return depth > 0 ? depth : 2;
}

QString objectFifoAnnotationText(const CanvasWire::ObjectFifoConfig& config, bool compact)
{
    const QString name = normalizedObjectFifoName(config.name);

    // Pivot wire of a split/join/broadcast: render as "SPLIT/JOIN/BCAST: {hubName}, {fifoName}".
    if (!config.hubName.trimmed().isEmpty()) {
        const QString prefix =
            (config.operation == CanvasWire::ObjectFifoOperation::Join)
                ? QStringLiteral("JOIN: ")
            : (config.operation == CanvasWire::ObjectFifoOperation::Forward)
                ? QStringLiteral("BCAST: ")
            : QStringLiteral("SPLIT: ");
        return prefix + QStringLiteral("%1, %2").arg(config.hubName.trimmed(), name);
    }

    // Arm wire of a broadcast hub: no annotation (arm names are never used in generated code).
    if (config.operation == CanvasWire::ObjectFifoOperation::Forward)
        return {};

    // Arm wire of a split/join hub: show only the sub-FIFO name (concise, near the tile port).
    if (config.operation == CanvasWire::ObjectFifoOperation::Split ||
        config.operation == CanvasWire::ObjectFifoOperation::Join)
        return name;

    const int depth = normalizedObjectFifoDepth(config.depth);
    const QString valueType = normalizeObjectFifoType(config.type.valueType);

    if (compact)
        return QStringLiteral("FIFO<\"%1\", D:%2>").arg(name).arg(depth);

    QString text = QStringLiteral("FIFO<\"%1\", D:%2, T:%3>").arg(name).arg(depth).arg(valueType);
    const QString dimensions = config.type.dimensions.trimmed();
    if (!dimensions.isEmpty()) {
        text.chop(1);
        text += QStringLiteral(", Dim:%1>").arg(dimensions);
    }
    return text;
}

struct AnnotationAnchor final {
    QPointF point;
    QPointF tangent;
    double segmentLength = 0.0;
};

AnnotationAnchor annotationAnchorForPath(const std::vector<QPointF>& pathScene,
                                         const CanvasWire::Endpoint& a,
                                         const CanvasWire::Endpoint& b,
                                         bool nearB = false)
{
    if (pathScene.size() >= 2) {
        if (nearB) {
            // Use the last segment so the annotation sits near the B endpoint (tile port).
            const QPointF first  = pathScene[pathScene.size() - 2];
            const QPointF second = pathScene[pathScene.size() - 1];
            const QPointF delta  = second - first;
            const double len2    = delta.x() * delta.x() + delta.y() * delta.y();
            const double len     = std::sqrt(len2);
            QPointF dir(1.0, 0.0);
            if (len > 1e-6)
                dir = QPointF(delta.x() / len, delta.y() / len);
            // Anchor at 25% from B (75% from the preceding waypoint).
            const QPointF anchor(first.x() * 0.25 + second.x() * 0.75,
                                 first.y() * 0.25 + second.y() * 0.75);
            return {anchor, dir, len};
        }

        double bestLen2 = -1.0;
        QPointF bestMidpoint = pathScene.front();
        QPointF bestDir(1.0, 0.0);
        double bestLength = 0.0;
        for (size_t i = 1; i < pathScene.size(); ++i) {
            const QPointF first = pathScene[i - 1];
            const QPointF second = pathScene[i];
            const QPointF delta = second - first;
            const double len2 = delta.x() * delta.x() + delta.y() * delta.y();
            if (len2 <= bestLen2)
                continue;

            bestLen2 = len2;
            bestMidpoint = QPointF((first.x() + second.x()) * 0.5,
                                   (first.y() + second.y()) * 0.5);
            const double len = std::sqrt(len2);
            if (len > 1e-6)
                bestDir = QPointF(delta.x() / len, delta.y() / len);
            bestLength = len;
        }
        return {bestMidpoint, bestDir, bestLength};
    }

    if (pathScene.size() == 1)
        return {pathScene.front(), QPointF(1.0, 0.0), 0.0};

    const QPointF delta = b.freeScene - a.freeScene;
    const double len2 = delta.x() * delta.x() + delta.y() * delta.y();
    QPointF dir(1.0, 0.0);
    if (len2 > 1e-6) {
        const double len = std::sqrt(len2);
        dir = QPointF(delta.x() / len, delta.y() / len);
    }

    return {QPointF((a.freeScene.x() + b.freeScene.x()) * 0.5,
                    (a.freeScene.y() + b.freeScene.y()) * 0.5),
            dir,
            std::sqrt(len2)};
}

WireAnnotationVisibilityMode effectiveWireAnnotationVisibility(const CanvasRenderContext& ctx)
{
    if (ctx.showAllWireAnnotations)
        return WireAnnotationVisibilityMode::ShowAll;
    return ctx.wireAnnotationVisibilityMode;
}

double annotationScaleFactor(const CanvasRenderContext& ctx)
{
    if (ctx.wireAnnotationsScaleWithZoom)
        return 1.0;
    return 1.0 / std::clamp(ctx.zoom, 0.25, 8.0);
}

QRectF annotationRectForPath(const QString& text,
                             const std::vector<QPointF>& pathScene,
                             const CanvasWire::Endpoint& a,
                             const CanvasWire::Endpoint& b,
                             ObjectId wireId,
                             const CanvasRenderContext& ctx,
                             bool nearB = false)
{
    const QString normalized = text.trimmed();
    if (normalized.isEmpty())
        return {};

    const double scale = annotationScaleFactor(ctx);

    QFont font = QApplication::font();
    font.setPointSizeF(Constants::kWireAnnotationPointSize * scale);
    font.setBold(false);

    QFontMetricsF metrics(font);
    const QSizeF textSize = metrics.size(Qt::TextSingleLine, normalized);
    const QSizeF boxSize(textSize.width() + (Constants::kWireAnnotationPadX * 2.0 * scale),
                         textSize.height() + (Constants::kWireAnnotationPadY * 2.0 * scale));
    const AnnotationAnchor anchor = annotationAnchorForPath(pathScene, a, b, nearB);
    const QPointF normal(-anchor.tangent.y(), anchor.tangent.x());

    const WireAnnotationVisibilityMode visibility = effectiveWireAnnotationVisibility(ctx);
    int normalLane = 0;
    int tangentLane = 0;
    if (visibility == WireAnnotationVisibilityMode::ShowAll) {
        const size_t hash = qHash(wireId);
        normalLane = static_cast<int>(hash % 3) - 1;
        tangentLane = static_cast<int>((hash / 3) % 3) - 1;
    }

    const double segmentSpan = std::max(0.0, anchor.segmentLength);
    const double normalBase = std::max(6.0 * scale, Constants::kWireAnnotationBaseNormalOffset * 0.45 * scale);
    const double normalStep = Constants::kWireAnnotationLaneOffset * 0.45 * scale;
    const double tangentStep = Constants::kWireAnnotationTangentOffset * 0.35 * scale;
    const double maxNormalOffset = std::max(10.0 * scale, segmentSpan * 0.25);
    const double maxTangentOffset = std::max(6.0 * scale, segmentSpan * 0.18);
    const double normalOffset = std::clamp(normalBase + (static_cast<double>(normalLane) * normalStep),
                                           -maxNormalOffset,
                                           maxNormalOffset);
    const double tangentOffset = std::clamp(static_cast<double>(tangentLane) * tangentStep,
                                            -maxTangentOffset,
                                            maxTangentOffset);

    const QPointF center = anchor.point
                           + (normal * normalOffset)
                           + (anchor.tangent * tangentOffset);
    const double x = center.x() - boxSize.width() * 0.5;
    const double y = center.y() - boxSize.height() * 0.5;
    return QRectF(x, y, boxSize.width(), boxSize.height());
}

} // namespace


void CanvasWire::draw(QPainter& p, const CanvasRenderContext& ctx) const
{
    QPointF aAnchor, aBorder, aFabric;
    QPointF bAnchor, bBorder, bFabric;
    endpointScene(m_a, ctx, &aAnchor, &aBorder, &aFabric);
    endpointScene(m_b, ctx, &bAnchor, &bBorder, &bFabric);

    const std::vector<QPointF> route = resolvedPathScene(ctx);

    if (m_hasColorOverride) {
        CanvasStyle::drawWirePathColored(p, aAnchor, aBorder, aFabric, bFabric, bBorder, bAnchor, route,
                                         m_colorOverride, ctx.zoom, ctx.selected(id()), m_arrowPolicy);
    } else {
        CanvasStyle::drawWirePath(p, aAnchor, aBorder, aFabric, bFabric, bBorder, bAnchor, route,
                                  ctx.zoom, ctx.selected(id()), m_arrowPolicy);
    }

}

void CanvasWire::drawPortBadge(QPainter& p, const CanvasRenderContext& ctx, bool badgeAtB) const
{
    if (!m_objectFifo.has_value() ||
        m_objectFifo->type.mode != CanvasWire::DimensionMode::Matrix)
        return;

    const Endpoint& ep = badgeAtB ? m_b : m_a;
    if (!ep.attached.has_value())
        return;

    QPointF anchor, border, fabric;
    endpointScene(ep, ctx, &anchor, &border, &fabric);

    static QSvgRenderer xRenderer(QStringLiteral(":/ui/icons/svg/port_x_badge.svg"));
    const double r = std::clamp(7.0 / ctx.zoom, 5.0, 14.0);
    const QRectF badgeRect(anchor.x() - r, anchor.y() - r, r * 2.0, r * 2.0);
    p.save();
    xRenderer.render(&p, badgeRect);
    p.restore();
}

QRectF CanvasWire::boundsScene() const
{
    QRectF r(m_a.freeScene, m_b.freeScene);
    return r.normalized().adjusted(-8.0, -8.0, 8.0, 8.0);
}

std::unique_ptr<CanvasItem> CanvasWire::clone() const
{
    auto w = std::make_unique<CanvasWire>(m_a, m_b);
    w->setId(id());
    w->m_routeOverride = m_routeOverride;
    w->m_arrowPolicy = m_arrowPolicy;
    w->m_hasColorOverride = m_hasColorOverride;
    w->m_colorOverride = m_colorOverride;
    w->m_objectFifo = m_objectFifo;
    return w;
}

bool CanvasWire::hitTest(const QPointF& scenePos) const
{
    return distanceToSegment(scenePos, m_a.freeScene, m_b.freeScene) <= kHitTestTolerance;
}

bool CanvasWire::hitTest(const QPointF& scenePos, const CanvasRenderContext& ctx) const
{
    const AnnotationDetail detail = annotationDetail(ctx);
    const QRectF annotationBounds = annotationRect(ctx, detail);
    if (annotationBounds.isValid() && annotationBounds.contains(scenePos))
        return true;

    const std::vector<QPointF> route = resolvedPathScene(ctx);

    if (route.size() >= 2) {
        for (size_t i = 1; i < route.size(); ++i) {
            if (distanceToSegment(scenePos, route[i - 1], route[i]) <= kHitTestTolerance)
                return true;
        }
        return false;
    }
    if (route.size() == 1)
        return QLineF(scenePos, route.front()).length() <= kHitTestTolerance;
    return false;
}

void CanvasWire::setRouteOverride(std::vector<FabricCoord> path)
{
    m_routeOverride = std::move(path);
    m_overrideStale = false;
    if (!m_routeOverride.empty()) {
        m_overrideStart = m_routeOverride.front();
        m_overrideEnd = m_routeOverride.back();
    }
}

void CanvasWire::clearRouteOverride()
{
    m_routeOverride.clear();
    m_overrideStale = false;
}

void CanvasWire::setColorOverride(const QColor& color)
{
    m_colorOverride = color;
    m_hasColorOverride = true;
}

void CanvasWire::clearColorOverride()
{
    m_hasColorOverride = false;
    m_colorOverride = QColor();
}

void CanvasWire::setObjectFifo(ObjectFifoConfig config)
{
    config.name = normalizedObjectFifoName(config.name);
    config.depth = normalizedObjectFifoDepth(config.depth);
    config.type.valueType = normalizeObjectFifoType(config.type.valueType);
    config.type.dimensions = config.type.dimensions.trimmed();
    m_objectFifo = std::move(config);
}

void CanvasWire::clearObjectFifo()
{
    m_objectFifo.reset();
}

bool CanvasWire::attachesTo(ObjectId itemId) const
{
    return (m_a.attached.has_value() && m_a.attached->itemId == itemId) ||
           (m_b.attached.has_value() && m_b.attached->itemId == itemId);
}

std::vector<QPointF> CanvasWire::resolvedPathScene(const CanvasRenderContext& ctx) const
{
    const EndpointPositions endpoints = resolveEndpoints(*this, ctx);
    const Internal::WireRouter router(ctx);
    const double step = ctx.fabricStep;

    if (step <= 0.0)
        return router.routeFabricPath(endpoints.aFabric, endpoints.bFabric);

    const FabricCoord startCoord = Support::toFabricCoord(endpoints.aFabric, step);
    const FabricCoord endCoord = Support::toFabricCoord(endpoints.bFabric, step);
    const EscapeInfo escapeA = computeEscape(endpoints.aBorder, endpoints.aFabric, startCoord, ctx);
    const EscapeInfo escapeB = computeEscape(endpoints.bBorder, endpoints.bFabric, endCoord, ctx);

    const FabricCoord routeStart = escapeA.hasEscape ? escapeA.coord : startCoord;
    const FabricCoord routeEnd = escapeB.hasEscape ? escapeB.coord : endCoord;

    bool useOverride = !m_routeOverride.empty() && !m_overrideStale;
    if (useOverride) {
        if (startCoord.x != m_overrideStart.x || startCoord.y != m_overrideStart.y ||
            endCoord.x != m_overrideEnd.x || endCoord.y != m_overrideEnd.y) {
            m_overrideStale = true;
            useOverride = false;
        }
    }

    std::vector<FabricCoord> core;
    if (!useOverride) {
        core = router.routeCoords(routeStart, routeEnd);
    } else {
        std::vector<FabricCoord> waypoints = m_routeOverride;
        if (!waypoints.empty()) {
            waypoints.front() = routeStart;
            waypoints.back() = routeEnd;
        }
        core = router.routeCoordsViaWaypoints(waypoints);
    }

    std::vector<FabricCoord> full;
    appendCoord(full, startCoord);
    if (escapeA.hasEscape)
        appendCoord(full, routeStart);
    for (const auto& coord : core)
        appendCoord(full, coord);
    if (escapeB.hasEscape)
        appendCoord(full, endCoord);

    return Internal::simplifyCoordsToScene(full, step, endpoints.aFabric, endpoints.bFabric);
}

std::vector<FabricCoord> CanvasWire::resolvedPathCoords(const CanvasRenderContext& ctx) const
{
    const double step = ctx.fabricStep;
    if (step <= 0.0)
        return {};

    const std::vector<QPointF> pathScene = resolvedPathScene(ctx);
    std::vector<FabricCoord> out;
    out.reserve(pathScene.size());
    for (const auto& pt : pathScene)
        out.push_back(Support::toFabricCoord(pt, step));
    return out;
}

bool CanvasWire::shouldShowAnnotation(const CanvasRenderContext& ctx) const
{
    const WireAnnotationVisibilityMode visibility = effectiveWireAnnotationVisibility(ctx);
    if (visibility == WireAnnotationVisibilityMode::Hidden)
        return false;
    if (visibility == WireAnnotationVisibilityMode::ShowAll)
        return true;

    const bool emphasized = ctx.selected(id()) || ctx.hovered(id());
    return emphasized;
}

CanvasWire::AnnotationDetail CanvasWire::annotationDetail(const CanvasRenderContext& ctx) const
{
    if (!shouldShowAnnotation(ctx))
        return AnnotationDetail::Hidden;

    switch (ctx.wireAnnotationDetailMode) {
        case WireAnnotationDetailMode::Compact:
            return AnnotationDetail::Compact;
        case WireAnnotationDetailMode::Full:
            return AnnotationDetail::Full;
        case WireAnnotationDetailMode::Adaptive:
            break;
    }

    const bool emphasized = ctx.selected(id()) || ctx.hovered(id());
    const WireAnnotationVisibilityMode visibility = effectiveWireAnnotationVisibility(ctx);

    if (ctx.zoom < Constants::kWireAnnotationHideZoom) {
        if (emphasized)
            return AnnotationDetail::Compact;
        if (visibility == WireAnnotationVisibilityMode::ShowAll)
            return AnnotationDetail::Compact;
        return AnnotationDetail::Hidden;
    }

    if (ctx.zoom < Constants::kWireAnnotationCompactZoom)
        return AnnotationDetail::Compact;

    return AnnotationDetail::Full;
}

QString CanvasWire::annotationText(AnnotationDetail detail, const CanvasRenderContext& ctx) const
{
    if (detail == AnnotationDetail::Hidden)
        return {};

    // Arm wire of a split/join hub: hub is always at endpoint A for arm wires.
    // Only check m_a so pivot wires (hub at endpoint B) fall through to the ObjectFifo check.
    // Broadcast arm wires (Forward op, no hubName) suppress the index — arms aren't referenced
    // individually in generated code.
    QString hubArmLabel;
    if (m_a.attached.has_value()) {
        const auto& ref = m_a.attached.value();
        if (ctx.hubArmLabelForEndpoint(ref.itemId, ref.portId, hubArmLabel)) {
            const bool isBroadcastArm = m_objectFifo.has_value()
                && m_objectFifo->operation == ObjectFifoOperation::Forward
                && m_objectFifo->hubName.trimmed().isEmpty();
            if (isBroadcastArm)
                return {};
            return hubArmLabel;
        }
    }

    if (m_objectFifo.has_value())
        return objectFifoAnnotationText(*m_objectFifo, detail == AnnotationDetail::Compact);

    QString consumerHandleLabel;
    const auto resolveHandleLabel = [&](const Endpoint& endpoint) {
        if (!endpoint.attached.has_value())
            return false;
        const auto& ref = endpoint.attached.value();
        return ctx.consumerHandleLabelForEndpoint(ref.itemId, ref.portId, consumerHandleLabel);
    };
    if (resolveHandleLabel(m_a) || resolveHandleLabel(m_b))
        return consumerHandleLabel;

    const QString idLabel = shortStrongId(id().toString());
    if (detail == AnnotationDetail::Compact)
        return QStringLiteral("W%1").arg(idLabel.left(4));

    return QStringLiteral("WIRE %1").arg(idLabel);
}

QRectF CanvasWire::annotationRect(const CanvasRenderContext& ctx, AnnotationDetail detail) const
{
    if (detail == AnnotationDetail::Hidden)
        return {};

    // Arm wires of split/join hubs anchor their label near the B endpoint (tile port side).
    const bool nearB = m_objectFifo.has_value()
        && m_objectFifo->hubName.trimmed().isEmpty()
        && (m_objectFifo->operation == ObjectFifoOperation::Split ||
            m_objectFifo->operation == ObjectFifoOperation::Join);

    return annotationRectForPath(annotationText(detail, ctx), resolvedPathScene(ctx), m_a, m_b, id(), ctx, nearB);
}

} // namespace Canvas
