// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/CanvasCommands.hpp"
#include "canvas/services/CanvasLayoutEngine.hpp"
#include "canvas/utils/CanvasAutoPorts.hpp"
#include "canvas/utils/CanvasPortBindings.hpp"

#include <algorithm>
#include <cmath>

TEST(CanvasPortTests, PortAnchorsAreClampedAwayFromCorners)
{
    Canvas::CanvasBlock blk(QRectF(10.0, 20.0, 100.0, 50.0), false);

    const Canvas::PortId portA = Canvas::PortId::create();
    const Canvas::PortId portB = Canvas::PortId::create();

    std::vector<Canvas::CanvasPort> ports;
    ports.push_back(Canvas::CanvasPort{portA, Canvas::PortRole::Consumer, Canvas::PortSide::Right, 0.0, {}});
    ports.push_back(Canvas::CanvasPort{portB, Canvas::PortRole::Consumer, Canvas::PortSide::Right, 1.0, {}});
    blk.setPorts(std::move(ports));

    const QPointF a0 = blk.portAnchorScene(portA);
    const QPointF a1 = blk.portAnchorScene(portB);

    EXPECT_DOUBLE_EQ(a0.x(), blk.boundsScene().right());
    EXPECT_DOUBLE_EQ(a1.x(), blk.boundsScene().right());

    const double step = Canvas::Constants::kGridStep;
    const double raw0 = blk.boundsScene().top();
    const double raw1 = blk.boundsScene().bottom();
    const double minY = blk.boundsScene().top() + step;
    const double maxY = blk.boundsScene().bottom() - step;

    auto snap = [&](double v) -> double {
        if (step <= 0.0)
            return v;
        return std::llround(v / step) * step;
    };

    const double y0 = std::clamp(snap(raw0), minY, maxY);
    const double y1 = std::clamp(snap(raw1), minY, maxY);

    EXPECT_DOUBLE_EQ(a0.y(), y0);
    EXPECT_DOUBLE_EQ(a1.y(), y1);
}

TEST(CanvasPortTests, AutoOppositeProducerPortCreatesAndRemoves)
{
    Canvas::CanvasDocument doc;
    auto* blk = doc.createBlock(QRectF(0.0, 0.0, 40.0, 40.0), false);
    ASSERT_NE(blk, nullptr);
    blk->setAutoOppositeProducerPort(true);

    const Canvas::PortId input = blk->addPort(Canvas::PortSide::Left, 0.25, Canvas::PortRole::Consumer);
    ASSERT_TRUE(input);

    EXPECT_TRUE(Canvas::Support::ensureOppositeProducerPort(doc, blk->id(), input));

    Canvas::PortId output{};
    for (const auto& port : blk->ports()) {
        if (port.role == Canvas::PortRole::Producer &&
            port.side == Canvas::PortSide::Right &&
            std::abs(port.t - 0.25) <= 1e-4) {
            output = port.id;
            break;
        }
    }
    ASSERT_TRUE(output);

    EXPECT_FALSE(Canvas::Support::ensureOppositeProducerPort(doc, blk->id(), input));

    auto removed = Canvas::Support::removeOppositeProducerPort(doc, blk->id(), input);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->port.id, output);
}

TEST(CanvasPortTests, AutoOppositeProducerPortRetainsAttached)
{
    Canvas::CanvasDocument doc;
    auto* blk = doc.createBlock(QRectF(0.0, 0.0, 40.0, 40.0), false);
    ASSERT_NE(blk, nullptr);
    blk->setAutoOppositeProducerPort(true);

    const Canvas::PortId input = blk->addPort(Canvas::PortSide::Left, 0.5, Canvas::PortRole::Consumer);
    ASSERT_TRUE(input);

    EXPECT_TRUE(Canvas::Support::ensureOppositeProducerPort(doc, blk->id(), input));

    Canvas::PortId output{};
    for (const auto& port : blk->ports()) {
        if (port.role == Canvas::PortRole::Producer &&
            port.side == Canvas::PortSide::Right &&
            std::abs(port.t - 0.5) <= 1e-4) {
            output = port.id;
            break;
        }
    }
    ASSERT_TRUE(output);

    auto* blkB = doc.createBlock(QRectF(60.0, 0.0, 40.0, 40.0), false);
    ASSERT_NE(blkB, nullptr);
    const Canvas::PortId target = blkB->addPort(Canvas::PortSide::Left, 0.5, Canvas::PortRole::Consumer);
    ASSERT_TRUE(target);

    Canvas::CanvasWire::Endpoint a{Canvas::PortRef{blk->id(), output}, QPointF()};
    Canvas::CanvasWire::Endpoint b{Canvas::PortRef{blkB->id(), target}, QPointF()};
    auto wire = std::make_unique<Canvas::CanvasWire>(a, b);
    wire->setId(doc.allocateId());
    ASSERT_TRUE(doc.commands().execute(std::make_unique<Canvas::CreateItemCommand>(std::move(wire))));

    auto removed = Canvas::Support::removeOppositeProducerPort(doc, blk->id(), input);
    EXPECT_FALSE(removed.has_value());
}

