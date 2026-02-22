// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/design/DesignStateJson.hpp"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

QString nodeKindToString(DesignNodeKind kind)
{
    switch (kind) {
        case DesignNodeKind::Tile: return u"tile"_s;
        case DesignNodeKind::LinkHub: return u"linkHub"_s;
    }
    return u"tile"_s;
}

bool nodeKindFromString(const QString& text, DesignNodeKind& out)
{
    const QString key = text.trimmed().toLower();
    if (key == u"tile"_s) {
        out = DesignNodeKind::Tile;
        return true;
    }
    if (key == u"linkhub"_s || key == u"link_hub"_s || key == u"link-hub"_s) {
        out = DesignNodeKind::LinkHub;
        return true;
    }
    return false;
}

QString hubKindToString(DesignLinkHubKind kind)
{
    switch (kind) {
        case DesignLinkHubKind::Split: return u"split"_s;
        case DesignLinkHubKind::Join: return u"join"_s;
        case DesignLinkHubKind::Broadcast: return u"broadcast"_s;
    }
    return u"split"_s;
}

bool hubKindFromString(const QString& text, DesignLinkHubKind& out)
{
    const QString key = text.trimmed().toLower();
    if (key == u"split"_s) {
        out = DesignLinkHubKind::Split;
        return true;
    }
    if (key == u"join"_s) {
        out = DesignLinkHubKind::Join;
        return true;
    }
    if (key == u"broadcast"_s) {
        out = DesignLinkHubKind::Broadcast;
        return true;
    }
    return false;
}

QString portSideToString(Canvas::PortSide side)
{
    switch (side) {
        case Canvas::PortSide::Left: return u"left"_s;
        case Canvas::PortSide::Right: return u"right"_s;
        case Canvas::PortSide::Top: return u"top"_s;
        case Canvas::PortSide::Bottom: return u"bottom"_s;
    }
    return u"left"_s;
}

bool portSideFromString(const QString& text, Canvas::PortSide& out)
{
    const QString key = text.trimmed().toLower();
    if (key == u"left"_s) {
        out = Canvas::PortSide::Left;
        return true;
    }
    if (key == u"right"_s) {
        out = Canvas::PortSide::Right;
        return true;
    }
    if (key == u"top"_s) {
        out = Canvas::PortSide::Top;
        return true;
    }
    if (key == u"bottom"_s) {
        out = Canvas::PortSide::Bottom;
        return true;
    }
    return false;
}

QString portRoleToString(Canvas::PortRole role)
{
    switch (role) {
        case Canvas::PortRole::Producer: return u"producer"_s;
        case Canvas::PortRole::Consumer: return u"consumer"_s;
        case Canvas::PortRole::Dynamic: return u"dynamic"_s;
    }
    return u"dynamic"_s;
}

bool portRoleFromString(const QString& text, Canvas::PortRole& out)
{
    const QString key = text.trimmed().toLower();
    if (key == u"producer"_s) {
        out = Canvas::PortRole::Producer;
        return true;
    }
    if (key == u"consumer"_s) {
        out = Canvas::PortRole::Consumer;
        return true;
    }
    if (key == u"dynamic"_s) {
        out = Canvas::PortRole::Dynamic;
        return true;
    }
    return false;
}

QString objectFifoOperationToString(DesignLink::ObjectFifo::Operation operation)
{
    switch (operation) {
        case DesignLink::ObjectFifo::Operation::Forward: return u"forward"_s;
        case DesignLink::ObjectFifo::Operation::Fifo: return u"fifo"_s;
    }
    return u"fifo"_s;
}

DesignLink::ObjectFifo::Operation objectFifoOperationFromString(const QString& text)
{
    const QString key = text.trimmed().toLower();
    if (key == u"forward"_s || key == u"fwd"_s || key == u"forward_fifo"_s || key == u"forward-fifo"_s)
        return DesignLink::ObjectFifo::Operation::Forward;
    return DesignLink::ObjectFifo::Operation::Fifo;
}

QJsonObject pointObject(const QPointF& point)
{
    QJsonObject obj;
    obj.insert(u"x"_s, point.x());
    obj.insert(u"y"_s, point.y());
    return obj;
}

QJsonObject rectObject(const QRectF& rect)
{
    QJsonObject obj;
    obj.insert(u"x"_s, rect.x());
    obj.insert(u"y"_s, rect.y());
    obj.insert(u"w"_s, rect.width());
    obj.insert(u"h"_s, rect.height());
    return obj;
}

QJsonObject coordObject(const Canvas::FabricCoord& coord)
{
    QJsonObject obj;
    obj.insert(u"x"_s, coord.x);
    obj.insert(u"y"_s, coord.y);
    return obj;
}

