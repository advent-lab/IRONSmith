// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/hlir_sync/HlirSyncService.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasWire.hpp"

#include <QtCore/QRectF>
#include <QtTest/QSignalSpy>
#include <QtWidgets/QApplication>

using namespace Aie::Internal;

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        static int argc = 1;
        static char arg0[] = "hlir-sync-service-tests";
        static char* argv[] = {arg0, nullptr};
        return new QApplication(argc, argv);
    }();
    return app;
}

Canvas::CanvasBlock* makeBlock(Canvas::CanvasDocument& doc, const QString& specId)
{
    auto* block = doc.createBlock(QRectF(0, 0, 64, 64), false);
    if (block)
        block->setSpecId(specId);
    return block;
}

Canvas::PortId addPort(Canvas::CanvasBlock* block, Canvas::PortRole role)
{
    return block->addPort(Canvas::PortSide::Right, 0.5, role);
}

void attachWire(Canvas::CanvasDocument& doc,
                Canvas::CanvasBlock* aBlock, Canvas::PortId aPort,
                Canvas::CanvasBlock* bBlock, Canvas::PortId bPort,
                std::optional<Canvas::CanvasWire::ObjectFifoConfig> fifo = {})
{
    Canvas::CanvasWire::Endpoint epA;
    epA.attached = Canvas::PortRef{aBlock->id(), aPort};
    Canvas::CanvasWire::Endpoint epB;
    epB.attached = Canvas::PortRef{bBlock->id(), bPort};

    auto wire = std::make_unique<Canvas::CanvasWire>(epA, epB);
    wire->setId(doc.allocateId());
    if (fifo)
        wire->setObjectFifo(*fifo);
    doc.insertItem(doc.items().size(), std::move(wire));
}

Canvas::CanvasWire::ObjectFifoConfig makeFifo()
{
    Canvas::CanvasWire::ObjectFifoConfig f;
    f.name = QStringLiteral("of");
    f.depth = 2;
    f.type.valueType = QStringLiteral("i32");
    f.type.dimensions = QStringLiteral("(256)");
    return f;
}

void buildValidPassthrough(Canvas::CanvasDocument& doc)
{
    auto* ddr     = makeBlock(doc, QStringLiteral("ddr"));
    auto* shimIn  = makeBlock(doc, QStringLiteral("shim0_0"));
    auto* aie     = makeBlock(doc, QStringLiteral("aie0_0"));
    auto* shimOut = makeBlock(doc, QStringLiteral("shim0_1"));

    attachWire(doc, ddr,    addPort(ddr,     Canvas::PortRole::Producer),
                   shimIn, addPort(shimIn,  Canvas::PortRole::Consumer));
    attachWire(doc, shimIn, addPort(shimIn, Canvas::PortRole::Producer),
                   aie,    addPort(aie,    Canvas::PortRole::Consumer), makeFifo());
    attachWire(doc, aie,     addPort(aie,     Canvas::PortRole::Producer),
                   shimOut, addPort(shimOut, Canvas::PortRole::Consumer), makeFifo());
    attachWire(doc, shimOut, addPort(shimOut, Canvas::PortRole::Producer),
                   ddr,     addPort(ddr,     Canvas::PortRole::Consumer));
}

