// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasInteractionTypes.hpp"
#include "canvas/CanvasTypes.hpp"

#include <optional>

namespace Canvas {
class CanvasDocument;
struct CanvasPort;
}

namespace Canvas::Support {

struct CANVAS_EXPORT BoundProducerPlacementRequest final {
    ObjectId consumerItemId{};
    PortId consumerPortId{};
    EdgeCandidate producerEdge;
};

struct CANVAS_EXPORT BoundProducerPlacementResult final {
    ObjectId producerItemId{};
    PortId producerPortId{};
};

CANVAS_EXPORT std::optional<BoundProducerPlacementResult>
createBoundProducerPort(CanvasDocument& doc, const BoundProducerPlacementRequest& request);

CANVAS_EXPORT bool isBoundConsumerEndpointValid(const CanvasDocument& doc, const CanvasPort& port);

} // namespace Canvas::Support