TEST(CanvasPortTests, AutoOppositeProducerPortReusesLegacyPairWithoutDuplication)
{
    Canvas::CanvasDocument doc;
    auto* block = doc.createBlock(QRectF(0.0, 0.0, 120.0, 120.0), false);
    ASSERT_NE(block, nullptr);
    block->setAutoOppositeProducerPort(true);

    const Canvas::PortId consumerId = block->addPort(Canvas::PortSide::Top,
                                                     0.25,
                                                     Canvas::PortRole::Dynamic);
    ASSERT_TRUE(consumerId);

    const QString legacyName = QStringLiteral("__paired:%1").arg(consumerId.toString());
    const Canvas::PortId producerId = block->addPort(Canvas::PortSide::Bottom,
                                                     0.25,
                                                     Canvas::PortRole::Producer,
                                                     legacyName);
    ASSERT_TRUE(producerId);

    const size_t beforeCount = block->ports().size();
    EXPECT_FALSE(Canvas::Support::ensureOppositeProducerPort(doc, block->id(), consumerId));
    ASSERT_EQ(block->ports().size(), beforeCount);

    Canvas::CanvasPort consumerMeta;
    Canvas::CanvasPort producerMeta;
    ASSERT_TRUE(doc.getPort(block->id(), consumerId, consumerMeta));
    ASSERT_TRUE(doc.getPort(block->id(), producerId, producerMeta));
    EXPECT_TRUE(Canvas::Support::pairedPortKey(consumerMeta).has_value());
    EXPECT_TRUE(Canvas::Support::pairedPortKey(producerMeta).has_value());
    EXPECT_EQ(Canvas::Support::pairedPortKey(consumerMeta), Canvas::Support::pairedPortKey(producerMeta));
}

TEST(CanvasPortTests, LinkHubManualPortRelocationIsNotOverriddenByAutoLayout)
{
    Canvas::CanvasDocument doc;

    auto* hub = doc.createBlock(QRectF(0.0, 0.0, 80.0, 80.0), false);
    ASSERT_NE(hub, nullptr);
    hub->setLinkHub(true);
    // Simulate legacy documents that persisted link hubs with auto-port-layout enabled.
    hub->setAutoPortLayout(true);
    hub->setShowPorts(false);

    const Canvas::PortId hubPort = hub->addPort(Canvas::PortSide::Left, 0.50, Canvas::PortRole::Dynamic);
    ASSERT_TRUE(hubPort);

    auto* sink = doc.createBlock(QRectF(200.0, 0.0, 80.0, 80.0), false);
    ASSERT_NE(sink, nullptr);
    const Canvas::PortId sinkPort = sink->addPort(Canvas::PortSide::Left, 0.50, Canvas::PortRole::Dynamic);
    ASSERT_TRUE(sinkPort);

    Canvas::CanvasWire::Endpoint a;
    a.attached = Canvas::PortRef{hub->id(), hubPort};
    a.freeScene = QPointF();
    Canvas::CanvasWire::Endpoint b;
    b.attached = Canvas::PortRef{sink->id(), sinkPort};
    b.freeScene = QPointF();
    auto wire = std::make_unique<Canvas::CanvasWire>(a, b);
    wire->setId(doc.allocateId());
    ASSERT_TRUE(doc.commands().execute(std::make_unique<Canvas::CreateItemCommand>(std::move(wire))));

    ASSERT_TRUE(hub->updatePort(hubPort, Canvas::PortSide::Top, 0.85));

    Canvas::CanvasPort before{};
    ASSERT_TRUE(doc.getPort(hub->id(), hubPort, before));
    ASSERT_EQ(before.side, Canvas::PortSide::Top);
    EXPECT_DOUBLE_EQ(before.t, 0.85);

    Canvas::Services::CanvasLayoutEngine layout;
    EXPECT_FALSE(layout.arrangeAutoPorts(doc, *hub));

    Canvas::CanvasPort after{};
    ASSERT_TRUE(doc.getPort(hub->id(), hubPort, after));
    EXPECT_EQ(after.side, Canvas::PortSide::Top);
    EXPECT_DOUBLE_EQ(after.t, 0.85);
}

