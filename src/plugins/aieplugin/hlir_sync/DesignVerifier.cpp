// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/DesignVerifier.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QSet>

#include <optional>

namespace Aie::Internal {

// ---------------------------------------------------------------------------
// Local helpers — node taxonomy and wire topology
// ---------------------------------------------------------------------------

namespace {

/// Local node kind — includes DDR which is not an AIE tile but is a canvas block.
enum class NodeKind { SHIM, MEM, COMPUTE, DDR };

/// Parsed node kind and grid position extracted from a specId.
struct ParsedSpec {
    NodeKind kind;
    int col = 0;
    int row = 0;
};

/// Parse a specId ("shim0_0", "mem1_2", "aie0_3", "ddr") into kind and coordinates.
std::optional<ParsedSpec> parseTileSpec(const QString& specId)
{
    if (specId == QLatin1StringView("ddr"))
        return ParsedSpec{NodeKind::DDR, 0, 0};

    static const struct { const char* prefix; NodeKind kind; } prefixes[] = {
        { "shim", NodeKind::SHIM    },
        { "mem",  NodeKind::MEM     },
        { "aie",  NodeKind::COMPUTE },
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

/// A wire whose both endpoints resolve to known canvas nodes (tiles or DDR).
struct ParsedWire {
    Canvas::CanvasWire*  wire          = nullptr;
    Canvas::CanvasBlock* producerBlock = nullptr; // endpoint A
    Canvas::CanvasBlock* consumerBlock = nullptr; // endpoint B
    ParsedSpec           producerSpec{};
    ParsedSpec           consumerSpec{};
    QString              fifoName;

    // Fill: DDR drives a SHIM — data flows DDR → SHIM → array.
    bool isFill() const
    {
        return producerSpec.kind == NodeKind::DDR && consumerSpec.kind == NodeKind::SHIM;
    }

    // Drain: a SHIM delivers to DDR — data flows array → SHIM → DDR.
    bool isDrain() const
    {
        return producerSpec.kind == NodeKind::SHIM && consumerSpec.kind == NodeKind::DDR;
    }

    // Object FIFO: a data-path wire between two non-DDR tiles.
    bool isObjectFifo() const
    {
        return producerSpec.kind != NodeKind::DDR && consumerSpec.kind != NodeKind::DDR;
    }
};

/// Format a node as "Shim(0,0)", "Mem(1,2)", "AIE(0,4)", or "DDR" for use in messages.
QString tileName(const ParsedSpec& spec)
{
    switch (spec.kind) {
        case NodeKind::SHIM:    return QStringLiteral("Shim(%1,%2)").arg(spec.col).arg(spec.row);
        case NodeKind::MEM:     return QStringLiteral("Mem(%1,%2)").arg(spec.col).arg(spec.row);
        case NodeKind::COMPUTE: return QStringLiteral("AIE(%1,%2)").arg(spec.col).arg(spec.row);
        case NodeKind::DDR:     return QStringLiteral("DDR");
    }
    return QStringLiteral("Tile");
}

/// Collect all fully-attached wires whose both endpoints have known specIds (tiles or DDR).
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

/// Returns true if a hub block is a broadcast (symbol "B"), as opposed to split/join.
bool isBroadcastHub(const Canvas::CanvasBlock* block)
{
    auto* sym = dynamic_cast<const Canvas::BlockContentSymbol*>(block->content());
    return sym && sym->symbol().trimmed()
               == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Broadcast).symbol;
}

/// Extra incoming/outgoing FIFO connections contributed by split/join hub blocks.
/// These are not visible as direct tile-to-tile wires but represent real DMA
/// channel usage that must be counted in connectivity and channel-limit checks.
struct HubConnections {
    QHash<Canvas::CanvasBlock*, ParsedSpec> blockSpec;
    QHash<Canvas::CanvasBlock*, int> extraIn;   // block → additional incoming connections
    QHash<Canvas::CanvasBlock*, int> extraOut;  // block → additional outgoing connections
};

/// Derive FIFO connection counts contributed by split/join hub blocks.
///
/// Topology conventions (derived from canvas document structure):
///   SPLIT hub: pivot wire has hub at endpoint B with a Consumer port;
///              arm wires have hub at endpoint A with Producer ports.
///              → placement tile (pivot endpoint A) gains +N outgoing;
///              → each arm tile (arm endpoint B) gains +1 incoming.
///   JOIN  hub: pivot wire has hub at endpoint B with a Producer port;
///              arm wires have hub at endpoint A with Consumer ports.
///              → placement tile (pivot endpoint A) gains +N incoming;
///              → each arm tile (arm endpoint B) gains +1 outgoing.
HubConnections collectHubConnections(const Canvas::CanvasDocument& doc)
{
    HubConnections result;
    const auto& items = doc.items();

    for (const auto& item : items) {
        auto* hubBlock = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!hubBlock || !hubBlock->isLinkHub() || !hubBlock->specId().isEmpty())
            continue;

        // Build port-role lookup for this hub block.
        QHash<Canvas::PortId, Canvas::PortRole> portRoles;
        for (const auto& port : hubBlock->ports())
            portRoles[port.id] = port.role;

        Canvas::CanvasBlock* pivotBlock = nullptr;
        ParsedSpec           pivotSpec{};
        Canvas::PortRole     pivotRole  = Canvas::PortRole::Dynamic;

        struct ArmEntry { Canvas::CanvasBlock* block; ParsedSpec spec; };
        QList<ArmEntry> armTiles;

        for (const auto& wItem : items) {
            auto* wire = dynamic_cast<Canvas::CanvasWire*>(wItem.get());
            if (!wire)
                continue;

            const auto& epA = wire->a();
            const auto& epB = wire->b();
            if (!epA.attached.has_value() || !epB.attached.has_value())
                continue;

            const bool aIsHub = (epA.attached->itemId == hubBlock->id());
            const bool bIsHub = (epB.attached->itemId == hubBlock->id());
            if (!aIsHub && !bIsHub)
                continue;

            if (bIsHub) {
                // Pivot wire: placement tile is at endpoint A.
                auto* blkA = dynamic_cast<Canvas::CanvasBlock*>(
                    doc.findItem(epA.attached->itemId));
                if (!blkA || blkA->isLinkHub())
                    continue;
                const auto specA = parseTileSpec(blkA->specId());
                if (!specA || specA->kind == NodeKind::DDR)
                    continue;
                pivotBlock = blkA;
                pivotSpec  = *specA;
                pivotRole  = portRoles.value(epB.attached->portId, Canvas::PortRole::Dynamic);
            } else { // aIsHub
                // Arm wire: real tile is at endpoint B.
                auto* blkB = dynamic_cast<Canvas::CanvasBlock*>(
                    doc.findItem(epB.attached->itemId));
                if (!blkB || blkB->isLinkHub())
                    continue;
                const auto specB = parseTileSpec(blkB->specId());
                if (!specB || specB->kind == NodeKind::DDR)
                    continue;
                armTiles.append({blkB, *specB});
            }
        }

        if (!pivotBlock || armTiles.isEmpty())
            continue;

        if (pivotRole == Canvas::PortRole::Consumer && isBroadcastHub(hubBlock)) {
            // BROADCAST: placement tile gains 1 outgoing (one forward op); arm tiles gain incoming.
            result.blockSpec.insert(pivotBlock, pivotSpec);
            result.extraOut[pivotBlock] += 1;
            for (const auto& arm : armTiles) {
                result.blockSpec.insert(arm.block, arm.spec);
                result.extraIn[arm.block]++;
            }
        } else if (pivotRole == Canvas::PortRole::Consumer) {
            // SPLIT: placement tile gains N outgoing connections; arm tiles gain incoming.
            result.blockSpec.insert(pivotBlock, pivotSpec);
            result.extraOut[pivotBlock] += armTiles.size();
            for (const auto& arm : armTiles) {
                result.blockSpec.insert(arm.block, arm.spec);
                result.extraIn[arm.block]++;
            }
        } else if (pivotRole == Canvas::PortRole::Producer) {
            // JOIN: placement tile gains incoming connections; arm tiles gain outgoing.
            result.blockSpec.insert(pivotBlock, pivotSpec);
            result.extraIn[pivotBlock] += armTiles.size();
            for (const auto& arm : armTiles) {
                result.blockSpec.insert(arm.block, arm.spec);
                result.extraOut[arm.block]++;
            }
        }
    }

    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Check 1 — RuntimeSequenceDefined
//
// A valid design needs at least one Fill (DDR → SHIM) and at least one
// Drain (SHIM → DDR) to form a complete runtime I/O sequence.
// ---------------------------------------------------------------------------

class RuntimeSequenceCheck : public IVerificationCheck
{
public:
    QString name() const override { return QStringLiteral("RuntimeSequenceDefined"); }
    QString displayName() const override { return QStringLiteral("Checking runtime I/O sequence"); }

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
                QStringLiteral("No Fill defined — connect DDR to a SHIM tile "
                               "(DDR \u2192 SHIM \u2192 array).")});

