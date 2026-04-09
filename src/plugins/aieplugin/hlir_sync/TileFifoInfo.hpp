// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"

#include <QtCore/QList>
#include <QtCore/QString>

namespace Canvas {
class CanvasBlock;
class CanvasDocument;
} // namespace Canvas

namespace Aie::Internal {

/// One object-fifo connection to a compute tile, classified as input or output.
struct TileFifoInfo final {
    QString name;     ///< Object fifo name (e.g. "splitA1", "bcastB1")
    int     depth;    ///< Array depth — 0 means scalar / not an array
    bool    isInput;  ///< true → tile consumes (cons()), false → tile produces (prod())
};

/// Collect all object-fifo connections to \a block by walking every wire in \a document.
/// Mirrors the wire-classification logic in HlirSyncService::buildWorkers() exactly:
///   - Hub Producer port  → tile input  (split/broadcast arm)
///   - Hub Consumer port  → tile output (join arm)
///   - Direct FIFO, tile at B (consumer endpoint) → input
///   - Direct FIFO, tile at A (producer endpoint) → output
/// Only wires with an ObjectFifo annotation are included; arm/DDR/pivot wires without
/// an ObjectFifo are skipped.
AIEPLUGIN_EXPORT QList<TileFifoInfo>
connectedFifosForTile(const Canvas::CanvasBlock* block,
                      const Canvas::CanvasDocument* document);

} // namespace Aie::Internal
