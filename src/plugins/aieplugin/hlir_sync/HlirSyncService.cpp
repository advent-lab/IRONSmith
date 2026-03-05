// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/HlirSyncService.hpp"
#include "aieplugin/hlir_sync/DesignVerifier.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasWire.hpp"
#include "hlir_cpp_bridge/HlirBridge.hpp"
#include "code_gen_bridge/CodeGenBridge.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QLoggingCategory>
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <QtWidgets/QMessageBox>

#include <filesystem>

namespace Aie::Internal {

Q_LOGGING_CATEGORY(hlirSyncLog, "ironsmith.aie.hlir")

HlirSyncService::HlirSyncService(QObject* parent)
    : QObject(parent)
{
}

HlirSyncService::~HlirSyncService() = default;

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
    // Diff canvas items against tracked maps and update tiles and FIFOs in the bridge.
    if (!m_document || !m_bridge)
        return;

    const auto& items = m_document->items();

    QSet<Canvas::ObjectId> currentIds;
    currentIds.reserve(static_cast<int>(items.size()));
    for (const auto& item : items)
        currentIds.insert(item->id());

    // Remove stale components in dependency order: split/join → FIFOs → tiles
    for (auto it = m_splitJoinMap.begin(); it != m_splitJoinMap.end(); ) {
        if (!currentIds.contains(it.key())) {
            m_bridge->remove(it.value());
            it = m_splitJoinMap.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_fifoMap.begin(); it != m_fifoMap.end(); ) {
        if (!currentIds.contains(it.key())) {
            m_bridge->remove(it.value());
            it = m_fifoMap.erase(it);
        } else {
            ++it;
        }
    }

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

    // Default type for plain wires with no explicit ObjectFifoConfig
    const hlir::ComponentId defaultTypeId = ensureNamedTensorType(
        QStringLiteral("data_ty"), QStringLiteral("1024"), QStringLiteral("int32"));

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
        hlir::ComponentId typeId = defaultTypeId;
        QString baseName;

        if (wire->hasObjectFifo()) {
            const auto& cfg = wire->objectFifo().value();
            baseName = cfg.name;
            depth = cfg.depth;
            if (!cfg.type.dimensions.isEmpty()) {
                // Normalise "i32" → "int32" so the Python DataType enum accepts it
                const QString vt = (cfg.type.valueType == QStringLiteral("i32"))
                                       ? QStringLiteral("int32")
                                       : cfg.type.valueType;
                typeId = ensureTensorType(cfg.type.dimensions, vt);
            }
            // else: dimensions unspecified → keep defaultTypeId (shared data type)
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

    // Build lookup: tile ObjectId → FIFO info (as consumer or producer of a registered FIFO).
    struct FifoInfo {
        hlir::ComponentId fifoId;
        hlir::ComponentId typeId;
        QString           baseName;    // objectFifo name or "fifo" (used for sub-FIFO name gen)
        int               elementCount = 1024; // total elements in type (for offset calculation)
    };
    QHash<Canvas::ObjectId, FifoInfo> consumerBlockFifo; // tile is consumer of this FIFO
    QHash<Canvas::ObjectId, FifoInfo> producerBlockFifo; // tile is producer of this FIFO

    const hlir::ComponentId defaultTypeId = m_typeMap.value(QStringLiteral("data_ty"));

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

        hlir::ComponentId typeId = defaultTypeId;
        QString baseName = QStringLiteral("fifo");
        int elemCount = 1024;
        if (wire->hasObjectFifo()) {
            const auto& cfg = wire->objectFifo().value();
            baseName = cfg.name;
            if (!cfg.type.dimensions.isEmpty()) {
                const QString vt = (cfg.type.valueType == QStringLiteral("i32"))
                                       ? QStringLiteral("int32")
                                       : cfg.type.valueType;
                typeId = ensureTensorType(cfg.type.dimensions, vt);
            }
            // else: dimensions unspecified → keep defaultTypeId (shared data type)
            elemCount = elementCountFromDims(cfg.type.dimensions);
        }

        const hlir::ComponentId fifoId = m_fifoMap.value(wire->id());
        producerBlockFifo[blockA->id()] = {fifoId, typeId, baseName, elemCount};
        consumerBlockFifo[blockB->id()] = {fifoId, typeId, baseName, elemCount};
    }

    // Process each hub block (split or join). Sequential counters provide stable names.
    int splitIdx = 0;
    int joinIdx  = 0;

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

        if (pivotRole == Canvas::PortRole::Consumer) {
            // ----- SPLIT -----
            // Pivot wire enters hub via consumer port → hub distributes to multiple outputs.
            const auto srcIt = consumerBlockFifo.constFind(pivotBlock->id());
            if (srcIt == consumerBlockFifo.constEnd() || srcIt->fifoId.empty()) {
                qCWarning(hlirSyncLog) << "HlirSyncService: split has no source FIFO for tile"
                                       << pivotBlock->specId();
                continue;
            }

            // Collect output sub-FIFO names ordered by hub producer port index.
            ++splitIdx;
            const QString splitName = QStringLiteral("split") + QString::number(splitIdx);

            std::vector<std::string> outputNames;
            for (const auto& port : hubBlock->ports()) {
                if (port.role != Canvas::PortRole::Producer)
                    continue;
                const int idx     = static_cast<int>(outputNames.size());
                auto*     armWire = portToArmWire.value(port.id, nullptr);
                QString   name;
                if (armWire && armWire->hasObjectFifo())
                    name = armWire->objectFifo().value().name;
                else
                    name = splitName + QStringLiteral("_out") + QString::number(idx + 1);
                outputNames.push_back(name.toStdString());
            }

            if (outputNames.empty())
                continue;

            const int numOut = static_cast<int>(outputNames.size());
            const int stride = srcIt->elementCount / numOut;
            std::vector<int> offsets;
            offsets.reserve(numOut);
            for (int i = 0; i < numOut; ++i)
                offsets.push_back(stride * i);

            auto result = m_bridge->addFifoSplit(
                splitName.toStdString(),
                srcIt->fifoId,
                numOut,
                srcIt->typeId,
                outputNames,
                offsets,
                placementTileId,
                existingId);

            if (result) {
                m_splitJoinMap[hubBlock->id()] = result.value();
            } else {
                qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync split" << splitName;
            }

        } else if (pivotRole == Canvas::PortRole::Producer) {
            // ----- JOIN -----
            // Pivot wire exits hub via producer port → hub collects from multiple inputs.
            const auto dstIt = producerBlockFifo.constFind(pivotBlock->id());
            if (dstIt == producerBlockFifo.constEnd() || dstIt->fifoId.empty()) {
                qCWarning(hlirSyncLog) << "HlirSyncService: join has no dest FIFO for tile"
                                       << pivotBlock->specId();
                continue;
            }

            // Collect input sub-FIFO names ordered by hub consumer port index.
            ++joinIdx;
            const QString joinName = QStringLiteral("join") + QString::number(joinIdx);

            std::vector<std::string> inputNames;
            for (const auto& port : hubBlock->ports()) {
                if (port.role != Canvas::PortRole::Consumer)
                    continue;
                const int idx     = static_cast<int>(inputNames.size());
                auto*     armWire = portToArmWire.value(port.id, nullptr);
                QString   name;
                if (armWire && armWire->hasObjectFifo())
                    name = armWire->objectFifo().value().name;
                else
                    name = joinName + QStringLiteral("_in") + QString::number(idx + 1);
                inputNames.push_back(name.toStdString());
            }

            if (inputNames.empty())
                continue;

            const int numIn = static_cast<int>(inputNames.size());
            const int stride = dstIt->elementCount / numIn;
            std::vector<int> offsets;
            offsets.reserve(numIn);
            for (int i = 0; i < numIn; ++i)
                offsets.push_back(stride * i);

            auto result = m_bridge->addFifoJoin(
                joinName.toStdString(),
                dstIt->fifoId,
                numIn,
                dstIt->typeId,
                inputNames,
                offsets,
                placementTileId,
                existingId);

            if (result) {
                m_splitJoinMap[hubBlock->id()] = result.value();
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
               "  \u2022 SHIM tiles: %1\n"
               "  \u2022 MEM tiles:  %2\n"
               "  \u2022 AIE tiles:  %3\n"
               "  \u2022 FIFOs:      %4\n"
               "  \u2022 Splits:     %5\n"
               "  \u2022 Joins:      %6\n"
               "  \u2022 Fills:      %7\n"
               "  \u2022 Drains:     %8")
            .arg(stats.shimTiles).arg(stats.memTiles).arg(stats.aieTiles)
            .arg(stats.fifos).arg(stats.splits).arg(stats.joins)
            .arg(stats.fills).arg(stats.drains);
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
    // Normalise short aliases the Python DataType enum doesn't accept
    const QString vt = (valueType == QStringLiteral("i32")) ? QStringLiteral("int32") : valueType;

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

    // Build a unique, sanitised type name: "type_int32_1024" or "type_int32_1024_512"
    const QString sanitised = QString(dimensions).replace(u'x', u'_');
    const QString typeName = QStringLiteral("type_") + vt + u'_' + sanitised;

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

void HlirSyncService::buildRuntime()
{
    // Create a runtime sequence with Fill/Drain ops driven by DDR↔SHIM wires.
    if (!m_document || !m_bridge)
        return;

    const hlir::ComponentId defaultTypeId = m_typeMap.value(QStringLiteral("data_ty"));
    const QLatin1StringView ddrSpecId{"ddr"};

    const auto& items = m_document->items();

    // --- Pass 1: Find fill SHIMs (DDR → SHIM) and drain SHIMs (SHIM → DDR) ---
    QSet<Canvas::ObjectId> fillShimIds;
    QSet<Canvas::ObjectId> drainShimIds;

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
            if (parsedB && parsedB->kind == hlir::TileKind::SHIM)
                fillShimIds.insert(blockB->id());
        } else if (!aDdr && bDdr) {
            // SHIM → DDR: blockA is a drain SHIM
            const auto parsedA = parseTileSpecId(blockA->specId());
            if (parsedA && parsedA->kind == hlir::TileKind::SHIM)
                drainShimIds.insert(blockA->id());
        }
    }

    if (fillShimIds.isEmpty() && drainShimIds.isEmpty())
        return;

    // --- Pass 2: Match fill/drain SHIMs to their FIFO wires ---
    struct ShimFifoEntry {
        hlir::ComponentId fifoId;
        hlir::ComponentId shimTileId;
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
                fillEntries.append({fifoId, shimTileId});
        }

        // Consumer SHIM is a drain SHIM → Drain (array → SHIM → DDR)
        if (drainShimIds.contains(blockB->id())) {
            const hlir::ComponentId shimTileId = m_tileMap.value(blockB->id());
            if (!shimTileId.empty())
                drainEntries.append({fifoId, shimTileId});
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

    for (int i = 0; i < fillEntries.size(); ++i) {
        if (!defaultTypeId.empty())
            m_bridge->runtimeAddInputType(defaultTypeId);
    }
    for (int i = 0; i < drainEntries.size(); ++i) {
        if (!defaultTypeId.empty())
            m_bridge->runtimeAddOutputType(defaultTypeId);
    }

    // Fills first so the serialiser assigns _in/_out suffixes correctly
    for (int i = 0; i < fillEntries.size(); ++i)
        m_bridge->runtimeAddParam("input_" + std::to_string(i));
    for (int i = 0; i < drainEntries.size(); ++i)
        m_bridge->runtimeAddParam("output_" + std::to_string(i));

    for (int i = 0; i < fillEntries.size(); ++i) {
        const std::string name = "input_" + std::to_string(i);
        m_bridge->runtimeAddFill(name, fillEntries[i].fifoId, name, fillEntries[i].shimTileId);
    }
    for (int i = 0; i < drainEntries.size(); ++i) {
        const std::string name = "output_" + std::to_string(i);
        m_bridge->runtimeAddDrain(name, drainEntries[i].fifoId, name, drainEntries[i].shimTileId);
    }

    auto buildResult = m_bridge->runtimeBuild();
    if (!buildResult) {
        qCWarning(hlirSyncLog) << "HlirSyncService: failed to build runtime";
    }
}

void HlirSyncService::resetTrackedComponents()
{
    // Remove all tracked HLIR components in dependency order: split/join → FIFOs → types → tiles
    if (!m_bridge)
        return;
    for (const hlir::ComponentId& id : std::as_const(m_splitJoinMap))
        m_bridge->remove(id);
    m_splitJoinMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_fifoMap))
        m_bridge->remove(id);
    m_fifoMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_typeMap))
        m_bridge->remove(id);
    m_typeMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_tileMap))
        m_bridge->remove(id);
    m_tileMap.clear();
}

} // namespace Aie::Internal
