// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/HlirSyncService.hpp"
#include "aieplugin/hlir_sync/DesignVerifier.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"
#include "hlir_cpp_bridge/HlirBridge.hpp"
#include "code_gen_bridge/CodeGenBridge.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QLoggingCategory>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QThread>

#include <filesystem>

namespace Aie::Internal {

Q_LOGGING_CATEGORY(hlirSyncLog, "ironsmith.aie.hlir")

// Prefix Python keywords with "of_" so they are valid variable names.
static QString sanitizePythonName(const QString& name)
{
    static const QSet<QString> kPythonKeywords = {
        QStringLiteral("False"),  QStringLiteral("None"),    QStringLiteral("True"),
        QStringLiteral("and"),    QStringLiteral("as"),      QStringLiteral("assert"),
        QStringLiteral("async"),  QStringLiteral("await"),   QStringLiteral("break"),
        QStringLiteral("class"),  QStringLiteral("continue"),QStringLiteral("def"),
        QStringLiteral("del"),    QStringLiteral("elif"),    QStringLiteral("else"),
        QStringLiteral("except"), QStringLiteral("finally"), QStringLiteral("for"),
        QStringLiteral("from"),   QStringLiteral("global"),  QStringLiteral("if"),
        QStringLiteral("import"), QStringLiteral("in"),      QStringLiteral("is"),
        QStringLiteral("lambda"), QStringLiteral("nonlocal"),QStringLiteral("not"),
        QStringLiteral("or"),     QStringLiteral("pass"),    QStringLiteral("raise"),
        QStringLiteral("return"), QStringLiteral("try"),     QStringLiteral("while"),
        QStringLiteral("with"),   QStringLiteral("yield"),
    };
    return kPythonKeywords.contains(name) ? QStringLiteral("of_") + name : name;
}

// Map MLIR short-form type aliases to the Python DataType enum names.
static QString normalizeValueType(const QString& vt)
{
    static const QHash<QString, QString> aliases = {
        { QStringLiteral("i8"),   QStringLiteral("int8")    },
        { QStringLiteral("i16"),  QStringLiteral("int16")   },
        { QStringLiteral("i32"),  QStringLiteral("int32")   },
        { QStringLiteral("i64"),  QStringLiteral("int64")   },
        { QStringLiteral("ui8"),  QStringLiteral("uint8")   },
        { QStringLiteral("ui16"), QStringLiteral("uint16")  },
        { QStringLiteral("ui32"), QStringLiteral("uint32")  },
        { QStringLiteral("bf16"), QStringLiteral("bfloat16") },
        { QStringLiteral("f32"),  QStringLiteral("float32") },
        { QStringLiteral("f64"),  QStringLiteral("float64") },
    };
    return aliases.value(vt, vt);
}

HlirSyncService::HlirSyncService(QObject* parent)
    : QObject(parent)
{
}

HlirSyncService::~HlirSyncService() = default;

void HlirSyncService::setKernelRegistry(KernelRegistryService* registry)
{
    m_kernelRegistry = registry;
}

void HlirSyncService::attachDocument(Canvas::CanvasDocument* doc, const QString& outputBaseDir)
{
    // Swap to a new document, resetting bridge state and starting a fresh sync.
    if (m_document) {
        disconnect(m_document, &Canvas::CanvasDocument::changed,
                   this, &HlirSyncService::onCanvasChanged);
    }

    resetTrackedComponents();

    m_document = doc;
    m_outputDir = outputBaseDir;

    if (!doc)
        return;

    // Create bridge once — reused across designs
    if (!m_bridge) {
        try {
            m_bridge = std::make_unique<hlir::HlirBridge>("gui_design");
        } catch (const std::exception& e) {
            qCWarning(hlirSyncLog) << "HlirSyncService: bridge init failed:" << e.what();
            m_document = nullptr;
            return;
        }
    }

    // Subscribe to future changes and do the initial sync
    connect(doc, &Canvas::CanvasDocument::changed,
            this, &HlirSyncService::onCanvasChanged);
    syncCanvas();
}

void HlirSyncService::detachDocument()
{
    // Disconnect the current document and remove all tracked bridge components.
    if (m_document) {
        disconnect(m_document, &Canvas::CanvasDocument::changed,
                   this, &HlirSyncService::onCanvasChanged);
        m_document = nullptr;
    }
    resetTrackedComponents();
    m_outputDir.clear();
}

void HlirSyncService::onCanvasChanged()
{
    // Re-sync the bridge whenever the canvas changes.
    syncCanvas();
}

void HlirSyncService::syncCanvas()
{
    // Guard against re-entrant calls triggered by pivot-wire annotations written in
    // syncSplitsAndJoins() → setObjectFifo() → CanvasDocument::changed signal.
    if (m_syncInProgress)
        return;
    m_syncInProgress = true;

    // Diff canvas items against tracked maps and update tiles and FIFOs in the bridge.
    if (!m_document || !m_bridge) {
        m_syncInProgress = false;
        return;
    }

    const auto& items = m_document->items();

    QSet<Canvas::ObjectId> currentIds;
    currentIds.reserve(static_cast<int>(items.size()));
    for (const auto& item : items)
        currentIds.insert(item->id());

    // Workers, core functions, and kernels are always rebuilt from scratch by buildWorkers().
    // Clear them first so they never block FIFO or split/join removal below.
    m_bridge->clearRuntime();
    for (const hlir::ComponentId& id : std::as_const(m_workerMap))
        m_bridge->remove(id);
    m_workerMap.clear();
    for (const hlir::ComponentId& id : std::as_const(m_coreFuncMap))
        m_bridge->remove(id);
    m_coreFuncMap.clear();
    for (const hlir::ComponentId& id : std::as_const(m_kernelMap))
        m_bridge->remove(id);
    m_kernelMap.clear();

    // Fully remove all split/joins, FIFOs, tilers, and types on every sync so that stale
    // configs (e.g. from a prior 4-way split, or a changed TAP) never accumulate.
    // Dependency order: split/join → FIFOs → tilers → types.
    for (const hlir::ComponentId& id : std::as_const(m_splitJoinMap))
        m_bridge->remove(id);
    m_splitJoinMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_fifoMap))
        m_bridge->remove(id);
    m_fifoMap.clear();

    // Tiler cache must be cleared alongside types so TAP config changes take effect on re-gen.
    for (const hlir::ComponentId& id : std::as_const(m_tilerMap))
        m_bridge->remove(id);
    m_tilerMap.clear();
    m_branchTypeMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_typeMap))
        m_bridge->remove(id);
    m_typeMap.clear();

    // Tiles are not rebuilt every sync — only remove ones that are no longer on the canvas.
    for (auto it = m_tileMap.begin(); it != m_tileMap.end(); ) {
        if (!currentIds.contains(it.key())) {
            m_bridge->remove(it.value());
            it = m_tileMap.erase(it);
        } else {
            ++it;
        }
    }

    // Sync tiles
    for (const auto& item : items) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block || block->specId().isEmpty())
            continue;

        const auto parsed = parseTileSpecId(block->specId());
        if (!parsed)
            continue;

        const hlir::ComponentId existingId = m_tileMap.value(block->id());
        const std::string tileName = block->specId().toStdString();

        auto result = m_bridge->addTile(tileName, parsed->kind, parsed->col, parsed->row, existingId);
        if (result) {
            m_tileMap[block->id()] = result.value();
        } else {
            qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync tile" << block->specId();
        }
    }

    // Sync FIFOs — two-pass to handle colliding base names.
    // When multiple wires share the same base name all are suffixed _a, _b, _c…
    struct FifoEntry {
        Canvas::CanvasWire* wire;
        QString             baseName;
        int                 depth;
        hlir::ComponentId   typeId;
        hlir::ComponentId   producerCompId;
        hlir::ComponentId   consumerCompId;
    };

    // Pass 1: validate every wire and collect its base name + parameters.
    QList<FifoEntry> fifoEntries;
    for (const auto& item : items) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire)
            continue;

        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;

        auto* blockA = dynamic_cast<Canvas::CanvasBlock*>(
            m_document->findItem(epA.attached->itemId));
        auto* blockB = dynamic_cast<Canvas::CanvasBlock*>(
            m_document->findItem(epB.attached->itemId));
        if (!blockA || !blockB)
            continue;

        const QString producerSpecId = blockA->specId().trimmed();
        const QString consumerSpecId = blockB->specId().trimmed();

        if (producerSpecId.isEmpty() || !parseTileSpecId(producerSpecId))
            continue;
        if (consumerSpecId.isEmpty() || !parseTileSpecId(consumerSpecId))
            continue;

        const hlir::ComponentId producerCompId = m_tileMap.value(blockA->id());
        const hlir::ComponentId consumerCompId = m_tileMap.value(blockB->id());
        if (producerCompId.empty() || consumerCompId.empty())
            continue;

        int depth = 2;
        hlir::ComponentId typeId;
        QString baseName;

        if (wire->hasObjectFifo()) {
            const auto& cfg = wire->objectFifo().value();
            baseName = sanitizePythonName(cfg.name);
            depth = cfg.depth;
            if (!cfg.type.dimensions.isEmpty())
                typeId = ensureTensorType(cfg.type.dimensions, normalizeValueType(cfg.type.valueType));
            // else: dimensions unspecified → typeId stays empty → wire is skipped below
        } else {
            baseName = QStringLiteral("fifo_%1_to_%2").arg(producerSpecId, consumerSpecId);
        }

        if (typeId.empty())
            continue;

        fifoEntries.append({wire, baseName, depth, typeId, producerCompId, consumerCompId});
    }

    // Pass 2: count occurrences of each base name.
    QHash<QString, int> nameCount;
    for (const auto& e : fifoEntries)
        nameCount[e.baseName]++;

    // Pass 3: assign final names — suffix _a/_b/_c… when base name is not unique.
    QHash<QString, int> nameIndex;
    for (const auto& e : fifoEntries) {
        const QString fifoName = (nameCount[e.baseName] > 1)
            ? e.baseName + u'_' + QChar(u'a' + nameIndex[e.baseName]++)
            : e.baseName;

        const hlir::ComponentId existingId = m_fifoMap.value(e.wire->id());
        auto result = m_bridge->addFifo(
            fifoName.toStdString(),
            e.typeId,
            e.depth,
            e.producerCompId,
            {e.consumerCompId},
            existingId);

        if (result) {
            m_fifoMap[e.wire->id()] = result.value();
        } else {
            qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync FIFO" << fifoName;
        }
    }

    // Sync split and join hub blocks (depend on tiles and FIFOs being registered first).
    syncSplitsAndJoins();

    m_syncInProgress = false;
}