QJsonObject gridCoordObject(const Canvas::GridCoord& coord)
{
    QJsonObject obj;
    obj.insert(u"x"_s, coord.x);
    obj.insert(u"y"_s, coord.y);
    return obj;
}

} // namespace

QJsonObject serializeDesignState(const DesignState& state)
{
    QJsonObject root;
    root.insert(u"schemaVersion"_s, state.schemaVersion);

    QJsonObject view;
    view.insert(u"zoom"_s, state.view.zoom);
    view.insert(u"pan"_s, pointObject(state.view.pan));
    QJsonObject canvas;
    canvas.insert(u"view"_s, view);
    root.insert(u"canvas"_s, canvas);

    QJsonArray nodes;
    for (const auto& node : state.nodes) {
        if (node.id.trimmed().isEmpty())
            continue;
        QJsonObject obj;
        obj.insert(u"id"_s, node.id);
        obj.insert(u"kind"_s, nodeKindToString(node.kind));
        if (node.hasCoord)
            obj.insert(u"coord"_s, gridCoordObject(node.coord));
        if (node.hasKernelRef)
            obj.insert(u"kernelRef"_s, node.kernelRef);
        if (node.hasHubKind)
            obj.insert(u"hubKind"_s, hubKindToString(node.hubKind));
        if (node.hasBounds)
            obj.insert(u"bounds"_s, rectObject(node.bounds));
        nodes.append(obj);
    }
    root.insert(u"nodes"_s, nodes);

    QJsonArray links;
    for (const auto& link : state.links) {
        QJsonObject obj;
        if (!link.id.trimmed().isEmpty())
            obj.insert(u"id"_s, link.id);

        auto writeEndpoint = [](const DesignEndpoint& ep) {
            QJsonObject endpoint;
            endpoint.insert(u"nodeId"_s, ep.nodeId);
            QJsonObject port;
            port.insert(u"side"_s, portSideToString(ep.port.side));
            port.insert(u"t"_s, ep.port.t);
            port.insert(u"role"_s, portRoleToString(ep.port.role));
            if (ep.port.hasName)
                port.insert(u"name"_s, ep.port.name);
            if (ep.port.hasPairId)
                port.insert(u"pairId"_s, ep.port.pairId);
            endpoint.insert(u"port"_s, port);
            return endpoint;
        };

        obj.insert(u"from"_s, writeEndpoint(link.from));
        obj.insert(u"to"_s, writeEndpoint(link.to));

        if (!link.routeOverride.isEmpty()) {
            QJsonArray route;
            for (const auto& coord : link.routeOverride)
                route.append(coordObject(coord));
            obj.insert(u"routeOverride"_s, route);
        }

        if (link.hasObjectFifo) {
            QJsonObject objectFifo;
            objectFifo.insert(u"name"_s, link.objectFifo.name);
            objectFifo.insert(u"depth"_s, link.objectFifo.depth);
            objectFifo.insert(u"operation"_s, objectFifoOperationToString(link.objectFifo.operation));
            objectFifo.insert(u"dimensions"_s, link.objectFifo.type.dimensions);
            objectFifo.insert(u"valueType"_s, link.objectFifo.type.valueType);
            obj.insert(u"objectFifo"_s, objectFifo);
        }

        links.append(obj);
    }
    root.insert(u"links"_s, links);

    if (!state.metadata.isEmpty())
        root.insert(u"metadata"_s, state.metadata);

    return root;
}

