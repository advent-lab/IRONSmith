// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/DesignVerifier.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasWire.hpp"
#include "hlir_cpp_bridge/HlirTypes.hpp"

#include <QtCore/QHash>
#include <QtCore/QSet>

#include <optional>

namespace Aie::Internal {

// ---------------------------------------------------------------------------
// Local helpers — wire topology analysis
// ---------------------------------------------------------------------------

namespace {

/// Parsed tile kind and grid position extracted from a specId.
struct ParsedSpec {
    hlir::TileKind kind;
    int col = 0;
    int row = 0;
};

/// Parse a tile specId ("shim0_0", "mem1_2", "aie0_3") into kind and coordinates.
std::optional<ParsedSpec> parseTileSpec(const QString& specId)
{
    static const struct { const char* prefix; hlir::TileKind kind; } prefixes[] = {
        { "shim", hlir::TileKind::SHIM    },
        { "mem",  hlir::TileKind::MEM     },
        { "aie",  hlir::TileKind::COMPUTE },
    };

    for (const auto& p : prefixes) {
        const QLatin1StringView prefix{p.prefix};
        if (!specId.startsWith(prefix))
            continue;

        const QString rest = specId.sliced(prefix.size());
        const qsizetype underIdx = rest.indexOf(u'_');
        if (underIdx < 0)
            continue;

        bool okCol = false, okRow = false;
        const int col = rest.left(underIdx).toInt(&okCol);
        const int row = rest.sliced(underIdx + 1).toInt(&okRow);
        if (!okCol || !okRow)
            continue;

        return ParsedSpec{p.kind, col, row};
    }
    return std::nullopt;
}

/// A wire whose both endpoints resolve to tiles with valid specIds.
struct ParsedWire {
    Canvas::CanvasWire*  wire          = nullptr;
    Canvas::CanvasBlock* producerBlock = nullptr; // endpoint A (data source in this FIFO)
    Canvas::CanvasBlock* consumerBlock = nullptr; // endpoint B (data sink in this FIFO)
    ParsedSpec           producerSpec{};
    ParsedSpec           consumerSpec{};
    QString              fifoName;

    // A wire is a Fill when the SHIM tile is the FIFO producer —
    // the SHIM serves as the DDR input gateway (DDR → SHIM → array tile).
    bool isFill()  const { return producerSpec.kind == hlir::TileKind::SHIM; }

    // A wire is a Drain when the SHIM tile is the FIFO consumer —
    // the SHIM serves as the DDR output gateway (array tile → SHIM → DDR).
    bool isDrain() const { return consumerSpec.kind == hlir::TileKind::SHIM; }
};

/// Format a tile as "Shim(0,0)", "Mem(1,2)", or "AIE(0,4)" for use in messages.
QString tileName(const ParsedSpec& spec)
{
    const char* kind = "Tile";
    switch (spec.kind) {
        case hlir::TileKind::SHIM:    kind = "Shim"; break;
        case hlir::TileKind::MEM:     kind = "Mem";  break;
        case hlir::TileKind::COMPUTE: kind = "AIE";  break;
    }
    return QStringLiteral("%1(%2,%3)").arg(QLatin1StringView(kind)).arg(spec.col).arg(spec.row);
}

/// Collect all fully-attached wires whose both endpoints have valid specIds.
QList<ParsedWire> collectWires(const Canvas::CanvasDocument& doc)
{
    QList<ParsedWire> result;

    for (const auto& item : doc.items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire)
            continue;

        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;

        auto* blockA = dynamic_cast<Canvas::CanvasBlock*>(doc.findItem(epA.attached->itemId));
        auto* blockB = dynamic_cast<Canvas::CanvasBlock*>(doc.findItem(epB.attached->itemId));
        if (!blockA || !blockB)
            continue;

        const auto parsedA = parseTileSpec(blockA->specId());
        const auto parsedB = parseTileSpec(blockB->specId());
        if (!parsedA || !parsedB)
            continue;

        QString fifoName;
        if (wire->hasObjectFifo())
            fifoName = wire->objectFifo().value().name;
        else
            fifoName = QStringLiteral("fifo_%1_to_%2").arg(blockA->specId(), blockB->specId());

        result.append(ParsedWire{wire, blockA, blockB, *parsedA, *parsedB, fifoName});
    }

    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Check 1 — RuntimeSequenceDefined
//
// A valid design needs at least one Fill (DDR → SHIM → array) and at least one
// Drain (array → SHIM → DDR) to form a complete runtime I/O sequence.
// ---------------------------------------------------------------------------

class RuntimeSequenceCheck : public IVerificationCheck
{
public:
    QString name() const override { return QStringLiteral("RuntimeSequenceDefined"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        int fillCount = 0;
        int drainCount = 0;
        for (const auto& w : collectWires(*ctx.document)) {
            if (w.isFill())  ++fillCount;
            if (w.isDrain()) ++drainCount;
        }

        if (fillCount == 0)
            issues.append({VerificationIssue::Severity::Error,
                QStringLiteral("No Fill defined — connect a SHIM tile as a FIFO source "
                               "(DDR \u2192 SHIM \u2192 array).")});

        if (drainCount == 0)
            issues.append({VerificationIssue::Severity::Error,
                QStringLiteral("No Drain defined — connect a SHIM tile as a FIFO destination "
                               "(array \u2192 SHIM \u2192 DDR).")});

        return issues;
    }
};

// ---------------------------------------------------------------------------
// Check 2 — ShimFillConnectivity
//
// A Fill brings data from DDR into the device through a SHIM tile. The SHIM
// therefore needs a valid object FIFO going OUT into the array (to a MEM or
// AIE tile). Connecting a fill SHIM directly to another SHIM tile means the
// incoming DDR data has nowhere to go in the array.
// ---------------------------------------------------------------------------

class ShimFillCheck : public IVerificationCheck
{
public:
    QString name() const override { return QStringLiteral("ShimFillConnectivity"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        for (const auto& w : collectWires(*ctx.document)) {
            if (!w.isFill())
                continue;

            // The fill SHIM is the producer (endpoint A). The array-side tile is the consumer
            // (endpoint B). Verify that endpoint B is a MEM or AIE tile, not another SHIM.
            if (w.consumerSpec.kind == hlir::TileKind::SHIM) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("Fill at %1: FIFO '%2' goes to %3 instead of a MEM or AIE tile.")
                    .arg(tileName(w.producerSpec), w.fifoName, tileName(w.consumerSpec))});
            }
        }