        if (drainCount == 0)
            issues.append({VerificationIssue::Severity::Error,
                QStringLiteral("No Drain defined — connect a SHIM tile to DDR "
                               "(array \u2192 SHIM \u2192 DDR).")});

        return issues;
    }
};

// ---------------------------------------------------------------------------
// Check 2 — ShimFillConnectivity
//
// Each SHIM tile that receives a Fill (DDR → SHIM) must also have at least
// one object FIFO going OUT into the array (SHIM → MEM or AIE). Without it
// the incoming DDR data has nowhere to go.
// ---------------------------------------------------------------------------

class ShimFillCheck : public IVerificationCheck
{
public:
    QString name() const override { return QStringLiteral("ShimFillConnectivity"); }
    QString displayName() const override { return QStringLiteral("Checking SHIM fill connectivity"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        const auto wires = collectWires(*ctx.document);

        // SHIM blocks that produce at least one outgoing object FIFO (SHIM → MEM/AIE).
        QSet<Canvas::CanvasBlock*> shimsWithOutgoingFifo;
        for (const auto& w : wires) {
            if (w.isObjectFifo() && w.producerSpec.kind == NodeKind::SHIM)
                shimsWithOutgoingFifo.insert(w.producerBlock);
        }

        // Every fill SHIM must have an outgoing FIFO.
        for (const auto& w : wires) {
            if (!w.isFill())
                continue;
            // consumerBlock is the SHIM tile in a DDR → SHIM wire.
            if (!shimsWithOutgoingFifo.contains(w.consumerBlock)) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("Fill at %1: no FIFO leads from this SHIM into the array.")
                    .arg(tileName(w.consumerSpec))});
            }
        }

        return issues;
    }
};

