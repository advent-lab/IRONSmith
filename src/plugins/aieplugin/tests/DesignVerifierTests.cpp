// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/hlir_sync/DesignVerifier.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasWire.hpp"

#include <QtCore/QRectF>

using namespace Aie::Internal;

namespace {

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

Canvas::CanvasWire::ObjectFifoConfig makeFifo(const QString& dims = QStringLiteral("(256)"))
{
    Canvas::CanvasWire::ObjectFifoConfig f;
    f.name = QStringLiteral("of");
    f.depth = 2;
    f.type.valueType = QStringLiteral("i32");
    f.type.dimensions = dims;
    return f;
}

/// Builds the minimal valid passthrough design:
///   DDR → SHIM(0,0) → AIE(0,0) → SHIM(0,1) → DDR
/// All objectFifo wires have dimensions so every check passes.
void buildValidPassthrough(Canvas::CanvasDocument& doc)
{
    auto* ddr     = makeBlock(doc, QStringLiteral("ddr"));
    auto* shimIn  = makeBlock(doc, QStringLiteral("shim0_0"));
    auto* aie     = makeBlock(doc, QStringLiteral("aie0_0"));
    auto* shimOut = makeBlock(doc, QStringLiteral("shim0_1"));

    // DDR → SHIM_in  (fill)
    attachWire(doc, ddr,    addPort(ddr,     Canvas::PortRole::Producer),
                   shimIn, addPort(shimIn,  Canvas::PortRole::Consumer));

    // SHIM_in → AIE  (objectFifo)
    attachWire(doc, shimIn, addPort(shimIn, Canvas::PortRole::Producer),
                   aie,    addPort(aie,    Canvas::PortRole::Consumer), makeFifo());

    // AIE → SHIM_out (objectFifo)
    attachWire(doc, aie,     addPort(aie,     Canvas::PortRole::Producer),
                   shimOut, addPort(shimOut, Canvas::PortRole::Consumer), makeFifo());

    // SHIM_out → DDR (drain)
    attachWire(doc, shimOut, addPort(shimOut, Canvas::PortRole::Producer),
                   ddr,     addPort(ddr,     Canvas::PortRole::Consumer));
}

} // namespace

// ---------------------------------------------------------------------------
// Null / empty document
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, NullDocumentProducesNoIssues)
{
    const auto issues = DesignVerifier().verify({nullptr});
    EXPECT_TRUE(issues.isEmpty());
}

TEST(DesignVerifierTests, EmptyDocumentFailsRuntimeSequence)
{
    Canvas::CanvasDocument doc;
    const auto issues = DesignVerifier().verify({&doc});
    EXPECT_TRUE(DesignVerifier::hasErrors(issues));
    EXPECT_GE(issues.size(), 2); // both fill and drain missing
}

// ---------------------------------------------------------------------------
// Valid design
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, ValidPassthroughPassesAllChecks)
{
    Canvas::CanvasDocument doc;
    buildValidPassthrough(doc);

    const auto issues = DesignVerifier().verify({&doc});
    EXPECT_FALSE(DesignVerifier::hasErrors(issues))
        << [&] {
               QStringList msgs;
               for (const auto& i : issues)
                   msgs << i.message;
               return msgs.join(u'\n').toStdString();
           }();
}

// ---------------------------------------------------------------------------
// Check 1 — RuntimeSequenceDefined
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, MissingFillProducesError)
{
    Canvas::CanvasDocument doc;
    auto* ddr     = makeBlock(doc, QStringLiteral("ddr"));
    auto* shimOut = makeBlock(doc, QStringLiteral("shim0_0"));
    auto* aie     = makeBlock(doc, QStringLiteral("aie0_0"));

    // AIE → SHIM → DDR only (drain side; no fill)
    attachWire(doc, aie,     addPort(aie,     Canvas::PortRole::Producer),
                   shimOut, addPort(shimOut, Canvas::PortRole::Consumer), makeFifo());
    attachWire(doc, shimOut, addPort(shimOut, Canvas::PortRole::Producer),
                   ddr,     addPort(ddr,     Canvas::PortRole::Consumer));

    const auto issues = DesignVerifier().verify({&doc});
    EXPECT_TRUE(DesignVerifier::hasErrors(issues));
    const bool hasFillError = std::any_of(issues.begin(), issues.end(), [](const VerificationIssue& i) {
        return i.message.contains(QStringLiteral("Fill"));
    });
    EXPECT_TRUE(hasFillError);
}

