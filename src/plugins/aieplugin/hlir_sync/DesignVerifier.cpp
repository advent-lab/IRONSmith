// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/DesignVerifier.hpp"
#include "aieplugin/hlir_sync/TileFifoInfo.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"

#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QSet>

#include <functional>

#include <algorithm>
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

/// An entry representing one FillDrain wire plus the resolved SHIM arm tiles.
/// Handles both hub-based topology (DDR ↔ Distribute/Collect hub ↔ SHIMs)
/// and direct DDR ↔ SHIM wires.
struct FillDrainEntry {
    Canvas::CanvasWire*         wire;
    bool                        isFill;     // true = Distribute (DDR → SHIMs), false = Collect (SHIMs → DDR)
    Canvas::CanvasBlock*        ddrBlock;
    Canvas::CanvasBlock*        otherBlock; // hub or direct SHIM
    QList<Canvas::CanvasBlock*> shimArms;   // resolved SHIM tiles
};

QList<FillDrainEntry> collectFillDrainEntries(const Canvas::CanvasDocument& doc)
{
    // Pre-build port-role lookup for every hub block so we can infer fill/drain
    // direction for wires that carry no annotation (neither FillDrainConfig nor
    // ObjectFifoConfig with Fill/Drain operation).
    QHash<Canvas::ObjectId, QHash<Canvas::PortId, Canvas::PortRole>> hubPortRoles;
    for (const auto& item : doc.items()) {
        auto* hub = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!hub || !hub->isLinkHub()) continue;
        QHash<Canvas::PortId, Canvas::PortRole> roles;
        for (const auto& port : hub->ports())
            roles[port.id] = port.role;
        hubPortRoles[hub->id()] = roles;
    }

    QList<FillDrainEntry> result;

    for (const auto& item : doc.items()) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire) continue;

        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;

        auto* blkA = dynamic_cast<Canvas::CanvasBlock*>(doc.findItem(epA.attached->itemId));
        auto* blkB = dynamic_cast<Canvas::CanvasBlock*>(doc.findItem(epB.attached->itemId));
        if (!blkA || !blkB) continue;

        // Detection is purely topology-based: one endpoint must be the DDR block.
        Canvas::CanvasBlock* ddrBlock   = nullptr;
        Canvas::CanvasBlock* otherBlock = nullptr;
        const Canvas::CanvasWire::Endpoint* hubEp = nullptr; // endpoint at the non-DDR side
        if (blkA->specId().trimmed() == QLatin1StringView("ddr")) {
            ddrBlock = blkA; otherBlock = blkB; hubEp = &epB;
        } else if (blkB->specId().trimmed() == QLatin1StringView("ddr")) {
            ddrBlock = blkB; otherBlock = blkA; hubEp = &epA;
        } else {
            continue;
        }

        // Determine fill/drain direction — annotation first, then topology inference.
        bool isFill = true;
        if (wire->hasFillDrain()) {
            isFill = wire->fillDrain()->isFill;
        } else if (wire->hasObjectFifo()) {
            // Backward-compat: old designs may still carry ObjectFifo Fill/Drain.
            const auto op = wire->objectFifo()->operation;
            if (op == Canvas::CanvasWire::ObjectFifoOperation::Fill)
                isFill = true;
            else if (op == Canvas::CanvasWire::ObjectFifoOperation::Drain)
                isFill = false;
            else
                continue; // ObjectFifo but not Fill/Drain
        } else if (otherBlock->isLinkHub() && hubEp->attached.has_value()) {
            // No annotation: infer from the hub port role.
            // Consumer port → hub receives from DDR → Distribute → isFill=true.
            // Producer port → hub sends to DDR    → Collect    → isFill=false.
            const auto& roles = hubPortRoles.value(otherBlock->id());
            const auto role = roles.value(hubEp->attached->portId, Canvas::PortRole::Dynamic);
            if (role == Canvas::PortRole::Consumer)
                isFill = true;
            else if (role == Canvas::PortRole::Producer)
                isFill = false;
            else
                continue; // Dynamic role — can't determine direction
        } else {
            // No annotation, not a hub — infer from the SHIM port role for direct DDR↔SHIM wires.
            if (!hubEp->attached.has_value())
                continue;
            const auto spec = parseTileSpec(otherBlock->specId());
            if (!spec || spec->kind != NodeKind::SHIM)
                continue;
            Canvas::PortRole shimPortRole = Canvas::PortRole::Dynamic;
            for (const auto& port : otherBlock->ports()) {
                if (port.id == hubEp->attached->portId) {
                    shimPortRole = port.role;
                    break;
                }
            }
            if (shimPortRole == Canvas::PortRole::Consumer)
                isFill = true;
            else if (shimPortRole == Canvas::PortRole::Producer)
                isFill = false;
            else
                continue;
        }

        FillDrainEntry entry;
        entry.wire       = wire;
        entry.isFill     = isFill;
        entry.ddrBlock   = ddrBlock;
        entry.otherBlock = otherBlock;

        if (otherBlock->isLinkHub()) {
            // Collect SHIM arm tiles from all other wires touching this hub.
            for (const auto& wItem : doc.items()) {
                auto* armWire = dynamic_cast<Canvas::CanvasWire*>(wItem.get());
                if (!armWire || armWire == wire) continue;
                const auto& aEp = armWire->a();
                const auto& bEp = armWire->b();
                if (!aEp.attached.has_value() || !bEp.attached.has_value()) continue;
                Canvas::ObjectId armEndpointId;
                if (aEp.attached->itemId == otherBlock->id())
                    armEndpointId = bEp.attached->itemId;
                else if (bEp.attached->itemId == otherBlock->id())
                    armEndpointId = aEp.attached->itemId;
                else
                    continue;
                auto* armBlock = dynamic_cast<Canvas::CanvasBlock*>(doc.findItem(armEndpointId));
                if (!armBlock || armBlock->isLinkHub()) continue;
                const auto armSpec = parseTileSpec(armBlock->specId());
                if (!armSpec || armSpec->kind != NodeKind::SHIM) continue;
                entry.shimArms.append(armBlock);
            }
        } else {
            // Direct DDR ↔ SHIM — no hub.
            const auto spec = parseTileSpec(otherBlock->specId());
            if (spec && spec->kind == NodeKind::SHIM)
                entry.shimArms.append(otherBlock);
        }

        result.append(entry);
    }

    return result;
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

