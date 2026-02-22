// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasItem.hpp"

#include <QtGui/QColor>
#include <QtCore/QHash>
#include <QtCore/QRectF>
#include <QtCore/QString>
#include <cstdint>
#include <optional>

namespace Canvas {

struct CANVAS_EXPORT PortRef final {
    ObjectId itemId{};
    PortId   portId{};
};

inline bool operator==(const PortRef& a, const PortRef& b) noexcept
{
    return a.itemId == b.itemId && a.portId == b.portId;
}

inline size_t qHash(const PortRef& ref, size_t seed = 0) noexcept
{
    seed = ::qHash(ref.itemId, seed);
    return ::qHash(ref.portId, seed);
}

class CANVAS_EXPORT CanvasWire final : public CanvasItem
{
public:
    enum class ObjectFifoOperation : uint8_t {
        Fifo,
        Forward
    };

    enum class AnnotationDetail : uint8_t {
        Hidden,
        Compact,
        Full
    };

    struct ObjectFifoTypeAbstraction final {
        QString dimensions;
        QString valueType = QStringLiteral("i32");
    };

    struct ObjectFifoConfig final {
        QString name = QStringLiteral("in");
        int depth = 2;
        ObjectFifoOperation operation = ObjectFifoOperation::Fifo;
        ObjectFifoTypeAbstraction type;
    };

    struct Endpoint final {
        std::optional<PortRef> attached;
        QPointF freeScene{0.0, 0.0};
    };

    CanvasWire(Endpoint a, Endpoint b)
        : m_a(std::move(a))
        , m_b(std::move(b))
    {}

    const Endpoint& a() const { return m_a; }
    const Endpoint& b() const { return m_b; }

    void setEndpointA(Endpoint e) { m_a = std::move(e); }
    void setEndpointB(Endpoint e) { m_b = std::move(e); }

    WireArrowPolicy arrowPolicy() const noexcept { return m_arrowPolicy; }
    void setArrowPolicy(WireArrowPolicy policy) { m_arrowPolicy = policy; }

    bool hasColorOverride() const noexcept { return m_hasColorOverride; }
    const QColor& colorOverride() const { return m_colorOverride; }
    void setColorOverride(const QColor& color);
    void clearColorOverride();

    bool hasObjectFifo() const noexcept { return m_objectFifo.has_value(); }
    bool hasForwardObjectFifo() const noexcept
    {
        return m_objectFifo.has_value() && m_objectFifo->operation == ObjectFifoOperation::Forward;
    }
    const std::optional<ObjectFifoConfig>& objectFifo() const noexcept { return m_objectFifo; }
    void setObjectFifo(ObjectFifoConfig config);
    void clearObjectFifo();

    void draw(QPainter& p, const CanvasRenderContext& ctx) const override;
    QRectF boundsScene() const override;
    std::unique_ptr<CanvasItem> clone() const override;

    bool hitTest(const QPointF& scenePos) const override;
    bool hitTest(const QPointF& scenePos, const CanvasRenderContext& ctx) const;

    bool hasRouteOverride() const noexcept { return !m_routeOverride.empty(); }
    const std::vector<FabricCoord>& routeOverride() const noexcept { return m_routeOverride; }
    void setRouteOverride(std::vector<FabricCoord> path);
    void clearRouteOverride();

    bool attachesTo(ObjectId itemId) const;

    std::vector<QPointF> resolvedPathScene(const CanvasRenderContext& ctx) const;
    std::vector<FabricCoord> resolvedPathCoords(const CanvasRenderContext& ctx) const;
    bool shouldShowAnnotation(const CanvasRenderContext& ctx) const;
    AnnotationDetail annotationDetail(const CanvasRenderContext& ctx) const;
    QString annotationText(AnnotationDetail detail, const CanvasRenderContext& ctx) const;
    QRectF annotationRect(const CanvasRenderContext& ctx, AnnotationDetail detail) const;

private:
    Endpoint m_a;
    Endpoint m_b;
    std::vector<FabricCoord> m_routeOverride;
    FabricCoord m_overrideStart{};
    FabricCoord m_overrideEnd{};
    mutable bool m_overrideStale = false;
    WireArrowPolicy m_arrowPolicy = WireArrowPolicy::End;
    bool m_hasColorOverride = false;
    QColor m_colorOverride;
    std::optional<ObjectFifoConfig> m_objectFifo;
};

} // namespace Canvas