TEST(DesignVerifierTests, MissingDrainProducesError)
{
    Canvas::CanvasDocument doc;
    auto* ddr  = makeBlock(doc, QStringLiteral("ddr"));
    auto* shim = makeBlock(doc, QStringLiteral("shim0_0"));
    auto* aie  = makeBlock(doc, QStringLiteral("aie0_0"));

    // DDR → SHIM → AIE only (fill side; no drain)
    attachWire(doc, ddr,  addPort(ddr,  Canvas::PortRole::Producer),
                   shim, addPort(shim, Canvas::PortRole::Consumer));
    attachWire(doc, shim, addPort(shim, Canvas::PortRole::Producer),
                   aie,  addPort(aie,  Canvas::PortRole::Consumer), makeFifo());

    const auto issues = DesignVerifier().verify({&doc});
    EXPECT_TRUE(DesignVerifier::hasErrors(issues));
    const bool hasDrainError = std::any_of(issues.begin(), issues.end(), [](const VerificationIssue& i) {
        return i.message.contains(QStringLiteral("Drain"));
    });
    EXPECT_TRUE(hasDrainError);
}

// ---------------------------------------------------------------------------
// Check 2 — ShimFillConnectivity
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, ShimFillWithNoOutgoingFifoProducesError)
{
    Canvas::CanvasDocument doc;
    auto* ddr  = makeBlock(doc, QStringLiteral("ddr"));
    auto* shim = makeBlock(doc, QStringLiteral("shim0_0"));

    // DDR → SHIM only; no SHIM → array FIFO
    attachWire(doc, ddr,  addPort(ddr,  Canvas::PortRole::Producer),
                   shim, addPort(shim, Canvas::PortRole::Consumer));

    const auto issues = DesignVerifier().verify({&doc});
    EXPECT_TRUE(DesignVerifier::hasErrors(issues));
    const bool hasShimFillError = std::any_of(issues.begin(), issues.end(), [](const VerificationIssue& i) {
        return i.message.contains(QStringLiteral("no FIFO leads from"));
    });
    EXPECT_TRUE(hasShimFillError);
}

// ---------------------------------------------------------------------------
// Check 3 — ShimDrainConnectivity
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, ShimDrainWithNoIncomingFifoProducesError)
{
    Canvas::CanvasDocument doc;
    auto* ddr  = makeBlock(doc, QStringLiteral("ddr"));
    auto* shim = makeBlock(doc, QStringLiteral("shim0_0"));

    // SHIM → DDR only; no array → SHIM FIFO
    attachWire(doc, shim, addPort(shim, Canvas::PortRole::Producer),
                   ddr,  addPort(ddr,  Canvas::PortRole::Consumer));

    const auto issues = DesignVerifier().verify({&doc});
    EXPECT_TRUE(DesignVerifier::hasErrors(issues));
    const bool hasShimDrainError = std::any_of(issues.begin(), issues.end(), [](const VerificationIssue& i) {
        return i.message.contains(QStringLiteral("no FIFO leads into"));
    });
    EXPECT_TRUE(hasShimDrainError);
}

// ---------------------------------------------------------------------------
// Check 4 — DisconnectedDataflow
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, AieTileWithNoOutgoingFifoProducesError)
{
    Canvas::CanvasDocument doc;
    auto* ddr  = makeBlock(doc, QStringLiteral("ddr"));
    auto* shim = makeBlock(doc, QStringLiteral("shim0_0"));
    auto* aie  = makeBlock(doc, QStringLiteral("aie0_0"));

    // DDR → SHIM → AIE; AIE has no outgoing FIFO
    attachWire(doc, ddr,  addPort(ddr,  Canvas::PortRole::Producer),
                   shim, addPort(shim, Canvas::PortRole::Consumer));
    attachWire(doc, shim, addPort(shim, Canvas::PortRole::Producer),
                   aie,  addPort(aie,  Canvas::PortRole::Consumer), makeFifo());

    const auto issues = DesignVerifier().verify({&doc});
    EXPECT_TRUE(DesignVerifier::hasErrors(issues));
    const bool hasDataflowError = std::any_of(issues.begin(), issues.end(), [](const VerificationIssue& i) {
        return i.message.contains(QStringLiteral("no outgoing FIFO"));
    });
    EXPECT_TRUE(hasDataflowError);
}