void HlirSyncService::syncSplitsAndJoins()
{
    // For each split/join hub block, register an addFifoSplit or addFifoJoin operation.
    // Topology rules (from document.json analysis):
    //   SPLIT: hub appears at endpoint B with a consumer port (pivot wire);
    //          hub appears at endpoint A with producer ports (output arm wires).
    //   JOIN:  hub appears at endpoint B with a producer port (pivot wire);
    //          hub appears at endpoint A with consumer ports (input arm wires).
    // The placement tile is always the non-hub block at endpoint A of the pivot wire.
    // Source FIFO (split) = registered FIFO where placement tile is the consumer.
    // Dest FIFO  (join)   = registered FIFO where placement tile is the producer.

    if (!m_document || !m_bridge)
        return;

    const auto& items = m_document->items();

    // Build lookup: tile ObjectId → all FIFOs connected to that tile as consumer/producer.
    // A list is used so that when multiple FIFOs connect to the same mem tile the user
    // can select which one drives the split/join via the pivot wire's "Name" field.
    struct FifoInfo {
        hlir::ComponentId fifoId;
        hlir::ComponentId typeId;
        QString           baseName;    // objectFifo name or "fifo" (used for sub-FIFO name gen)
        int               elementCount = 1024; // total elements in type (for offset calculation)
        int               depth = 2;
        QString           valueType = QStringLiteral("i32");
        QString           dimensions;
    };
    QHash<Canvas::ObjectId, QList<FifoInfo>> consumerBlockFifos; // tile → all FIFOs consumed
    QHash<Canvas::ObjectId, QList<FifoInfo>> producerBlockFifos; // tile → all FIFOs produced

    // Compute total element count from "1024" → 1024, "32x4" → 128, "" → 1024 (data_ty default).
    auto elementCountFromDims = [](const QString& dims) -> int {
        if (dims.isEmpty())
            return 1024;
        int count = 1;
        for (const QString& d : dims.split(u'x', Qt::SkipEmptyParts))
            count *= d.trimmed().toInt();
        return count;
    };

    for (const auto& item : items) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire || !m_fifoMap.contains(wire->id()))
            continue;

        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;

        auto* blockA = dynamic_cast<Canvas::CanvasBlock*>(
            m_document->findItem(epA.attached->itemId));
        auto* blockB = dynamic_cast<Canvas::CanvasBlock*>(
            m_document->findItem(epB.attached->itemId));
        if (!blockA || !blockB)
            continue;

        hlir::ComponentId typeId;
        QString baseName = QStringLiteral("fifo");
        int elemCount = 1024;
        int fifoDepth = 2;
        QString fifoValueType = QStringLiteral("i32");
        QString fifoDimensions;
        if (wire->hasObjectFifo()) {
            const auto& cfg = wire->objectFifo().value();
            baseName = cfg.name;
            fifoDepth = cfg.depth;
            fifoValueType = cfg.type.valueType.isEmpty() ? QStringLiteral("i32") : cfg.type.valueType;
            fifoDimensions = cfg.type.dimensions;
            if (!cfg.type.dimensions.isEmpty())
                typeId = ensureTensorType(cfg.type.dimensions, normalizeValueType(cfg.type.valueType));
            // else: dimensions unspecified → keep defaultTypeId (shared data type)
            elemCount = elementCountFromDims(cfg.type.dimensions);
        }

        const hlir::ComponentId fifoId = m_fifoMap.value(wire->id());
        const FifoInfo info{fifoId, typeId, baseName, elemCount, fifoDepth, fifoValueType, fifoDimensions};
        producerBlockFifos[blockA->id()].append(info);
        consumerBlockFifos[blockB->id()].append(info);
    }

    // Helper: pick the FifoInfo for a given tile, preferring the one whose baseName matches
    // the user-selected name on the pivot wire. Falls back to the first available FIFO.
    const auto pickFifo = [](const QList<FifoInfo>& list,
                              const QString& preferredName) -> const FifoInfo* {
        if (list.isEmpty())
            return nullptr;
        if (!preferredName.isEmpty()) {
            for (const auto& info : list) {
                if (info.baseName == preferredName)
                    return &info;
            }
        }
        return &list.first();
    };

    // Process each hub block (split, join, or broadcast). Sequential counters provide stable names.
    int splitIdx = 0;
    int joinIdx  = 0;
    int bcastIdx = 0;

    for (const auto& item : items) {
        auto* hubBlock = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!hubBlock || !hubBlock->isLinkHub() || !hubBlock->specId().isEmpty())
            continue;

        // Build port-role lookup for this hub block.
        QHash<Canvas::PortId, Canvas::PortRole> portRoles;
        for (const auto& port : hubBlock->ports())
            portRoles[port.id] = port.role;

        // Classify hub wires: pivot (hub at endpoint B) vs arm (hub at endpoint A).
        Canvas::CanvasWire* pivotWire     = nullptr;
        Canvas::CanvasBlock* pivotBlock   = nullptr;
        Canvas::PortRole     pivotRole    = Canvas::PortRole::Dynamic;

        // arm entries: hub-port-id paired with the wire
        QList<QPair<Canvas::PortId, Canvas::CanvasWire*>> armWires;

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
                // Hub at B → pivot wire: the non-hub block at A is the placement tile.
                auto* blkA = dynamic_cast<Canvas::CanvasBlock*>(
                    m_document->findItem(epA.attached->itemId));
                if (!blkA || blkA->isLinkHub())
                    continue;
                pivotWire  = wire;
                pivotBlock = blkA;
                pivotRole  = portRoles.value(epB.attached->portId, Canvas::PortRole::Dynamic);
            } else { // aIsHub
                // Hub at A → arm wire (split output or join input).
                auto* blkB = dynamic_cast<Canvas::CanvasBlock*>(
                    m_document->findItem(epB.attached->itemId));
                if (blkB && !blkB->isLinkHub())
                    armWires.append({epA.attached->portId, wire});
            }
        }

        if (!pivotWire || !pivotBlock || armWires.isEmpty())
            continue;

        const hlir::ComponentId placementTileId = m_tileMap.value(pivotBlock->id());
        if (placementTileId.empty())
            continue;

        // Build portId → arm wire map (for ordering by hub port index).
        QHash<Canvas::PortId, Canvas::CanvasWire*> portToArmWire;
        for (const auto& pair : armWires)
            portToArmWire[pair.first] = pair.second;

        const hlir::ComponentId existingId = m_splitJoinMap.value(hubBlock->id());

        // Detect hub kind from block symbol content ("S"=split, "J"=join, "B"=broadcast).
        const bool isBroadcast = [&] {
            auto* sym = dynamic_cast<Canvas::BlockContentSymbol*>(hubBlock->content());
            return sym && sym->symbol().trimmed()
                       == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Broadcast).symbol;
        }();

        if (isBroadcast) {
            // ----- BROADCAST -----
            // Pivot wire enters hub via consumer port → hub forwards the same FIFO to other consumers.
            const QString bcastPreferred = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().name.trimmed() : QString();
            const FifoInfo* srcFifo = pickFifo(consumerBlockFifos.value(pivotBlock->id()), bcastPreferred);
            if (!srcFifo || srcFifo->fifoId.empty()) {
                qCWarning(hlirSyncLog) << "HlirSyncService: broadcast has no source FIFO for tile"
                                       << pivotBlock->specId();
                continue;
            }

            ++bcastIdx;
            // Use user-set hub name from the pivot wire if present; otherwise auto-generate.
            const QString existingBcastName = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().hubName.trimmed()
                : QString();
            const QString bcastName = existingBcastName.isEmpty()
                ? QStringLiteral("bcast") + QString::number(bcastIdx)
                : existingBcastName;
            // Write broadcast name and resolved FIFO name to the pivot wire for annotation.
            // Safe: syncCanvas() skips wires whose endpoints have empty specId (hubs).
            {
                Canvas::CanvasWire::ObjectFifoConfig cfg =
                    pivotWire->hasObjectFifo() ? pivotWire->objectFifo().value()
                                               : Canvas::CanvasWire::ObjectFifoConfig{};
                if (existingBcastName.isEmpty())
                    cfg.hubName = bcastName;
                if (bcastPreferred.isEmpty() || bcastPreferred != srcFifo->baseName)
                    cfg.name = srcFifo->baseName;
                cfg.operation = Canvas::CanvasWire::ObjectFifoOperation::Forward;
                cfg.depth = srcFifo->depth;
                cfg.type.valueType = srcFifo->valueType;
                cfg.type.dimensions = srcFifo->dimensions;
                pivotWire->setObjectFifo(cfg);
            }

            const std::map<std::string, std::string> metadata = {
                {"placement", pivotBlock->specId().toStdString()}
            };

            auto result = m_bridge->addFifoForward(
                bcastName.toStdString(),
                srcFifo->fifoId,
                existingId,
                metadata);

            if (result) {
                m_splitJoinMap[hubBlock->id()] = result.value();
                m_branchTypeMap[hubBlock->id()] = srcFifo->typeId;
            } else {
                qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync broadcast" << bcastName;
            }

            // Mark broadcast arm wires with Forward + empty hubName so they show no annotation.
            for (const auto& port : hubBlock->ports()) {
                if (port.role != Canvas::PortRole::Producer)
                    continue;
                auto* armWire = portToArmWire.value(port.id, nullptr);
                if (!armWire)
                    continue;
                Canvas::CanvasWire::ObjectFifoConfig cfg =
                    armWire->hasObjectFifo() ? armWire->objectFifo().value()
                                             : Canvas::CanvasWire::ObjectFifoConfig{};
                cfg.operation = Canvas::CanvasWire::ObjectFifoOperation::Forward;
                cfg.hubName   = QString();
                armWire->setObjectFifo(cfg);
            }

        } else if (pivotRole == Canvas::PortRole::Consumer) {
            // ----- SPLIT -----
            // Pivot wire enters hub via consumer port → hub distributes to multiple outputs.
            // User-set name on pivot wire selects which FIFO to split when multiple are present.
            const QString splitPreferred = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().name.trimmed() : QString();
            const FifoInfo* srcFifo = pickFifo(consumerBlockFifos.value(pivotBlock->id()), splitPreferred);
            if (!srcFifo || srcFifo->fifoId.empty()) {
                qCWarning(hlirSyncLog) << "HlirSyncService: split has no source FIFO for tile"
                                       << pivotBlock->specId();
                continue;
            }

            // Collect output sub-FIFO names ordered by hub producer port index.
            ++splitIdx;
            // Use user-set hub name from the pivot wire if present; otherwise auto-generate.
            const QString existingSplitName = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().hubName.trimmed()
                : QString();
            const QString splitName = existingSplitName.isEmpty()
                ? QStringLiteral("split") + QString::number(splitIdx)
                : existingSplitName;
            // Write split name and resolved FIFO name to the pivot wire for annotation.
            // Preserve user-typed name if it matched; write resolved name otherwise.
            // Safe: syncCanvas() skips wires whose endpoints have empty specId (hubs).
            {
                Canvas::CanvasWire::ObjectFifoConfig cfg =
                    pivotWire->hasObjectFifo() ? pivotWire->objectFifo().value()
                                               : Canvas::CanvasWire::ObjectFifoConfig{};
                if (existingSplitName.isEmpty())
                    cfg.hubName = splitName;
                if (splitPreferred.isEmpty() || splitPreferred != srcFifo->baseName)
                    cfg.name = srcFifo->baseName;
                cfg.operation = Canvas::CanvasWire::ObjectFifoOperation::Split;
                // Mirror source FIFO depth/type for the properties panel display.
                cfg.depth = srcFifo->depth;
                cfg.type.valueType = srcFifo->valueType;
                cfg.type.dimensions = srcFifo->dimensions;
                pivotWire->setObjectFifo(cfg);
            }

            std::vector<std::string> outputNames;
            for (const auto& port : hubBlock->ports()) {
                if (port.role != Canvas::PortRole::Producer)
                    continue;
                const int idx     = static_cast<int>(outputNames.size());
                auto*     armWire = portToArmWire.value(port.id, nullptr);
                QString   name;
                if (armWire && armWire->hasObjectFifo())
                    name = sanitizePythonName(armWire->objectFifo().value().name);
                else
                    name = splitName + QStringLiteral("_out") + QString::number(idx + 1);
                outputNames.push_back(name.toStdString());
                // Mark arm wire so its annotation shows only the sub-FIFO name, near the tile port.
                if (armWire) {
                    Canvas::CanvasWire::ObjectFifoConfig cfg =
                        armWire->hasObjectFifo() ? armWire->objectFifo().value()
                                                 : Canvas::CanvasWire::ObjectFifoConfig{};
                    cfg.name      = QString::fromStdString(outputNames.back());
                    cfg.operation = Canvas::CanvasWire::ObjectFifoOperation::Split;
                    cfg.hubName   = QString(); // empty hubName = arm wire, not pivot
                    armWire->setObjectFifo(cfg);
                }
            }

            if (outputNames.empty())
                continue;

            const int numOut = static_cast<int>(outputNames.size());
            const int stride = srcFifo->elementCount / numOut;
            const hlir::ComponentId branchTypeId =
                ensureTensorType(QString::number(stride), srcFifo->valueType);
            std::vector<int> offsets;
            offsets.reserve(numOut);
            for (int i = 0; i < numOut; ++i)
                offsets.push_back(stride * i);

            auto result = m_bridge->addFifoSplit(
                splitName.toStdString(),
                srcFifo->fifoId,
                numOut,
                branchTypeId,
                outputNames,
                offsets,
                placementTileId,
                existingId);

            if (result) {
                m_splitJoinMap[hubBlock->id()] = result.value();
                m_branchTypeMap[hubBlock->id()] = branchTypeId;
            } else {
                qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync split" << splitName;
            }

        } else if (pivotRole == Canvas::PortRole::Producer) {
            // ----- JOIN -----
            // Pivot wire exits hub via producer port → hub collects from multiple inputs.
            // User-set name on pivot wire selects which FIFO to join into when multiple are present.
            const QString joinPreferred = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().name.trimmed() : QString();
            const FifoInfo* dstFifo = pickFifo(producerBlockFifos.value(pivotBlock->id()), joinPreferred);
            if (!dstFifo || dstFifo->fifoId.empty()) {
                qCWarning(hlirSyncLog) << "HlirSyncService: join has no dest FIFO for tile"
                                       << pivotBlock->specId();
                continue;
            }

            // Collect input sub-FIFO names ordered by hub consumer port index.
            ++joinIdx;
            // Use user-set hub name from the pivot wire if present; otherwise auto-generate.
            const QString existingJoinName = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().hubName.trimmed()
                : QString();
            const QString joinName = existingJoinName.isEmpty()
                ? QStringLiteral("join") + QString::number(joinIdx)
                : existingJoinName;
            // Write join name and resolved FIFO name to the pivot wire for annotation.
            // Preserve user-typed name if it matched; write resolved name otherwise.
            // Safe: syncCanvas() skips wires whose endpoints have empty specId (hubs).
            {
                Canvas::CanvasWire::ObjectFifoConfig cfg =
                    pivotWire->hasObjectFifo() ? pivotWire->objectFifo().value()
                                               : Canvas::CanvasWire::ObjectFifoConfig{};
                if (existingJoinName.isEmpty())
                    cfg.hubName = joinName;
                if (joinPreferred.isEmpty() || joinPreferred != dstFifo->baseName)
                    cfg.name = dstFifo->baseName;
                cfg.operation = Canvas::CanvasWire::ObjectFifoOperation::Join;
                // Mirror destination FIFO depth/type for the properties panel display.
                cfg.depth = dstFifo->depth;
                cfg.type.valueType = dstFifo->valueType;
                cfg.type.dimensions = dstFifo->dimensions;
                pivotWire->setObjectFifo(cfg);
            }

            std::vector<std::string> inputNames;
            for (const auto& port : hubBlock->ports()) {
                if (port.role != Canvas::PortRole::Consumer)
                    continue;
                const int idx     = static_cast<int>(inputNames.size());
                auto*     armWire = portToArmWire.value(port.id, nullptr);
                QString   name;
                if (armWire && armWire->hasObjectFifo())
                    name = sanitizePythonName(armWire->objectFifo().value().name);
                else
                    name = joinName + QStringLiteral("_in") + QString::number(idx + 1);
                inputNames.push_back(name.toStdString());
                // Mark arm wire so its annotation shows only the sub-FIFO name, near the tile port.
                if (armWire) {
                    Canvas::CanvasWire::ObjectFifoConfig cfg =
                        armWire->hasObjectFifo() ? armWire->objectFifo().value()
                                                 : Canvas::CanvasWire::ObjectFifoConfig{};
                    cfg.name      = QString::fromStdString(inputNames.back());
                    cfg.operation = Canvas::CanvasWire::ObjectFifoOperation::Join;
                    cfg.hubName   = QString(); // empty hubName = arm wire, not pivot
                    armWire->setObjectFifo(cfg);
                }
            }

            if (inputNames.empty())
                continue;

            const int numIn = static_cast<int>(inputNames.size());
            const int stride = dstFifo->elementCount / numIn;
            const hlir::ComponentId branchTypeId =
                ensureTensorType(QString::number(stride), dstFifo->valueType);
            std::vector<int> offsets;
            offsets.reserve(numIn);
            for (int i = 0; i < numIn; ++i)
                offsets.push_back(stride * i);

            auto result = m_bridge->addFifoJoin(
                joinName.toStdString(),
                dstFifo->fifoId,
                numIn,
                branchTypeId,
                inputNames,
                offsets,
                placementTileId,
                existingId);

            if (result) {
                m_splitJoinMap[hubBlock->id()] = result.value();
                m_branchTypeMap[hubBlock->id()] = branchTypeId;
            } else {
                qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync join" << joinName;
            }
        }
    }
}

