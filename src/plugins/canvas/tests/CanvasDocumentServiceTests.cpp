// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasController.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/api/ICanvasHost.hpp"
#include "canvas/document/CanvasDocumentServiceImpl.hpp"

#include <QtCore/QDir>
#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>

namespace {

class StubCanvasHost final : public Canvas::Api::ICanvasHost
{
public:
    StubCanvasHost()
    {
        m_document = new Canvas::CanvasDocument(this);
    }

    QWidget* viewWidget() const override { return nullptr; }
    Canvas::CanvasDocument* document() const override { return m_document; }
    Canvas::CanvasController* controller() const override { return nullptr; }

    void setCanvasActive(bool active) override
    {
        if (m_active == active)
            return;
        m_active = active;
        emit canvasActiveChanged(m_active);
    }

    bool canvasActive() const override { return m_active; }
    void setEmptyStateText(const QString&, const QString&) override {}

private:
    Canvas::CanvasDocument* m_document = nullptr;
    bool m_active = false;
};

QCoreApplication* ensureCoreApp()
{
    if (auto* existing = QCoreApplication::instance())
        return existing;

    static int argc = 1;
    static char appName[] = "CanvasTests";
    static char* argv[] = {appName, nullptr};
    static QCoreApplication app(argc, argv);
    return &app;
}

} // namespace

TEST(CanvasDocumentServiceTests, CreateSaveCloseAndReopenRoundTrip)
{
    ensureCoreApp();

    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    const QString bundlePath = QDir(temp.path()).filePath(QStringLiteral("RoundTrip.ironsmith"));
    ASSERT_TRUE(QDir().mkpath(bundlePath));

    StubCanvasHost host;
    Canvas::Internal::CanvasDocumentServiceImpl service;
    service.setCanvasHost(&host);

    Canvas::Api::CanvasDocumentCreateRequest createRequest;
    createRequest.bundlePath = bundlePath;
    createRequest.persistenceRelativePath = QStringLiteral("canvas/document.json");

    Canvas::Api::CanvasDocumentHandle handle;
    const Utils::Result createResult = service.createDocument(createRequest, handle);
    ASSERT_TRUE(createResult.ok) << createResult.errors.join("\n").toStdString();
    ASSERT_TRUE(handle.isValid());
    EXPECT_TRUE(QFileInfo::exists(handle.persistencePath));

    auto* block = host.document()->createBlock(QRectF(100.0, 100.0, 40.0, 40.0), true);
    ASSERT_NE(block, nullptr);
    block->setSpecId(QStringLiteral("tile-0"));
    const Canvas::ObjectId blockId = block->id();

    const Canvas::PortId sinkPort = block->addPort(Canvas::PortSide::Left,
                                                   0.5,
                                                   Canvas::PortRole::Consumer,
                                                   QStringLiteral("IN"));
    ASSERT_TRUE(sinkPort);

    Canvas::PortRef ref;
    ref.itemId = block->id();
    ref.portId = block->addPort(Canvas::PortSide::Right, 0.5, Canvas::PortRole::Producer, QStringLiteral("OUT"));
    ASSERT_TRUE(ref.portId);
    ASSERT_TRUE(block->updatePortBinding(ref.portId, block->id(), sinkPort));

    Canvas::CanvasWire::Endpoint a;
    a.attached = ref;
    a.freeScene = QPointF();

    Canvas::CanvasWire::Endpoint b;
    b.freeScene = QPointF(180.0, 120.0);

    auto wire = std::make_unique<Canvas::CanvasWire>(a, b);
    wire->setId(host.document()->allocateId());
    Canvas::CanvasWire::ObjectFifoConfig fifo;
    fifo.name = QStringLiteral("in");
    fifo.depth = 2;
    fifo.operation = Canvas::CanvasWire::ObjectFifoOperation::Forward;
    fifo.type.valueType = QStringLiteral("i32");
    wire->setObjectFifo(fifo);
    ASSERT_TRUE(host.document()->insertItem(host.document()->items().size(), std::move(wire)));

    const Utils::Result saveResult = service.saveDocument(handle);
    ASSERT_TRUE(saveResult.ok) << saveResult.errors.join("\n").toStdString();

    const Utils::Result closeResult = service.closeDocument(handle,
        Canvas::Api::CanvasDocumentCloseReason::UserClosed);
    ASSERT_TRUE(closeResult.ok) << closeResult.errors.join("\n").toStdString();
    EXPECT_FALSE(service.hasOpenDocument());

    Canvas::Api::CanvasDocumentOpenRequest openRequest;
    openRequest.bundlePath = bundlePath;
    openRequest.persistencePath = handle.persistencePath;

    Canvas::Api::CanvasDocumentHandle reopened;
    const Utils::Result openResult = service.openDocument(openRequest, reopened);
    ASSERT_TRUE(openResult.ok) << openResult.errors.join("\n").toStdString();
    EXPECT_TRUE(reopened.isValid());
    EXPECT_EQ(host.document()->items().size(), 2u);

    bool foundObjectFifoWire = false;
    for (const auto& item : host.document()->items()) {
        const auto* reopenedWire = dynamic_cast<const Canvas::CanvasWire*>(item.get());
        if (!reopenedWire || !reopenedWire->hasObjectFifo())
            continue;

        const auto& objectFifo = reopenedWire->objectFifo().value();
        EXPECT_EQ(objectFifo.name, QStringLiteral("in"));
        EXPECT_EQ(objectFifo.depth, 2);
        EXPECT_EQ(objectFifo.operation, Canvas::CanvasWire::ObjectFifoOperation::Forward);
        EXPECT_EQ(objectFifo.type.valueType, QStringLiteral("i32"));
        foundObjectFifoWire = true;
    }
    EXPECT_TRUE(foundObjectFifoWire);

    const auto* reopenedBlock = dynamic_cast<const Canvas::CanvasBlock*>(host.document()->findItem(blockId));
    ASSERT_NE(reopenedBlock, nullptr);

    bool foundBoundProducer = false;
    for (const auto& port : reopenedBlock->ports()) {
        if (port.role != Canvas::PortRole::Producer || !port.hasBinding)
            continue;
        EXPECT_EQ(port.bindingItemId, reopenedBlock->id());
        EXPECT_EQ(port.bindingPortId, sinkPort);
        foundBoundProducer = true;
    }
    EXPECT_TRUE(foundBoundProducer);
}

