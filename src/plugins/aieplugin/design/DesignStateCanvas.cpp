// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/design/DesignStateCanvas.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasCommands.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasAutoPorts.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"
#include "canvas/utils/CanvasLinkWireStyle.hpp"

#include <QtCore/QHash>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMarginsF>
#include <QtCore/QUuid>

#include <cmath>

Q_LOGGING_CATEGORY(aiedesignstatelog, "ironsmith.aie.designstate")

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

QString hubIdForBlock(Canvas::CanvasBlock& block)
{
    QString id = block.specId().trimmed();
    if (!id.isEmpty())
        return id;

    id = QStringLiteral("hub-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    block.setSpecId(id);
    return id;
}

bool detectHubKind(const Canvas::CanvasBlock& block, DesignLinkHubKind& outKind)
{
    auto* content = dynamic_cast<Canvas::BlockContentSymbol*>(block.content());
    if (!content)
        return false;

    const QString symbol = content->symbol().trimmed();
    if (symbol == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Split).symbol) {
        outKind = DesignLinkHubKind::Split;
        return true;
    }
    if (symbol == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Join).symbol) {
        outKind = DesignLinkHubKind::Join;
        return true;
    }
    if (symbol == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Broadcast).symbol) {
        outKind = DesignLinkHubKind::Broadcast;
        return true;
    }
    if (symbol == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Distribute).symbol) {
        outKind = DesignLinkHubKind::Distribute;
        return true;
    }
    if (symbol == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Collect).symbol) {
        outKind = DesignLinkHubKind::Collect;
        return true;
    }
    return false;
}

Canvas::Support::LinkHubKind toCanvasHubKind(DesignLinkHubKind kind)
{
    switch (kind) {
        case DesignLinkHubKind::Split: return Canvas::Support::LinkHubKind::Split;
        case DesignLinkHubKind::Join: return Canvas::Support::LinkHubKind::Join;
        case DesignLinkHubKind::Broadcast: return Canvas::Support::LinkHubKind::Broadcast;
        case DesignLinkHubKind::Distribute: return Canvas::Support::LinkHubKind::Distribute;
        case DesignLinkHubKind::Collect: return Canvas::Support::LinkHubKind::Collect;
    }
    return Canvas::Support::LinkHubKind::Split;
}

std::optional<Canvas::Support::LinkWireRole> wireRoleFromHubPortRole(Canvas::PortRole role)
{
    switch (role) {
        case Canvas::PortRole::Producer:
            return Canvas::Support::LinkWireRole::Consumer;
        case Canvas::PortRole::Consumer:
            return Canvas::Support::LinkWireRole::Producer;
        case Canvas::PortRole::Dynamic:
            break;
    }
    return std::nullopt;
}

DesignLink::ObjectFifo::Operation toDesignObjectFifoOperation(Canvas::CanvasWire::ObjectFifoOperation operation)
{
    switch (operation) {
        case Canvas::CanvasWire::ObjectFifoOperation::Forward:
            return DesignLink::ObjectFifo::Operation::Forward;
        case Canvas::CanvasWire::ObjectFifoOperation::Fill:
            return DesignLink::ObjectFifo::Operation::Fill;
        case Canvas::CanvasWire::ObjectFifoOperation::Drain:
            return DesignLink::ObjectFifo::Operation::Drain;
        case Canvas::CanvasWire::ObjectFifoOperation::Split:
            return DesignLink::ObjectFifo::Operation::Split;
        case Canvas::CanvasWire::ObjectFifoOperation::Join:
            return DesignLink::ObjectFifo::Operation::Join;
        case Canvas::CanvasWire::ObjectFifoOperation::Fifo:
            return DesignLink::ObjectFifo::Operation::Fifo;
    }
    return DesignLink::ObjectFifo::Operation::Fifo;
}