QList<VerificationIssue> HlirSyncService::runVerification() const
{
    // Run all registered design checks against the current canvas document.
    if (!m_document)
        return {};
    return DesignVerifier().verify({m_document});
}

void HlirSyncService::verifyDesign()
{
    // Run verification and emit per-step progress, then a final summary.
    if (!m_document) {
        emit verificationFinished(false, tr("No design is open."));
        return;
    }

    emit runStarted();
    QCoreApplication::processEvents();

    const auto results = DesignVerifier().verifyDetailed({m_document});

    QList<VerificationIssue> allErrors;
    for (const auto& result : results) {
        const bool passed = !DesignVerifier::hasErrors(result.issues);
        emit stepLogged(passed, result.displayName);
        QCoreApplication::processEvents();
        QThread::msleep(250);
        if (!passed) {
            for (const auto& issue : result.issues)
                if (issue.severity == VerificationIssue::Severity::Error)
                    allErrors.append(issue);
        }
    }

    if (!allErrors.isEmpty()) {
        QString msg = tr("Verification failed:\n\n");
        for (const auto& issue : allErrors)
            msg += QStringLiteral("\u2022 ") + issue.message + u'\n';
        emit verificationFinished(false, msg);
    } else {
        const DesignStats stats = collectStats({m_document});
        const QString msg =
            tr("Design verification passed.\n\n"
               "Design summary:\n"
               "  \u2022 SHIM tiles:  %1\n"
               "  \u2022 MEM tiles:   %2\n"
               "  \u2022 AIE tiles:   %3\n"
               "  \u2022 FIFOs:       %4\n"
               "  \u2022 Splits:      %5\n"
               "  \u2022 Joins:       %6\n"
               "  \u2022 Broadcasts:  %7\n"
               "  \u2022 Fills:       %8\n"
               "  \u2022 Drains:      %9")
            .arg(stats.shimTiles).arg(stats.memTiles).arg(stats.aieTiles)
            .arg(stats.fifos).arg(stats.splits).arg(stats.joins)
            .arg(stats.broadcasts).arg(stats.fills).arg(stats.drains);
        emit verificationFinished(true, msg);
    }
}