// ---------------------------------------------------------------------------
// Check 7 — ObjectFifoDimensions
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, ObjectFifoWithNoDimensionsProducesError)
{
    Canvas::CanvasDocument doc;
    buildValidPassthrough(doc);

    // Add an extra wire with an objectFifo that has no dimensions.
    auto* extra1 = makeBlock(doc, QStringLiteral("shim0_2"));
    auto* extra2 = makeBlock(doc, QStringLiteral("aie0_1"));
    attachWire(doc, extra1, addPort(extra1, Canvas::PortRole::Producer),
                   extra2, addPort(extra2, Canvas::PortRole::Consumer),
               makeFifo(QString{})); // empty dimensions

    const auto issues = DesignVerifier().verify({&doc});
    EXPECT_TRUE(DesignVerifier::hasErrors(issues));
    const bool hasDimsError = std::any_of(issues.begin(), issues.end(), [](const VerificationIssue& i) {
        return i.message.contains(QStringLiteral("no dimensions"));
    });
    EXPECT_TRUE(hasDimsError);
}

// ---------------------------------------------------------------------------
// collectStats
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, CollectStatsCountsPassthroughCorrectly)
{
    Canvas::CanvasDocument doc;
    buildValidPassthrough(doc);

    const DesignStats stats = collectStats({&doc});
    EXPECT_EQ(stats.shimTiles, 2);
    EXPECT_EQ(stats.aieTiles,  1);
    EXPECT_EQ(stats.fifos,     2); // SHIM→AIE and AIE→SHIM
    EXPECT_EQ(stats.fills,     1);
    EXPECT_EQ(stats.drains,    1);
    EXPECT_EQ(stats.splits,    0);
    EXPECT_EQ(stats.joins,     0);
    EXPECT_EQ(stats.broadcasts,0);
}

TEST(DesignVerifierTests, CollectStatsNullDocumentReturnsZeros)
{
    const DesignStats stats = collectStats({nullptr});
    EXPECT_EQ(stats.shimTiles, 0);
    EXPECT_EQ(stats.fifos,     0);
    EXPECT_EQ(stats.fills,     0);
    EXPECT_EQ(stats.drains,    0);
}

// ---------------------------------------------------------------------------
// DesignVerifier::hasErrors helper
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, HasErrorsReturnsTrueWhenErrorPresent)
{
    QList<VerificationIssue> issues;
    issues.append({VerificationIssue::Severity::Warning, QStringLiteral("warn")});
    issues.append({VerificationIssue::Severity::Error,   QStringLiteral("err")});
    EXPECT_TRUE(DesignVerifier::hasErrors(issues));
}

TEST(DesignVerifierTests, HasErrorsReturnsFalseForWarningsOnly)
{
    QList<VerificationIssue> issues;
    issues.append({VerificationIssue::Severity::Warning, QStringLiteral("warn")});
    EXPECT_FALSE(DesignVerifier::hasErrors(issues));
}

TEST(DesignVerifierTests, HasErrorsReturnsFalseForEmptyList)
{
    EXPECT_FALSE(DesignVerifier::hasErrors({}));
}

// ---------------------------------------------------------------------------
// verifyDetailed — per-check results
// ---------------------------------------------------------------------------

TEST(DesignVerifierTests, VerifyDetailedReturnsOneResultPerCheck)
{
    Canvas::CanvasDocument doc;
    const auto results = DesignVerifier().verifyDetailed({&doc});
    // DesignVerifier registers 8 checks.
    EXPECT_EQ(results.size(), 8);
    for (const auto& r : results)
        EXPECT_FALSE(r.displayName.isEmpty());
}

TEST(DesignVerifierTests, VerifyDetailedAllPassForValidDesign)
{
    Canvas::CanvasDocument doc;
    buildValidPassthrough(doc);

    const auto results = DesignVerifier().verifyDetailed({&doc});
    for (const auto& r : results) {
        EXPECT_FALSE(DesignVerifier::hasErrors(r.issues))
            << r.displayName.toStdString() << " produced errors";
    }
}
