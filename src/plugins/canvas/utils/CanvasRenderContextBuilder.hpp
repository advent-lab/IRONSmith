// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasRenderContext.hpp"

#include <QtCore/QRectF>

namespace Canvas {
class CanvasDocument;
class CanvasView;
}

namespace Canvas::Support {

struct RenderContextSelection final {
    CanvasRenderContext::IsSelectedFn isSelected = nullptr;
    void* user = nullptr;

    bool hasHoveredItem = false;
    ObjectId hoveredItem{};
};

struct RenderContextPortState final {
    bool hasHoveredPort = false;
    ObjectId hoveredPortItem{};
    PortId hoveredPortId{};

    bool hasSelectedPort = false;
    ObjectId selectedPortItem{};
    PortId selectedPortId{};

    CanvasRenderContext::IsPortSelectedFn isPortSelected = nullptr;
    void* isPortSelectedUser = nullptr;
};

struct RenderContextAnnotationState final {
    WireAnnotationVisibilityMode wireAnnotationVisibilityMode = WireAnnotationVisibilityMode::Auto;
    WireAnnotationDetailMode wireAnnotationDetailMode = WireAnnotationDetailMode::Adaptive;
    bool wireAnnotationsScaleWithZoom = true;
};

CanvasRenderContext buildRenderContext(const CanvasDocument* doc,
                                       const QRectF& visibleSceneRect,
                                       double zoom,
                                       const RenderContextSelection& selection = RenderContextSelection{},
                                       const RenderContextPortState& ports = RenderContextPortState{},
                                       const RenderContextAnnotationState& annotations = RenderContextAnnotationState{});

QRectF computeVisibleSceneRect(const CanvasView& view);

} // namespace Canvas::Support