// ---------------------------------------------------------------------------
// Check 3 — ShimDrainConnectivity
//
// Each SHIM tile that feeds a Drain (SHIM → DDR) must also have at least
// one object FIFO coming IN from the array (MEM or AIE → SHIM). Without it
// there is no array-computed data to drain.
// ---------------------------------------------------------------------------

class ShimDrainCheck : public IVerificationCheck
{
public:
    QString name() const override { return QStringLiteral("ShimDrainConnectivity"); }
    QString displayName() const override { return QStringLiteral("Checking SHIM drain connectivity"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        const auto wires = collectWires(*ctx.document);

        // SHIM blocks that consume at least one incoming object FIFO (MEM/AIE → SHIM).
        QSet<Canvas::CanvasBlock*> shimsWithIncomingFifo;
        for (const auto& w : wires) {
            if (w.isObjectFifo() && w.consumerSpec.kind == NodeKind::SHIM)
                shimsWithIncomingFifo.insert(w.consumerBlock);
        }

        // Every drain SHIM must have an incoming FIFO.
        for (const auto& w : wires) {
            if (!w.isDrain())
                continue;
            // producerBlock is the SHIM tile in a SHIM → DDR wire.
            if (!shimsWithIncomingFifo.contains(w.producerBlock)) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("Drain at %1: no FIFO leads into this SHIM from the array.")
                    .arg(tileName(w.producerSpec))});
            }
        }

        return issues;
    }
};

// ---------------------------------------------------------------------------
// Check 4 — DisconnectedDataflow
//
// Every MEM/AIE tile that participates in the dataflow must have FIFO
// connections on BOTH sides — at least one incoming and at least one
// outgoing. SHIM tiles are excluded because they are intentional endpoints
// (one side connects to DDR, the other to the array via FIFO). DDR is
// excluded because it is the external memory boundary.
// ---------------------------------------------------------------------------

