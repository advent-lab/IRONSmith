// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/TileFifoInfo.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"

namespace Aie::Internal {

// ------------------------------------------------------------------------------------------------
// connectedFifosForTile()
//
// Mirrors exactly how HlirSyncService classifies wires for buildSplitJoinHubs +
// buildWorkers, so the displayed names match what code generation uses.
//
// Two passes:
//
// Pass 1 — iterate over hub blocks.
//   For each hub find its pivot wire (hub at B, non-hub at A) and arm wires (hub at A).
//   If the pivot tile is our block → it's a pivot connection:
//     - Broadcast or Split hub, pivot role Consumer → tile OUTPUT (feeds the hub)
//     - Join hub, pivot role Producer → tile INPUT (receives from the hub)
//   Name = hubName (from pivot wire's hubName field, or its name field as fallback).
//
// Pass 2 — iterate over all wires for arm connections to our block.
//   Arm wire: hub at A, tile at B.
//   Broadcast hub → tile INPUT (arm index not used).
//   Non-broadcast hub, Producer port → tile INPUT (split arm[idx]).
//   Non-broadcast hub, Consumer port → tile OUTPUT (join arm[idx]).
//   Name = hubName[armIndex] for split/join, hubName for broadcast.
//
// Pass 3 — iterate over all wires for direct (non-hub) FIFO connections.
//   Direct wire, non-hub at A, tile at B → tile INPUT.
//   Direct wire, tile at A, non-hub at B → tile OUTPUT.
// ------------------------------------------------------------------------------------------------

static bool isBroadcastHub(const Canvas::CanvasBlock* hub)
{
    const auto* sym = dynamic_cast<const Canvas::BlockContentSymbol*>(hub->content());
    return sym && sym->symbol().trimmed()
               == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Broadcast).symbol;
}

static int armIndexFor(const Canvas::CanvasBlock* hub,
                       Canvas::PortId portId,
                       Canvas::PortRole role)
{
    int idx = 0;
    for (const auto& port : hub->ports()) {
        if (port.role != role)
            continue;
        if (port.id == portId)
            return idx;
        ++idx;
    }
    return -1;
}

