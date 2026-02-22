// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/design/DesignStateJson.hpp"

#include <QtCore/QJsonArray>

using Aie::Internal::DesignState;
using Aie::Internal::DesignNode;
using Aie::Internal::DesignNodeKind;
using Aie::Internal::DesignLink;
using Aie::Internal::DesignLinkHubKind;
using Aie::Internal::DesignPort;

namespace {

DesignNode* findNode(QVector<DesignNode>& nodes, const QString& id)
{
    for (auto& node : nodes) {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

DesignLink* findLink(QVector<DesignLink>& links, const QString& id)
{
    for (auto& link : links) {
        if (link.id == id)
            return &link;
    }
    return nullptr;
}

} // namespace

TEST(DesignStateJsonTests, RoundTripSerializeParse)
{
    DesignState state;
    state.schemaVersion = 1;
    state.view.zoom = 1.25;
    state.view.pan = QPointF(12.0, -8.0);

    DesignNode hub;
    hub.id = QStringLiteral("hub-1");
    hub.kind = DesignNodeKind::LinkHub;
    hub.hubKind = DesignLinkHubKind::Split;
    hub.hasHubKind = true;
    hub.bounds = QRectF(10.0, 20.0, 32.0, 32.0);
    hub.hasBounds = true;
    state.nodes.push_back(hub);

    DesignNode tile;
    tile.id = QStringLiteral("aie0_0");
    tile.kind = DesignNodeKind::Tile;
    tile.coord = Canvas::GridCoord{0, 0};
    tile.hasCoord = true;
    tile.kernelRef = QStringLiteral("kernel:add");
    tile.hasKernelRef = true;
    state.nodes.push_back(tile);

    DesignLink link;
    link.id = QStringLiteral("wire-1");
    link.from.nodeId = tile.id;
    link.from.port.side = Canvas::PortSide::Right;
    link.from.port.role = Canvas::PortRole::Producer;
    link.from.port.t = 0.25;
    link.from.port.pairId = QStringLiteral("pair-1");
    link.from.port.hasPairId = true;
    link.to.nodeId = hub.id;
    link.to.port.side = Canvas::PortSide::Left;
    link.to.port.role = Canvas::PortRole::Consumer;
    link.to.port.t = 0.75;
    link.routeOverride = { Canvas::FabricCoord{1, 2}, Canvas::FabricCoord{1, 3} };
    link.objectFifo.name = QStringLiteral("in");
    link.objectFifo.depth = 3;
    link.objectFifo.operation = Aie::Internal::DesignLink::ObjectFifo::Operation::Forward;
    link.objectFifo.type.valueType = QStringLiteral("i32");
    link.objectFifo.type.dimensions = QStringLiteral("(M, N)");
    link.hasObjectFifo = true;
    state.links.push_back(link);

    QJsonObject metadata;
    metadata.insert(QStringLiteral("notes"), QStringLiteral("test"));
    metadata.insert(QStringLiteral("tags"), QJsonArray{QStringLiteral("a"), QStringLiteral("b")});
    state.metadata = metadata;

    const QJsonObject json = Aie::Internal::serializeDesignState(state);

    DesignState parsed;
    const Utils::Result result = Aie::Internal::parseDesignState(json, parsed);
    ASSERT_TRUE(result.ok) << result.errors.join("\n").toStdString();

    EXPECT_EQ(parsed.schemaVersion, 1);
    EXPECT_DOUBLE_EQ(parsed.view.zoom, 1.25);
    EXPECT_DOUBLE_EQ(parsed.view.pan.x(), 12.0);
    EXPECT_DOUBLE_EQ(parsed.view.pan.y(), -8.0);

    EXPECT_EQ(parsed.nodes.size(), 2);
    DesignNode* parsedHub = findNode(parsed.nodes, QStringLiteral("hub-1"));
    ASSERT_NE(parsedHub, nullptr);
    EXPECT_EQ(parsedHub->kind, DesignNodeKind::LinkHub);
    EXPECT_TRUE(parsedHub->hasHubKind);
    EXPECT_EQ(parsedHub->hubKind, DesignLinkHubKind::Split);
    EXPECT_TRUE(parsedHub->hasBounds);
    EXPECT_DOUBLE_EQ(parsedHub->bounds.x(), 10.0);
    EXPECT_DOUBLE_EQ(parsedHub->bounds.width(), 32.0);

    DesignNode* parsedTile = findNode(parsed.nodes, QStringLiteral("aie0_0"));
    ASSERT_NE(parsedTile, nullptr);
    EXPECT_EQ(parsedTile->kind, DesignNodeKind::Tile);
    EXPECT_TRUE(parsedTile->hasCoord);
    EXPECT_EQ(parsedTile->coord.x, 0);
    EXPECT_EQ(parsedTile->coord.y, 0);
    EXPECT_TRUE(parsedTile->hasKernelRef);
    EXPECT_EQ(parsedTile->kernelRef, QStringLiteral("kernel:add"));

    EXPECT_EQ(parsed.links.size(), 1);
    DesignLink* parsedLink = findLink(parsed.links, QStringLiteral("wire-1"));
    ASSERT_NE(parsedLink, nullptr);
    EXPECT_EQ(parsedLink->from.nodeId, QStringLiteral("aie0_0"));
    EXPECT_EQ(parsedLink->to.nodeId, QStringLiteral("hub-1"));
    EXPECT_EQ(parsedLink->from.port.side, Canvas::PortSide::Right);
    EXPECT_EQ(parsedLink->to.port.side, Canvas::PortSide::Left);
    EXPECT_EQ(parsedLink->from.port.role, Canvas::PortRole::Producer);
    EXPECT_EQ(parsedLink->to.port.role, Canvas::PortRole::Consumer);
    EXPECT_NEAR(parsedLink->from.port.t, 0.25, 1e-6);
    EXPECT_NEAR(parsedLink->to.port.t, 0.75, 1e-6);
    EXPECT_TRUE(parsedLink->from.port.hasPairId);
    EXPECT_EQ(parsedLink->from.port.pairId, QStringLiteral("pair-1"));
    ASSERT_EQ(parsedLink->routeOverride.size(), 2);
    EXPECT_EQ(parsedLink->routeOverride[0].x, 1);
    EXPECT_EQ(parsedLink->routeOverride[0].y, 2);
    EXPECT_TRUE(parsedLink->hasObjectFifo);
    EXPECT_EQ(parsedLink->objectFifo.name, QStringLiteral("in"));
    EXPECT_EQ(parsedLink->objectFifo.depth, 3);
    EXPECT_EQ(parsedLink->objectFifo.operation, Aie::Internal::DesignLink::ObjectFifo::Operation::Forward);
    EXPECT_EQ(parsedLink->objectFifo.type.valueType, QStringLiteral("i32"));
    EXPECT_EQ(parsedLink->objectFifo.type.dimensions, QStringLiteral("(M, N)"));

    EXPECT_EQ(parsed.metadata.value(QStringLiteral("notes")).toString(), QStringLiteral("test"));
}