        return issues;
    }
};

// ---------------------------------------------------------------------------
// Check 3 — ShimDrainConnectivity
//
// A Drain collects results from the device array and transfers them to DDR
// through a SHIM tile. The SHIM therefore needs a valid object FIFO coming IN
// from the array (from a MEM or AIE tile). A drain SHIM connected directly to
// another SHIM means there is no array-computed data to drain.
// ---------------------------------------------------------------------------

class ShimDrainCheck : public IVerificationCheck
{
public:
    QString name() const override { return QStringLiteral("ShimDrainConnectivity"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        for (const auto& w : collectWires(*ctx.document)) {
            if (!w.isDrain())
                continue;

            // The drain SHIM is the consumer (endpoint B). The array-side tile is the producer
            // (endpoint A). Verify that endpoint A is a MEM or AIE tile, not another SHIM.
            if (w.producerSpec.kind == hlir::TileKind::SHIM) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("Drain at %1: FIFO '%2' comes from %3 instead of a MEM or AIE tile.")
                    .arg(tileName(w.consumerSpec), w.fifoName, tileName(w.producerSpec))});
            }
        }

        return issues;
    }
};

// ---------------------------------------------------------------------------
// Check 4 — DisconnectedDataflow
//
// Every non-SHIM tile that participates in the dataflow must have FIFO
// connections on BOTH sides — at least one incoming FIFO (data in) and at
// least one outgoing FIFO (data out). A tile with only inputs creates a dead
// end; a tile with only outputs has no data source. SHIM tiles are excluded
// because they are intentionally one-directional (fill = output only,
// drain = input only).
// ---------------------------------------------------------------------------

class DisconnectedDataflowCheck : public IVerificationCheck
{
public:
    QString name() const override { return QStringLiteral("DisconnectedDataflow"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        // Count each non-SHIM tile's incoming and outgoing FIFO connections.
        // SHIM tiles are skipped — they are the intentional endpoints of the flow.
        QHash<Canvas::CanvasBlock*, ParsedSpec> blockSpec;
        QHash<Canvas::CanvasBlock*, int> inCount;
        QHash<Canvas::CanvasBlock*, int> outCount;

        for (const auto& w : collectWires(*ctx.document)) {
            if (w.producerSpec.kind != hlir::TileKind::SHIM) {
                blockSpec.insert(w.producerBlock, w.producerSpec);
                outCount[w.producerBlock]++;
            }
            if (w.consumerSpec.kind != hlir::TileKind::SHIM) {
                blockSpec.insert(w.consumerBlock, w.consumerSpec);
                inCount[w.consumerBlock]++;
            }
        }

        // Tile has outgoing FIFOs but no incoming — data appears from nowhere
        for (auto it = outCount.cbegin(); it != outCount.cend(); ++it) {
            Canvas::CanvasBlock* block = it.key();
            if (!inCount.contains(block)) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("%1 has no incoming FIFO — no upstream data source.")
                    .arg(tileName(blockSpec[block]))});
            }
        }

        // Tile has incoming FIFOs but no outgoing — data flows in and goes nowhere
        for (auto it = inCount.cbegin(); it != inCount.cend(); ++it) {
            Canvas::CanvasBlock* block = it.key();
            if (!outCount.contains(block)) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("%1 has no outgoing FIFO — data has no downstream path.")
                    .arg(tileName(blockSpec[block]))});
            }
        }

        return issues;
    }
};