/// Probe bridge availability once and cache the result for the entire test run.
/// Each bridge-dependent test calls attachDocument itself; this only determines
/// whether the bridge can be initialised at all, so we don't pay the full
/// verifyDesign() cost on every test.
bool isBridgeAvailable()
{
    ensureApp();
    static bool result = [] {
        HlirSyncService::setAnimateSteps(false);
        Canvas::CanvasDocument doc;
        buildValidPassthrough(doc);
        HlirSyncService svc;
        svc.attachDocument(&doc, QStringLiteral(""));
        QSignalSpy spy(&svc, &HlirSyncService::runStarted);
        svc.verifyDesign();
        return spy.count() == 1;
    }();
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Default state — no bridge required
// ---------------------------------------------------------------------------

TEST(HlirSyncServiceTests, OutputDirIsEmptyByDefault)
{
    HlirSyncService service;
    EXPECT_TRUE(service.outputDir().isEmpty());
}

TEST(HlirSyncServiceTests, VerifyWithNoDocumentEmitsFailure)
{
    ensureApp();
    HlirSyncService service;

    QSignalSpy spy(&service, &HlirSyncService::verificationFinished);
    service.verifyDesign();

    ASSERT_EQ(spy.count(), 1);
    EXPECT_FALSE(spy.first().at(0).toBool());
}

TEST(HlirSyncServiceTests, VerifyWithNoDocumentDoesNotEmitRunStarted)
{
    ensureApp();
    HlirSyncService service;

    QSignalSpy spy(&service, &HlirSyncService::runStarted);
    service.verifyDesign();

    EXPECT_EQ(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// Verification pipeline — bridge required
// ---------------------------------------------------------------------------

TEST(HlirSyncServiceTests, VerifyWithValidDocumentEmitsRunStarted)
{
    if (!isBridgeAvailable())
        GTEST_SKIP() << "HlirBridge not available in this environment";

    HlirSyncService service;
    Canvas::CanvasDocument doc;
    buildValidPassthrough(doc);
    service.attachDocument(&doc, QStringLiteral(""));

    QSignalSpy spy(&service, &HlirSyncService::runStarted);
    service.verifyDesign();

    EXPECT_EQ(spy.count(), 1);
}

TEST(HlirSyncServiceTests, VerifyWithValidDocumentPasses)
{
    if (!isBridgeAvailable())
        GTEST_SKIP() << "HlirBridge not available in this environment";

    HlirSyncService service;
    Canvas::CanvasDocument doc;
    buildValidPassthrough(doc);
    service.attachDocument(&doc, QStringLiteral(""));

    QSignalSpy spy(&service, &HlirSyncService::verificationFinished);
    service.verifyDesign();

    ASSERT_EQ(spy.count(), 1);
    EXPECT_TRUE(spy.first().at(0).toBool());
}

TEST(HlirSyncServiceTests, VerifyWithEmptyDocumentFails)
{
    if (!isBridgeAvailable())
        GTEST_SKIP() << "HlirBridge not available in this environment";

    HlirSyncService service;
    Canvas::CanvasDocument doc; // empty — no tiles or wires
    service.attachDocument(&doc, QStringLiteral(""));

    QSignalSpy spy(&service, &HlirSyncService::verificationFinished);
    service.verifyDesign();

    ASSERT_EQ(spy.count(), 1);
    EXPECT_FALSE(spy.first().at(0).toBool());
}

TEST(HlirSyncServiceTests, VerifyEmitsOneStepLoggedPerCheck)
{
    if (!isBridgeAvailable())
        GTEST_SKIP() << "HlirBridge not available in this environment";

    HlirSyncService service;
    Canvas::CanvasDocument doc;
    buildValidPassthrough(doc);
    service.attachDocument(&doc, QStringLiteral(""));

    QSignalSpy spy(&service, &HlirSyncService::stepLogged);
    service.verifyDesign();

    // DesignVerifier registers 8 checks.
    EXPECT_EQ(spy.count(), 8);
    // All steps should pass for a valid design.
    for (int i = 0; i < spy.count(); ++i)
        EXPECT_TRUE(spy.at(i).at(0).toBool()) << "step " << i << " failed";
}

TEST(HlirSyncServiceTests, DetachDocumentCausesNextVerifyToReportNoDesign)
{
    if (!isBridgeAvailable())
        GTEST_SKIP() << "HlirBridge not available in this environment";

    HlirSyncService service;
    Canvas::CanvasDocument doc;
    buildValidPassthrough(doc);
    service.attachDocument(&doc, QStringLiteral(""));

    service.detachDocument();

    QSignalSpy spy(&service, &HlirSyncService::verificationFinished);
    service.verifyDesign();

    ASSERT_EQ(spy.count(), 1);
    EXPECT_FALSE(spy.first().at(0).toBool());
    EXPECT_TRUE(spy.first().at(1).toString().contains(QStringLiteral("No design")));
}

TEST(HlirSyncServiceTests, OutputDirReflectsAttachParameter)
{
    ensureApp();
    HlirSyncService service;
    Canvas::CanvasDocument doc;

    service.attachDocument(&doc, QStringLiteral("/some/output/path"));

    // outputDir is set regardless of whether the bridge init succeeded.
    // If bridge failed, document is cleared but outputDir is still stored.
    EXPECT_EQ(service.outputDir(), QStringLiteral("/some/output/path"));
}