TEST(CanvasDocumentServiceTests, MetadataUpdatesRoundTripAcrossReopen)
{
    ensureCoreApp();

    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    const QString bundlePath = QDir(temp.path()).filePath(QStringLiteral("MetadataRoundTrip.ironsmith"));
    ASSERT_TRUE(QDir().mkpath(bundlePath));

    StubCanvasHost host;
    Canvas::Internal::CanvasDocumentServiceImpl service;
    service.setCanvasHost(&host);

    Canvas::Api::CanvasDocumentCreateRequest createRequest;
    createRequest.bundlePath = bundlePath;
    createRequest.persistenceRelativePath = QStringLiteral("canvas/document.json");
    createRequest.metadata.insert(QStringLiteral("deviceId"), QStringLiteral("phoenix"));

    Canvas::Api::CanvasDocumentHandle handle;
    const Utils::Result createResult = service.createDocument(createRequest, handle);
    ASSERT_TRUE(createResult.ok) << createResult.errors.join("\n").toStdString();
    EXPECT_EQ(service.activeMetadata().value(QStringLiteral("deviceId")).toString(), QStringLiteral("phoenix"));

    QJsonObject updatedMetadata = service.activeMetadata();
    updatedMetadata.insert(QStringLiteral("symbols"),
                           QJsonObject{{QStringLiteral("schema"), QStringLiteral("aie.symbols/1")}});

    const Utils::Result updateResult = service.updateActiveMetadata(updatedMetadata);
    ASSERT_TRUE(updateResult.ok) << updateResult.errors.join("\n").toStdString();

    const Utils::Result saveResult = service.saveDocument(handle);
    ASSERT_TRUE(saveResult.ok) << saveResult.errors.join("\n").toStdString();

    const Utils::Result closeResult =
        service.closeDocument(handle, Canvas::Api::CanvasDocumentCloseReason::UserClosed);
    ASSERT_TRUE(closeResult.ok) << closeResult.errors.join("\n").toStdString();

    Canvas::Api::CanvasDocumentOpenRequest openRequest;
    openRequest.bundlePath = bundlePath;
    openRequest.persistencePath = handle.persistencePath;

    Canvas::Api::CanvasDocumentHandle reopened;
    const Utils::Result openResult = service.openDocument(openRequest, reopened);
    ASSERT_TRUE(openResult.ok) << openResult.errors.join("\n").toStdString();

    const QJsonObject reopenedMetadata = service.activeMetadata();
    EXPECT_EQ(reopenedMetadata.value(QStringLiteral("deviceId")).toString(), QStringLiteral("phoenix"));
    EXPECT_TRUE(reopenedMetadata.contains(QStringLiteral("symbols")));
}
