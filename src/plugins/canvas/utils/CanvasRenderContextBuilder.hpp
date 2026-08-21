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

    CanvasRenderContext::IsHubHighlightedFn isHubHighlighted = nullptr;
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

// `computeWirePaths` runs the document-order wire-overlap-avoidance pre-pass, filling
// the returned context's `resolvedWirePaths`. Callers that repaint far more often than
// the document actually changes (i.e. CanvasScene's main paint path) should cache the
// result themselves, keyed on CanvasDocument::changed(), and pass false to skip
// redundantly rerouting every wire in the document on every call. Defaults to true so
// existing (infrequent — hit-testing, drag, context menu) callers are unaffected.
CanvasRenderContext buildRenderContext(const CanvasDocument* doc,
                                       const QRectF& visibleSceneRect,
                                       double zoom,
                                       const RenderContextSelection& selection = RenderContextSelection{},
                                       const RenderContextPortState& ports = RenderContextPortState{},
                                       const RenderContextAnnotationState& annotations = RenderContextAnnotationState{},
                                       bool computeWirePaths = true);

QRectF computeVisibleSceneRect(const CanvasView& view);

} // namespace Canvas::Support