Canvas::CanvasWire::ObjectFifoOperation toCanvasObjectFifoOperation(DesignLink::ObjectFifo::Operation operation)
{
    switch (operation) {
        case DesignLink::ObjectFifo::Operation::Forward:
            return Canvas::CanvasWire::ObjectFifoOperation::Forward;
        case DesignLink::ObjectFifo::Operation::Fill:
            return Canvas::CanvasWire::ObjectFifoOperation::Fill;
        case DesignLink::ObjectFifo::Operation::Drain:
            return Canvas::CanvasWire::ObjectFifoOperation::Drain;
        case DesignLink::ObjectFifo::Operation::Split:
            return Canvas::CanvasWire::ObjectFifoOperation::Split;
        case DesignLink::ObjectFifo::Operation::Join:
            return Canvas::CanvasWire::ObjectFifoOperation::Join;
        case DesignLink::ObjectFifo::Operation::Fifo:
            return Canvas::CanvasWire::ObjectFifoOperation::Fifo;
    }
    return Canvas::CanvasWire::ObjectFifoOperation::Fifo;
}

QString portKey(const DesignPort& port)
{
    const QString name = port.hasPairId ? port.pairId : (port.hasName ? port.name : QString());
    return QStringLiteral("%1|%2|%3|%4")
        .arg(QString::number(static_cast<int>(port.side)))
        .arg(QString::number(static_cast<int>(port.role)))
        .arg(QString::number(port.t, 'f', 6))
        .arg(name);
}

Utils::Result clearDesignState(Canvas::CanvasDocument& doc)
{
    QVector<Canvas::ObjectId> wireIds;
    QVector<Canvas::ObjectId> hubIds;
    QVector<Canvas::ObjectId> portBlockIds;

    for (const auto& item : doc.items()) {
        if (!item)
            continue;
        if (dynamic_cast<Canvas::CanvasWire*>(item.get())) {
            wireIds.push_back(item->id());
            continue;
        }
        if (auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get())) {
            if (block->isLinkHub())
                hubIds.push_back(block->id());
            else if (block->hasPorts())
                portBlockIds.push_back(block->id());
        }
    }

    if (!wireIds.isEmpty() || !hubIds.isEmpty()) {
        auto cmd = std::make_unique<Canvas::CompositeCommand>(QStringLiteral("Clear Design"));
        for (const auto& id : wireIds)
            cmd->add(std::make_unique<Canvas::DeleteItemCommand>(id));
        for (const auto& id : hubIds)
            cmd->add(std::make_unique<Canvas::DeleteItemCommand>(id));

        if (!cmd->empty())
            doc.commands().execute(std::move(cmd));
    }

    if (!portBlockIds.isEmpty()) {
        bool changed = false;
        for (const auto& id : portBlockIds) {
            auto* block = dynamic_cast<Canvas::CanvasBlock*>(doc.findItem(id));
            if (!block || !block->hasPorts())
                continue;
            block->setPorts({});
            changed = true;
        }
        if (changed)
            doc.notifyChanged();
    }
    return Utils::Result::success();
}

} // namespace

