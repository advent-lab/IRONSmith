// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/utils/CanvasPortBindings.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/utils/CanvasAutoPorts.hpp"
#include "canvas/utils/CanvasGeometry.hpp"

namespace Canvas::Support {

namespace {

QString resolveBoundConsumerName(const CanvasDocument& doc,
                                 ObjectId consumerItemId,
                                 PortId consumerPortId,
                                 const CanvasPort& consumerMeta)
{
    QString fifoName;
    if (doc.resolveObjectFifoNameForEndpoint(consumerItemId, consumerPortId, fifoName))
        return fifoName;

    const QString consumerName = consumerMeta.name.trimmed();
    if (consumerName.isEmpty())
        return QStringLiteral("in");
    if (Support::isPairedPortName(consumerName) || Support::isLegacyPairedPortName(consumerName))
        return QStringLiteral("in");
    return consumerName;
}

QString boundConsumerLabelText(const QString& consumerName)
{
    const QString normalized = consumerName.trimmed();
    const QString effectiveName = normalized.isEmpty() ? QStringLiteral("in") : normalized;
    return QStringLiteral("C: \"%1\"").arg(effectiveName);
}

} // namespace

std::optional<BoundProducerPlacementResult>
createBoundProducerPort(CanvasDocument& doc, const BoundProducerPlacementRequest& request)
{
    if (request.consumerItemId.isNull() || request.consumerPortId.isNull())
        return std::nullopt;
    if (request.producerEdge.itemId != request.consumerItemId)
        return std::nullopt;

    CanvasPort consumerMeta;
    if (!doc.getPort(request.consumerItemId, request.consumerPortId, consumerMeta))
        return std::nullopt;
    if (consumerMeta.role == PortRole::Producer)
        return std::nullopt;

    auto* producerBlock = dynamic_cast<CanvasBlock*>(doc.findItem(request.producerEdge.itemId));
    if (!producerBlock)
        return std::nullopt;
    if (producerBlock->isLinkHub())
        return std::nullopt;

    const PortId producerPortId =
        producerBlock->addPort(request.producerEdge.side,
                               Support::clampT(request.producerEdge.t),
                               PortRole::Producer,
                               boundConsumerLabelText(resolveBoundConsumerName(doc,
                                                                               request.consumerItemId,
                                                                               request.consumerPortId,
                                                                               consumerMeta)));
    if (producerPortId.isNull())
        return std::nullopt;

    producerBlock->updatePortBinding(producerPortId,
                                     request.consumerItemId,
                                     request.consumerPortId);
    doc.notifyChanged();

    return BoundProducerPlacementResult{producerBlock->id(), producerPortId};
}

bool isBoundConsumerEndpointValid(const CanvasDocument& doc, const CanvasPort& port)
{
    if (!port.hasBinding)
        return false;

    CanvasPort consumerMeta;
    if (!doc.getPort(port.bindingItemId, port.bindingPortId, consumerMeta))
        return false;
    return consumerMeta.role != PortRole::Producer;
}

} // namespace Canvas::Support
