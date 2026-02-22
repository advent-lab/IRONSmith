// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasTypes.hpp"

#include <QtCore/QRectF>
#include <QtCore/QString>
#include <cstdint>

namespace Canvas {

enum class WireAnnotationVisibilityMode : uint8_t {
    Auto,
    ShowAll,
    Hidden
};

enum class WireAnnotationDetailMode : uint8_t {
    Adaptive,
    Compact,
    Full
};

struct CANVAS_EXPORT CanvasRenderContext final {
    double zoom = 1.0;
    QRectF visibleSceneRect;

    using IsSelectedFn = bool (*)(void*, ObjectId);
    IsSelectedFn isSelected = nullptr;
    void* isSelectedUser = nullptr;

    bool selected(ObjectId id) const {
        return isSelected ? isSelected(isSelectedUser, id) : false;
    }

    bool hasHoveredItem = false;
    ObjectId hoveredItem{};

    bool hovered(ObjectId id) const {
        return hasHoveredItem && hoveredItem == id;
    }

    using ComputePortTerminalFn = bool (*)(void*, ObjectId, PortId, QPointF& outAnchor, QPointF& outBorder, QPointF& outFabric);
    ComputePortTerminalFn computePortTerminal = nullptr;
    void* computePortTerminalUser = nullptr;

    bool portTerminal(ObjectId itemId, PortId portId, QPointF& outAnchor, QPointF& outBorder, QPointF& outFabric) const {
        return computePortTerminal ? computePortTerminal(computePortTerminalUser, itemId, portId, outAnchor, outBorder, outFabric) : false;
    }

    using ResolveObjectFifoNameForEndpointFn = bool (*)(void*, ObjectId, PortId, QString& outName);
    ResolveObjectFifoNameForEndpointFn resolveObjectFifoNameForEndpoint = nullptr;
    void* resolveObjectFifoNameForEndpointUser = nullptr;

    bool objectFifoNameForEndpoint(ObjectId itemId, PortId portId, QString& outName) const {
        return resolveObjectFifoNameForEndpoint
            ? resolveObjectFifoNameForEndpoint(resolveObjectFifoNameForEndpointUser, itemId, portId, outName)
            : false;
    }

    using ResolveConsumerHandleLabelForEndpointFn = bool (*)(void*, ObjectId, PortId, QString& outLabel);
    ResolveConsumerHandleLabelForEndpointFn resolveConsumerHandleLabelForEndpoint = nullptr;
    void* resolveConsumerHandleLabelForEndpointUser = nullptr;

    bool consumerHandleLabelForEndpoint(ObjectId itemId, PortId portId, QString& outLabel) const {
        return resolveConsumerHandleLabelForEndpoint
            ? resolveConsumerHandleLabelForEndpoint(resolveConsumerHandleLabelForEndpointUser,
                                                    itemId,
                                                    portId,
                                                    outLabel)
            : false;
    }

    using IsFabricBlockedFn = bool (*)(const FabricCoord& coord, void* user);
    IsFabricBlockedFn isFabricBlocked = nullptr;
    void* isFabricBlockedUser = nullptr;
    double fabricStep = 16.0;

    bool fabricBlocked(const FabricCoord& coord) const {
        return isFabricBlocked ? isFabricBlocked(coord, isFabricBlockedUser) : false;
    }

    bool hasHoveredPort = false;
    ObjectId hoveredPortItem{};
    PortId hoveredPortId{};

    bool portHovered(ObjectId itemId, PortId portId) const {
        return hasHoveredPort && hoveredPortItem == itemId && hoveredPortId == portId;
    }

    bool hasSelectedPort = false;
    ObjectId selectedPortItem{};
    PortId selectedPortId{};

    using IsPortSelectedFn = bool (*)(void*, ObjectId, PortId);
    IsPortSelectedFn isPortSelected = nullptr;
    void* isPortSelectedUser = nullptr;

    bool portSelected(ObjectId itemId, PortId portId) const {
        if (isPortSelected)
            return isPortSelected(isPortSelectedUser, itemId, portId);
        return hasSelectedPort && selectedPortItem == itemId && selectedPortId == portId;
    }

    // Legacy compatibility path used by older call-sites.
    bool showAllWireAnnotations = false;
    WireAnnotationVisibilityMode wireAnnotationVisibilityMode = WireAnnotationVisibilityMode::Auto;
    WireAnnotationDetailMode wireAnnotationDetailMode = WireAnnotationDetailMode::Adaptive;
    bool wireAnnotationsScaleWithZoom = true;
};

} // namespace Canvas
