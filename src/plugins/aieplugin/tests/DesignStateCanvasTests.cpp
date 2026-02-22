// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/design/DesignStateCanvas.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasWire.hpp"

#include <QtCore/QJsonObject>
#include <QtCore/QRectF>

using Aie::Internal::DesignState;

namespace {

Canvas::CanvasBlock* makeBlock(Canvas::CanvasDocument& doc, const QString& specId)
{
    auto* block = doc.createBlock(QRectF(0.0, 0.0, 64.0, 64.0), false);
    if (!block)
        return nullptr;
    block->setSpecId(specId);
    return block;
}

} // namespace

TEST(DesignStateCanvasTests, ApplyDesignStateClearsPorts)
{
    Canvas::CanvasDocument doc;
    auto* block = makeBlock(doc, QStringLiteral("shim0_0"));
    ASSERT_NE(block, nullptr);
    block->addPort(Canvas::PortSide::Left, 0.5, Canvas::PortRole::Consumer);
    ASSERT_TRUE(block->hasPorts());

    DesignState state;
    const Utils::Result result = Aie::Internal::applyDesignStateToCanvas(state, doc, nullptr);
    ASSERT_TRUE(result.ok) << result.errors.join("\n").toStdString();

    EXPECT_FALSE(block->hasPorts());
    EXPECT_TRUE(block->ports().empty());
}

TEST(DesignStateCanvasTests, BuildAndApplyPreservesObjectFifoMetadata)
{
    Canvas::CanvasDocument source;

    auto* producer = makeBlock(source, QStringLiteral("aie0_0"));
    auto* consumer = makeBlock(source, QStringLiteral("shim0_0"));
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(consumer, nullptr);

    const Canvas::PortId producerPort =
        producer->addPort(Canvas::PortSide::Right, 0.5, Canvas::PortRole::Producer, QStringLiteral("OUT_1"));
    const Canvas::PortId consumerPort =
        consumer->addPort(Canvas::PortSide::Left, 0.5, Canvas::PortRole::Consumer, QStringLiteral("IN_1"));
    ASSERT_FALSE(producerPort.isNull());
    ASSERT_FALSE(consumerPort.isNull());

    Canvas::CanvasWire::Endpoint a;
    a.attached = Canvas::PortRef{producer->id(), producerPort};
    Canvas::CanvasWire::Endpoint b;
    b.attached = Canvas::PortRef{consumer->id(), consumerPort};
    auto wire = std::make_unique<Canvas::CanvasWire>(a, b);
    wire->setId(source.allocateId());
    Canvas::CanvasWire::ObjectFifoConfig objectFifo;
    objectFifo.name = QStringLiteral("in");
    objectFifo.depth = 3;
    objectFifo.operation = Canvas::CanvasWire::ObjectFifoOperation::Forward;
    objectFifo.type.valueType = QStringLiteral("i16");
    objectFifo.type.dimensions = QStringLiteral("(M, N)");
    wire->setObjectFifo(objectFifo);
    ASSERT_TRUE(source.insertItem(source.items().size(), std::move(wire)));

    DesignState state;
    const Utils::Result buildResult =
        Aie::Internal::buildDesignStateFromCanvas(source, nullptr, QJsonObject{}, state);
    ASSERT_TRUE(buildResult.ok) << buildResult.errors.join("\n").toStdString();
    ASSERT_EQ(state.links.size(), 1);
    EXPECT_TRUE(state.links[0].hasObjectFifo);
    EXPECT_EQ(state.links[0].objectFifo.name, QStringLiteral("in"));
    EXPECT_EQ(state.links[0].objectFifo.depth, 3);
    EXPECT_EQ(state.links[0].objectFifo.operation, Aie::Internal::DesignLink::ObjectFifo::Operation::Forward);
    EXPECT_EQ(state.links[0].objectFifo.type.valueType, QStringLiteral("i16"));
    EXPECT_EQ(state.links[0].objectFifo.type.dimensions, QStringLiteral("(M, N)"));

    Canvas::CanvasDocument restored;
    ASSERT_NE(makeBlock(restored, QStringLiteral("aie0_0")), nullptr);
    ASSERT_NE(makeBlock(restored, QStringLiteral("shim0_0")), nullptr);

    const Utils::Result applyResult = Aie::Internal::applyDesignStateToCanvas(state, restored, nullptr);
    ASSERT_TRUE(applyResult.ok) << applyResult.errors.join("\n").toStdString();

    const Canvas::CanvasWire* restoredWire = nullptr;
    for (const auto& item : restored.items()) {
        restoredWire = dynamic_cast<const Canvas::CanvasWire*>(item.get());
        if (restoredWire)
            break;
    }
    ASSERT_NE(restoredWire, nullptr);
    ASSERT_TRUE(restoredWire->hasObjectFifo());
    const auto& restoredObjectFifo = restoredWire->objectFifo().value();
    EXPECT_EQ(restoredObjectFifo.name, QStringLiteral("in"));
    EXPECT_EQ(restoredObjectFifo.depth, 3);
    EXPECT_EQ(restoredObjectFifo.operation, Canvas::CanvasWire::ObjectFifoOperation::Forward);
    EXPECT_EQ(restoredObjectFifo.type.valueType, QStringLiteral("i16"));
    EXPECT_EQ(restoredObjectFifo.type.dimensions, QStringLiteral("(M, N)"));
}