Utils::Result parseDesignState(const QJsonObject& json, DesignState& out)
{
    out = DesignState{};
    QStringList errors;

    const QJsonValue schemaValue = json.value(u"schemaVersion"_s);
    if (!schemaValue.isUndefined()) {
        if (!schemaValue.isDouble()) {
            errors.push_back(QStringLiteral("schemaVersion must be a number."));
        } else {
            const int version = schemaValue.toInt();
            if (version != 1)
                errors.push_back(QStringLiteral("Unsupported schemaVersion: %1").arg(version));
            out.schemaVersion = version;
        }
    }

    const QJsonObject canvas = json.value(u"canvas"_s).toObject();
    const QJsonObject view = canvas.value(u"view"_s).toObject();
    if (!view.isEmpty()) {
        const QJsonValue zoomValue = view.value(u"zoom"_s);
        if (!zoomValue.isUndefined()) {
            if (!zoomValue.isDouble())
                errors.push_back(QStringLiteral("canvas.view.zoom must be a number."));
            else
                out.view.zoom = zoomValue.toDouble();
        }
        const QJsonObject pan = view.value(u"pan"_s).toObject();
        if (!pan.isEmpty()) {
            const QJsonValue x = pan.value(u"x"_s);
            const QJsonValue y = pan.value(u"y"_s);
            if (x.isDouble() && y.isDouble())
                out.view.pan = QPointF(x.toDouble(), y.toDouble());
            else
                errors.push_back(QStringLiteral("canvas.view.pan must contain numeric x/y."));
        }
    }

    const QJsonValue nodesValue = json.value(u"nodes"_s);
    if (!nodesValue.isUndefined()) {
        if (!nodesValue.isArray()) {
            errors.push_back(QStringLiteral("nodes must be an array."));
        } else {
            const QJsonArray nodes = nodesValue.toArray();
            out.nodes.reserve(nodes.size());
            for (int i = 0; i < nodes.size(); ++i) {
                const QJsonObject obj = nodes.at(i).toObject();
                const QString id = obj.value(u"id"_s).toString();
                const QString kindStr = obj.value(u"kind"_s).toString();
                if (id.trimmed().isEmpty() || kindStr.trimmed().isEmpty()) {
                    errors.push_back(QStringLiteral("nodes[%1] missing id/kind.").arg(i));
                    continue;
                }

                DesignNodeKind kind;
                if (!nodeKindFromString(kindStr, kind)) {
                    errors.push_back(QStringLiteral("nodes[%1] has unknown kind '%2'.").arg(i).arg(kindStr));
                    continue;
                }

                DesignNode node;
                node.id = id;
                node.kind = kind;

                const QJsonObject coord = obj.value(u"coord"_s).toObject();
                if (!coord.isEmpty()) {
                    const QJsonValue x = coord.value(u"x"_s);
                    const QJsonValue y = coord.value(u"y"_s);
                    if (x.isDouble() && y.isDouble()) {
                        node.coord = Canvas::GridCoord{x.toInt(), y.toInt()};
                        node.hasCoord = true;
                    } else {
                        errors.push_back(QStringLiteral("nodes[%1].coord must have numeric x/y.").arg(i));
                    }
                }

                const QJsonValue kernelRef = obj.value(u"kernelRef"_s);
                if (kernelRef.isString()) {
                    node.kernelRef = kernelRef.toString();
                    node.hasKernelRef = true;
                } else if (!kernelRef.isUndefined() && !kernelRef.isNull()) {
                    errors.push_back(QStringLiteral("nodes[%1].kernelRef must be a string.").arg(i));
                }

                const QJsonValue hubKindValue = obj.value(u"hubKind"_s);
                if (!hubKindValue.isUndefined()) {
                    if (!hubKindValue.isString()) {
                        errors.push_back(QStringLiteral("nodes[%1].hubKind must be a string.").arg(i));
                    } else {
                        DesignLinkHubKind hubKind;
                        if (!hubKindFromString(hubKindValue.toString(), hubKind)) {
                            errors.push_back(QStringLiteral("nodes[%1].hubKind invalid.").arg(i));
                        } else {
                            node.hubKind = hubKind;
                            node.hasHubKind = true;
                        }
                    }
                }

                const QJsonObject bounds = obj.value(u"bounds"_s).toObject();
                if (!bounds.isEmpty()) {
                    const QJsonValue x = bounds.value(u"x"_s);
                    const QJsonValue y = bounds.value(u"y"_s);
                    const QJsonValue w = bounds.value(u"w"_s);
                    const QJsonValue h = bounds.value(u"h"_s);
                    if (x.isDouble() && y.isDouble() && w.isDouble() && h.isDouble()) {
                        node.bounds = QRectF(x.toDouble(), y.toDouble(),
                                             w.toDouble(), h.toDouble());
                        node.hasBounds = true;
                    } else {
                        errors.push_back(QStringLiteral("nodes[%1].bounds must have numeric x/y/w/h.").arg(i));
                    }
                }

                out.nodes.push_back(node);
            }
        }
    }

    const QJsonValue linksValue = json.value(u"links"_s);
    if (!linksValue.isUndefined()) {
        if (!linksValue.isArray()) {
            errors.push_back(QStringLiteral("links must be an array."));
        } else {
            const QJsonArray links = linksValue.toArray();
            out.links.reserve(links.size());
            for (int i = 0; i < links.size(); ++i) {
                const QJsonObject obj = links.at(i).toObject();
                DesignLink link;
                link.id = obj.value(u"id"_s).toString();

                auto readEndpoint = [&](const QJsonObject& epObj, const QString& label, DesignEndpoint& ep) {
                    const QString nodeId = epObj.value(u"nodeId"_s).toString();
                    if (nodeId.trimmed().isEmpty()) {
                        errors.push_back(QStringLiteral("links[%1].%2.nodeId missing.").arg(i).arg(label));
                        return false;
                    }
                    const QJsonObject portObj = epObj.value(u"port"_s).toObject();
                    if (portObj.isEmpty()) {
                        errors.push_back(QStringLiteral("links[%1].%2.port missing.").arg(i).arg(label));
                        return false;
                    }
                    Canvas::PortSide side;
                    const QString sideStr = portObj.value(u"side"_s).toString();
                    if (!portSideFromString(sideStr, side)) {
                        errors.push_back(QStringLiteral("links[%1].%2.port.side invalid.").arg(i).arg(label));
                        return false;
                    }
                    Canvas::PortRole role = Canvas::PortRole::Dynamic;
                    const QString roleStr = portObj.value(u"role"_s).toString(u"dynamic"_s);
                    if (!portRoleFromString(roleStr, role)) {
                        errors.push_back(QStringLiteral("links[%1].%2.port.role invalid.").arg(i).arg(label));
                        return false;
                    }
                    double t = 0.5;
                    const QJsonValue tVal = portObj.value(u"t"_s);
                    if (!tVal.isUndefined()) {
                        if (!tVal.isDouble()) {
                            errors.push_back(QStringLiteral("links[%1].%2.port.t invalid.").arg(i).arg(label));
                            return false;
                        }
                        t = tVal.toDouble();
                    }
                    ep.nodeId = nodeId;
                    ep.port.side = side;
                    ep.port.role = role;
                    ep.port.t = t;
                    const QJsonValue nameVal = portObj.value(u"name"_s);
                    if (nameVal.isString()) {
                        ep.port.name = nameVal.toString();
                        ep.port.hasName = true;
                    }
                    const QJsonValue pairVal = portObj.value(u"pairId"_s);
                    if (pairVal.isString()) {
                        ep.port.pairId = pairVal.toString();
                        ep.port.hasPairId = true;
                    }
                    return true;
                };

                const QJsonObject fromObj = obj.value(u"from"_s).toObject();
                const QJsonObject toObj = obj.value(u"to"_s).toObject();
                const bool okFrom = readEndpoint(fromObj, QStringLiteral("from"), link.from);
                const bool okTo = readEndpoint(toObj, QStringLiteral("to"), link.to);
                if (!okFrom || !okTo)
                    continue;

                const QJsonValue routeVal = obj.value(u"routeOverride"_s);
                if (!routeVal.isUndefined()) {
                    if (!routeVal.isArray()) {
                        errors.push_back(QStringLiteral("links[%1].routeOverride must be an array.").arg(i));
                    } else {
                        const QJsonArray route = routeVal.toArray();
                        link.routeOverride.reserve(route.size());
                        for (int r = 0; r < route.size(); ++r) {
                            const QJsonObject coord = route.at(r).toObject();
                            const QJsonValue x = coord.value(u"x"_s);
                            const QJsonValue y = coord.value(u"y"_s);
                            if (x.isDouble() && y.isDouble()) {
                                link.routeOverride.push_back(Canvas::FabricCoord{x.toInt(), y.toInt()});
                            } else {
                                errors.push_back(QStringLiteral("links[%1].routeOverride[%2] invalid.")
                                                     .arg(i).arg(r));
                            }
                        }
                    }
                }

                const QJsonValue objectFifoValue = obj.value(u"objectFifo"_s);
                if (!objectFifoValue.isUndefined()) {
                    if (!objectFifoValue.isObject()) {
                        errors.push_back(QStringLiteral("links[%1].objectFifo must be an object.").arg(i));
                    } else {
                        const QJsonObject objectFifoObject = objectFifoValue.toObject();
                        link.objectFifo.name = objectFifoObject.value(u"name"_s).toString();
                        link.objectFifo.depth = objectFifoObject.value(u"depth"_s).toInt(2);
                        link.objectFifo.operation =
                            objectFifoOperationFromString(objectFifoObject.value(u"operation"_s).toString());
                        link.objectFifo.type.dimensions = objectFifoObject.value(u"dimensions"_s).toString();
                        link.objectFifo.type.valueType = objectFifoObject.value(u"valueType"_s).toString();
                        link.hasObjectFifo = true;
                    }
                }

                out.links.push_back(link);
            }
        }
    }

    const QJsonValue metadataVal = json.value(u"metadata"_s);
    if (metadataVal.isObject())
        out.metadata = metadataVal.toObject();

    if (!errors.isEmpty())
        return Utils::Result::failure(errors);
    return Utils::Result::success();
}

} // namespace Aie::Internal