// ---------------------------------------------------------------------------
// Check 5 — DmaChannelLimit
//
// Each tile type has a fixed number of DMA channels available for FIFO
// connections. Exceeding the channel count means the hardware cannot support
// the design as laid out.
//   SHIM   — 4 channels  (error if 5 or more connections)
//   MEM    — 6 channels  (error if 7 or more connections)
//   AIE    — 4 channels  (error if 5 or more connections)
// ---------------------------------------------------------------------------

class DmaChannelLimitCheck : public IVerificationCheck
{
    static int channelLimit(hlir::TileKind kind)
    {
        switch (kind) {
            case hlir::TileKind::SHIM:    return 4;
            case hlir::TileKind::MEM:     return 6;
            case hlir::TileKind::COMPUTE: return 4;
        }
        return 4;
    }

public:
    QString name() const override { return QStringLiteral("DmaChannelLimit"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        // Count total connections (in + out) for every tile that appears in a wire.
        QHash<Canvas::CanvasBlock*, ParsedSpec> blockSpec;
        QHash<Canvas::CanvasBlock*, int> connectionCount;

        for (const auto& w : collectWires(*ctx.document)) {
            blockSpec.insert(w.producerBlock, w.producerSpec);
            blockSpec.insert(w.consumerBlock, w.consumerSpec);
            connectionCount[w.producerBlock]++;
            connectionCount[w.consumerBlock]++;
        }

        for (auto it = connectionCount.cbegin(); it != connectionCount.cend(); ++it) {
            const ParsedSpec& spec = blockSpec[it.key()];
            const int limit = channelLimit(spec.kind);
            if (it.value() > limit) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("%1 has %2 connections but only %3 DMA channels are available.")
                    .arg(tileName(spec)).arg(it.value()).arg(limit)});
            }
        }

        return issues;
    }
};

// ---------------------------------------------------------------------------
// DesignVerifier
// ---------------------------------------------------------------------------

DesignVerifier::DesignVerifier()
{
    // Register checks in the order they will be reported to the user.
    m_checks.push_back(std::make_unique<RuntimeSequenceCheck>());
    m_checks.push_back(std::make_unique<ShimFillCheck>());
    m_checks.push_back(std::make_unique<ShimDrainCheck>());
    m_checks.push_back(std::make_unique<DisconnectedDataflowCheck>());
    m_checks.push_back(std::make_unique<DmaChannelLimitCheck>());
}

DesignVerifier::~DesignVerifier() = default;

QList<VerificationIssue> DesignVerifier::verify(const VerificationContext& ctx) const
{
    // Run every registered check and collect all issues.
    QList<VerificationIssue> all;
    for (const auto& check : m_checks)
        all.append(check->run(ctx));
    return all;
}

bool DesignVerifier::hasErrors(const QList<VerificationIssue>& issues)
{
    for (const auto& issue : issues) {
        if (issue.severity == VerificationIssue::Severity::Error)
            return true;
    }
    return false;
}

DesignStats collectStats(const VerificationContext& ctx)
{
    DesignStats stats;
    if (!ctx.document)
        return stats;

    // Only count tiles that participate in at least one FIFO connection.
    QHash<Canvas::CanvasBlock*, ParsedSpec> connectedTiles;

    for (const auto& w : collectWires(*ctx.document)) {
        ++stats.fifos;
        if (w.isFill())  ++stats.fills;
        if (w.isDrain()) ++stats.drains;

        connectedTiles.insert(w.producerBlock, w.producerSpec);
        connectedTiles.insert(w.consumerBlock, w.consumerSpec);
    }

    for (auto it = connectedTiles.cbegin(); it != connectedTiles.cend(); ++it) {
        switch (it.value().kind) {
            case hlir::TileKind::SHIM:    ++stats.shimTiles; break;
            case hlir::TileKind::MEM:     ++stats.memTiles;  break;
            case hlir::TileKind::COMPUTE: ++stats.aieTiles;  break;
        }
    }

    return stats;
}

} // namespace Aie::Internal
