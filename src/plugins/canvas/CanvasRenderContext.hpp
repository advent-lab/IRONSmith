// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasTypes.hpp"

#include <QtCore/QHash>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <cstdint>
#include <vector>

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

    // True if `id` is a currently-selected link-hub block — used so a hub's attached
    // wires (root + branch arms) can be drawn highlighted even though the wires
    // themselves aren't individually selected.
    using IsHubHighlightedFn = bool (*)(void*, ObjectId);
    IsHubHighlightedFn isHubHighlighted = nullptr;
    void* isHubHighlightedUser = nullptr;

    bool hubHighlighted(ObjectId id) const {
        return isHubHighlighted ? isHubHighlighted(isHubHighlightedUser, id) : false;
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

    using ResolveHubArmLabelForEndpointFn = bool (*)(void*, ObjectId, PortId, QString& outLabel);
    ResolveHubArmLabelForEndpointFn resolveHubArmLabelForEndpoint = nullptr;
    void* resolveHubArmLabelForEndpointUser = nullptr;

    bool hubArmLabelForEndpoint(ObjectId itemId, PortId portId, QString& outLabel) const {
        return resolveHubArmLabelForEndpoint
            ? resolveHubArmLabelForEndpoint(resolveHubArmLabelForEndpointUser, itemId, portId, outLabel)
            : false;
    }

    using ResolveItemSpecIdFn = bool (*)(void*, ObjectId, QString& outSpecId);
    ResolveItemSpecIdFn resolveItemSpecId = nullptr;
    void* resolveItemSpecIdUser = nullptr;

    bool itemSpecId(ObjectId itemId, QString& outSpecId) const {
        return resolveItemSpecId
            ? resolveItemSpecId(resolveItemSpecIdUser, itemId, outSpecId)
            : false;
    }

    using ResolveItemSymbolFn = bool (*)(void*, ObjectId, QString& outSymbol);
    ResolveItemSymbolFn resolveItemSymbol = nullptr;
    void* resolveItemSymbolUser = nullptr;

    bool itemSymbol(ObjectId itemId, QString& outSymbol) const {
        return resolveItemSymbol
            ? resolveItemSymbol(resolveItemSymbolUser, itemId, outSymbol)
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

    // FIFO names that appear as Join-hub pivot targets in the document.
    // Used by BCAST pivot wire annotation to decide whether to suppress the hub name.
    QSet<QString> joinTargetFifoNames;

    // Opaque Canvas::Internal::EdgeOccupancy*, cast back inside WireRouter. Transient —
    // only valid while the occupancy pre-pass in Support::buildRenderContext is running;
    // never read outside that function. Declared opaque here (rather than typed) because
    // CanvasWireRouting.hpp already includes this header, so including it back here would
    // be circular.
    const void* wireEdgeOccupancy = nullptr;

    // Every wire's resolved scene-space path, precomputed once per repaint by the
    // occupancy pre-pass. CanvasWire::resolvedPathScene() returns this cached value when
    // present instead of re-routing, so all draw/annotation/badge call sites within one
    // repaint agree on the same geometry (and avoid redundant recomputation).
    QHash<ObjectId, std::vector<QPointF>> resolvedWirePaths;
};

} // namespace Canvas
