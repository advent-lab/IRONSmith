// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/utils/CanvasAutoPorts.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/utils/CanvasPortUsage.hpp"

#include <QtCore/QUuid>

namespace Canvas::Support {

namespace {

using namespace Qt::StringLiterals;

const QString kPairPrefix = u"__pair:"_s;
const QString kLegacyPairPrefix = u"__paired:"_s;

} // namespace

PortSide oppositeSide(PortSide side)
{
    switch (side) {
        case PortSide::Left: return PortSide::Right;
        case PortSide::Right: return PortSide::Left;
        case PortSide::Top: return PortSide::Bottom;
        case PortSide::Bottom: return PortSide::Top;
    }
    return PortSide::Left;
}

QString pairedPortName(const QString& pairKey)
{
    return kPairPrefix + pairKey;
}

bool isPairedPortName(const QString& name)
{
    return name.startsWith(kPairPrefix);
}

bool isLegacyPairedPortName(const QString& name)
{
    return name.startsWith(kLegacyPairPrefix);
}

std::optional<QString> pairedPortKeyFromName(const QString& name)
{
    if (name.startsWith(kPairPrefix))
        return name.mid(kPairPrefix.size());
    if (name.startsWith(kLegacyPairPrefix))
        return name.mid(kLegacyPairPrefix.size());
    return std::nullopt;
}

bool isPairedProducerPort(const CanvasPort& port)
{
    return port.role == PortRole::Producer && pairedPortKeyFromName(port.name).has_value();
}

std::optional<QString> pairedPortKey(const CanvasPort& port)
{
    return pairedPortKeyFromName(port.name);
}

bool ensureOppositeProducerPort(CanvasDocument& doc, ObjectId itemId, PortId portId)
{
    auto* block = dynamic_cast<CanvasBlock*>(doc.findItem(itemId));
    if (!block || !block->autoOppositeProducerPort())
        return false;

    CanvasPort meta;
    if (!doc.getPort(itemId, portId, meta))
        return false;
    if (meta.role == PortRole::Producer)
        return false;

    const PortSide targetSide = oppositeSide(meta.side);
    const double targetT = meta.t;
    QString pairKey;
    if (const auto existingKey = pairedPortKeyFromName(meta.name); existingKey && !existingKey->isEmpty()) {
        pairKey = *existingKey;
    } else {
        // Legacy documents can leave the consumer unnamed while the producer key is based on consumer port id.
        const QString consumerIdKey = portId.toString();
        bool foundLegacyProducer = false;
        for (const auto& port : block->ports()) {
            if (port.role != PortRole::Producer)
                continue;
            if (const auto key = pairedPortKey(port); key && *key == consumerIdKey) {
                foundLegacyProducer = true;
                break;
            }
        }
        pairKey = foundLegacyProducer
            ? consumerIdKey
            : QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    const QString targetName = pairedPortName(pairKey);
    bool renamed = false;
    if (meta.name != targetName && block->updatePortName(portId, targetName))
        renamed = true;

    for (const auto& port : block->ports()) {
        if (port.role != PortRole::Producer)
            continue;
        const auto key = pairedPortKey(port);
        if (!key || *key != pairKey)
            continue;
        if (port.name != targetName && block->updatePortName(port.id, targetName))
            renamed = true;
        if (renamed)
            doc.notifyChanged();
        return false;
    }

    const PortId created = block->addPort(targetSide, targetT, PortRole::Producer, targetName);
    if (created.isNull())
        return false;

    doc.notifyChanged();
    return true;
}

std::optional<AutoPortRemoval> removeOppositeProducerPort(CanvasDocument& doc,
                                                          ObjectId itemId,
                                                          PortId portId)
{
    auto* block = dynamic_cast<CanvasBlock*>(doc.findItem(itemId));
    if (!block || !block->autoOppositeProducerPort())
        return std::nullopt;

    CanvasPort meta;
    if (!doc.getPort(itemId, portId, meta))
        return std::nullopt;

    QStringList candidateNames;
    if (isPairedPortName(meta.name) || isLegacyPairedPortName(meta.name)) {
        candidateNames.push_back(meta.name);
    } else {
        const QString idText = portId.toString();
        candidateNames.push_back(pairedPortName(idText));
        candidateNames.push_back(kLegacyPairPrefix + idText);
    }

    const auto& ports = block->ports();
    for (const auto& candidate : candidateNames) {
        for (size_t i = 0; i < ports.size(); ++i) {
            const auto& port = ports[i];
            if (port.name != candidate)
                continue;
            if (port.role != PortRole::Producer)
                continue;
            if (countPortAttachments(doc, block->id(), port.id) != 0)
                return std::nullopt;

            size_t index = 0;
            auto removed = block->removePort(port.id, &index);
            if (!removed)
                return std::nullopt;
            return AutoPortRemoval{block->id(), index, *removed};
        }
    }

    return std::nullopt;
}

} // namespace Canvas::Support
