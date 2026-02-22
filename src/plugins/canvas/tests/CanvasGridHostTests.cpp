// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/internal/CanvasGridHostImpl.hpp"
#include "canvas/utils/CanvasAutoPorts.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>

namespace {

QCoreApplication* ensureCoreApp()
{
    if (auto* existing = QCoreApplication::instance())
        return existing;

    static int argc = 1;
    static char appName[] = "CanvasGridHostTests";
    static char* argv[] = {appName, nullptr};
    static QCoreApplication app(argc, argv);
    return &app;
}

void drainEvents()
{
    for (int i = 0; i < 8; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

QVector<Canvas::CanvasBlock*> blocksBySpecId(Canvas::CanvasDocument& document, const QString& specId)
{
    QVector<Canvas::CanvasBlock*> out;
    for (const auto& item : document.items()) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block || block->specId() != specId)
            continue;
        out.push_back(block);
    }
    return out;
}

void clearDocument(Canvas::CanvasDocument& document)
{
    QVector<Canvas::ObjectId> ids;
    ids.reserve(static_cast<int>(document.items().size()));
    for (const auto& item : document.items()) {
        if (item)
            ids.push_back(item->id());
    }
    for (const auto& id : ids)
        document.removeItem(id);
}

} // namespace

TEST(CanvasGridHostTests, RebuildAdoptsPersistedBlockAndRemovesDuplicateForSpecId)
{
    ensureCoreApp();

    Canvas::CanvasDocument document;
    Canvas::Internal::CanvasGridHostImpl gridHost(&document, nullptr, nullptr);

    Utils::GridSpec gridSpec;
    gridSpec.columns = 1;
    gridSpec.rows = 1;
    gridSpec.origin = Utils::GridOrigin::BottomLeft;
    gridSpec.autoCellSize = false;
    gridSpec.cellSize = QSizeF(120.0, 120.0);
    gridHost.setGridSpec(gridSpec);

    Canvas::Api::CanvasBlockSpec shimSpec;
    shimSpec.id = QStringLiteral("shim_0_0");
    shimSpec.gridRect = Utils::GridRect{0, 0, 1, 1};
    shimSpec.showPorts = true;
    shimSpec.autoOppositeProducerPort = true;

    QVector<Canvas::Api::CanvasBlockSpec> specs;
    specs.push_back(shimSpec);
    gridHost.setBlocks(specs);
    drainEvents();

    auto initialBlocks = blocksBySpecId(document, shimSpec.id);
    ASSERT_EQ(initialBlocks.size(), 1);

    clearDocument(document);
    ASSERT_TRUE(blocksBySpecId(document, shimSpec.id).isEmpty());

    auto* persisted = document.createBlock(QRectF(0.0, 0.0, 120.0, 120.0), false);
    ASSERT_NE(persisted, nullptr);
    persisted->setSpecId(shimSpec.id);
    persisted->setShowPorts(true);
    persisted->setAutoOppositeProducerPort(true);
    const Canvas::PortId consumer = persisted->addPort(Canvas::PortSide::Right,
                                                       0.5,
                                                       Canvas::PortRole::Dynamic);
    ASSERT_TRUE(consumer);
    ASSERT_TRUE(Canvas::Support::ensureOppositeProducerPort(document, persisted->id(), consumer));

    auto* duplicate = document.createBlock(QRectF(0.0, 0.0, 120.0, 120.0), false);
    ASSERT_NE(duplicate, nullptr);
    duplicate->setSpecId(shimSpec.id);

    auto* sink = document.createBlock(QRectF(220.0, 0.0, 120.0, 120.0), false);
    ASSERT_NE(sink, nullptr);
    const Canvas::PortId sinkPort = sink->addPort(Canvas::PortSide::Left, 0.5, Canvas::PortRole::Dynamic);
    ASSERT_TRUE(sinkPort);

    Canvas::CanvasWire::Endpoint a;
    a.attached = Canvas::PortRef{persisted->id(), consumer};
    a.freeScene = QPointF();
    Canvas::CanvasWire::Endpoint b;
    b.attached = Canvas::PortRef{sink->id(), sinkPort};
    b.freeScene = QPointF();

    auto wire = std::make_unique<Canvas::CanvasWire>(a, b);
    wire->setId(document.allocateId());
    ASSERT_TRUE(document.insertItem(document.items().size(), std::move(wire)));

    gridHost.setBlocks(specs);
    drainEvents();

    const auto finalBlocks = blocksBySpecId(document, shimSpec.id);
    ASSERT_EQ(finalBlocks.size(), 1);
    EXPECT_EQ(finalBlocks.front()->id(), persisted->id());
    EXPECT_EQ(finalBlocks.front()->ports().size(), 2u);

    bool sawWireOnPersistedConsumer = false;
    for (const auto& item : document.items()) {
        const auto* wireItem = dynamic_cast<const Canvas::CanvasWire*>(item.get());
        if (!wireItem)
            continue;
        if (wireItem->a().attached
            && wireItem->a().attached->itemId == persisted->id()
            && wireItem->a().attached->portId == consumer) {
            sawWireOnPersistedConsumer = true;
            break;
        }
    }
    EXPECT_TRUE(sawWireOnPersistedConsumer);
}