TEST(CanvasPortTests, BoundProducerPersistsWithInvalidBindingWhenConsumerDeleted)
{
    Canvas::CanvasDocument doc;

    auto* consumerBlock = doc.createBlock(QRectF(0.0, 0.0, 80.0, 80.0), false);
    ASSERT_NE(consumerBlock, nullptr);
    const Canvas::PortId consumerPort =
        consumerBlock->addPort(Canvas::PortSide::Bottom,
                               0.5,
                               Canvas::PortRole::Consumer,
                               QStringLiteral("in"));
    ASSERT_TRUE(consumerPort);

    Canvas::EdgeCandidate edge;
    edge.itemId = consumerBlock->id();
    edge.side = Canvas::PortSide::Left;
    edge.t = 0.4;

    Canvas::Support::BoundProducerPlacementRequest request;
    request.consumerItemId = consumerBlock->id();
    request.consumerPortId = consumerPort;
    request.producerEdge = edge;

    const auto created = Canvas::Support::createBoundProducerPort(doc, request);
    ASSERT_TRUE(created.has_value());

    Canvas::CanvasPort producerMeta;
    ASSERT_TRUE(doc.getPort(created->producerItemId, created->producerPortId, producerMeta));
    EXPECT_TRUE(producerMeta.hasBinding);
    EXPECT_EQ(producerMeta.bindingItemId, consumerBlock->id());
    EXPECT_EQ(producerMeta.bindingPortId, consumerPort);
    EXPECT_EQ(producerMeta.name, QStringLiteral("C: \"in\""));
    EXPECT_TRUE(Canvas::Support::isBoundConsumerEndpointValid(doc, producerMeta));

    ASSERT_TRUE(doc.commands().execute(
        std::make_unique<Canvas::DeletePortCommand>(consumerBlock->id(), consumerPort)));

    ASSERT_TRUE(doc.getPort(created->producerItemId, created->producerPortId, producerMeta));
    EXPECT_TRUE(producerMeta.hasBinding);
    EXPECT_EQ(producerMeta.bindingItemId, consumerBlock->id());
    EXPECT_EQ(producerMeta.bindingPortId, consumerPort);
    EXPECT_FALSE(Canvas::Support::isBoundConsumerEndpointValid(doc, producerMeta));
}

TEST(CanvasPortTests, BoundProducerLabelUsesConnectedObjectFifoName)
{
    Canvas::CanvasDocument doc;

    auto* consumerBlock = doc.createBlock(QRectF(0.0, 0.0, 80.0, 80.0), false);
    ASSERT_NE(consumerBlock, nullptr);
    const Canvas::PortId consumerPort = consumerBlock->addPort(Canvas::PortSide::Top,
                                                                0.5,
                                                                Canvas::PortRole::Consumer,
                                                                Canvas::Support::pairedPortName(
                                                                    QStringLiteral("legacy-id")));
    ASSERT_TRUE(consumerPort);

    auto* sourceBlock = doc.createBlock(QRectF(-200.0, 0.0, 80.0, 80.0), false);
    ASSERT_NE(sourceBlock, nullptr);
    const Canvas::PortId sourcePort = sourceBlock->addPort(Canvas::PortSide::Right,
                                                           0.5,
                                                           Canvas::PortRole::Producer,
                                                           QStringLiteral("out"));
    ASSERT_TRUE(sourcePort);

    Canvas::CanvasWire::Endpoint a;
    a.attached = Canvas::PortRef{sourceBlock->id(), sourcePort};
    Canvas::CanvasWire::Endpoint b;
    b.attached = Canvas::PortRef{consumerBlock->id(), consumerPort};
    auto wire = std::make_unique<Canvas::CanvasWire>(a, b);
    wire->setId(doc.allocateId());
    Canvas::CanvasWire::ObjectFifoConfig fifo;
    fifo.name = QStringLiteral("in");
    wire->setObjectFifo(fifo);
    ASSERT_TRUE(doc.commands().execute(std::make_unique<Canvas::CreateItemCommand>(std::move(wire))));

    Canvas::EdgeCandidate edge;
    edge.itemId = consumerBlock->id();
    edge.side = Canvas::PortSide::Left;
    edge.t = 0.5;

    Canvas::Support::BoundProducerPlacementRequest request;
    request.consumerItemId = consumerBlock->id();
    request.consumerPortId = consumerPort;
    request.producerEdge = edge;

    const auto created = Canvas::Support::createBoundProducerPort(doc, request);
    ASSERT_TRUE(created.has_value());

    Canvas::CanvasPort producerMeta;
    ASSERT_TRUE(doc.getPort(created->producerItemId, created->producerPortId, producerMeta));
    EXPECT_EQ(producerMeta.name, QStringLiteral("C: \"in\""));
}

TEST(CanvasPortTests, BoundProducerPlacementIsRejectedAcrossTiles)
{
    Canvas::CanvasDocument doc;

    auto* consumerBlock = doc.createBlock(QRectF(0.0, 0.0, 80.0, 80.0), false);
    ASSERT_NE(consumerBlock, nullptr);
    const Canvas::PortId consumerPort = consumerBlock->addPort(Canvas::PortSide::Bottom,
                                                                0.5,
                                                                Canvas::PortRole::Consumer,
                                                                QStringLiteral("in"));
    ASSERT_TRUE(consumerPort);

    auto* otherBlock = doc.createBlock(QRectF(200.0, 0.0, 80.0, 80.0), false);
    ASSERT_NE(otherBlock, nullptr);
    const size_t beforePortCount = otherBlock->ports().size();

    Canvas::EdgeCandidate edge;
    edge.itemId = otherBlock->id();
    edge.side = Canvas::PortSide::Top;
    edge.t = 0.5;

    Canvas::Support::BoundProducerPlacementRequest request;
    request.consumerItemId = consumerBlock->id();
    request.consumerPortId = consumerPort;
    request.producerEdge = edge;

    const auto created = Canvas::Support::createBoundProducerPort(doc, request);
    EXPECT_FALSE(created.has_value());
    EXPECT_EQ(otherBlock->ports().size(), beforePortCount);
}
