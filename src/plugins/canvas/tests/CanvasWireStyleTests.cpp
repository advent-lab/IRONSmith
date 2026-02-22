// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/CanvasWire.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/utils/CanvasLinkWireStyle.hpp"

#include <gtest/gtest.h>

namespace Canvas::Tests {

TEST(CanvasWireStyleTests, ColorOverrideAndArrowPolicyClone)
{
    CanvasWire::Endpoint a;
    CanvasWire::Endpoint b;
    CanvasWire wire(a, b);

    const QColor overrideColor("#123456");
    wire.setColorOverride(overrideColor);
    wire.setArrowPolicy(WireArrowPolicy::Start);

    auto clone = wire.clone();
    auto* clonedWire = dynamic_cast<CanvasWire*>(clone.get());
    ASSERT_TRUE(clonedWire != nullptr);
    EXPECT_TRUE(clonedWire->hasColorOverride());
    EXPECT_EQ(clonedWire->colorOverride().name(QColor::HexArgb),
              overrideColor.name(QColor::HexArgb));
    EXPECT_EQ(clonedWire->arrowPolicy(), WireArrowPolicy::Start);
}

TEST(CanvasWireStyleTests, ObjectFifoAnnotationAndClone)
{
    CanvasWire::Endpoint a;
    CanvasWire::Endpoint b;
    CanvasWire wire(a, b);

    CanvasWire::ObjectFifoConfig fifo;
    fifo.name = QStringLiteral("in");
    fifo.depth = 3;
    fifo.type.valueType = QStringLiteral("i16");
    fifo.type.dimensions = QStringLiteral("(M, N)");
    wire.setObjectFifo(fifo);

    const CanvasRenderContext ctx{};
    EXPECT_TRUE(wire.hasObjectFifo());
    EXPECT_EQ(wire.annotationText(CanvasWire::AnnotationDetail::Compact, ctx),
              QStringLiteral("FIFO<\"in\", D:3>"));
    EXPECT_EQ(wire.annotationText(CanvasWire::AnnotationDetail::Full, ctx),
              QStringLiteral("FIFO<\"in\", D:3, T:i16, Dim:(M, N)>"));

    auto clone = wire.clone();
    auto* clonedWire = dynamic_cast<CanvasWire*>(clone.get());
    ASSERT_TRUE(clonedWire != nullptr);
    ASSERT_TRUE(clonedWire->hasObjectFifo());
    const auto& clonedFifo = clonedWire->objectFifo().value();
    EXPECT_EQ(clonedFifo.name, QStringLiteral("in"));
    EXPECT_EQ(clonedFifo.depth, 3);
    EXPECT_EQ(clonedFifo.operation, CanvasWire::ObjectFifoOperation::Fifo);
    EXPECT_EQ(clonedFifo.type.valueType, QStringLiteral("i16"));
    EXPECT_EQ(clonedFifo.type.dimensions, QStringLiteral("(M, N)"));
}

TEST(CanvasWireStyleTests, ForwardObjectFifoAnnotationAndClone)
{
    CanvasWire::Endpoint a;
    CanvasWire::Endpoint b;
    CanvasWire wire(a, b);

    CanvasWire::ObjectFifoConfig fifo;
    fifo.name = QStringLiteral("in");
    fifo.depth = 3;
    fifo.operation = CanvasWire::ObjectFifoOperation::Forward;
    fifo.type.valueType = QStringLiteral("i32");
    wire.setObjectFifo(fifo);

    const CanvasRenderContext ctx{};
    EXPECT_TRUE(wire.hasForwardObjectFifo());
    EXPECT_EQ(wire.annotationText(CanvasWire::AnnotationDetail::Compact, ctx),
              QStringLiteral("FWD: FIFO<\"in\", D:3>"));
    EXPECT_EQ(wire.annotationText(CanvasWire::AnnotationDetail::Full, ctx),
              QStringLiteral("FWD: FIFO<\"in\", D:3, T:i32>"));

    auto clone = wire.clone();
    auto* clonedWire = dynamic_cast<CanvasWire*>(clone.get());
    ASSERT_TRUE(clonedWire != nullptr);
    ASSERT_TRUE(clonedWire->hasObjectFifo());
    EXPECT_TRUE(clonedWire->hasForwardObjectFifo());
    const auto& clonedFifo = clonedWire->objectFifo().value();
    EXPECT_EQ(clonedFifo.operation, CanvasWire::ObjectFifoOperation::Forward);
}

TEST(CanvasLinkWireStyleTests, RoleColorsMatchConstants)
{
    const auto producer = Support::linkWireStyle(Support::LinkWireRole::Producer);
    const auto consumer = Support::linkWireStyle(Support::LinkWireRole::Consumer);

    EXPECT_EQ(producer.color.name(QColor::HexArgb),
              QColor(Constants::kLinkWireProducerColor).name(QColor::HexArgb));
    EXPECT_EQ(consumer.color.name(QColor::HexArgb),
              QColor(Constants::kLinkWireConsumerColor).name(QColor::HexArgb));
}

} // namespace Canvas::Tests