void HlirSyncService::generateCode()
{
    // Sync canvas, build HLIR, export to GUI XML, and run code generation.
    // Each step emits stepLogged() + processEvents() so the log updates in real time.
    if (!m_bridge || !m_document) {
        emit codeGenFinished(false, tr("No design is open."));
        return;
    }

    emit runStarted();
    QCoreApplication::processEvents();

    const auto emitStep = [this](const QString& label, bool ok) {
        emit stepLogged(ok, label);
        QCoreApplication::processEvents();
        QThread::msleep(250);
    };

    // Step 1: Verify the design before generating code
    const auto verifyResults = DesignVerifier().verifyDetailed({m_document});
    bool verifyPassed = true;
    QList<VerificationIssue> verifyErrors;
    for (const auto& result : verifyResults) {
        const bool passed = !DesignVerifier::hasErrors(result.issues);
        emitStep(result.displayName, passed);
        if (!passed) {
            verifyPassed = false;
            for (const auto& issue : result.issues)
                if (issue.severity == VerificationIssue::Severity::Error)
                    verifyErrors.append(issue);
        }
    }

    if (!verifyPassed) {
        QString msg = tr("Cannot generate code — design verification failed.\n\nIssues:\n");
        for (const auto& issue : verifyErrors)
            msg += QStringLiteral("\u2022 ") + issue.message + u'\n';
        emit codeGenFinished(false, msg);
        return;
    }

    // Step 2: Sync canvas → HLIR (specIds assigned lazily by grid host)
    syncCanvas();
    buildWorkers();
    buildRuntime();
    emitStep(tr("Syncing canvas to HLIR"), true);

    // Step 3: Build and validate the HLIR program
    auto buildResult = m_bridge->build();
    emitStep(tr("Building HLIR program"), buildResult.has_value());
    if (!buildResult) {
        QStringList errors;
        for (const auto& diag : buildResult.error())
            errors << QString::fromStdString(diag.message);
        emit codeGenFinished(false, errors.join(u'\n'));
        return;
    }

    // Step 4: Export to GUI XML — "_gui.xml" suffix triggers the XMLTransformer step
    QDir().mkpath(m_outputDir);
    const QString xmlPath = m_outputDir + QStringLiteral("/design_gui.xml");
    auto exportResult = m_bridge->exportToGuiXml(xmlPath.toStdString());
    emitStep(tr("Exporting design to XML"), exportResult.has_value());
    if (!exportResult) {
        emit codeGenFinished(false, tr("Failed to export HLIR to XML."));
        return;
    }

    // Step 5: Run Python code generation
    codegen::CodeGenBridge codegenBridge;
    codegen::CodeGenOptions options;
    options.outputDir = m_outputDir.toStdString();

    auto genResult = codegenBridge.runCodeGen(
        std::filesystem::path(xmlPath.toStdWString()), options);
    emitStep(tr("Running code generator"), genResult.has_value());

    if (genResult) {
        emit codeGenFinished(true, tr("Code generated in %1").arg(m_outputDir));
    } else {
        QStringList errors;
        for (const auto& diag : genResult.error())
            errors << QString::fromStdString(diag.message);
        emit codeGenFinished(false, errors.join(u'\n'));
    }
}

