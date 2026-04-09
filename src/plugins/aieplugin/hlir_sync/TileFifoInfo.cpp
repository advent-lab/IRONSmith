// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/TileFifoInfo.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasWire.hpp"

namespace Aie::Internal {

// ------------------------------------------------------------------------------------------------
// connectedFifosForTile()
//
// Name format for hub arms:
//   - Split arm (hub Producer port):    hubName[armIndex]   e.g. "splitA1[0]", "splitA1[3]"
//   - Join arm (hub Consumer port):     hubName[armIndex]   e.g. "joinC1[0]"
//   - Broadcast arm (Forward pivot):    hubName             e.g. "bcastB1"
//   - Direct FIFO:                      wire objectFifo name
//
// Hub name and depth always come from the pivot wire (the wire where the hub is at endpoint B).
// The arm wire's objectFifo.name is NOT used — it defaults to "of" and is unreliable before
// code generation runs.
// ------------------------------------------------------------------------------------------------

/// Find the pivot wire for a hub block: the unique wire where the hub is at endpoint B
/// and the other endpoint is a non-hub block.  Returns nullptr when not found.
static const Canvas::CanvasWire*
findPivotWire(const Canvas::CanvasBlock* hub, const Canvas::CanvasDocument* document)
{
    for (const auto& item : document->items()) {
        const auto* wire = dynamic_cast<const Canvas::CanvasWire*>(item.get());
        if (!wire)
            continue;
        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;
        if (epB.attached->itemId != hub->id())
            continue;
        const auto* blockA = dynamic_cast<const Canvas::CanvasBlock*>(
            document->findItem(epA.attached->itemId));
        if (blockA && !blockA->isLinkHub())
            return wire;
    }
    return nullptr;
}

/// Return the 0-based index of portId among all hub ports with the given role.
/// Returns -1 when not found.
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

    for (const auto& item : document->items()) {
        const auto* wire = dynamic_cast<const Canvas::CanvasWire*>(item.get());
        if (!wire)
            continue;

        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;

        const bool tileIsB = (epB.attached->itemId == block->id());
        if (!tileIsB && epA.attached->itemId != block->id())
            continue;

        if (tileIsB) {
            // ---- Tile at consumer endpoint ----
            const auto* blockA = dynamic_cast<const Canvas::CanvasBlock*>(
                document->findItem(epA.attached->itemId));
            if (!blockA)
                continue;

            if (!blockA->isLinkHub()) {
                // Direct FIFO — tile consumes → input.
                if (!wire->hasObjectFifo())
                    continue;
                const auto& cfg = wire->objectFifo().value();
                result.append({cfg.name, cfg.depth, /*isInput=*/true});
            } else {
                // Hub arm wire: hub at A, tile at B.
                // Find the pivot wire to get the hub logical name and depth.
                const Canvas::CanvasWire* pivot = findPivotWire(blockA, document);
                if (!pivot || !pivot->hasObjectFifo())
                    continue;
                const auto& pivotCfg = pivot->objectFifo().value();
                const QString hubName = pivotCfg.hubName.trimmed().isEmpty()
                    ? pivotCfg.name.trimmed()
                    : pivotCfg.hubName.trimmed();
                if (hubName.isEmpty())
                    continue;

                // Broadcast hub (Forward pivot) — all arms share the same fifo, no arm index.
                if (pivotCfg.operation == Canvas::CanvasWire::ObjectFifoOperation::Forward) {
                    result.append({hubName, pivotCfg.depth, /*isInput=*/true});
                    continue;
                }

                // Split hub (Split pivot): Producer arm ports → tile input.
                // Join hub (Join pivot): Consumer arm ports → tile output.
                // Direction follows the hub port role at endpoint A.
                Canvas::PortRole hubPortRole = Canvas::PortRole::Dynamic;
                for (const auto& port : blockA->ports()) {
                    if (port.id == epA.attached->portId) {
                        hubPortRole = port.role;
                        break;
                    }
                }

                if (hubPortRole == Canvas::PortRole::Producer) {
                    // Split arm — tile receives → input.
                    const int idx = armIndexFor(blockA, epA.attached->portId,
                                                Canvas::PortRole::Producer);
                    const QString name = (idx >= 0)
                        ? QStringLiteral("%1[%2]").arg(hubName).arg(idx)
                        : hubName;
                    result.append({name, pivotCfg.depth, /*isInput=*/true});
                } else if (hubPortRole == Canvas::PortRole::Consumer) {
                    // Join arm — tile sends → output.
                    const int idx = armIndexFor(blockA, epA.attached->portId,
                                                Canvas::PortRole::Consumer);
                    const QString name = (idx >= 0)
                        ? QStringLiteral("%1[%2]").arg(hubName).arg(idx)
                        : hubName;
                    result.append({name, pivotCfg.depth, /*isInput=*/false});
                }
                // Dynamic role → unclassified, skip.
            }
        } else {
            // ---- Tile at producer endpoint ----
            const auto* blockB = dynamic_cast<const Canvas::CanvasBlock*>(
                document->findItem(epB.attached->itemId));
            if (!blockB || blockB->isLinkHub())
                continue;

            // Direct FIFO — tile produces → output.
            if (!wire->hasObjectFifo())
                continue;
            const auto& cfg = wire->objectFifo().value();
            result.append({cfg.name, cfg.depth, /*isInput=*/false});
        }
    }

    return result;
}

} // namespace Aie::Internal