/// Returns true if a hub block is a Distribute or Collect hub (DDR fan-out / fan-in).
bool isDistributeOrCollectHub(const Canvas::CanvasBlock* block)
{
    auto* sym = dynamic_cast<const Canvas::BlockContentSymbol*>(block->content());
    if (!sym) return false;
    const QString s = sym->symbol().trimmed();
    return s == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Distribute).symbol
        || s == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Collect).symbol;
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
        bool                 ddrIsPivot = false; // true for Distribute/Collect hubs

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
                if (!specA)
                    continue;
                if (specA->kind == NodeKind::DDR) {
                    // Distribute/Collect hub: DDR is the pivot.
                    // DDR has no DMA channel limit — record pivotRole only.
                    pivotRole  = portRoles.value(epB.attached->portId, Canvas::PortRole::Dynamic);
                    ddrIsPivot = true;
                    continue;
                }
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

        // Distribute/Collect hubs (ddrIsPivot) carry DDR↔SHIM data paths.
        // Like direct DDR↔SHIM wires, these do NOT consume SHIM DMA channels.
        if (ddrIsPivot || !pivotBlock || armTiles.isEmpty())
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
        for (const auto& entry : collectFillDrainEntries(*ctx.document)) {
            if (entry.isFill)  ++fillCount;
            else               ++drainCount;
        }

        if (fillCount == 0)
            issues.append({VerificationIssue::Severity::Error,
                QStringLiteral("No Fill defined — connect DDR to a Distribute hub or SHIM tile "
                               "(DDR \u2192 Distribute \u2192 SHIMs \u2192 array).")});

        if (drainCount == 0)
            issues.append({VerificationIssue::Severity::Error,
                QStringLiteral("No Drain defined — connect a Collect hub or SHIM tile to DDR "
                               "(array \u2192 SHIMs \u2192 Collect \u2192 DDR).")});

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

        // SHIM blocks that produce at least one outgoing object FIFO (SHIM → MEM/AIE).
        QSet<Canvas::CanvasBlock*> shimsWithOutgoingFifo;
        for (const auto& w : collectWires(*ctx.document)) {
            if (w.isObjectFifo() && w.producerSpec.kind == NodeKind::SHIM)
                shimsWithOutgoingFifo.insert(w.producerBlock);
        }

        // Every fill SHIM arm (reachable via Distribute hub or direct DDR→SHIM) must
        // have at least one outgoing FIFO into the array.
        for (const auto& entry : collectFillDrainEntries(*ctx.document)) {
            if (!entry.isFill)
                continue;
            for (auto* shim : entry.shimArms) {
                if (!shimsWithOutgoingFifo.contains(shim)) {
                    const auto spec = parseTileSpec(shim->specId());
                    issues.append({VerificationIssue::Severity::Error,
                        QStringLiteral("Fill at %1: no FIFO leads from this SHIM into the array.")
                        .arg(spec ? tileName(*spec) : shim->specId())});
                }
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

        // SHIM blocks that consume at least one incoming object FIFO (MEM/AIE → SHIM).
        QSet<Canvas::CanvasBlock*> shimsWithIncomingFifo;
        for (const auto& w : collectWires(*ctx.document)) {
            if (w.isObjectFifo() && w.consumerSpec.kind == NodeKind::SHIM)
                shimsWithIncomingFifo.insert(w.consumerBlock);
        }

        // Every drain SHIM arm (reachable via Collect hub or direct SHIM→DDR) must
        // have at least one incoming FIFO from the array.
        for (const auto& entry : collectFillDrainEntries(*ctx.document)) {
            if (entry.isFill)
                continue;
            for (auto* shim : entry.shimArms) {
                if (!shimsWithIncomingFifo.contains(shim)) {
                    const auto spec = parseTileSpec(shim->specId());
                    issues.append({VerificationIssue::Severity::Error,
                        QStringLiteral("Drain at %1: no FIFO leads into this SHIM from the array.")
                        .arg(spec ? tileName(*spec) : shim->specId())});
                }
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
// Each tile type has a fixed number of DMA channels.
// DDR↔SHIM fill/drain wires do NOT consume SHIM DMA channels — only
// SHIM↔MEM, SHIM↔AIE, and SHIM↔SHIM object FIFOs count toward the SHIM budget.
// MEM and AIE count all non-DDR connections.
// DDR itself is excluded — it is not a real tile with a channel limit.
//   SHIM   — 4 channels  (error at 5 or more)
//   MEM    — 12 channels (error at 13 or more)
//   AIE    — 4 channels  (error at 5 or more)
// ---------------------------------------------------------------------------

class DmaChannelLimitCheck : public IVerificationCheck
{
    static int channelLimit(NodeKind kind)
    {
        switch (kind) {
            case NodeKind::SHIM:    return 4;
            case NodeKind::MEM:     return 12;
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

        // Count connections toward each tile's DMA channel budget.
        // DDR↔SHIM fill/drain wires are excluded from the SHIM count — only
        // object FIFOs between non-DDR tiles consume SHIM DMA channels.
        // MEM and AIE count all non-DDR connections.
        QHash<Canvas::CanvasBlock*, ParsedSpec> blockSpec;
        QHash<Canvas::CanvasBlock*, int> connectionCount;

        for (const auto& w : collectWires(*ctx.document)) {
            const bool involvesDdr = w.producerSpec.kind == NodeKind::DDR
                                  || w.consumerSpec.kind == NodeKind::DDR;
            if (w.producerSpec.kind != NodeKind::DDR) {
                // Skip DDR↔SHIM wires for the SHIM endpoint.
                if (involvesDdr && w.producerSpec.kind == NodeKind::SHIM)
                    continue;
                blockSpec.insert(w.producerBlock, w.producerSpec);
                connectionCount[w.producerBlock]++;
            }
            if (w.consumerSpec.kind != NodeKind::DDR) {
                // Skip DDR↔SHIM wires for the SHIM endpoint.
                if (involvesDdr && w.consumerSpec.kind == NodeKind::SHIM)
                    continue;
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
            // For Distribute/Collect hubs the "source" is a FillDrain wire — use its totalDims.
            int fifoElemCount = 1024;
            for (const auto& wItem : items) {
                auto* wire = dynamic_cast<Canvas::CanvasWire*>(wItem.get());
                if (!wire)
                    continue;

                const auto& epA = wire->a();
                const auto& epB = wire->b();
                if (!epA.attached.has_value() || !epB.attached.has_value())
                    continue;

                // Distribute/Collect hub: FillDrain wire connects the hub to DDR.
                if (wire->hasFillDrain()) {
                    const bool aIsHub = (epA.attached->itemId == hubBlock->id());
                    const bool bIsHub = (epB.attached->itemId == hubBlock->id());
                    if (aIsHub || bIsHub) {
                        fifoElemCount = elementCountFromDims(wire->fillDrain()->totalDims);
                        break;
                    }
                    continue;
                }

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
// Check 7 — ObjectFifoDimensions
//
// Every ObjectFifo wire (between two non-DDR tiles) must have dimensions
// specified. Without dimensions the tensor type cannot be determined and
// code generation will silently skip the FIFO.
// ---------------------------------------------------------------------------

class ObjectFifoDimensionsCheck : public IVerificationCheck
{
public:
    QString name() const override { return QStringLiteral("ObjectFifoDimensions"); }
    QString displayName() const override { return QStringLiteral("Checking ObjectFifo dimensions"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document)
            return issues;

        for (const auto& w : collectWires(*ctx.document)) {
            if (!w.isObjectFifo())
                continue;
            if (!w.wire->hasObjectFifo())
                continue;
            if (w.wire->objectFifo().value().type.dimensions.trimmed().isEmpty()) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("ObjectFifo \"%1\" (%2 \u2192 %3) has no dimensions — "
                                   "set dimensions in the properties panel.")
                    .arg(w.fifoName, tileName(w.producerSpec), tileName(w.consumerSpec))});
            }
        }

        return issues;
    }
};

// ---------------------------------------------------------------------------
// Check 8 — KernelArityCheck
//
// For each compute tile with assigned kernels, validates that the kernel
// body does not pass more arguments than the kernel's iron_signature declares,
// and that argument types (when resolvable) match the declared types.
//
// Two code paths are checked:
//   a) BodyStmts tiles: explicit body JSON — each KernelCall's args list is
//      validated against the target kernel's iron_signature.arg_types.
//   b) Multi-kernel default tiles (no body stmts, >1 kernel): the auto-
//      generated body passes every connected FIFO buffer to every kernel;
//      this is flagged when any kernel expects fewer args than connected FIFOs.
// ---------------------------------------------------------------------------

class KernelArityCheck : public IVerificationCheck
{
public:
    QString name()        const override { return QStringLiteral("KernelArityCheck"); }
    QString displayName() const override { return QStringLiteral("Checking kernel argument counts"); }

    QList<VerificationIssue> run(const VerificationContext& ctx) const override
    {
        QList<VerificationIssue> issues;
        if (!ctx.document || !ctx.kernels)
            return issues;

        using Mode = Canvas::CanvasBlock::CoreFunctionConfig::Mode;

        for (const auto& item : ctx.document->items()) {
            auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
            if (!block) continue;

            const auto spec = parseTileSpec(block->specId());
            if (!spec || spec->kind != NodeKind::COMPUTE) continue;

            const QStringList& assigned = block->assignedKernels();
            if (assigned.isEmpty()) continue;

            const QString tileId = tileName(*spec);

            const bool isBodyStmts = block->hasCoreFunctionConfig()
                && block->coreFunctionConfig()->mode == Mode::BodyStmts
                && !block->coreFunctionConfig()->bodyStmtsJson.isEmpty();
            const bool isSharedRef = block->hasCoreFunctionConfig()
                && block->coreFunctionConfig()->mode == Mode::SharedRef;

            if (isBodyStmts) {
                checkBodyStmts(*block, *ctx.kernels, *ctx.document, tileId, issues);
            } else if (isSharedRef) {
                checkSharedRef(*block, *ctx.kernels, *ctx.document,
                               ctx.activeMetadata, tileId, issues);
            } else if (assigned.size() > 1) {
                // Multi-kernel default: auto-generated body calls every kernel
                // with all connected FIFO buffers as arguments.
                checkMultiKernelDefault(*block, *ctx.kernels, *ctx.document, tileId, issues);
            }
        }
        return issues;
    }

private:
    // ---- KernelCall record parsed from a body stmts JSON array ----
    struct KernelCallInfo {
        QString     kernelParamName;
        QStringList args;           // local variable names passed as arguments
    };

    // Walk body JSON array recursively and collect all KernelCall statements.
    static QList<KernelCallInfo> kernelCallsFromBody(const QJsonArray& bodyArr)
    {
        QList<KernelCallInfo> result;
        std::function<void(const QJsonArray&)> walk = [&](const QJsonArray& stmts) {
            for (const QJsonValue& v : stmts) {
                if (!v.isObject()) continue;
                const QJsonObject stmt = v.toObject();
                const QString type = stmt.value(QStringLiteral("type")).toString();
                if (type == QStringLiteral("KernelCall")) {
                    KernelCallInfo info;
                    info.kernelParamName = stmt.value(QStringLiteral("kernel_param")).toString();
                    for (const QJsonValue& a : stmt.value(QStringLiteral("args")).toArray())
                        info.args.append(a.toString());
                    result.append(std::move(info));
                } else if (stmt.contains(QStringLiteral("body"))) {
                    walk(stmt.value(QStringLiteral("body")).toArray());
                }
            }
        };
        walk(bodyArr);
        return result;
    }

    // Build map: local_var → fifo_param from Acquire statements (recursively).
    static QHash<QString,QString> buildLocalVarToFifoParam(const QJsonArray& bodyArr)
    {
        QHash<QString,QString> result;
        std::function<void(const QJsonArray&)> walk = [&](const QJsonArray& stmts) {
            for (const QJsonValue& v : stmts) {
                if (!v.isObject()) continue;
                const QJsonObject stmt = v.toObject();
                const QString type = stmt.value(QStringLiteral("type")).toString();
                if (type == QStringLiteral("Acquire")) {
                    const QString localVar  = stmt.value(QStringLiteral("local_var")).toString();
                    const QString fifoParam = stmt.value(QStringLiteral("fifo_param")).toString();
                    if (!localVar.isEmpty() && !fifoParam.isEmpty())
                        result.insert(localVar, fifoParam);
                } else if (stmt.contains(QStringLiteral("body"))) {
                    walk(stmt.value(QStringLiteral("body")).toArray());
                }
            }
        };
        walk(bodyArr);
        return result;
    }

    // Build map: fifo_name → ObjectFifoTypeAbstraction from all canvas wires.
    // For split/join/broadcast hub arm wires the canonical lookup key used by
    // coreBodyArgs is "hubName[armIndex]".  We walk hub blocks to insert these
    // bracket-notation entries alongside the regular cfg.name entries.
    static QHash<QString, Canvas::CanvasWire::ObjectFifoTypeAbstraction>
    buildFifoTypeMap(const Canvas::CanvasDocument& doc)
    {
        QHash<QString, Canvas::CanvasWire::ObjectFifoTypeAbstraction> result;

        // Pass 1: index every named wire by its cfg.name.
        for (const auto& item : doc.items()) {
            auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
            if (!wire || !wire->hasObjectFifo()) continue;
            const auto& cfg = wire->objectFifo().value();
            if (!cfg.name.isEmpty())
                result.insert(cfg.name, cfg.type);
        }

        // Pass 2: for each split/join/broadcast hub block, also insert arm wire types
        // under the key that coreBodyArgs uses for lookup:
        //   broadcast  → "hubName"        (all arms share the same forwarded FIFO)
        //   split/join → "hubName[armIndex]"
        for (const auto& hubItem : doc.items()) {
            auto* hubBlock = dynamic_cast<Canvas::CanvasBlock*>(hubItem.get());
            if (!hubBlock || !hubBlock->isLinkHub() || !hubBlock->specId().isEmpty())
                continue;

            // Find the pivot wire (hub at endpoint B) to get the hub name and kind.
            QString hubName;
            bool isBroadcast = false;
            for (const auto& wItem : doc.items()) {
                const auto* w = dynamic_cast<const Canvas::CanvasWire*>(wItem.get());
                if (!w || !w->hasObjectFifo()) continue;
                const auto& epB = w->b();
                if (!epB.attached.has_value() || epB.attached->itemId != hubBlock->id())
                    continue;
                const auto& cfg = w->objectFifo().value();
                hubName = cfg.hubName.trimmed().isEmpty()
                    ? cfg.name.trimmed() : cfg.hubName.trimmed();
                // Pivot operation Forward → broadcast hub.
                isBroadcast = (cfg.operation ==
                    Canvas::CanvasWire::ObjectFifoOperation::Forward);
                break;
            }
            if (hubName.isEmpty()) continue;

            // Build a map from portId → arm wire (hub at endpoint A).
            QHash<Canvas::PortId, Canvas::CanvasWire*> portToArmWire;
            for (const auto& wItem : doc.items()) {
                auto* w = dynamic_cast<Canvas::CanvasWire*>(wItem.get());
                if (!w || !w->hasObjectFifo()) continue;
                const auto& epA = w->a();
                if (!epA.attached.has_value() || epA.attached->itemId != hubBlock->id())
                    continue;
                portToArmWire.insert(epA.attached->portId, w);
            }
            if (portToArmWire.isEmpty()) continue;

            if (isBroadcast) {
                // All broadcast arms carry the same type — insert under the plain hubName.
                for (auto* armWire : std::as_const(portToArmWire))
                    result.insert(hubName, armWire->objectFifo().value().type);
            } else {
                // Split/join: walk ports in declared order to assign 0-based arm indices,
                // matching the order armIndexFor() uses in buildWorkers().
                int consumerIdx = 0;
                int producerIdx = 0;
                for (const auto& port : hubBlock->ports()) {
                    auto* armWire = portToArmWire.value(port.id, nullptr);
                    if (!armWire) {
                        if (port.role == Canvas::PortRole::Consumer) ++consumerIdx;
                        else if (port.role == Canvas::PortRole::Producer) ++producerIdx;
                        continue;
                    }
                    const int idx = (port.role == Canvas::PortRole::Consumer)
                        ? consumerIdx++ : producerIdx++;
                    result.insert(
                        QStringLiteral("%1[%2]").arg(hubName).arg(idx),
                        armWire->objectFifo().value().type);
                }
            }
        }

        return result;
    }

    // Short-form value type (e.g. "i16") → full Python/numpy dtype name (e.g. "int16").
    static QString dtypeFullName(const QString& vt)
    {
        static const QHash<QString,QString> kMap = {
            {QStringLiteral("i8"),   QStringLiteral("int8")},
            {QStringLiteral("i16"),  QStringLiteral("int16")},
            {QStringLiteral("i32"),  QStringLiteral("int32")},
            {QStringLiteral("i64"),  QStringLiteral("int64")},
            {QStringLiteral("ui8"),  QStringLiteral("uint8")},
            {QStringLiteral("ui16"), QStringLiteral("uint16")},
            {QStringLiteral("ui32"), QStringLiteral("uint32")},
            {QStringLiteral("bf16"), QStringLiteral("bfloat16")},
            {QStringLiteral("f16"),  QStringLiteral("float16")},
            {QStringLiteral("f32"),  QStringLiteral("float32")},
            {QStringLiteral("f64"),  QStringLiteral("float64")},
        };
        return kMap.value(vt.toLower(), vt);
    }

    // Try to parse an iron_signature type name of the form "type_{dtype}_{dims}".
    // Returns true and sets outDtype/outDims on success.
    // Examples: "type_int16_256" → ("int16","256")
    //           "type_bfloat16_64" → ("bfloat16","64")
    static bool parseIronTypeName(const QString& name, QString& outDtype, QString& outDims)
    {
        if (!name.startsWith(QStringLiteral("type_")))
            return false;
        const QString rest = name.sliced(5); // strip leading "type_"
        const int lastUnderscore = rest.lastIndexOf(u'_');
        if (lastUnderscore <= 0)
            return false;
        outDims  = rest.sliced(lastUnderscore + 1);
        outDtype = rest.left(lastUnderscore);
        bool ok = false;
        outDims.toInt(&ok);
        return ok && !outDtype.isEmpty();
    }

    // Resolve the FIFO type for a given wire name directly from the type map.
    // HlirSyncService propagates the correct valueType and dimensions to branch arm
    // wires during sync, so a direct lookup is sufficient.
    static Canvas::CanvasWire::ObjectFifoTypeAbstraction
    resolveFifoType(const QString& fifoName,
                    const QHash<QString, Canvas::CanvasWire::ObjectFifoTypeAbstraction>& typeMap)
    {
        return typeMap.value(fifoName);
    }

    // ---- BodyStmts check ----
    void checkBodyStmts(const Canvas::CanvasBlock& block,
                        const QVector<KernelAsset>& kernels,
                        const Canvas::CanvasDocument& doc,
                        const QString& tileId,
                        QList<VerificationIssue>& issues) const
    {
        QJsonParseError parseErr;
        const QJsonDocument jdoc = QJsonDocument::fromJson(
            block.coreFunctionConfig()->bodyStmtsJson.toUtf8(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError) return;

        QJsonArray bodyArr;
        if (jdoc.isObject())
            bodyArr = jdoc.object().value(QStringLiteral("body")).toArray();
        else if (jdoc.isArray())
            bodyArr = jdoc.array();
        if (bodyArr.isEmpty()) return;

        // Build kernel_param_name → kernelId from coreBodyArgs().
        QHash<QString,QString> paramToKernelId;
        for (const auto& argSpec : block.coreBodyArgs()) {
            using K = Canvas::CanvasBlock::CoreBodyArgSpec::Kind;
            if (argSpec.kind == K::Kernel)
                paramToKernelId.insert(argSpec.paramName, argSpec.ref);
        }

        // Fallback: infer from assignedKernels() order when coreBodyArgs is empty.
        if (paramToKernelId.isEmpty() && !block.assignedKernels().isEmpty()) {
            QStringList kernelParamsInOrder;
            std::function<void(const QJsonArray&)> collect =
                [&](const QJsonArray& stmts) {
                    for (const QJsonValue& v : stmts) {
                        if (!v.isObject()) continue;
                        const QJsonObject stmt = v.toObject();
                        const QString type = stmt.value(QStringLiteral("type")).toString();
                        if (type == QStringLiteral("KernelCall")) {
                            const QString kp = stmt.value(QStringLiteral("kernel_param")).toString();
                            if (!kp.isEmpty() && !kernelParamsInOrder.contains(kp))
                                kernelParamsInOrder.append(kp);
                        } else if (stmt.contains(QStringLiteral("body")))
                            collect(stmt.value(QStringLiteral("body")).toArray());
                    }
                };
            collect(bodyArr);
            const QStringList& assigned = block.assignedKernels();
            for (int i = 0; i < kernelParamsInOrder.size() && i < assigned.size(); ++i)
                paramToKernelId.insert(kernelParamsInOrder[i], assigned[i]);
        }

        // Build local_var → fifo_param_name from Acquire stmts.
        const auto localVarToFifoParam = buildLocalVarToFifoParam(bodyArr);

        // Build fifo_param_name → FIFO object name from coreBodyArgs().
        QHash<QString,QString> fifoParamToName;
        for (const auto& argSpec : block.coreBodyArgs()) {
            using K = Canvas::CanvasBlock::CoreBodyArgSpec::Kind;
            if (argSpec.kind == K::FifoConsumer || argSpec.kind == K::FifoProducer)
                fifoParamToName.insert(argSpec.paramName, argSpec.ref);
        }

        // Build FIFO object name → wire type from canvas.
        const auto fifoTypeMap = buildFifoTypeMap(doc);

        for (const auto& callInfo : kernelCallsFromBody(bodyArr)) {
            const QString kernelId = paramToKernelId.value(callInfo.kernelParamName);
            if (kernelId.isEmpty()) continue;

            const KernelAsset* ka = findKernelById(kernels, kernelId);
            if (!ka) continue;

            const QJsonObject ironSig =
                ka->metadata.value(QStringLiteral("iron_signature")).toObject();
            if (ironSig.isEmpty()) continue;

            const QJsonArray expectedArgTypes =
                ironSig.value(QStringLiteral("arg_types")).toArray();
            const int expectedCount = expectedArgTypes.size();
            if (expectedCount == 0) continue;

            const int actualCount = callInfo.args.size();

            // --- Arg count check ---
            if (actualCount > expectedCount) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("%1: kernel '%2' called via '%3' with %4 argument(s) "
                                   "but kernel.json declares only %5.")
                    .arg(tileId, kernelId, callInfo.kernelParamName)
                    .arg(actualCount)
                    .arg(expectedCount)});
                continue; // Type check is moot when arg count is wrong.
            }

            // --- Best-effort type check (requires naming convention type_{dtype}_{dims}) ---
            for (int i = 0; i < actualCount; ++i) {
                const QString expectedTypeName = expectedArgTypes[i].toString();
                QString expDtype, expDims;
                if (!parseIronTypeName(expectedTypeName, expDtype, expDims))
                    continue; // Non-standard name — cannot verify type for this arg.

                const QString localVar  = callInfo.args[i];
                const QString fifoParam = localVarToFifoParam.value(localVar);
                if (fifoParam.isEmpty()) continue;
                const QString fifoName  = fifoParamToName.value(fifoParam);
                if (fifoName.isEmpty())  continue;

                const auto fifoType = resolveFifoType(fifoName, fifoTypeMap);
                if (fifoType.valueType.isEmpty() && fifoType.dimensions.isEmpty()) continue;

                const QString actualDtype = dtypeFullName(fifoType.valueType);
                const QString actualDims  = fifoType.dimensions.trimmed();

                if (actualDtype != expDtype || actualDims != expDims) {
                    issues.append({VerificationIssue::Severity::Error,
                        QStringLiteral("%1: kernel '%2' argument %3 expects type '%4' "
                                       "(%5[%6]) but FIFO '%7' provides %8[%9].")
                        .arg(tileId, kernelId)
                        .arg(i + 1)
                        .arg(expectedTypeName, expDtype, expDims)
                        .arg(fifoName, actualDtype, actualDims)});
                }
            }
        }
    }

    // ---- SharedRef check ----
    // The tile references a named shared function from the coreFunctionLibrary
    // stored in document metadata.  Load that function's body JSON and validate
    // all KernelCall statements against the tile's kernel iron_signatures.
    void checkSharedRef(const Canvas::CanvasBlock& block,
                        const QVector<KernelAsset>& kernels,
                        const Canvas::CanvasDocument& doc,
                        const QJsonObject& activeMetadata,
                        const QString& tileId,
                        QList<VerificationIssue>& issues) const
    {
        if (!block.hasCoreFunctionConfig()) return;
        const QString& sharedName = block.coreFunctionConfig()->sharedFunctionName;
        if (sharedName.isEmpty()) return;

        // Build the shared function library: name → bodyStmtsJson.
        QHash<QString, QString> sharedFnLibrary;
        const QJsonArray lib =
            activeMetadata.value(QStringLiteral("coreFunctionLibrary")).toArray();
        for (const QJsonValue& v : lib) {
            const QJsonObject entry = v.toObject();
            const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
            const QString body = entry.value(QStringLiteral("bodyStmtsJson")).toString();
            if (!name.isEmpty() && !body.isEmpty())
                sharedFnLibrary.insert(name, body);
        }

        const QString bodyJson = sharedFnLibrary.value(sharedName);
        if (bodyJson.isEmpty()) return; // shared function not found — nothing to check

        // Parse the shared function body JSON.
        QJsonParseError parseErr;
        const QJsonDocument jdoc = QJsonDocument::fromJson(bodyJson.toUtf8(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError) return;

        QJsonArray bodyArr;
        if (jdoc.isObject())
            bodyArr = jdoc.object().value(QStringLiteral("body")).toArray();
        else if (jdoc.isArray())
            bodyArr = jdoc.array();
        if (bodyArr.isEmpty()) return;

        // Build kernel_param_name → kernelId from the tile's coreBodyArgs().
        QHash<QString, QString> paramToKernelId;
        for (const auto& argSpec : block.coreBodyArgs()) {
            using K = Canvas::CanvasBlock::CoreBodyArgSpec::Kind;
            if (argSpec.kind == K::Kernel)
                paramToKernelId.insert(argSpec.paramName, argSpec.ref);
        }

        // Fallback: infer from assignedKernels() order.
        if (paramToKernelId.isEmpty() && !block.assignedKernels().isEmpty()) {
            QStringList kernelParamsInOrder;
            std::function<void(const QJsonArray&)> collect = [&](const QJsonArray& stmts) {
                for (const QJsonValue& v : stmts) {
                    if (!v.isObject()) continue;
                    const QJsonObject stmt = v.toObject();
                    const QString type = stmt.value(QStringLiteral("type")).toString();
                    if (type == QStringLiteral("KernelCall")) {
                        const QString kp = stmt.value(QStringLiteral("kernel_param")).toString();
                        if (!kp.isEmpty() && !kernelParamsInOrder.contains(kp))
                            kernelParamsInOrder.append(kp);
                    } else if (stmt.contains(QStringLiteral("body")))
                        collect(stmt.value(QStringLiteral("body")).toArray());
                }
            };
            collect(bodyArr);
            const QStringList& assigned = block.assignedKernels();
            for (int i = 0; i < kernelParamsInOrder.size() && i < assigned.size(); ++i)
                paramToKernelId.insert(kernelParamsInOrder[i], assigned[i]);
        }

        // Build fifo_param_name → FIFO object name from coreBodyArgs().
        QHash<QString, QString> fifoParamToName;
        for (const auto& argSpec : block.coreBodyArgs()) {
            using K = Canvas::CanvasBlock::CoreBodyArgSpec::Kind;
            if (argSpec.kind == K::FifoConsumer || argSpec.kind == K::FifoProducer)
                fifoParamToName.insert(argSpec.paramName, argSpec.ref);
        }

        // Build local_var → fifo_param from Acquire stmts.
        const auto localVarToFifoParam = buildLocalVarToFifoParam(bodyArr);

        // Build FIFO object name → wire type from canvas.
        const auto fifoTypeMap = buildFifoTypeMap(doc);

        // Validate each KernelCall in the shared function body.
        for (const auto& callInfo : kernelCallsFromBody(bodyArr)) {
            const QString kernelId = paramToKernelId.value(callInfo.kernelParamName);
            if (kernelId.isEmpty()) continue;

            const KernelAsset* ka = findKernelById(kernels, kernelId);
            if (!ka) continue;

            const QJsonObject ironSig =
                ka->metadata.value(QStringLiteral("iron_signature")).toObject();
            if (ironSig.isEmpty()) continue;

            const QJsonArray expectedArgTypes =
                ironSig.value(QStringLiteral("arg_types")).toArray();
            const int expectedCount = expectedArgTypes.size();
            if (expectedCount == 0) continue;

            const int actualCount = callInfo.args.size();

            if (actualCount > expectedCount) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("%1 (shared fn '%2'): kernel '%3' called via '%4' with %5 "
                                   "argument(s) but kernel.json declares only %6.")
                    .arg(tileId, sharedName, kernelId, callInfo.kernelParamName)
                    .arg(actualCount)
                    .arg(expectedCount)});
                continue;
            }

            // Best-effort type check.
            for (int i = 0; i < actualCount; ++i) {
                const QString expectedTypeName = expectedArgTypes[i].toString();
                QString expDtype, expDims;
                if (!parseIronTypeName(expectedTypeName, expDtype, expDims))
                    continue;

                const QString localVar  = callInfo.args[i];
                const QString fifoParam = localVarToFifoParam.value(localVar);
                if (fifoParam.isEmpty()) continue;
                const QString fifoName  = fifoParamToName.value(fifoParam);
                if (fifoName.isEmpty()) continue;

                const auto fifoType = resolveFifoType(fifoName, fifoTypeMap);
                if (fifoType.valueType.isEmpty() && fifoType.dimensions.isEmpty()) continue;

                const QString actualDtype = dtypeFullName(fifoType.valueType);
                const QString actualDims  = fifoType.dimensions.trimmed();

                if (actualDtype != expDtype || actualDims != expDims) {
                    issues.append({VerificationIssue::Severity::Error,
                        QStringLiteral("%1 (shared fn '%2'): kernel '%3' argument %4 expects "
                                       "type '%5' (%6[%7]) but FIFO '%8' provides %9[%10].")
                        .arg(tileId, sharedName, kernelId)
                        .arg(i + 1)
                        .arg(expectedTypeName, expDtype, expDims)
                        .arg(fifoName, actualDtype, actualDims)});
                }
            }
        }
    }

    // ---- Multi-kernel default check ----
    // The auto-generated body passes every connected FIFO buffer to every kernel.
    // If any kernel expects fewer args than connected FIFOs, flag it.
    void checkMultiKernelDefault(const Canvas::CanvasBlock& block,
                                 const QVector<KernelAsset>& kernels,
                                 const Canvas::CanvasDocument& doc,
                                 const QString& tileId,
                                 QList<VerificationIssue>& issues) const
    {
        const int totalFifoArgs =
            static_cast<int>(connectedFifosForTile(&block, &doc).size());

        for (const QString& kernelId : block.assignedKernels()) {
            const KernelAsset* ka = findKernelById(kernels, kernelId);
            if (!ka) continue;

            const QJsonObject ironSig =
                ka->metadata.value(QStringLiteral("iron_signature")).toObject();
            if (ironSig.isEmpty()) continue;

            const int expectedCount =
                ironSig.value(QStringLiteral("arg_types")).toArray().size();
            if (expectedCount == 0) continue;

            if (totalFifoArgs > expectedCount) {
                issues.append({VerificationIssue::Severity::Error,
                    QStringLiteral("%1: kernel '%2' would receive %3 buffer argument(s) "
                                   "(one per connected FIFO) but kernel.json declares only %4. "
                                   "Add a custom core body to pass the correct arguments.")
                    .arg(tileId, kernelId)
                    .arg(totalFifoArgs)
                    .arg(expectedCount)});
            }
        }
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
    //m_checks.push_back(std::make_unique<ObjectFifoDimensionsCheck>());
    //m_checks.push_back(std::make_unique<KernelArityCheck>());
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
        if (w.producerSpec.kind != NodeKind::DDR)
            connectedTiles.insert(w.producerBlock, w.producerSpec);
        if (w.consumerSpec.kind != NodeKind::DDR)
            connectedTiles.insert(w.consumerBlock, w.consumerSpec);
    }

    // Count fills and drains — each SHIM arm of a Distribute/Collect hub is one fill/drain.
    for (const auto& entry : collectFillDrainEntries(*ctx.document)) {
        const int armCount = std::max(1, static_cast<int>(entry.shimArms.size()));
        if (entry.isFill)  stats.fills  += armCount;
        else               stats.drains += armCount;
        for (auto* shim : entry.shimArms) {
            const auto spec = parseTileSpec(shim->specId());
            if (spec) connectedTiles.insert(shim, *spec);
        }
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

        // Determine hub type: broadcasts first, then Distribute/Collect (already in fills/drains), then split/join.
        if (isBroadcastHub(hubBlock)) {
            ++stats.broadcasts;
            continue;
        }
        if (isDistributeOrCollectHub(hubBlock))
            continue; // Counted via arm fills/drains, not as split/join.

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