class DisconnectedDataflowCheck : public IVerificationCheck
{
public:
    QString name() const override { return QStringLiteral("DisconnectedDataflow"); }
    QString displayName() const override { return QStringLiteral("Checking connected dataflow"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        // Count in/out object FIFO connections for MEM/AIE tiles only.
        QHash<Canvas::CanvasBlock*, ParsedSpec> blockSpec;
        QHash<Canvas::CanvasBlock*, int> inCount;
        QHash<Canvas::CanvasBlock*, int> outCount;

        for (const auto& w : collectWires(*ctx.document)) {
            if (!w.isObjectFifo())
                continue; // DDR wires are handled by ShimFill/DrainCheck

            if (w.producerSpec.kind != NodeKind::SHIM) {
                blockSpec.insert(w.producerBlock, w.producerSpec);
                outCount[w.producerBlock]++;
            }
            if (w.consumerSpec.kind != NodeKind::SHIM) {
                blockSpec.insert(w.consumerBlock, w.consumerSpec);
                inCount[w.consumerBlock]++;
            }
        }

        // Augment with split/join hub connections (arm wires count as real FIFO paths).
        const auto hub = collectHubConnections(*ctx.document);
        for (auto it = hub.extraIn.cbegin(); it != hub.extraIn.cend(); ++it) {
            blockSpec.insert(it.key(), hub.blockSpec.value(it.key()));
            inCount[it.key()] += it.value();
        }
        for (auto it = hub.extraOut.cbegin(); it != hub.extraOut.cend(); ++it) {
            blockSpec.insert(it.key(), hub.blockSpec.value(it.key()));
            outCount[it.key()] += it.value();
        }

        // Tile has outgoing FIFOs but no incoming — data appears from nowhere.
        for (auto it = outCount.cbegin(); it != outCount.cend(); ++it) {
            Canvas::CanvasBlock* block = it.key();
            if (!inCount.contains(block)) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("%1 has no incoming FIFO — no upstream data source.")
                    .arg(tileName(blockSpec[block]))});
            }
        }

        // Tile has incoming FIFOs but no outgoing — data flows in and goes nowhere.
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
// Each tile type has a fixed number of DMA channels. All connections
// (both DDR wires and object FIFO wires) count toward a tile's channel
// budget. DDR itself is excluded — it is not a real tile with a channel limit.
//   SHIM   — 4 channels  (error at 5 or more)
//   MEM    — 6 channels  (error at 7 or more)
//   AIE    — 4 channels  (error at 5 or more)
// ---------------------------------------------------------------------------