std::optional<HlirSyncService::ParsedTileSpec>
HlirSyncService::parseTileSpecId(const QString& specId) const
{
    // Parse a tile specId (e.g. "shim0_0") into tile kind and coordinates.
    // Format: "{kind}{col}_{row}" where kind ∈ {"shim","mem","aie"}
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

        return ParsedTileSpec{p.kind, col, row};
    }

    return std::nullopt;
}

hlir::ComponentId
HlirSyncService::ensureNamedTensorType(const QString& name,
                                       const QString& dimensions,
                                       const QString& valueType)
{
    // Return a cached or newly registered tensor type with an explicit name.
    if (m_typeMap.contains(name))
        return m_typeMap.value(name);

    // Build the shape vector from the dimensions string
    const QStringList dimParts = dimensions.isEmpty()
        ? QStringList{QStringLiteral("1")}
        : dimensions.split(u'x', Qt::SkipEmptyParts);

    std::vector<std::string> shape;
    shape.reserve(static_cast<size_t>(dimParts.size()));
    for (const QString& d : dimParts)
        shape.push_back(d.trimmed().toStdString());

    auto result = m_bridge->addTensorType(name.toStdString(), shape, valueType.toStdString());
    if (result) {
        m_typeMap[name] = result.value();
        return result.value();
    }

    auto lookup = m_bridge->lookupByName(hlir::ComponentType::TENSOR_TYPE, name.toStdString());
    if (lookup) {
        m_typeMap[name] = lookup.value();
        return lookup.value();
    }

    qCWarning(hlirSyncLog) << "HlirSyncService: could not resolve tensor type" << name;
    return hlir::ComponentId{};
}

hlir::ComponentId
HlirSyncService::ensureTensorType(const QString& dimensions, const QString& valueType)
{
    // Return a cached or newly registered tensor type derived from dimensions and dtype.
    const QString vt = normalizeValueType(valueType);

    const QString key = dimensions + u'|' + vt;
    if (m_typeMap.contains(key))
        return m_typeMap.value(key);

    // Parse "1024" → {"1024"}, "1024x512" → {"1024","512"}
    const QStringList dimParts = dimensions.isEmpty()
        ? QStringList{QStringLiteral("1")}
        : dimensions.split(u'x', Qt::SkipEmptyParts);

    std::vector<std::string> shape;
    shape.reserve(static_cast<size_t>(dimParts.size()));
    for (const QString& d : dimParts)
        shape.push_back(d.trimmed().toStdString());

    // Build a unique type name: "type_int32_1024" or "type_int16_16x4096"
    const QString typeName = QStringLiteral("type_") + vt + u'_' + dimensions;

    auto result = m_bridge->addTensorType(typeName.toStdString(), shape, vt.toStdString());
    if (result) {
        m_typeMap[key] = result.value();
        return result.value();
    }

    auto lookup = m_bridge->lookupByName(hlir::ComponentType::TENSOR_TYPE, typeName.toStdString());
    if (lookup) {
        m_typeMap[key] = lookup.value();
        return lookup.value();
    }

    qCWarning(hlirSyncLog) << "HlirSyncService: could not resolve tensor type" << typeName;
    return hlir::ComponentId{};
}

hlir::ComponentId HlirSyncService::ensureTensorTiler2D(
    const QString& name,
    const Canvas::CanvasWire::TensorTilerConfig& tap,
    const QString& totalDims)
{
    if (m_tilerMap.contains(name))
        return m_tilerMap.value(name);

    // Parse a "rows x cols" or "expr" string into a vector of string tokens.
    const auto parseDims = [](const QString& s) {
        std::vector<std::string> out;
        for (const QString& p : s.split(u'x', Qt::SkipEmptyParts))
            out.push_back(p.trimmed().toStdString());
        return out;
    };

    const auto tensorDims = parseDims(totalDims);
    const auto tileDims   = parseDims(tap.tileDims);
    const auto tileCounts = parseDims(tap.tileCounts);

    auto result = m_bridge->addTensorTiler2D(
        name.toStdString(),
        tensorDims, tileDims, tileCounts,
        tap.pruneStep, tap.index,
        tap.patternRepeat.toStdString());

    if (result) {
        m_tilerMap[name] = result.value();
        return result.value();
    }

    qCWarning(hlirSyncLog) << "HlirSyncService: could not register TensorTiler2D" << name;
    return hlir::ComponentId{};
}