Utils::Result buildDesignStateFromCanvas(Canvas::CanvasDocument& doc,
                                         Canvas::CanvasView* view,
                                         const QJsonObject& metadata,
                                         DesignState& out)
{
    out = DesignState{};
    out.metadata = metadata;
    if (view) {
        out.view.zoom = view->zoom();
        out.view.pan = view->pan();
    }

    QHash<Canvas::ObjectId, QString> nodeIds;
    nodeIds.reserve(static_cast<int>(doc.items().size()));

    for (const auto& item : doc.items()) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block)
            continue;

        const QString specId = block->specId().trimmed();
        if (block->isLinkHub()) {
            DesignLinkHubKind hubKind;
            if (!detectHubKind(*block, hubKind))
                return Utils::Result::failure(QStringLiteral("Unknown link hub symbol."));

            DesignNode node;
            node.id = hubIdForBlock(*block);
            node.kind = DesignNodeKind::LinkHub;
            node.hubKind = hubKind;
            node.hasHubKind = true;
            node.bounds = block->boundsScene();
            node.hasBounds = true;
            out.nodes.push_back(node);
            nodeIds.insert(block->id(), node.id);
        } else if (!specId.isEmpty()) {
            nodeIds.insert(block->id(), specId);
        }
    }

    QHash<QString, QString> legacyPairIds;
    for (const auto& item : doc.items()) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block || !block->hasPorts())
            continue;
        for (const auto& port : block->ports()) {
            if (!Canvas::Support::isLegacyPairedPortName(port.name))
                continue;
            const auto key = Canvas::Support::pairedPortKey(port);
            if (!key || key->isEmpty())
                continue;
            if (!legacyPairIds.contains(*key))
                legacyPairIds.insert(*key, QUuid::createUuid().toString(QUuid::WithoutBraces));
        }
    }

    auto resolvePairId = [&](const Canvas::CanvasPort& meta) -> std::optional<QString> {
        if (Canvas::Support::isPairedPortName(meta.name)) {
            const auto key = Canvas::Support::pairedPortKey(meta);
            if (key && !key->isEmpty())
                return *key;
        } else if (Canvas::Support::isLegacyPairedPortName(meta.name)) {
            const auto key = Canvas::Support::pairedPortKey(meta);
            if (key && legacyPairIds.contains(*key))
                return legacyPairIds.value(*key);
        }

        const QString legacyKey = meta.id.toString();
        if (legacyPairIds.contains(legacyKey))
            return legacyPairIds.value(legacyKey);

        return std::nullopt;
    };

    for (const auto& item : doc.items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire)
            continue;

        const auto& a = wire->a();
        const auto& b = wire->b();
        if (!a.attached.has_value() || !b.attached.has_value())
            continue;

        Canvas::CanvasPort aMeta;
        Canvas::CanvasPort bMeta;
        if (!doc.getPort(a.attached->itemId, a.attached->portId, aMeta) ||
            !doc.getPort(b.attached->itemId, b.attached->portId, bMeta)) {
            return Utils::Result::failure(QStringLiteral("Failed to resolve wire ports."));
        }

        const QString fromNode = nodeIds.value(a.attached->itemId);
        const QString toNode = nodeIds.value(b.attached->itemId);
        if (fromNode.isEmpty() || toNode.isEmpty())
            return Utils::Result::failure(QStringLiteral("Wire endpoint missing node id."));

        DesignLink link;
        link.id = wire->id().toString();
        link.from.nodeId = fromNode;
        link.from.port.side = aMeta.side;
        link.from.port.role = aMeta.role;
        link.from.port.t = aMeta.t;
        if (const auto pairId = resolvePairId(aMeta)) {
            link.from.port.pairId = *pairId;
            link.from.port.hasPairId = true;
        } else if (!aMeta.name.isEmpty()) {
            link.from.port.name = aMeta.name;
            link.from.port.hasName = true;
        }

        link.to.nodeId = toNode;
        link.to.port.side = bMeta.side;
        link.to.port.role = bMeta.role;
        link.to.port.t = bMeta.t;
        if (const auto pairId = resolvePairId(bMeta)) {
            link.to.port.pairId = *pairId;
            link.to.port.hasPairId = true;
        } else if (!bMeta.name.isEmpty()) {
            link.to.port.name = bMeta.name;
            link.to.port.hasName = true;
        }

        if (wire->hasRouteOverride()) {
            const auto& route = wire->routeOverride();
            link.routeOverride.reserve(static_cast<int>(route.size()));
            for (const auto& coord : route)
                link.routeOverride.push_back(coord);
        }

        if (wire->hasObjectFifo()) {
            const auto& objectFifo = wire->objectFifo().value();
            link.objectFifo.name = objectFifo.name;
            link.objectFifo.depth = objectFifo.depth;
            link.objectFifo.operation = toDesignObjectFifoOperation(objectFifo.operation);
            link.objectFifo.type.typeId = objectFifo.type.typeId;
            link.objectFifo.type.tapSymbolId = objectFifo.type.tapSymbolId;
            link.objectFifo.type.dimensions = objectFifo.type.dimensions;
            link.objectFifo.type.valueType = objectFifo.type.valueType;
            link.objectFifo.hubName = objectFifo.hubName;
            link.hasObjectFifo = true;
        }

        out.links.push_back(link);
    }

    return Utils::Result::success();
}