class DmaChannelLimitCheck : public IVerificationCheck
{
    static int channelLimit(NodeKind kind)
    {
        switch (kind) {
            case NodeKind::SHIM:    return 4;
            case NodeKind::MEM:     return 6;
            case NodeKind::COMPUTE: return 4;
            case NodeKind::DDR:     return INT_MAX;
        }
        return 4;
    }

public:
    QString name() const override { return QStringLiteral("DmaChannelLimit"); }
    QString displayName() const override { return QStringLiteral("Checking DMA channel limits"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        // Count all connections (DDR wires + FIFO wires) for every non-DDR tile.
        QHash<Canvas::CanvasBlock*, ParsedSpec> blockSpec;
        QHash<Canvas::CanvasBlock*, int> connectionCount;

        for (const auto& w : collectWires(*ctx.document)) {
            if (w.producerSpec.kind != NodeKind::DDR) {
                blockSpec.insert(w.producerBlock, w.producerSpec);
                connectionCount[w.producerBlock]++;
            }
            if (w.consumerSpec.kind != NodeKind::DDR) {
                blockSpec.insert(w.consumerBlock, w.consumerSpec);
                connectionCount[w.consumerBlock]++;
            }
        }

        // Augment with split/join hub connections — each arm wire uses one DMA channel.
        const auto hub = collectHubConnections(*ctx.document);
        for (auto it = hub.extraIn.cbegin(); it != hub.extraIn.cend(); ++it) {
            blockSpec.insert(it.key(), hub.blockSpec.value(it.key()));
            connectionCount[it.key()] += it.value();
        }
        for (auto it = hub.extraOut.cbegin(); it != hub.extraOut.cend(); ++it) {
            blockSpec.insert(it.key(), hub.blockSpec.value(it.key()));
            connectionCount[it.key()] += it.value();
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
// Check 6 — SplitJoinDivisibility
//
// For a split or join to produce equal-sized sub-FIFOs, the source FIFO's
// element count must be exactly divisible by the number of arms. If not,
// the offset stride would be fractional and code generation would produce
// incorrect memory layouts.
// ---------------------------------------------------------------------------

class SplitJoinDivisibilityCheck : public IVerificationCheck
{
    // Compute total element count from "1024" → 1024, "32x4" → 128, "" → 1024.
    static int elementCountFromDims(const QString& dims)
    {
        if (dims.isEmpty())
            return 1024;
        int count = 1;
        for (const QString& d : dims.split(u'x', Qt::SkipEmptyParts))
            count *= d.trimmed().toInt();
        return count;
    }

public:
    QString name() const override { return QStringLiteral("SplitJoinDivisibility"); }
    QString displayName() const override { return QStringLiteral("Checking split/join divisibility"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        const auto& items = ctx.document->items();

        for (const auto& item : items) {
            auto* hubBlock = dynamic_cast<Canvas::CanvasBlock*>(item.get());
            if (!hubBlock || !hubBlock->isLinkHub() || !hubBlock->specId().isEmpty())
                continue;

            // Broadcasts forward the whole FIFO unchanged — divisibility does not apply.
            if (isBroadcastHub(hubBlock))
                continue;

            // Build port-role lookup for this hub.
            QHash<Canvas::PortId, Canvas::PortRole> portRoles;
            for (const auto& port : hubBlock->ports())
                portRoles[port.id] = port.role;

            Canvas::CanvasBlock* pivotBlock = nullptr;
            Canvas::PortRole     pivotRole  = Canvas::PortRole::Dynamic;
            int                  numArms    = 0;

            for (const auto& wItem : items) {
                auto* wire = dynamic_cast<Canvas::CanvasWire*>(wItem.get());
                if (!wire)
                    continue;

                const auto& epA = wire->a();
                const auto& epB = wire->b();
                if (!epA.attached.has_value() || !epB.attached.has_value())
                    continue;

                const bool aIsHub = (epA.attached->itemId == hubBlock->id());
                const bool bIsHub = (epB.attached->itemId == hubBlock->id());
                if (!aIsHub && !bIsHub)
                    continue;

                if (bIsHub) {
                    auto* blkA = dynamic_cast<Canvas::CanvasBlock*>(
                        ctx.document->findItem(epA.attached->itemId));
                    if (blkA && !blkA->isLinkHub()) {
                        pivotBlock = blkA;
                        pivotRole  = portRoles.value(epB.attached->portId,
                                                     Canvas::PortRole::Dynamic);
                    }
                } else {
                    auto* blkB = dynamic_cast<Canvas::CanvasBlock*>(
                        ctx.document->findItem(epB.attached->itemId));
                    if (blkB && !blkB->isLinkHub())
                        ++numArms;
                }
            }

            if (!pivotBlock || numArms == 0)
                continue;

            // Find the source (SPLIT) or destination (JOIN) FIFO wire and read its type.
            int fifoElemCount = 1024;
            for (const auto& wItem : items) {
                auto* wire = dynamic_cast<Canvas::CanvasWire*>(wItem.get());
                if (!wire)
                    continue;

                const auto& epA = wire->a();
                const auto& epB = wire->b();
                if (!epA.attached.has_value() || !epB.attached.has_value())
                    continue;

                auto* blkA = dynamic_cast<Canvas::CanvasBlock*>(
                    ctx.document->findItem(epA.attached->itemId));
                auto* blkB = dynamic_cast<Canvas::CanvasBlock*>(
                    ctx.document->findItem(epB.attached->itemId));
                if (!blkA || !blkB || blkA->isLinkHub() || blkB->isLinkHub())
                    continue;

                // SPLIT: source FIFO has the placement tile as its consumer (endpoint B).
                // JOIN:  dest   FIFO has the placement tile as its producer (endpoint A).
                const bool isRelevant =
                    (pivotRole == Canvas::PortRole::Consumer && blkB == pivotBlock) ||
                    (pivotRole == Canvas::PortRole::Producer && blkA == pivotBlock);

                if (isRelevant) {
                    if (wire->hasObjectFifo())
                        fifoElemCount = elementCountFromDims(
                            wire->objectFifo().value().type.dimensions);
                    break;
                }
            }

            if (fifoElemCount % numArms != 0) {
                const auto spec = parseTileSpec(pivotBlock->specId());
                const QString kind = (pivotRole == Canvas::PortRole::Consumer)
                    ? QStringLiteral("Split")
                    : QStringLiteral("Join");
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("%1 at %2: FIFO has %3 elements but %4 arms — "
                                   "not evenly divisible.")
                    .arg(kind,
                         spec ? tileName(*spec) : pivotBlock->specId(),
                         QString::number(fifoElemCount),
                         QString::number(numArms))});
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
    m_checks.push_back(std::make_unique<SplitJoinDivisibilityCheck>());
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

QList<CheckResult> DesignVerifier::verifyDetailed(const VerificationContext& ctx) const
{
    // Run every check individually and return per-check results for progress logging.
    QList<CheckResult> results;
    results.reserve(static_cast<int>(m_checks.size()));
    for (const auto& check : m_checks)
        results.append({check->displayName(), check->run(ctx)});
    return results;
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

    // Count tiles reachable via direct wires.
    QHash<Canvas::CanvasBlock*, ParsedSpec> connectedTiles;

    for (const auto& w : collectWires(*ctx.document)) {
        if (w.isObjectFifo()) ++stats.fifos;
        if (w.isFill())       ++stats.fills;
        if (w.isDrain())      ++stats.drains;

        if (w.producerSpec.kind != NodeKind::DDR)
            connectedTiles.insert(w.producerBlock, w.producerSpec);
        if (w.consumerSpec.kind != NodeKind::DDR)
            connectedTiles.insert(w.consumerBlock, w.consumerSpec);
    }

    // Also count tiles that are only reachable via split/join arm wires,
    // and count the split/join hub blocks themselves.
    const auto hub = collectHubConnections(*ctx.document);
    for (auto it = hub.blockSpec.cbegin(); it != hub.blockSpec.cend(); ++it)
        connectedTiles.insert(it.key(), it.value());

    // Count hub blocks as splits or joins.
    for (const auto& item : ctx.document->items()) {
        auto* hubBlock = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!hubBlock || !hubBlock->isLinkHub() || !hubBlock->specId().isEmpty())
            continue;

        QHash<Canvas::PortId, Canvas::PortRole> portRoles;
        for (const auto& port : hubBlock->ports())
            portRoles[port.id] = port.role;

        // Determine hub type: broadcasts first (also have Consumer pivot role), then split/join.
        if (isBroadcastHub(hubBlock)) {
            ++stats.broadcasts;
            continue;
        }

        for (const auto& wItem : ctx.document->items()) {
            auto* wire = dynamic_cast<Canvas::CanvasWire*>(wItem.get());
            if (!wire)
                continue;
            const auto& epB = wire->b();
            if (!epB.attached.has_value() || epB.attached->itemId != hubBlock->id())
                continue;
            const Canvas::PortRole role = portRoles.value(epB.attached->portId,
                                                           Canvas::PortRole::Dynamic);
            if (role == Canvas::PortRole::Consumer) ++stats.splits;
            else if (role == Canvas::PortRole::Producer) ++stats.joins;
            break;
        }
    }

    for (auto it = connectedTiles.cbegin(); it != connectedTiles.cend(); ++it) {
        switch (it.value().kind) {
            case NodeKind::SHIM:    ++stats.shimTiles; break;
            case NodeKind::MEM:     ++stats.memTiles;  break;
            case NodeKind::COMPUTE: ++stats.aieTiles;  break;
            case NodeKind::DDR:     break;
        }
    }

    return stats;
}

} // namespace Aie::Internal