void HlirSyncService::buildWorkers()
{
    // For each COMPUTE tile with a <<kernel: id>> stereotype:
    //   - One external kernel declaration per unique kernel ID (shared across tiles).
    //   - One core body function per unique kernel ID (shared across tiles).
    //   - One worker per tile, referencing the shared kernel and core function.
    if (!m_document || !m_bridge || !m_kernelRegistry)
        return;

    // Workers, kernels, and core functions were already cleared by syncCanvas() which
    // always runs before buildWorkers() in generateCode(). Maps are empty here.

    static const QRegularExpression kKernelAnnotationPattern(
        QStringLiteral("<<\\s*kernel\\s*:\\s*([A-Za-z0-9_.-]+)\\s*>>"),
        QRegularExpression::CaseInsensitiveOption);

    const auto& items = m_document->items();

    // Return the tensor type ID to use for a wire's FIFO argument.
    auto typeIdForWire = [&](Canvas::CanvasWire* wire) -> hlir::ComponentId {
        if (wire && wire->hasObjectFifo()) {
            const auto& cfg = wire->objectFifo().value();
            if (!cfg.type.dimensions.isEmpty())
                return ensureTensorType(cfg.type.dimensions, normalizeValueType(cfg.type.valueType));
        }
        return hlir::ComponentId{};
    };

    // For arm wires connected to a split/join/broadcast hub, prefer the pre-computed branch
    // type (which derives from the source/dest FIFO's actual element count and dtype).
    // Arm wires typically have no explicit FIFO config, so typeIdForWire() would fall back to
    // the global default (type_int32_1024), producing wrong kernel argument types.
    auto typeIdForHubArm = [&](Canvas::CanvasWire* wire,
                                const Canvas::CanvasBlock* hub) -> hlir::ComponentId {
        const hlir::ComponentId branchType = m_branchTypeMap.value(hub->id());
        return branchType.empty() ? typeIdForWire(wire) : branchType;
    };

    // Return the port role for portId on block, Dynamic if not found.
    auto portRoleFor = [](const Canvas::CanvasBlock* block,
                          Canvas::PortId portId) -> Canvas::PortRole {
        for (const auto& port : block->ports()) {
            if (port.id == portId)
                return port.role;
        }
        return Canvas::PortRole::Dynamic;
    };

    // 0-based position of portId among ports of the given role on hub (in port order).
    auto armIndexFor = [](const Canvas::CanvasBlock* hub,
                          Canvas::PortId portId,
                          Canvas::PortRole role) -> int {
        int idx = 0;
        for (const auto& port : hub->ports()) {
            if (port.role != role)
                continue;
            if (port.id == portId)
                return idx;
            ++idx;
        }
        return 0;
    };

    // True when the hub block is a broadcast (uses FifoForward).
    auto isBroadcastHub = [](const Canvas::CanvasBlock* hub) -> bool {
        const auto* sym = dynamic_cast<const Canvas::BlockContentSymbol*>(hub->content());
        return sym && sym->symbol().trimmed()
               == Canvas::Support::linkHubStyle(Canvas::Support::LinkHubKind::Broadcast).symbol;
    };

    // ---- Phase 1: collect per-tile FIFO endpoints, grouped by kernel ID ----

    struct FifoEndpoint {
        hlir::HlirBridge::FunctionArg arg;
        hlir::ComponentId typeId;
    };

    struct TileWorkerSpec {
        Canvas::ObjectId blockId;
        std::string      tileName;
        hlir::ComponentId tileId;
        QList<FifoEndpoint> inputs;
        QList<FifoEndpoint> outputs;
    };

    QHash<QString, QList<TileWorkerSpec>> specsByKernelId;

    for (const auto& item : items) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block || block->specId().isEmpty())
            continue;

        const auto parsed = parseTileSpecId(block->specId());
        if (!parsed || parsed->kind != hlir::TileKind::COMPUTE)
            continue;

        // Extract kernel ID from stereotype, then label as fallback.
        QString kernelId;
        {
            QRegularExpressionMatch m = kKernelAnnotationPattern.match(block->stereotype());
            if (!m.hasMatch())
                m = kKernelAnnotationPattern.match(block->label());
            if (!m.hasMatch())
                continue;
            kernelId = m.captured(1).trimmed();
        }

        const hlir::ComponentId tileId = m_tileMap.value(block->id());
        if (tileId.empty()) {
            qCWarning(hlirSyncLog) << "HlirSyncService: tile not tracked for" << block->specId();
            continue;
        }

        // Classify each wire connected to this tile as an input or output endpoint.
        QList<FifoEndpoint> inputs;
        QList<FifoEndpoint> outputs;

        for (const auto& wItem : items) {
            auto* wire = dynamic_cast<Canvas::CanvasWire*>(wItem.get());
            if (!wire)
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
                // Tile is at the consumer endpoint of the wire.
                auto* blockA = dynamic_cast<Canvas::CanvasBlock*>(
                    m_document->findItem(epA.attached->itemId));
                if (!blockA)
                    continue;

                if (!blockA->isLinkHub()) {
                    // Direct FIFO: tile consumes → input.
                    const hlir::ComponentId fifoId = m_fifoMap.value(wire->id());
                    if (!fifoId.empty())
                        inputs.append({hlir::HlirBridge::FunctionArg::fifoConsumer(fifoId, 0),
                                       typeIdForWire(wire)});
                } else {
                    // Arm wire with hub at A — direction depends on hub port role.
                    const hlir::ComponentId hubId = m_splitJoinMap.value(blockA->id());
                    if (hubId.empty())
                        continue;

                    if (isBroadcastHub(blockA)) {
                        inputs.append({hlir::HlirBridge::FunctionArg::forwardConsumer(hubId),
                                       typeIdForHubArm(wire, blockA)});
                    } else {
                        const Canvas::PortRole role =
                            portRoleFor(blockA, epA.attached->portId);
                        if (role == Canvas::PortRole::Producer) {
                            const int idx = armIndexFor(
                                blockA, epA.attached->portId, Canvas::PortRole::Producer);
                            inputs.append({hlir::HlirBridge::FunctionArg::splitConsumer(hubId, idx),
                                           typeIdForHubArm(wire, blockA)});
                        } else if (role == Canvas::PortRole::Consumer) {
                            const int idx = armIndexFor(
                                blockA, epA.attached->portId, Canvas::PortRole::Consumer);
                            outputs.append({hlir::HlirBridge::FunctionArg::joinProducer(hubId, idx),
                                            typeIdForHubArm(wire, blockA)});
                        }
                    }
                }
            } else { // tileIsA
                // Tile is at the producer endpoint of the wire.
                auto* blockB = dynamic_cast<Canvas::CanvasBlock*>(
                    m_document->findItem(epB.attached->itemId));
                if (!blockB || blockB->isLinkHub())
                    continue;

                // Direct FIFO: tile produces → output.
                const hlir::ComponentId fifoId = m_fifoMap.value(wire->id());
                if (!fifoId.empty())
                    outputs.append({hlir::HlirBridge::FunctionArg::fifoProducer(fifoId),
                                    typeIdForWire(wire)});
            }
        }

        specsByKernelId[kernelId].append({
            block->id(),
            block->specId().toStdString(),
            tileId,
            std::move(inputs),
            std::move(outputs)
        });
    }

    // ---- Phase 2: one external kernel + core function per unique kernel ID ----

    for (auto it = specsByKernelId.begin(); it != specsByKernelId.end(); ++it) {
        const QString& kernelId   = it.key();
        auto&          tileSpecs  = it.value();

        if (tileSpecs.isEmpty())
            continue;

        const KernelAsset* kernel = m_kernelRegistry->kernelById(kernelId);
        if (!kernel) {
            qCWarning(hlirSyncLog) << "HlirSyncService: kernel not found:" << kernelId;
            continue;
        }

        // Use the first tile's endpoint counts to define the shared function signature.
        // All tiles using the same kernel are expected to have the same number of
        // inputs and outputs (matching the kernel's C++ parameter list).
        const auto& firstSpec = tileSpecs.first();
        const int numInputs  = firstSpec.inputs.size();
        const int numOutputs = firstSpec.outputs.size();

        // ---- External kernel declaration (once per kernel ID) ----
        std::vector<hlir::ComponentId> argTypes;
        for (const auto& ep : firstSpec.inputs)  argTypes.push_back(ep.typeId);
        for (const auto& ep : firstSpec.outputs) argTypes.push_back(ep.typeId);

        std::vector<std::string> includeDirs;
        for (const QString& dir : kernel->includeDirs)
            includeDirs.push_back(dir.toStdString());

        // Extract the actual C function name from the signature string:
        // "void matvec_vectorized_i16_i32(...);" → "matvec_vectorized_i16_i32"
        // Fall back to kernel->id if the signature is missing or unparseable.
        auto functionNameFromSig = [](const QString& sig, const QString& fallback) -> QString {
            const int paren = sig.indexOf(u'(');
            if (paren < 0)
                return fallback;
            const QString beforeParen = sig.left(paren).trimmed();
            const int spaceIdx = beforeParen.lastIndexOf(u' ');
            const QString name = (spaceIdx >= 0)
                ? beforeParen.sliced(spaceIdx + 1).trimmed()
                : beforeParen;
            return name.isEmpty() ? fallback : name;
        };
        const QString kernelFunctionName = functionNameFromSig(kernel->signature, kernel->id);

        auto kernelResult = m_bridge->addExternalKernel(
            "kernel_" + kernel->id.toStdString(),
            kernelFunctionName.toStdString(),
            kernel->absoluteEntryPath().toStdString(),
            argTypes,
            includeDirs,
            {});

        if (!kernelResult) {
            qCWarning(hlirSyncLog) << "HlirSyncService: failed to add external kernel for"
                                   << kernelId;
            continue;
        }
        m_kernelMap[kernelId] = kernelResult.value();

        // ---- Core body function (once per kernel ID) ----
        // Parameters: ["kernel", "in0", ..., "out0", ...]
        std::vector<std::string> params = {"kernel"};
        for (int i = 0; i < numInputs;  ++i) params.push_back("in"  + std::to_string(i));
        for (int i = 0; i < numOutputs; ++i) params.push_back("out" + std::to_string(i));

        std::vector<hlir::HlirBridge::AcquireSpec> acquires;
        for (int i = 0; i < numInputs;  ++i)
            acquires.push_back({"in"  + std::to_string(i), 1, "buf_in"  + std::to_string(i)});
        for (int i = 0; i < numOutputs; ++i)
            acquires.push_back({"out" + std::to_string(i), 1, "buf_out" + std::to_string(i)});

        std::vector<std::string> kernelArgVars;
        for (int i = 0; i < numInputs;  ++i) kernelArgVars.push_back("buf_in"  + std::to_string(i));
        for (int i = 0; i < numOutputs; ++i) kernelArgVars.push_back("buf_out" + std::to_string(i));

        std::vector<hlir::HlirBridge::ReleaseSpec> releases;
        for (int i = 0; i < numInputs;  ++i) releases.push_back({"in"  + std::to_string(i), 1});
        for (int i = 0; i < numOutputs; ++i) releases.push_back({"out" + std::to_string(i), 1});

        auto coreFuncResult = m_bridge->addCoreFunction(
            "core_" + kernel->id.toStdString(),
            params,
            acquires,
            {"kernel", kernelArgVars},
            releases,
            {});

        if (!coreFuncResult) {
            qCWarning(hlirSyncLog) << "HlirSyncService: failed to add core function for"
                                   << kernelId;
            continue;
        }
        m_coreFuncMap[kernelId] = coreFuncResult.value();

        // ---- Phase 3: one worker per tile using this kernel ----
        for (const auto& spec : tileSpecs) {
            std::vector<hlir::HlirBridge::FunctionArg> fnArgs;
            fnArgs.push_back(hlir::HlirBridge::FunctionArg::kernel(kernelResult.value()));
            for (const auto& ep : spec.inputs)  fnArgs.push_back(ep.arg);
            for (const auto& ep : spec.outputs) fnArgs.push_back(ep.arg);

            auto workerResult = m_bridge->addWorker(
                "worker_" + spec.tileName,
                coreFuncResult.value(),
                fnArgs,
                spec.tileId,
                {});

            if (!workerResult) {
                qCWarning(hlirSyncLog) << "HlirSyncService: failed to add worker for"
                                       << QString::fromStdString(spec.tileName);
                continue;
            }
            m_workerMap[spec.blockId] = workerResult.value();
        }
    }
}