Utils::Result applyDesignStateToCanvas(const DesignState& state,
                                       Canvas::CanvasDocument& doc,
                                       Canvas::CanvasView* view)
{
    Utils::Result clearResult = clearDesignState(doc);
    if (!clearResult)
        return clearResult;

    qCDebug(aiedesignstatelog) << "applyDesignStateToCanvas:"
                               << "nodes=" << state.nodes.size()
                               << "links=" << state.links.size()
                               << "docItems=" << doc.items().size();

    if (view) {
        view->setZoom(state.view.zoom);
        view->setPan(state.view.pan);
    }

    QHash<QString, Canvas::ObjectId> nodeMap;
    QHash<QString, DesignNodeKind> nodeKinds;
    nodeMap.reserve(state.nodes.size() + static_cast<int>(doc.items().size()));
    nodeKinds.reserve(state.nodes.size());

    for (const auto& item : doc.items()) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block || block->isLinkHub())
            continue;
        const QString specId = block->specId().trimmed();
        if (!specId.isEmpty())
        nodeMap.insert(specId, block->id());
    }

    for (const auto& node : state.nodes) {
        nodeKinds.insert(node.id, node.kind);
        if (node.kind != DesignNodeKind::LinkHub)
            continue;
        if (!node.hasBounds || !node.hasHubKind)
            return Utils::Result::failure(QStringLiteral("Link hub missing bounds or kind."));

        auto hub = std::make_unique<Canvas::CanvasBlock>(node.bounds, true, QString());
        hub->setShowPorts(false);
        hub->setAutoPortLayout(false);
        hub->setPortSnapStep(Canvas::Constants::kGridStep);
        hub->setLinkHub(true);
        hub->setKeepoutMargin(0.0);
        hub->setContentPadding(QMarginsF(0.0, 0.0, 0.0, 0.0));
        hub->setId(doc.allocateId());
        hub->setSpecId(node.id);

        const auto style = Canvas::Support::linkHubStyle(toCanvasHubKind(node.hubKind));
        hub->setCustomColors(style.outline, style.fill, style.text);

        Canvas::SymbolContentStyle symbolStyle;
        symbolStyle.text = style.text;
        auto content = std::make_unique<Canvas::BlockContentSymbol>(style.symbol, symbolStyle);
        hub->setContent(std::move(content));

        const Canvas::ObjectId hubId = hub->id();
        doc.commands().execute(std::make_unique<Canvas::CreateItemCommand>(std::move(hub)));
        nodeMap.insert(node.id, hubId);
    }

    QHash<QString, QHash<QString, Canvas::PortId>> portMap;
    portMap.reserve(nodeMap.size());
    QHash<QString, QString> legacyPairIds;

    auto resolvePortName = [&](const DesignPort& port) -> QString {
        if (port.hasPairId && !port.pairId.isEmpty())
            return Canvas::Support::pairedPortName(port.pairId);
        if (!port.hasName || port.name.isEmpty())
            return QString();

        if (Canvas::Support::isLegacyPairedPortName(port.name)) {
            const auto key = Canvas::Support::pairedPortKeyFromName(port.name);
            if (!key || key->isEmpty())
                return QString();
            QString pairId = legacyPairIds.value(*key);
            if (pairId.isEmpty()) {
                pairId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                legacyPairIds.insert(*key, pairId);
            }
            return Canvas::Support::pairedPortName(pairId);
        }

        if (Canvas::Support::isPairedPortName(port.name))
            return port.name;

        return port.name;
    };

    auto resolvePort = [&](const DesignEndpoint& endpoint, Canvas::PortRef& out) -> Utils::Result {
        const auto it = nodeMap.find(endpoint.nodeId);
        if (it == nodeMap.end())
            return Utils::Result::failure(QStringLiteral("Unknown node id: %1").arg(endpoint.nodeId));
        const Canvas::ObjectId itemId = it.value();
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(doc.findItem(itemId));
        if (!block)
            return Utils::Result::failure(QStringLiteral("Node id not a block: %1").arg(endpoint.nodeId));

        const QString key = portKey(endpoint.port);
        Canvas::PortId portId;
        auto& nodePorts = portMap[endpoint.nodeId];
        if (nodePorts.contains(key)) {
            portId = nodePorts.value(key);
        } else {
            const QString portName = resolvePortName(endpoint.port);
            portId = block->addPort(endpoint.port.side, endpoint.port.t,
                                    endpoint.port.role,
                                    portName);
            if (portId.isNull())
                return Utils::Result::failure(QStringLiteral("Failed to create port for %1.").arg(endpoint.nodeId));
            nodePorts.insert(key, portId);
        }

        out.itemId = itemId;
        out.portId = portId;
        return Utils::Result::success();
    };

    for (const auto& link : state.links) {
        Canvas::PortRef from{};
        Canvas::PortRef to{};
        Utils::Result fromResult = resolvePort(link.from, from);
        if (!fromResult)
            return fromResult;
        Utils::Result toResult = resolvePort(link.to, to);
        if (!toResult)
            return toResult;

        Canvas::CanvasWire::Endpoint a{from, QPointF()};
        Canvas::CanvasWire::Endpoint b{to, QPointF()};
        auto wire = std::make_unique<Canvas::CanvasWire>(a, b);
        wire->setId(doc.allocateId());
        if (!link.routeOverride.isEmpty()) {
            std::vector<Canvas::FabricCoord> route;
            route.reserve(link.routeOverride.size());
            for (const auto& coord : link.routeOverride)
                route.push_back(coord);
            wire->setRouteOverride(std::move(route));
        }
        if (link.hasObjectFifo) {
            Canvas::CanvasWire::ObjectFifoConfig objectFifo;
            objectFifo.name = link.objectFifo.name;
            objectFifo.depth = link.objectFifo.depth;
            objectFifo.operation = toCanvasObjectFifoOperation(link.objectFifo.operation);
            objectFifo.type.typeId = link.objectFifo.type.typeId;
            objectFifo.type.tapSymbolId = link.objectFifo.type.tapSymbolId;
            objectFifo.type.dimensions = link.objectFifo.type.dimensions;
            objectFifo.type.valueType = link.objectFifo.type.valueType;
            objectFifo.hubName = link.objectFifo.hubName;
            wire->setObjectFifo(objectFifo);
        }

        std::optional<Canvas::Support::LinkWireRole> hubRole;
        if (nodeKinds.value(link.from.nodeId, DesignNodeKind::Tile) == DesignNodeKind::LinkHub)
            hubRole = wireRoleFromHubPortRole(link.from.port.role);
        if (!hubRole && nodeKinds.value(link.to.nodeId, DesignNodeKind::Tile) == DesignNodeKind::LinkHub)
            hubRole = wireRoleFromHubPortRole(link.to.port.role);
        if (hubRole) {
            const auto style = Canvas::Support::linkWireStyle(*hubRole);
            wire->setColorOverride(style.color);
        }

        doc.commands().execute(std::make_unique<Canvas::CreateItemCommand>(std::move(wire)));
    }

    bool portsRebound = false;
    for (const auto& item : doc.items()) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block || !block->autoOppositeProducerPort() || !block->hasPorts())
            continue;

        struct PortSnapshot final {
            Canvas::PortId id{};
            Canvas::PortSide side = Canvas::PortSide::Left;
            double t = 0.5;
        };

        QHash<QString, PortSnapshot> producerByKey;
        QVector<PortSnapshot> producersWithoutKey;
        QVector<Canvas::PortId> orphanProducers;
        QHash<QString, Canvas::PortId> consumersByKey;
        QVector<PortSnapshot> consumersWithoutKey;
        for (const auto& port : block->ports()) {
            if (const auto key = Canvas::Support::pairedPortKey(port); key && !key->isEmpty()) {
                if (port.role == Canvas::PortRole::Consumer)
                    consumersByKey.insert(*key, port.id);
                else if (port.role == Canvas::PortRole::Producer)
                    producerByKey.insert(*key, PortSnapshot{port.id, port.side, port.t});
            } else if (port.role == Canvas::PortRole::Consumer) {
                consumersWithoutKey.push_back(PortSnapshot{port.id, port.side, port.t});
            } else if (port.role == Canvas::PortRole::Producer) {
                producersWithoutKey.push_back(PortSnapshot{port.id, port.side, port.t});
            }
        }

        auto snapshotFor = [&](Canvas::PortId id) {
            for (const auto& port : block->ports()) {
                if (port.id == id)
                    return PortSnapshot{port.id, port.side, port.t};
            }
            return PortSnapshot{id, Canvas::PortSide::Left, 0.5};
        };

        auto matchesProducer = [](const PortSnapshot& consumer, const PortSnapshot& producer) {
            if (consumer.side != Canvas::Support::oppositeSide(producer.side))
                return false;
            return std::abs(consumer.t - producer.t) <= 1e-4;
        };

        for (auto it = producerByKey.begin(); it != producerByKey.end(); ++it) {
            if (consumersByKey.contains(it.key()))
                continue;
            for (int i = 0; i < consumersWithoutKey.size(); ++i) {
                const auto consumer = consumersWithoutKey.at(i);
                if (!matchesProducer(consumer, it.value()))
                    continue;
                if (block->updatePortName(consumer.id, Canvas::Support::pairedPortName(it.key()))) {
                    consumersByKey.insert(it.key(), consumer.id);
                    portsRebound = true;
                }
                consumersWithoutKey.removeAt(i);
                break;
            }
        }

        for (auto it = producerByKey.begin(); it != producerByKey.end(); ++it) {
            if (!consumersByKey.contains(it.key()))
                orphanProducers.push_back(it.value().id);
        }

        for (int i = consumersWithoutKey.size() - 1; i >= 0; --i) {
            const auto consumer = consumersWithoutKey.at(i);
            bool matched = false;
            for (int p = 0; p < producersWithoutKey.size(); ++p) {
                const auto producer = producersWithoutKey.at(p);
                if (!matchesProducer(consumer, producer))
                    continue;

                const QString pairKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
                if (block->updatePortName(consumer.id, Canvas::Support::pairedPortName(pairKey)))
                    portsRebound = true;
                if (block->updatePortName(producer.id, Canvas::Support::pairedPortName(pairKey)))
                    portsRebound = true;

                consumersByKey.insert(pairKey, consumer.id);
                producerByKey.insert(pairKey, producer);
                producersWithoutKey.removeAt(p);
                consumersWithoutKey.removeAt(i);
                matched = true;
                break;
            }
            if (!matched) {
                Canvas::Support::ensureOppositeProducerPort(doc, block->id(), consumer.id);
                consumersWithoutKey.removeAt(i);
            }
        }

        for (auto it = consumersByKey.begin(); it != consumersByKey.end(); ++it) {
            const QString key = it.key();
            const Canvas::PortId consumerId = it.value();
            if (producerByKey.contains(key))
                continue;
            if (!orphanProducers.isEmpty()) {
                const Canvas::PortId orphanId = orphanProducers.takeFirst();
                if (block->updatePortName(orphanId, Canvas::Support::pairedPortName(key))) {
                    producerByKey.insert(key, snapshotFor(orphanId));
                    portsRebound = true;
                }
            } else {
                Canvas::Support::ensureOppositeProducerPort(doc, block->id(), consumerId);
            }
        }
    }

    if (portsRebound)
        doc.notifyChanged();

    return Utils::Result::success();
}

} // namespace Aie::Internal