QList<TileFifoInfo>
connectedFifosForTile(const Canvas::CanvasBlock* block,
                      const Canvas::CanvasDocument* document)
{
    QList<TileFifoInfo> result;
    const auto& items = document->items();

    // ---- Pass 1: pivot connections via hub blocks ----
    for (const auto& hubItem : items) {
        const auto* hubBlock = dynamic_cast<const Canvas::CanvasBlock*>(hubItem.get());
        if (!hubBlock || !hubBlock->isLinkHub() || !hubBlock->specId().isEmpty())
            continue;

        // Build port-role lookup for this hub.
        QHash<Canvas::PortId, Canvas::PortRole> portRoles;
        for (const auto& port : hubBlock->ports())
            portRoles[port.id] = port.role;

        // Find all pivot wires for this hub and the tiles they connect to.
        // Split/Broadcast: hub at B, tile at A — tile is the source (OUTPUT).
        // Join:            hub at A, tile at B — tile is the destination (INPUT).
        for (const auto& wItem : items) {
            const auto* wire = dynamic_cast<const Canvas::CanvasWire*>(wItem.get());
            if (!wire || !wire->hasObjectFifo())
                continue;
            const auto& epA = wire->a();
            const auto& epB = wire->b();
            if (!epA.attached.has_value() || !epB.attached.has_value())
                continue;

            const bool hubAtB = (epB.attached->itemId == hubBlock->id());
            const bool hubAtA = (epA.attached->itemId == hubBlock->id());
            if (!hubAtA && !hubAtB)
                continue;

            if (hubAtB) {
                // Pivot wire: tile at A, hub at B.
                // pivotRole = hub port role at endpoint B:
                //   Consumer → Split/Broadcast: tile feeds hub → tile OUTPUT
                //   Producer → Join: hub sends to tile  → tile INPUT
                const auto* blkA = dynamic_cast<const Canvas::CanvasBlock*>(
                    document->findItem(epA.attached->itemId));
                if (!blkA || blkA->isLinkHub())
                    continue;
                if (blkA->id() != block->id())
                    continue;
                const auto& cfg = wire->objectFifo().value();
                const QString hubName = cfg.hubName.trimmed().isEmpty()
                    ? cfg.name.trimmed() : cfg.hubName.trimmed();
                if (hubName.isEmpty())
                    continue;
                const Canvas::PortRole pivotRole =
                    portRoles.value(epB.attached->portId, Canvas::PortRole::Dynamic);
                if (pivotRole == Canvas::PortRole::Consumer) {
                    result.append({hubName, cfg.depth, /*isInput=*/false}); // Split/Bcast source
                } else if (pivotRole == Canvas::PortRole::Producer) {
                    result.append({hubName, cfg.depth, /*isInput=*/true});  // Join destination
                }
            } else {
                // Hub at A, non-empty hubName: only remaining case is a pivot where
                // the hub itself is not the endpoint-B hub (shouldn't happen with current
                // topology but skip arm wires here regardless).
                // Arm wires have empty hubName — handled in Pass 2.
                const auto& cfg = wire->objectFifo().value();
                if (cfg.hubName.trimmed().isEmpty())
                    continue;
                // Skip — all hub-at-A pivot cases are arm-wire variants handled in Pass 2.
            }
        }
    }

    // ---- Pass 2: arm connections (hub at A, tile at B) ----
    for (const auto& wItem : items) {
        const auto* wire = dynamic_cast<const Canvas::CanvasWire*>(wItem.get());
        if (!wire)
            continue;
        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;
        if (epB.attached->itemId != block->id())
            continue; // tile must be at B for arm wires
        const auto* hubBlock = dynamic_cast<const Canvas::CanvasBlock*>(
            document->findItem(epA.attached->itemId));
        if (!hubBlock || !hubBlock->isLinkHub())
            continue; // hub must be at A

        // Get hub name from the config wire (non-empty hubName).
        // Split/Broadcast: config wire has hub at B.
        // Join:            config wire has hub at A with non-empty hubName.
        const Canvas::CanvasWire* configWire = nullptr;
        for (const auto& pItem : items) {
            const auto* pw = dynamic_cast<const Canvas::CanvasWire*>(pItem.get());
            if (!pw || !pw->hasObjectFifo())
                continue;
            if (pw->objectFifo().value().hubName.trimmed().isEmpty())
                continue; // arm wires have empty hubName — skip
            const auto& pA = pw->a();
            const auto& pB = pw->b();
            if (!pA.attached.has_value() || !pB.attached.has_value())
                continue;
            const bool cfgHubAtB = (pB.attached->itemId == hubBlock->id());
            const bool cfgHubAtA = (pA.attached->itemId == hubBlock->id());
            if (!cfgHubAtA && !cfgHubAtB)
                continue;
            configWire = pw;
            break;
        }

        if (!configWire)
            continue;
        const auto& pivotCfg = configWire->objectFifo().value();
        const QString hubName = pivotCfg.hubName.trimmed().isEmpty()
            ? pivotCfg.name.trimmed()
            : pivotCfg.hubName.trimmed();
        if (hubName.isEmpty())
            continue;

        if (isBroadcastHub(hubBlock)) {
            // Broadcast arm → tile INPUT.
            result.append({hubName, pivotCfg.depth, /*isInput=*/true});
        } else {
            // Split or Join arm — direction from hub port role at endpoint A of this arm wire.
            Canvas::PortRole armPortRole = Canvas::PortRole::Dynamic;
            for (const auto& port : hubBlock->ports()) {
                if (port.id == epA.attached->portId) {
                    armPortRole = port.role;
                    break;
                }
            }
            if (armPortRole == Canvas::PortRole::Producer) {
                // Split arm → tile INPUT.
                const int idx = armIndexFor(hubBlock, epA.attached->portId,
                                            Canvas::PortRole::Producer);
                const QString name = (idx >= 0)
                    ? QStringLiteral("%1[%2]").arg(hubName).arg(idx)
                    : hubName;
                result.append({name, pivotCfg.depth, /*isInput=*/true});
            } else if (armPortRole == Canvas::PortRole::Consumer) {
                // Join arm → tile OUTPUT.
                const int idx = armIndexFor(hubBlock, epA.attached->portId,
                                            Canvas::PortRole::Consumer);
                const QString name = (idx >= 0)
                    ? QStringLiteral("%1[%2]").arg(hubName).arg(idx)
                    : hubName;
                result.append({name, pivotCfg.depth, /*isInput=*/false});
            }
        }
    }

    // ---- Pass 3: direct FIFO connections (no hub) ----
    for (const auto& wItem : items) {
        const auto* wire = dynamic_cast<const Canvas::CanvasWire*>(wItem.get());
        if (!wire || !wire->hasObjectFifo())
            continue;
        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;

        const bool tileIsA = (epA.attached->itemId == block->id());
        const bool tileIsB = (epB.attached->itemId == block->id());
        if (!tileIsA && !tileIsB)
            continue;

        if (tileIsB) {
            const auto* blockA = dynamic_cast<const Canvas::CanvasBlock*>(
                document->findItem(epA.attached->itemId));
            if (!blockA || blockA->isLinkHub())
                continue; // hubs handled in passes 1 & 2
            const auto& cfg = wire->objectFifo().value();
            // For direct forward wires use hubName (the forward result name), not the source name.
            const QString name =
                (cfg.operation == Canvas::CanvasWire::ObjectFifoOperation::Forward
                 && !cfg.hubName.trimmed().isEmpty())
                ? cfg.hubName.trimmed() : cfg.name;
            result.append({name, cfg.depth, /*isInput=*/true});
        } else {
            const auto* blockB = dynamic_cast<const Canvas::CanvasBlock*>(
                document->findItem(epB.attached->itemId));
            if (!blockB || blockB->isLinkHub())
                continue; // hubs handled in pass 1
            const auto& cfg = wire->objectFifo().value();
            result.append({cfg.name, cfg.depth, /*isInput=*/false});
        }
    }

    return result;
}

} // namespace Aie::Internal