void HlirSyncService::buildRuntime()
{
    // Create a runtime sequence with Fill/Drain ops driven by DDR↔SHIM wires.
    if (!m_document || !m_bridge)
        return;

    const QLatin1StringView ddrSpecId{"ddr"};

    const auto& items = m_document->items();

    // --- Pass 1: Find fill SHIMs (DDR → SHIM) and drain SHIMs (SHIM → DDR).
    //     Also retain the DDR↔SHIM wire: its ObjectFifo dimensions = total DDR buffer size. ---
    QSet<Canvas::ObjectId> fillShimIds;
    QSet<Canvas::ObjectId> drainShimIds;
    QHash<Canvas::ObjectId, Canvas::CanvasWire*> fillShimDdrWires;
    QHash<Canvas::ObjectId, Canvas::CanvasWire*> drainShimDdrWires;

    for (const auto& item : items) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire)
            continue;

        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;

        auto* blockA = dynamic_cast<Canvas::CanvasBlock*>(
            m_document->findItem(epA.attached->itemId));
        auto* blockB = dynamic_cast<Canvas::CanvasBlock*>(
            m_document->findItem(epB.attached->itemId));
        if (!blockA || !blockB)
            continue;

        const bool aDdr = blockA->specId() == ddrSpecId;
        const bool bDdr = blockB->specId() == ddrSpecId;

        if (aDdr && !bDdr) {
            // DDR → SHIM: blockB is a fill SHIM
            const auto parsedB = parseTileSpecId(blockB->specId());
            if (parsedB && parsedB->kind == hlir::TileKind::SHIM) {
                fillShimIds.insert(blockB->id());
                fillShimDdrWires.insert(blockB->id(), wire);
            }
        } else if (!aDdr && bDdr) {
            // SHIM → DDR: blockA is a drain SHIM
            const auto parsedA = parseTileSpecId(blockA->specId());
            if (parsedA && parsedA->kind == hlir::TileKind::SHIM) {
                drainShimIds.insert(blockA->id());
                drainShimDdrWires.insert(blockA->id(), wire);
            }
        }
    }

    if (fillShimIds.isEmpty() && drainShimIds.isEmpty())
        return;

    // --- Pass 2: Match fill/drain SHIMs to their FIFO wires ---
    struct ShimFifoEntry {
        hlir::ComponentId   fifoId;
        hlir::ComponentId   shimTileId;
        Canvas::CanvasWire* wire    = nullptr;  // SHIM→compute FIFO wire (transfer size)
        Canvas::CanvasWire* ddrWire = nullptr;  // DDR→SHIM wire (total buffer size for main())
        std::string         paramName;
    };

    QList<ShimFifoEntry> fillEntries;
    QList<ShimFifoEntry> drainEntries;

    for (const auto& item : items) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire)
            continue;

        const hlir::ComponentId fifoId = m_fifoMap.value(wire->id());
        if (fifoId.empty())
            continue;

        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;

        auto* blockA = dynamic_cast<Canvas::CanvasBlock*>(
            m_document->findItem(epA.attached->itemId));
        auto* blockB = dynamic_cast<Canvas::CanvasBlock*>(
            m_document->findItem(epB.attached->itemId));
        if (!blockA || !blockB)
            continue;

        // Producer SHIM is a fill SHIM → Fill (DDR → SHIM → array)
        if (fillShimIds.contains(blockA->id())) {
            const hlir::ComponentId shimTileId = m_tileMap.value(blockA->id());
            if (!shimTileId.empty())
                fillEntries.append({fifoId, shimTileId, wire, fillShimDdrWires.value(blockA->id())});
        }

        // Consumer SHIM is a drain SHIM → Drain (array → SHIM → DDR)
        if (drainShimIds.contains(blockB->id())) {
            const hlir::ComponentId shimTileId = m_tileMap.value(blockB->id());
            if (!shimTileId.empty())
                drainEntries.append({fifoId, shimTileId, wire, drainShimDdrWires.value(blockB->id())});
        }
    }

    if (fillEntries.isEmpty() && drainEntries.isEmpty())
        return;

    // Build the runtime sequence with input/output types, params, and fill/drain ops
    auto runtimeResult = m_bridge->createRuntime("runtime");
    if (!runtimeResult) {
        qCWarning(hlirSyncLog) << "HlirSyncService: failed to create runtime";
        return;
    }

    // Register each tracked worker with the runtime (StartWorkers).
    for (const hlir::ComponentId& workerId : std::as_const(m_workerMap)) {
        if (!workerId.empty())
            m_bridge->runtimeAddWorker(workerId);
    }

    // rt.sequence type = DDR wire dims (2D host buffer shape matching the TensorTiler2D).
    // main() buffer size also comes from DDR dims.
    // Value type comes from the FIFO wire (the actual element dtype); fall back to DDR wire.
    for (int i = 0; i < fillEntries.size(); ++i) {
        hlir::ComponentId typeId;
        QString mainSize;
        QString valueType = QStringLiteral("int32");
        // Prefer FIFO wire value type (the dtype flowing through the transfer)
        if (auto* w = fillEntries[i].wire; w && w->hasObjectFifo()) {
            const QString vt = w->objectFifo().value().type.valueType;
            if (!vt.isEmpty())
                valueType = normalizeValueType(vt);
        }
        // Sequence type and main() size from DDR wire (2D host shape)
        if (auto* d = fillEntries[i].ddrWire; d && d->hasObjectFifo()) {
            const auto& dc = d->objectFifo().value();
            if (valueType == QStringLiteral("int32") && !dc.type.valueType.isEmpty())
                valueType = normalizeValueType(dc.type.valueType);
            mainSize = dc.type.dimensions.trimmed();
            if (!mainSize.isEmpty())
                typeId = ensureTensorType(mainSize, valueType);
        }
        // Fall back to FIFO transfer dims if DDR wire not yet configured
        if (typeId.empty()) {
            if (auto* w = fillEntries[i].wire; w && w->hasObjectFifo()) {
                const QString dims = w->objectFifo().value().type.dimensions.trimmed();
                if (!dims.isEmpty())
                    typeId = ensureTensorType(dims, valueType);
            }
        }
        if (!typeId.empty())
            m_bridge->runtimeAddInputType(typeId);
        m_bridge->runtimeAddMainSize(mainSize.toStdString());
    }

    // Output types: same pattern
    for (int i = 0; i < drainEntries.size(); ++i) {
        hlir::ComponentId typeId;
        QString mainSize;
        QString valueType = QStringLiteral("int32");
        if (auto* w = drainEntries[i].wire; w && w->hasObjectFifo()) {
            const QString vt = w->objectFifo().value().type.valueType;
            if (!vt.isEmpty())
                valueType = normalizeValueType(vt);
        }
        if (auto* d = drainEntries[i].ddrWire; d && d->hasObjectFifo()) {
            const auto& dc = d->objectFifo().value();
            if (valueType == QStringLiteral("int32") && !dc.type.valueType.isEmpty())
                valueType = normalizeValueType(dc.type.valueType);
            mainSize = dc.type.dimensions.trimmed();
            if (!mainSize.isEmpty())
                typeId = ensureTensorType(mainSize, valueType);
        }
        if (typeId.empty()) {
            if (auto* w = drainEntries[i].wire; w && w->hasObjectFifo()) {
                const QString dims = w->objectFifo().value().type.dimensions.trimmed();
                if (!dims.isEmpty())
                    typeId = ensureTensorType(dims, valueType);
            }
        }
        if (!typeId.empty())
            m_bridge->runtimeAddOutputType(typeId);
        m_bridge->runtimeAddMainSize(mainSize.toStdString());
    }

    // Param names: use FIFO wire's configured name; fall back to "input_N"/"output_N"
    for (int i = 0; i < fillEntries.size(); ++i) {
        std::string name = "input_" + std::to_string(i);
        if (auto* w = fillEntries[i].wire; w && w->hasObjectFifo()) {
            const QString n = w->objectFifo().value().name;
            if (!n.isEmpty()) name = sanitizePythonName(n).toStdString();
        }
        fillEntries[i].paramName = name;
        m_bridge->runtimeAddParam(name);
    }
    for (int i = 0; i < drainEntries.size(); ++i) {
        std::string name = "output_" + std::to_string(i);
        if (auto* w = drainEntries[i].wire; w && w->hasObjectFifo()) {
            const QString n = w->objectFifo().value().name;
            if (!n.isEmpty()) name = sanitizePythonName(n).toStdString();
        }
        drainEntries[i].paramName = name;
        m_bridge->runtimeAddParam(name);
    }

    for (int i = 0; i < fillEntries.size(); ++i) {
        const std::string& name = fillEntries[i].paramName;
        hlir::ComponentId tapId;
        if (auto* d = fillEntries[i].ddrWire; d && d->hasObjectFifo()) {
            const auto& dc = d->objectFifo().value();
            if (dc.type.mode == Canvas::CanvasWire::DimensionMode::Matrix && dc.type.tap.has_value()) {
                const QString tapName = QString::fromStdString(name) + QStringLiteral("_tap");
                tapId = ensureTensorTiler2D(tapName, *dc.type.tap, dc.type.dimensions);
            }
        }
        m_bridge->runtimeAddFill(name, fillEntries[i].fifoId, name,
                                  fillEntries[i].shimTileId, -1, !tapId.empty(), tapId);
    }
    for (int i = 0; i < drainEntries.size(); ++i) {
        const std::string& name = drainEntries[i].paramName;
        hlir::ComponentId tapId;
        if (auto* d = drainEntries[i].ddrWire; d && d->hasObjectFifo()) {
            const auto& dc = d->objectFifo().value();
            if (dc.type.mode == Canvas::CanvasWire::DimensionMode::Matrix && dc.type.tap.has_value()) {
                const QString tapName = QString::fromStdString(name) + QStringLiteral("_tap");
                tapId = ensureTensorTiler2D(tapName, *dc.type.tap, dc.type.dimensions);
            }
        }
        m_bridge->runtimeAddDrain(name, drainEntries[i].fifoId, name,
                                   drainEntries[i].shimTileId, -1, !tapId.empty(), tapId);
    }

    auto buildResult = m_bridge->runtimeBuild();
    if (!buildResult) {
        qCWarning(hlirSyncLog) << "HlirSyncService: failed to build runtime";
    }
}

void HlirSyncService::resetTrackedComponents()
{
    // Remove all tracked HLIR components in dependency order:
    // workers → coreFuncs → kernels → split/join → FIFOs → types → tiles
    if (!m_bridge)
        return;

    // Clear the runtime first so workers can be removed without dependency errors.
    m_bridge->clearRuntime();

    for (const hlir::ComponentId& id : std::as_const(m_workerMap))
        m_bridge->remove(id);
    m_workerMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_coreFuncMap))
        m_bridge->remove(id);
    m_coreFuncMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_kernelMap))
        m_bridge->remove(id);
    m_kernelMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_splitJoinMap))
        m_bridge->remove(id);
    m_splitJoinMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_fifoMap))
        m_bridge->remove(id);
    m_fifoMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_tilerMap))
        m_bridge->remove(id);
    m_tilerMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_typeMap))
        m_bridge->remove(id);
    m_typeMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_tileMap))
        m_bridge->remove(id);
    m_tileMap.clear();
}

} // namespace Aie::Internal
