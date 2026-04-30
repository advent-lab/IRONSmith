// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/HlirSyncService.hpp"
#include "aieplugin/hlir_sync/DesignVerifier.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"
#include "aieplugin/symbol_table/SymbolsController.hpp"
#include "aieplugin/symbol_table/SymbolTableTypes.hpp"

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
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QTextStream>
#include <QtCore/QThread>

#include <filesystem>
#include <unordered_set>

namespace Aie::Internal {

Q_LOGGING_CATEGORY(hlirSyncLog, "ironsmith.aie.hlir")

bool HlirSyncService::s_animateSteps = true;

/// Derives the design name from the output directory path.
/// outputDir is "<bundlePath>/codegen", bundlePath ends with "<Name>.ironsmith",
/// so the design name is the bundle directory name without its extension.
static QString designNameFromOutputDir(const QString& outputDir)
{
    // e.g. "/path/MatrixVectorMul.ironsmith/codegen" -> "MatrixVectorMul"
    const QString bundleDirName = QFileInfo(outputDir).dir().dirName();
    return QFileInfo(bundleDirName).completeBaseName();
}

QString HlirSyncService::generatedScriptPath() const
{
    if (m_outputDir.isEmpty())
        return {};
    const QString designName = designNameFromOutputDir(m_outputDir);
    return m_outputDir + QStringLiteral("/generated_") + designName + QStringLiteral(".py");
}

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

void HlirSyncService::setSymbolsController(SymbolsController* controller)
{
    m_symbolsController = controller;
}

void HlirSyncService::setCanvasDocumentService(Canvas::Api::ICanvasDocumentService* service)
{
    m_canvasDocuments = service;
}

HlirSyncService::ResolvedType
HlirSyncService::resolveType(const Canvas::CanvasWire::ObjectFifoTypeAbstraction& typeAbs,
                              const QHash<QString, qint64>& /*constants*/) const
{
    if (typeAbs.symbolRef.has_value() && !typeAbs.symbolRef->isEmpty() && m_symbolsController) {
        const auto& syms = m_symbolsController->symbols();
        for (const auto& sym : syms) {
            if (sym.name != *typeAbs.symbolRef || sym.kind != SymbolKind::TypeAbstraction)
                continue;
            QStringList parts;
            parts.reserve(sym.type.shapeTokens.size());
            for (const QString& token : sym.type.shapeTokens) {
                const QString t = token.trimmed();
                qint64 val = 0;
                if (parseIntegralToken(t, val))
                    parts.append(QString::number(val));
                else
                    parts.append(t); // constant name or unknown — keep symbolic for generated code
            }
            return {parts.join(u'x'), sym.type.dtype, *typeAbs.symbolRef};
        }
    }
    return {typeAbs.dimensions, typeAbs.valueType, {}};
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

    // Build constant name → value map for resolving symbol shape tokens.
    QHash<QString, qint64> constantsMap;
    if (m_symbolsController) {
        for (const auto& sym : m_symbolsController->symbols()) {
            if (sym.kind == SymbolKind::Constant && !sym.name.isEmpty())
                constantsMap.insert(sym.name, sym.constant.value);
        }
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
    {
        // Multiple tiles may share the same default core function ComponentId.
        // Deduplicate before removing to avoid double-free in the bridge.
        std::unordered_set<std::string> seenCoreFns;
        for (const hlir::ComponentId& id : std::as_const(m_coreFuncMap)) {
            if (seenCoreFns.insert(id.value).second)
                m_bridge->remove(id);
        }
    }
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
    m_typeNameByKey.clear();

    // Re-add Constant symbols to the bridge (must precede tensor types so shape tokens resolve).
    for (const hlir::ComponentId& id : std::as_const(m_constantMap))
        m_bridge->remove(id);
    m_constantMap.clear();
    if (m_symbolsController) {
        for (const auto& sym : m_symbolsController->symbols()) {
            if (sym.kind != SymbolKind::Constant || sym.name.isEmpty())
                continue;
            const auto res = m_bridge->addConstant(sym.name.toStdString(),
                                                   QString::number(sym.constant.value).toStdString(),
                                                   "int");
            if (res)
                m_constantMap[sym.name] = res.value();
        }
    }

    // Re-add TypeAbstraction symbols from the symbol table so user-defined named types
    // appear in generated code even when no FIFO wire references them by symbolRef.
    if (m_symbolsController) {
        for (const auto& sym : m_symbolsController->symbols()) {
            if (sym.kind != SymbolKind::TypeAbstraction || sym.name.isEmpty())
                continue;
            QStringList parts;
            parts.reserve(sym.type.shapeTokens.size());
            for (const auto& t : sym.type.shapeTokens) {
                qint64 val = 0;
                if (parseIntegralToken(t, val))
                    parts.append(QString::number(val));
                else
                    parts.append(t); // symbolic token — keep as-is
            }
            const QString dimensions = parts.join(u'x');
            ensureNamedTensorType(sym.name, dimensions, normalizeValueType(sym.type.dtype));
        }
    }

    // Re-add TAP symbols from the symbol table so user-defined TAPs are available
    // for use in Fill/Drain operations.
    if (m_symbolsController) {
        for (const auto& sym : m_symbolsController->symbols()) {
            if (sym.kind != SymbolKind::TensorAccessPattern || sym.name.isEmpty())
                continue;
            ensureTap(sym.name,
                     sym.tap.rows,
                     sym.tap.cols,
                     sym.tap.offset,
                     sym.tap.sizes,
                     sym.tap.strides,
                     sym.tap.useTiler2D,
                     sym.tap.tensorDims,
                     sym.tap.tileDims,
                     sym.tap.tileCounts,
                     sym.tap.patternRepeat);
        }
    }

    // Re-add LayoutDims symbols as list constants so they are emitted verbatim in generated code.
    if (m_symbolsController) {
        for (const auto& sym : m_symbolsController->symbols()) {
            if (sym.kind != SymbolKind::LayoutDims || sym.name.isEmpty())
                continue;
            const QString pyExpr = layoutDimsPythonExpr(sym.layoutDims);
            const auto res = m_bridge->addConstant(sym.name.toStdString(), pyExpr.toStdString(), "list");
            if (res)
                m_constantMap[sym.name] = res.value();
        }
    }

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

        // Forward-operation wires are processed in syncSplitsAndJoins, not as standalone FIFOs.
        if (wire->hasObjectFifo() &&
            wire->objectFifo().value().operation == Canvas::CanvasWire::ObjectFifoOperation::Forward)
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
            const auto resolved = resolveType(cfg.type, constantsMap);
            if (!resolved.dimensions.isEmpty()) {
                if (!resolved.symbolName.isEmpty())
                    typeId = ensureNamedTensorType(resolved.symbolName, resolved.dimensions,
                                                   normalizeValueType(resolved.valueType));
                else
                    typeId = ensureTensorType(resolved.dimensions, normalizeValueType(resolved.valueType));
            }
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

    QHash<QString, qint64> constantsMap;
    if (m_symbolsController) {
        for (const auto& sym : m_symbolsController->symbols()) {
            if (sym.kind == SymbolKind::Constant && !sym.name.isEmpty())
                constantsMap.insert(sym.name, sym.constant.value);
        }
    }

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
    // Evaluate a single token (integer literal, constant name, or "A * B" expression).
    auto evalToken = [&constantsMap](const QString& raw) -> qint64 {
        const QString t = raw.trimmed();
        bool ok = false;
        qint64 v = t.toLongLong(&ok);
        if (ok)
            return v;
        if (constantsMap.contains(t))
            return constantsMap.value(t);
        // Simple product expression: "A * B * ..."
        if (t.contains(u'*')) {
            qint64 product = 1;
            for (const QString& part : t.split(u'*')) {
                const QString p = part.trimmed();
                bool ok2 = false;
                qint64 pv = p.toLongLong(&ok2);
                if (!ok2) pv = constantsMap.value(p, 0);
                product *= pv;
            }
            return product;
        }
        return 0;
    };
    auto elementCountFromDims = [&evalToken](const QString& dims) -> int {
        if (dims.isEmpty())
            return 1024;
        qint64 count = 1;
        for (const QString& d : dims.split(u'x', Qt::SkipEmptyParts))
            count *= evalToken(d);
        return count > 0 ? static_cast<int>(count) : 1024;
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
            const auto resolved = resolveType(cfg.type, constantsMap);
            fifoValueType = resolved.valueType.isEmpty() ? QStringLiteral("i32") : resolved.valueType;
            fifoDimensions = resolved.dimensions;
            if (!resolved.dimensions.isEmpty()) {
                if (!resolved.symbolName.isEmpty())
                    typeId = ensureNamedTensorType(resolved.symbolName, resolved.dimensions,
                                                   normalizeValueType(resolved.valueType));
                else
                    typeId = ensureTensorType(resolved.dimensions, normalizeValueType(resolved.valueType));
            }
            elemCount = elementCountFromDims(resolved.dimensions);
        }

        const hlir::ComponentId fifoId = m_fifoMap.value(wire->id());
        const FifoInfo info{fifoId, typeId, baseName, elemCount, fifoDepth, fifoValueType, fifoDimensions};
        producerBlockFifos[blockA->id()].append(info);
        consumerBlockFifos[blockB->id()].append(info);
    }

    // Helper: pick the FifoInfo for a given tile.
    // If preferredName is set, it MUST match a connected FIFO — returns nullptr if not found,
    // so the user cannot label a hub with a FIFO that isn't connected to that tile.
    // If no name is set, falls back to the first available FIFO.
    const auto pickFifo = [](const QList<FifoInfo>& list,
                              const QString& preferredName) -> const FifoInfo* {
        if (list.isEmpty())
            return nullptr;
        if (!preferredName.isEmpty()) {
            for (const auto& info : list) {
                if (info.baseName == preferredName)
                    return &info;
            }
            return nullptr; // named FIFO not connected to this tile — reject
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

            // Assign hub name and write operation to the pivot wire immediately so the annotation
            // shows "BCAST: bcast1, ..." even before a source FIFO is connected.
            ++bcastIdx;
            const QString existingBcastName = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().hubName.trimmed()
                : QString();
            const QString bcastName = existingBcastName.isEmpty()
                ? QStringLiteral("bcast") + QString::number(bcastIdx)
                : existingBcastName;
            {
                Canvas::CanvasWire::ObjectFifoConfig cfg =
                    pivotWire->hasObjectFifo() ? pivotWire->objectFifo().value()
                                               : Canvas::CanvasWire::ObjectFifoConfig{};
                if (existingBcastName.isEmpty())
                    cfg.hubName = bcastName;
                cfg.operation = Canvas::CanvasWire::ObjectFifoOperation::Forward;
                pivotWire->setObjectFifo(cfg);
            }

            const FifoInfo* srcFifo = pickFifo(consumerBlockFifos.value(pivotBlock->id()), bcastPreferred);
            if (!srcFifo || srcFifo->fifoId.empty()) {
                if (!bcastPreferred.isEmpty()) {
                    qCWarning(hlirSyncLog) << "HlirSyncService: broadcast source FIFO '" << bcastPreferred
                                           << "' is not connected to tile" << pivotBlock->specId()
                                           << "— falling back to first available FIFO";
                    srcFifo = pickFifo(consumerBlockFifos.value(pivotBlock->id()), QString());
                    if (srcFifo && !srcFifo->fifoId.empty()) {
                        Canvas::CanvasWire::ObjectFifoConfig cfg = pivotWire->objectFifo().value();
                        cfg.name = srcFifo->baseName;
                        pivotWire->setObjectFifo(cfg);
                    }
                }
                if (!srcFifo || srcFifo->fifoId.empty()) {
                    qCWarning(hlirSyncLog) << "HlirSyncService: broadcast has no source FIFO for tile"
                                           << pivotBlock->specId();
                    continue;
                }
            }

            // Update pivot wire annotation with resolved FIFO name and type.
            {
                Canvas::CanvasWire::ObjectFifoConfig cfg = pivotWire->objectFifo().value();
                if (bcastPreferred.isEmpty() || bcastPreferred != srcFifo->baseName)
                    cfg.name = srcFifo->baseName;
                cfg.depth = srcFifo->depth;
                cfg.type.valueType = srcFifo->valueType;
                cfg.type.dimensions = srcFifo->dimensions;
                pivotWire->setObjectFifo(cfg);
            }

            const std::map<std::string, std::string> metadata = {
                {"placement", pivotBlock->specId().toStdString()}
            };

            const auto& pivotFifo = pivotWire->objectFifo().value();
            auto result = m_bridge->addFifoForward(
                bcastName.toStdString(),
                srcFifo->fifoId,
                pivotFifo.dimsFromStream.toStdString(),
                pivotFifo.dimsToStream.toStdString(),
                existingId,
                metadata);

            if (result) {
                m_splitJoinMap[hubBlock->id()] = result.value();
                m_branchTypeMap[hubBlock->id()] = srcFifo->typeId;
            } else {
                qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync broadcast" << bcastName;
            }

            // Mark broadcast arm wires with Forward + empty hubName so they show no annotation.
            // Also propagate the pivot FIFO's resolved type so the type map is correct.
            for (const auto& port : hubBlock->ports()) {
                if (port.role != Canvas::PortRole::Producer)
                    continue;
                auto* armWire = portToArmWire.value(port.id, nullptr);
                if (!armWire)
                    continue;
                Canvas::CanvasWire::ObjectFifoConfig cfg =
                    armWire->hasObjectFifo() ? armWire->objectFifo().value()
                                             : Canvas::CanvasWire::ObjectFifoConfig{};
                cfg.operation          = Canvas::CanvasWire::ObjectFifoOperation::Forward;
                cfg.hubName            = QString();
                cfg.type.valueType     = srcFifo->valueType;
                cfg.type.dimensions    = srcFifo->dimensions;
                armWire->setObjectFifo(cfg);
            }

        } else if (pivotRole == Canvas::PortRole::Consumer) {
            // ----- SPLIT -----
            // Pivot wire enters hub via consumer port → hub distributes to multiple outputs.
            // User-set name on pivot wire selects which FIFO to split when multiple are present.
            const QString splitPreferred = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().name.trimmed() : QString();

            // Assign hub name and write operation to the pivot wire immediately so the annotation
            // shows "SPLIT: split1, ..." even before a source FIFO is connected.
            ++splitIdx;
            const QString existingSplitName = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().hubName.trimmed()
                : QString();
            const QString splitName = existingSplitName.isEmpty()
                ? QStringLiteral("split") + QString::number(splitIdx)
                : existingSplitName;
            {
                Canvas::CanvasWire::ObjectFifoConfig cfg =
                    pivotWire->hasObjectFifo() ? pivotWire->objectFifo().value()
                                               : Canvas::CanvasWire::ObjectFifoConfig{};
                if (existingSplitName.isEmpty())
                    cfg.hubName = splitName;
                cfg.operation = Canvas::CanvasWire::ObjectFifoOperation::Split;
                pivotWire->setObjectFifo(cfg);
            }

            const FifoInfo* srcFifo = pickFifo(consumerBlockFifos.value(pivotBlock->id()), splitPreferred);
            if (!srcFifo || srcFifo->fifoId.empty()) {
                if (!splitPreferred.isEmpty()) {
                    qCWarning(hlirSyncLog) << "HlirSyncService: split source FIFO '" << splitPreferred
                                           << "' is not connected to tile" << pivotBlock->specId()
                                           << "— falling back to first available FIFO";
                    srcFifo = pickFifo(consumerBlockFifos.value(pivotBlock->id()), QString());
                    if (srcFifo && !srcFifo->fifoId.empty()) {
                        Canvas::CanvasWire::ObjectFifoConfig cfg = pivotWire->objectFifo().value();
                        cfg.name = srcFifo->baseName;
                        pivotWire->setObjectFifo(cfg);
                    }
                }
                if (!srcFifo || srcFifo->fifoId.empty()) {
                    qCWarning(hlirSyncLog) << "HlirSyncService: split has no source FIFO for tile"
                                           << pivotBlock->specId();
                    continue;
                }
            }

            // Update pivot wire annotation with resolved FIFO name and type.
            // Preserve user-typed name if it matched; write resolved name otherwise.
            {
                Canvas::CanvasWire::ObjectFifoConfig cfg = pivotWire->objectFifo().value();
                if (splitPreferred.isEmpty() || splitPreferred != srcFifo->baseName)
                    cfg.name = srcFifo->baseName;
                // Mirror source FIFO depth/type for the properties panel display.
                cfg.depth = srcFifo->depth;
                cfg.type.valueType = srcFifo->valueType;
                cfg.type.dimensions = srcFifo->dimensions;
                pivotWire->setObjectFifo(cfg);
            }

            // Collect output sub-FIFO names ordered by hub producer port index.

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

            // Branch type: prefer user-specified symbol; fall back to auto-computed flat type.
            const QString splitPivotBranchSym = pivotWire->hasObjectFifo()
                ? pivotWire->objectFifo().value().branchTypeSymbol.trimmed() : QString();
            hlir::ComponentId branchTypeId;
            QString branchDims = QString::number(stride);
            if (!splitPivotBranchSym.isEmpty()) {
                if (m_symbolsController) {
                    for (const auto& sym : m_symbolsController->symbols()) {
                        if (sym.name == splitPivotBranchSym
                                && sym.kind == SymbolKind::TypeAbstraction) {
                            Canvas::CanvasWire::ObjectFifoTypeAbstraction typeAbs;
                            typeAbs.symbolRef  = splitPivotBranchSym;
                            typeAbs.dimensions = sym.type.shapeTokens.join(u'x');
                            typeAbs.valueType  = sym.type.dtype;
                            const auto resolved = resolveType(typeAbs, constantsMap);
                            branchDims = resolved.dimensions;
                            branchTypeId = ensureNamedTensorType(
                                splitPivotBranchSym, resolved.dimensions,
                                normalizeValueType(resolved.valueType));
                            break;
                        }
                    }
                }
            }
            if (branchTypeId.empty())
                branchTypeId = ensureTensorType(branchDims, srcFifo->valueType);

            // Offsets: prefer user-specified comma-separated list; fall back to auto-computed.
            const QString splitOffsetsOverride = pivotWire->hasObjectFifo()
                ? pivotWire->objectFifo().value().offsetsOverride.trimmed() : QString();
            std::vector<int> offsets;
            offsets.reserve(numOut);
            if (!splitOffsetsOverride.isEmpty()) {
                for (const QString& tok : splitOffsetsOverride.split(u',', Qt::SkipEmptyParts))
                    offsets.push_back(static_cast<int>(evalToken(tok)));
            } else {
                for (int i = 0; i < numOut; ++i)
                    offsets.push_back(stride * i);
            }

            // Propagate resolved arm type to every arm wire for verification.
            for (const auto& port : hubBlock->ports()) {
                if (port.role != Canvas::PortRole::Producer) continue;
                auto* armWire = portToArmWire.value(port.id, nullptr);
                if (!armWire || !armWire->hasObjectFifo()) continue;
                Canvas::CanvasWire::ObjectFifoConfig cfg = armWire->objectFifo().value();
                cfg.type.valueType  = srcFifo->valueType;
                cfg.type.dimensions = branchDims;
                armWire->setObjectFifo(cfg);
            }

            const QString splitDimsToStream = pivotWire->hasObjectFifo()
                ? pivotWire->objectFifo().value().dimsToStream.trimmed() : QString();

            auto result = m_bridge->addFifoSplit(
                splitName.toStdString(),
                srcFifo->fifoId,
                numOut,
                branchTypeId,
                outputNames,
                offsets,
                placementTileId,
                splitDimsToStream.toStdString(),
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

            // Assign hub name and write operation to the pivot wire immediately so the annotation
            // shows "JOIN: join1, ..." even before a dest FIFO is connected.
            ++joinIdx;
            const QString existingJoinName = (pivotWire->hasObjectFifo())
                ? pivotWire->objectFifo().value().hubName.trimmed()
                : QString();
            const QString joinName = existingJoinName.isEmpty()
                ? QStringLiteral("join") + QString::number(joinIdx)
                : existingJoinName;
            {
                Canvas::CanvasWire::ObjectFifoConfig cfg =
                    pivotWire->hasObjectFifo() ? pivotWire->objectFifo().value()
                                               : Canvas::CanvasWire::ObjectFifoConfig{};
                if (existingJoinName.isEmpty())
                    cfg.hubName = joinName;
                cfg.operation = Canvas::CanvasWire::ObjectFifoOperation::Join;
                pivotWire->setObjectFifo(cfg);
            }

            const FifoInfo* dstFifo = pickFifo(producerBlockFifos.value(pivotBlock->id()), joinPreferred);
            if (!dstFifo || dstFifo->fifoId.empty()) {
                if (!joinPreferred.isEmpty()) {
                    qCWarning(hlirSyncLog) << "HlirSyncService: join dest FIFO '" << joinPreferred
                                           << "' is not connected to tile" << pivotBlock->specId()
                                           << "— falling back to first available FIFO";
                    dstFifo = pickFifo(producerBlockFifos.value(pivotBlock->id()), QString());
                    if (dstFifo && !dstFifo->fifoId.empty()) {
                        Canvas::CanvasWire::ObjectFifoConfig cfg = pivotWire->objectFifo().value();
                        cfg.name = dstFifo->baseName;
                        pivotWire->setObjectFifo(cfg);
                    }
                }
                if (!dstFifo || dstFifo->fifoId.empty()) {
                    qCWarning(hlirSyncLog) << "HlirSyncService: join has no dest FIFO for tile"
                                           << pivotBlock->specId();
                    continue;
                }
            }

            // Update pivot wire annotation with resolved FIFO name and type.
            // Preserve user-typed name if it matched; write resolved name otherwise.
            {
                Canvas::CanvasWire::ObjectFifoConfig cfg = pivotWire->objectFifo().value();
                if (joinPreferred.isEmpty() || joinPreferred != dstFifo->baseName)
                    cfg.name = dstFifo->baseName;
                // Mirror destination FIFO depth/type for the properties panel display.
                cfg.depth = dstFifo->depth;
                cfg.type.valueType = dstFifo->valueType;
                cfg.type.dimensions = dstFifo->dimensions;
                pivotWire->setObjectFifo(cfg);
            }

            // Collect input sub-FIFO names ordered by hub consumer port index.

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

            // Branch type: prefer user-specified symbol; fall back to auto-computed flat type.
            const QString joinPivotBranchSym = pivotWire->hasObjectFifo()
                ? pivotWire->objectFifo().value().branchTypeSymbol.trimmed() : QString();
            hlir::ComponentId branchTypeId;
            QString branchDims = QString::number(stride);
            if (!joinPivotBranchSym.isEmpty()) {
                if (m_symbolsController) {
                    for (const auto& sym : m_symbolsController->symbols()) {
                        if (sym.name == joinPivotBranchSym
                                && sym.kind == SymbolKind::TypeAbstraction) {
                            Canvas::CanvasWire::ObjectFifoTypeAbstraction typeAbs;
                            typeAbs.symbolRef  = joinPivotBranchSym;
                            typeAbs.dimensions = sym.type.shapeTokens.join(u'x');
                            typeAbs.valueType  = sym.type.dtype;
                            const auto resolved = resolveType(typeAbs, constantsMap);
                            branchDims = resolved.dimensions;
                            branchTypeId = ensureNamedTensorType(
                                joinPivotBranchSym, resolved.dimensions,
                                normalizeValueType(resolved.valueType));
                            break;
                        }
                    }
                }
            }
            if (branchTypeId.empty())
                branchTypeId = ensureTensorType(branchDims, dstFifo->valueType);

            // Offsets: prefer user-specified comma-separated list; fall back to auto-computed.
            const QString joinOffsetsOverride = pivotWire->hasObjectFifo()
                ? pivotWire->objectFifo().value().offsetsOverride.trimmed() : QString();
            std::vector<int> offsets;
            offsets.reserve(numIn);
            if (!joinOffsetsOverride.isEmpty()) {
                for (const QString& tok : joinOffsetsOverride.split(u',', Qt::SkipEmptyParts))
                    offsets.push_back(static_cast<int>(evalToken(tok)));
            } else {
                for (int i = 0; i < numIn; ++i)
                    offsets.push_back(stride * i);
            }

            // Propagate resolved arm type to every arm wire for verification.
            for (const auto& port : hubBlock->ports()) {
                if (port.role != Canvas::PortRole::Consumer) continue;
                auto* armWire = portToArmWire.value(port.id, nullptr);
                if (!armWire || !armWire->hasObjectFifo()) continue;
                Canvas::CanvasWire::ObjectFifoConfig cfg = armWire->objectFifo().value();
                cfg.type.valueType  = dstFifo->valueType;
                cfg.type.dimensions = branchDims;
                armWire->setObjectFifo(cfg);
            }

            const QString joinDimsFromStream = pivotWire->hasObjectFifo()
                ? pivotWire->objectFifo().value().dimsFromStream.trimmed() : QString();

            auto result = m_bridge->addFifoJoin(
                joinName.toStdString(),
                dstFifo->fifoId,
                numIn,
                branchTypeId,
                inputNames,
                offsets,
                placementTileId,
                joinDimsFromStream.toStdString(),
                existingId);

            if (result) {
                m_splitJoinMap[hubBlock->id()] = result.value();
                m_branchTypeMap[hubBlock->id()] = branchTypeId;
            } else {
                qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync join" << joinName;
            }
        }
    }

    // Handle direct forward wires: operation == Forward, both endpoints are tile blocks (no hub block).
    // These represent a single-output forward: fwdName = srcFifo.cons().forward(placement=Tile(...))
    for (const auto& item : items) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire || !wire->hasObjectFifo())
            continue;
        const auto& fifo = wire->objectFifo().value();
        if (fifo.operation != Canvas::CanvasWire::ObjectFifoOperation::Forward)
            continue;
        const QString fwdName = fifo.hubName.trimmed();
        if (fwdName.isEmpty())
            continue;

        const auto& epA = wire->a();
        const auto& epB = wire->b();
        if (!epA.attached.has_value() || !epB.attached.has_value())
            continue;

        auto* blockA = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(epA.attached->itemId));
        auto* blockB = dynamic_cast<Canvas::CanvasBlock*>(m_document->findItem(epB.attached->itemId));
        // Skip wires connected to a hub block — those are already handled by the hub block pass above.
        if (!blockA || !blockB || blockA->isLinkHub() || blockB->isLinkHub())
            continue;
        if (blockA->specId().isEmpty() || blockB->specId().isEmpty())
            continue;

        const QString srcPreferred = fifo.name.trimmed();
        const FifoInfo* srcFifo = pickFifo(consumerBlockFifos.value(blockA->id()), srcPreferred);
        if (!srcFifo || srcFifo->fifoId.empty()) {
            qCWarning(hlirSyncLog) << "HlirSyncService: direct forward '" << fwdName
                                   << "' has no source FIFO for tile" << blockA->specId();
            continue;
        }

        // Propagate resolved name/type back to the wire annotation.
        {
            Canvas::CanvasWire::ObjectFifoConfig cfg = fifo;
            cfg.name              = srcFifo->baseName;
            cfg.depth             = srcFifo->depth;
            cfg.type.valueType    = srcFifo->valueType;
            cfg.type.dimensions   = srcFifo->dimensions;
            wire->setObjectFifo(cfg);
        }

        const hlir::ComponentId existingId = m_splitJoinMap.value(wire->id());
        const std::map<std::string, std::string> metadata = {
            {"placement", blockA->specId().toStdString()}
        };

        auto result = m_bridge->addFifoForward(
            fwdName.toStdString(),
            srcFifo->fifoId,
            fifo.dimsFromStream.toStdString(),
            fifo.dimsToStream.toStdString(),
            existingId,
            metadata);

        if (result) {
            m_splitJoinMap[wire->id()] = result.value();
        } else {
            qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync direct forward" << fwdName;
        }
    }
}

QList<VerificationIssue> HlirSyncService::runVerification() const
{
    // Run all registered design checks against the current canvas document.
    if (!m_document)
        return {};
    const QVector<KernelAsset>* kernelAssets =
        m_kernelRegistry ? &m_kernelRegistry->kernels() : nullptr;
    const QJsonObject metadata =
        (m_canvasDocuments && m_canvasDocuments->hasOpenDocument())
            ? m_canvasDocuments->activeMetadata()
            : QJsonObject{};
    return DesignVerifier().verify({m_document, kernelAssets, metadata});
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

    const QVector<KernelAsset>* kernelAssets =
        m_kernelRegistry ? &m_kernelRegistry->kernels() : nullptr;
    const QJsonObject metadata =
        (m_canvasDocuments && m_canvasDocuments->hasOpenDocument())
            ? m_canvasDocuments->activeMetadata()
            : QJsonObject{};
    const auto results = DesignVerifier().verifyDetailed({m_document, kernelAssets, metadata});

    QList<VerificationIssue> allErrors;
    for (const auto& result : results) {
        const bool passed = !DesignVerifier::hasErrors(result.issues);
        emit stepLogged(passed, result.displayName);
        QCoreApplication::processEvents();
        if (s_animateSteps) QThread::msleep(250);
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
        if (s_animateSteps) QThread::msleep(250);
    };

    // Step 1: Verify the design before generating code
    const QJsonObject genMetadata =
        (m_canvasDocuments && m_canvasDocuments->hasOpenDocument())
            ? m_canvasDocuments->activeMetadata()
            : QJsonObject{};
    const QVector<KernelAsset>* genKernelAssets =
        m_kernelRegistry ? &m_kernelRegistry->kernels() : nullptr;
    const auto verifyResults =
        DesignVerifier().verifyDetailed({m_document, genKernelAssets, genMetadata});
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

    // Step 4: Export to GUI XML — "_gui.xml" suffix triggers the XMLTransformer step.
    // Use the design name so the code generator produces generated_<Name>.py.
    QDir().mkpath(m_outputDir);
    const QString designName = designNameFromOutputDir(m_outputDir);
    const QString xmlPath = m_outputDir + QStringLiteral("/") + designName + QStringLiteral("_gui.xml");
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
        generateMakefile();
        emit codeGenFinished(true, tr("Code generated in %1").arg(m_outputDir));
    } else {
        QStringList errors;
        for (const auto& diag : genResult.error()) {
            errors << QString::fromStdString(diag.message);
            const QString details = QString::fromStdString(diag.details).trimmed();
            if (!details.isEmpty())
                errors << details;
        }
        emit codeGenFinished(false, errors.join(u'\n'));
    }
}

void HlirSyncService::generateMakefile() const
{
    if (m_kernelMap.isEmpty() || !m_kernelRegistry || !m_document)
        return;

    // ── Build asset lookup ─────────────────────────────────────────────────────
    QHash<QString, const KernelAsset*> assetById;
    for (const QString& kernelId : m_kernelMap.keys()) {
        const KernelAsset* ka = findKernelById(m_kernelRegistry->kernels(), kernelId);
        if (ka)
            assetById.insert(kernelId, ka);
    }

    // ── Collect unique per-tile kernel combinations ────────────────────────────
    // Each unique ordered kernel list on a tile becomes its own archive.
    // comboKey (IDs joined by "|") → ordered unique source files for that combo.
    QMap<QString, QStringList> comboSources; // QMap keeps insertion order for determinism
    QHash<QString, QString>    comboArchive; // comboKey → "<joined_ids>.a"

    const auto& items = m_document->items();
    for (const auto& item : items) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block || block->specId().isEmpty())
            continue;

        const auto parsed = parseTileSpecId(block->specId());
        if (!parsed || parsed->kind != hlir::TileKind::COMPUTE)
            continue;

        const QStringList& assigned = block->assignedKernels();
        if (assigned.isEmpty())
            continue;

        bool allKnown = true;
        for (const QString& kid : assigned)
            if (!assetById.contains(kid)) { allKnown = false; break; }
        if (!allKnown)
            continue;

        const QString comboKey = assigned.join(u'|');
        if (comboSources.contains(comboKey))
            continue;

        comboArchive.insert(comboKey, assigned.join(u'_') + QStringLiteral(".a"));

        QStringList sources;
        for (const QString& kid : assigned) {
            for (const QString& fname : assetById.value(kid)->files)
                if (!sources.contains(fname))
                    sources.append(fname);
        }
        comboSources.insert(comboKey, sources);
    }

    if (comboSources.isEmpty())
        return;

    // ── Copy kernel sources to codegen/kernels/ ────────────────────────────────
    const QString kernelsDir = m_outputDir + QStringLiteral("/kernels");
    QDir().mkpath(kernelsDir);

    QSet<QString> copied;
    for (const QString& kernelId : m_kernelMap.keys()) {
        const KernelAsset* ka = assetById.value(kernelId);
        if (!ka) continue;
        for (const QString& fname : ka->files) {
            if (copied.contains(fname)) continue;
            copied.insert(fname);
            const QString src = ka->directoryPath + QStringLiteral("/") + fname;
            const QString dst = kernelsDir + QStringLiteral("/") + fname;
            QFile::remove(dst);
            QFile::copy(src, dst);
        }
    }

    // ── Generate Makefile ──────────────────────────────────────────────────────
    const QString designName = designNameFromOutputDir(m_outputDir);
    QFile makefile(m_outputDir + QStringLiteral("/Makefile"));
    if (!makefile.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&makefile);

    out << QStringLiteral(
        "#\n"
        "# This file is licensed under the Apache License v2.0 with LLVM Exceptions.\n"
        "# See https://llvm.org/LICENSE.txt for license information.\n"
        "# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n"
        "#\n"
        "# (c) Copyright 2025 Advanced Micro Devices, Inc. or its affiliates\n"
        "\n"
        "# ── Toolchain discovery ────────────────────────────────────────────────────────\n"
        "MLIR_AIE_DIR      ?= $(shell python3 -c \"from aie.utils.config import root_path; print(root_path())\")\n"
        "PEANO_INSTALL_DIR ?= $(shell python3 -c \\\n"
        "\t\"import site, os; \\\n"
        "\t hits = [os.path.join(d,'llvm-aie') for d in site.getsitepackages() \\\n"
        "\t         if os.path.isdir(os.path.join(d,'llvm-aie'))]; \\\n"
        "\t print(hits[0] if hits else '')\")\n"
        "\n"
        "KERNEL_CC         := $(PEANO_INSTALL_DIR)/bin/clang++\n"
        "\n"
        "# ── Compiler flags ─────────────────────────────────────────────────────────────\n"
        "KERNEL_CFLAGS     := -O2 -std=c++20 --target=aie2-none-unknown-elf \\\n"
        "                     -Wno-parentheses -Wno-attributes -Wno-macro-redefined \\\n"
        "                     -Wno-empty-body -DNDEBUG \\\n"
        "                     -I $(MLIR_AIE_DIR)/include\n"
        "\n"
        "# Adjust defines to match your design tile dimensions.\n"
        "KERNEL_DEFINES    :=\n"
        "\n"
        "# ── Build artifacts ────────────────────────────────────────────────────────────\n"
        "BUILD_DIR         := build\n");

    const auto toVarStem = [](const QString& name) -> QString {
        return QFileInfo(name).completeBaseName().toUpper()
                   .replace(QLatin1Char('-'), QLatin1Char('_'))
                   .replace(QLatin1Char('.'), QLatin1Char('_'));
    };

    // Object variable per unique source file
    QHash<QString, QString> fileToObjVar;
    for (auto it = comboSources.cbegin(); it != comboSources.cend(); ++it) {
        for (const QString& fname : it.value()) {
            if (fileToObjVar.contains(fname)) continue;
            const QString varName = toVarStem(fname) + QStringLiteral("_OBJ");
            fileToObjVar.insert(fname, varName);
            out << varName << QStringLiteral(" := $(BUILD_DIR)/")
                << QFileInfo(fname).completeBaseName() << QStringLiteral(".o\n");
        }
    }

    // Archive variable per unique combination
    QHash<QString, QString> comboToVar;
    for (auto it = comboSources.cbegin(); it != comboSources.cend(); ++it) {
        const QString& comboKey = it.key();
        const QString archive   = comboArchive.value(comboKey);
        const QString varName   = toVarStem(archive) + QStringLiteral("_ARCHIVE");
        comboToVar.insert(comboKey, varName);
        out << varName << QStringLiteral(" := $(BUILD_DIR)/") << archive << QStringLiteral("\n");
    }

    // all target
    QStringList allTargets;
    for (auto it = comboSources.cbegin(); it != comboSources.cend(); ++it)
        allTargets << QStringLiteral("$(") + comboToVar.value(it.key()) + QStringLiteral(")");
    out << QStringLiteral("\n# ── Top-level targets ──────────────────────────────────────────────────────────\n");
    out << QStringLiteral(".PHONY: all run_generated clean\n\n");
    out << QStringLiteral("all: ") << allTargets.join(QStringLiteral(" ")) << QStringLiteral("\n\n");

    // Archive rules — one per unique combination
    for (auto it = comboSources.cbegin(); it != comboSources.cend(); ++it) {
        const QString& comboKey = it.key();
        QStringList objRefs;
        for (const QString& fname : it.value())
            objRefs << QStringLiteral("$(") + fileToObjVar.value(fname) + QStringLiteral(")");
        out << QStringLiteral("$(") << comboToVar.value(comboKey) << QStringLiteral("): ")
            << objRefs.join(QStringLiteral(" ")) << QStringLiteral("\n")
            << QStringLiteral("\tar rcs $@ $^\n\n");
    }

    // Compile rules — one per unique source file
    for (auto it = fileToObjVar.cbegin(); it != fileToObjVar.cend(); ++it) {
        const QString& fname  = it.key();
        const QString objName = QFileInfo(fname).completeBaseName() + QStringLiteral(".o");
        out << QStringLiteral("$(") << it.value() << QStringLiteral("): kernels/") << fname
            << QStringLiteral(" | $(BUILD_DIR)\n")
            << QStringLiteral("\tcd $(BUILD_DIR) && \\\n")
            << QStringLiteral("\t$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/")
            << fname << QStringLiteral(" -o ") << objName << QStringLiteral("\n\n");
    }

    out << QStringLiteral("$(BUILD_DIR):\n\tmkdir -p $(BUILD_DIR)\n\n");

    out << QStringLiteral("# ── Run ────────────────────────────────────────────────────────────────────────\n");
    out << QStringLiteral("run_generated: all\n\tpython3 generated_")
        << designName << QStringLiteral(".py\n\n");

    out << QStringLiteral("# ── Clean ──────────────────────────────────────────────────────────────────────\n");
    out << QStringLiteral("clean:\n\trm -rf $(BUILD_DIR)\n");
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
        // Register reverse mapping so ensureTensorType can reuse this named type
        // instead of generating a duplicate anonymous one for the same dims+dtype.
        const QString reverseKey = dimensions + u'|' + normalizeValueType(valueType);
        m_typeNameByKey[reverseKey] = name;
        return result.value();
    }

    auto lookup = m_bridge->lookupByName(hlir::ComponentType::TENSOR_TYPE, name.toStdString());
    if (lookup) {
        m_typeMap[name] = lookup.value();
        const QString reverseKey = dimensions + u'|' + normalizeValueType(valueType);
        m_typeNameByKey[reverseKey] = name;
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

    // Reuse a named type that already covers the same dims+dtype to avoid duplicates.
    if (m_typeNameByKey.contains(key)) {
        const QString& namedKey = m_typeNameByKey[key];
        if (m_typeMap.contains(namedKey))
            return m_typeMap[namedKey];
    }

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

hlir::ComponentId HlirSyncService::ensureTap(
    const QString& name,
    int rows,
    int cols,
    int offset,
    const QVector<int>& sizes,
    const QVector<int>& strides,
    bool useTiler2D,
    const QString& tensorDims,
    const QString& tileDims,
    const QString& tileCounts,
    const QString& patternRepeat)
{
    if (m_tilerMap.contains(name))
        return m_tilerMap.value(name);

    if (useTiler2D) {
        // Use TensorTiler2D path with explicit tile dims/counts like the DDR block does.
        Canvas::CanvasWire::TensorTilerConfig tap;
        tap.tileDims      = tileDims;
        tap.tileCounts    = tileCounts;
        tap.patternRepeat = patternRepeat;
        tap.pruneStep     = false;
        tap.index         = 0;

        // Use the tensorDims string if provided, otherwise fall back to rows x cols.
        const QString totalDims = tensorDims.isEmpty()
            ? QStringLiteral("%1 x %2").arg(rows).arg(cols)
            : tensorDims;
        const hlir::ComponentId id = ensureTensorTiler2D(name, tap, totalDims);
        if (!id.value.empty())
            m_tilerMap[name] = id;
        else
            qCWarning(hlirSyncLog) << "HlirSyncService: could not register TensorTiler2D TAP" << name;
        return id;
    }

    // TensorAccessPattern path: pass sizes/strides directly.
    std::vector<std::string> tapTensorDims;
    tapTensorDims.push_back(QString::number(rows).toStdString());
    tapTensorDims.push_back(QString::number(cols).toStdString());

    std::vector<std::string> sizesStr;
    sizesStr.reserve(sizes.size());
    for (int s : sizes)
        sizesStr.push_back(std::to_string(s));

    std::vector<std::string> stridesStr;
    stridesStr.reserve(strides.size());
    for (int s : strides)
        stridesStr.push_back(std::to_string(s));

    auto result = m_bridge->addTap(
        name.toStdString(),
        tapTensorDims,
        QString::number(offset).toStdString(),
        sizesStr,
        stridesStr,
        false,  // pruneStep
        0,      // index
        false); // useTiler2D=false

    if (result) {
        m_tilerMap[name] = result.value();
        return result.value();
    }

    const auto& diags = result.error();
    if (!diags.empty()) {
        // If the TAP already exists, recover its existing ID rather than failing.
        if (diags[0].code == hlir::ErrorCode::DUPLICATE_NAME && !diags[0].entityId.empty()) {
            const hlir::ComponentId existingId{diags[0].entityId};
            m_tilerMap[name] = existingId;
            return existingId;
        }
        qCWarning(hlirSyncLog) << "HlirSyncService: could not register TAP" << name
                               << ":" << QString::fromStdString(diags[0].message);
    } else {
        qCWarning(hlirSyncLog) << "HlirSyncService: could not register TAP" << name;
    }
    return hlir::ComponentId{};
}

// Parse the body_stmts JSON and return the params vector and the body array string.
//
// Two formats are supported:
//
//   Array format (inferred params — order = first appearance in depth-first walk):
//     [ { "type": "Acquire", ... }, ... ]
//
//   Object format (explicit params — recommended when param order matters):
//     { "params": ["matvec", "a_in", "b_in", "c_out"], "body": [ ... ] }
//
// Returns false if the JSON is unparseable or contains no params.
static bool parseBodyStmtsJson(const QString& raw,
                               std::vector<std::string>& outParams,
                               QString& outBodyJson)
{
    using namespace Qt::StringLiterals;
    outParams.clear();
    outBodyJson.clear();

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError)
        return false;

    // --- Object format: explicit "params" + "body" ---
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        const QJsonArray paramsArr = obj.value(u"params"_s).toArray();
        const QJsonArray bodyArr   = obj.value(u"body"_s).toArray();
        if (paramsArr.isEmpty() || bodyArr.isEmpty())
            return false;
        for (const QJsonValue& v : paramsArr) {
            const std::string s = v.toString().trimmed().toStdString();
            if (!s.empty())
                outParams.push_back(s);
        }
        outBodyJson = QString::fromUtf8(
            QJsonDocument(bodyArr).toJson(QJsonDocument::Compact));
        return !outParams.empty();
    }

    // --- Array format: infer params from first-appearance depth-first walk ---
    if (!doc.isArray())
        return false;

    std::unordered_set<std::string> seen;
    std::function<void(const QJsonArray&)> walk = [&](const QJsonArray& stmts) {
        for (const QJsonValue& val : stmts) {
            if (!val.isObject()) continue;
            const QJsonObject stmt = val.toObject();
            const QString type = stmt.value(u"type"_s).toString();

            auto addIfNew = [&](const QString& name) {
                const std::string s = name.trimmed().toStdString();
                if (!s.empty() && seen.insert(s).second)
                    outParams.push_back(s);
            };

            if (type == u"Acquire"_s || type == u"Release"_s)
                addIfNew(stmt.value(u"fifo_param"_s).toString());
            else if (type == u"KernelCall"_s)
                addIfNew(stmt.value(u"kernel_param"_s).toString());
            else if (stmt.contains(u"body"_s))
                walk(stmt.value(u"body"_s).toArray());
        }
    };
    walk(doc.array());
    outBodyJson = raw;
    return !outParams.empty();
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

    // Compute the coreBodyArgs lookup name for a hub arm wire.
    // Format: "hubName[armIndex]" for split/join; "hubName" for broadcast.
    // The hubName comes from the pivot wire's objectFifo.hubName (set by buildSplitJoinHubs()).
    auto hubArmFifoName = [&](const Canvas::CanvasBlock* hub,
                               Canvas::PortId armPortId,
                               Canvas::PortRole armRole,
                               bool isBroadcast) -> QString {
        // Find pivot wire: hub at B, non-hub at A.
        for (const auto& wItem : items) {
            const auto* w = dynamic_cast<const Canvas::CanvasWire*>(wItem.get());
            if (!w || !w->hasObjectFifo()) continue;
            const auto& wEpB = w->b();
            if (!wEpB.attached.has_value() || wEpB.attached->itemId != hub->id()) continue;
            const auto& wEpA = w->a();
            if (!wEpA.attached.has_value()) continue;
            const auto* blkA = dynamic_cast<const Canvas::CanvasBlock*>(
                m_document->findItem(wEpA.attached->itemId));
            if (!blkA || blkA->isLinkHub()) continue;
            const auto& pivotCfg = w->objectFifo().value();
            const QString hubName = pivotCfg.hubName.trimmed().isEmpty()
                ? pivotCfg.name.trimmed() : pivotCfg.hubName.trimmed();
            if (hubName.isEmpty()) return QString();
            if (isBroadcast) return hubName;
            const int idx = armIndexFor(hub, armPortId, armRole);
            return QStringLiteral("%1[%2]").arg(hubName).arg(idx);
        }
        return QString();
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
        QString fifoName; // object-fifo name for coreBodyArgs lookup (empty if unavailable)
    };

    struct TileWorkerSpec {
        Canvas::ObjectId blockId;
        std::string      tileName;
        hlir::ComponentId tileId;
        QList<FifoEndpoint> inputs;
        QList<FifoEndpoint> outputs;
        QStringList allKernels; // all assigned kernel IDs; [0] == the primary (group key)
    };

    QHash<QString, QList<TileWorkerSpec>> specsByKernelId;

    for (const auto& item : items) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block || block->specId().isEmpty())
            continue;

        const auto parsed = parseTileSpecId(block->specId());
        if (!parsed || parsed->kind != hlir::TileKind::COMPUTE)
            continue;

        // Extract kernel ID: prefer new assignedKernels list (first entry); fall back to
        // legacy <<kernel: id>> stereotype annotation for backward compatibility with old
        // documents that were saved before the kernel refactor.
        QString kernelId;
        {
            const QStringList& assigned = block->assignedKernels();
            if (!assigned.isEmpty()) {
                kernelId = assigned.first();
            } else {
                QRegularExpressionMatch m = kKernelAnnotationPattern.match(block->stereotype());
                if (!m.hasMatch())
                    m = kKernelAnnotationPattern.match(block->label());
                if (!m.hasMatch())
                    continue;
                kernelId = m.captured(1).trimmed();
            }
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
                    if (!fifoId.empty()) {
                        const QString fifoName = wire->hasObjectFifo()
                            ? wire->objectFifo().value().name.trimmed() : QString();
                        inputs.append({hlir::HlirBridge::FunctionArg::fifoConsumer(fifoId, 0),
                                       typeIdForWire(wire), fifoName});
                    } else if (wire->hasObjectFifo() &&
                               wire->objectFifo().value().operation ==
                                   Canvas::CanvasWire::ObjectFifoOperation::Forward) {
                        // Direct forward wire: tile is consumer of the forward result.
                        const hlir::ComponentId fwdId = m_splitJoinMap.value(wire->id());
                        if (!fwdId.empty()) {
                            const QString fwdName = wire->objectFifo().value().hubName.trimmed();
                            inputs.append({hlir::HlirBridge::FunctionArg::forwardConsumer(fwdId),
                                           typeIdForWire(wire), fwdName});
                        }
                    }
                } else {
                    // Arm wire with hub at A — direction depends on hub port role.
                    const hlir::ComponentId hubId = m_splitJoinMap.value(blockA->id());
                    if (hubId.empty())
                        continue;

                    if (isBroadcastHub(blockA)) {
                        const QString name = hubArmFifoName(blockA, epA.attached->portId,
                                                            Canvas::PortRole::Producer, true);
                        inputs.append({hlir::HlirBridge::FunctionArg::forwardConsumer(hubId),
                                       typeIdForHubArm(wire, blockA), name});
                    } else {
                        const Canvas::PortRole role =
                            portRoleFor(blockA, epA.attached->portId);
                        if (role == Canvas::PortRole::Producer) {
                            const int idx = armIndexFor(
                                blockA, epA.attached->portId, Canvas::PortRole::Producer);
                            const QString name = hubArmFifoName(blockA, epA.attached->portId,
                                                                Canvas::PortRole::Producer, false);
                            inputs.append({hlir::HlirBridge::FunctionArg::splitConsumer(hubId, idx),
                                           typeIdForHubArm(wire, blockA), name});
                        } else if (role == Canvas::PortRole::Consumer) {
                            const int idx = armIndexFor(
                                blockA, epA.attached->portId, Canvas::PortRole::Consumer);
                            const QString name = hubArmFifoName(blockA, epA.attached->portId,
                                                                Canvas::PortRole::Consumer, false);
                            outputs.append({hlir::HlirBridge::FunctionArg::joinProducer(hubId, idx),
                                            typeIdForHubArm(wire, blockA), name});
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
                if (!fifoId.empty()) {
                    const QString fifoName = wire->hasObjectFifo()
                        ? wire->objectFifo().value().name.trimmed() : QString();
                    outputs.append({hlir::HlirBridge::FunctionArg::fifoProducer(fifoId),
                                    typeIdForWire(wire), fifoName});
                }
            }
        }

        specsByKernelId[kernelId].append({
            block->id(),
            block->specId().toStdString(),
            tileId,
            std::move(inputs),
            std::move(outputs),
            block->assignedKernels().isEmpty()
                ? QStringList{kernelId} : block->assignedKernels()
        });
    }

    // ---- Phase 2: one external kernel per unique kernel ID ----
    // ---- Phase 3: one core function per tile (shared when default, per-tile when custom) ----
    // ---- Phase 4: one worker per tile ----
    //
    // Default tiles sharing the same kernel share one core function named "core_{kernelId}".
    // Tiles with a custom body_stmts config each get their own "core_{tileName}" so that
    // different tiles using the same kernel can independently use default or custom bodies.

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

    // Build a lookup: blockId → CanvasBlock* for fast per-tile config access.
    QHash<Canvas::ObjectId, const Canvas::CanvasBlock*> blockById;
    for (const auto& docItem : items) {
        if (const auto* blk = dynamic_cast<const Canvas::CanvasBlock*>(docItem.get()))
            blockById.insert(blk->id(), blk);
    }

    for (auto it = specsByKernelId.begin(); it != specsByKernelId.end(); ++it) {
        const QString& kernelId  = it.key();
        auto&          tileSpecs = it.value();

        if (tileSpecs.isEmpty())
            continue;

        const KernelAsset* kernel = m_kernelRegistry->kernelById(kernelId);
        if (!kernel) {
            qCWarning(hlirSyncLog) << "HlirSyncService: kernel not found:" << kernelId;
            continue;
        }

        // All tiles sharing a kernel have the same I/O count — use the first to build types.
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

        auto kernelResult = m_bridge->addExternalKernel(
            "kernel_" + kernel->id.toStdString(),
            functionNameFromSig(kernel->signature, kernel->id).toStdString(),
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

        // Shared parameter list used by every core function for this kernel.
        std::vector<std::string> params = {"kernel"};
        for (int i = 0; i < numInputs;  ++i) params.push_back("in"  + std::to_string(i));
        for (int i = 0; i < numOutputs; ++i) params.push_back("out" + std::to_string(i));

        // ---- Core function + worker per tile ----
        // Default tiles share one "core_{kernelId}" (created lazily on first default tile).
        // Custom body_stmts tiles each get "core_{tileName}".
        // SharedRef tiles referencing the same library name share one registration keyed by name.
        hlir::ComponentId sharedDefaultCoreFnId; // empty until first default tile registers it
        QHash<QString, hlir::ComponentId> sharedRefCoreFnIds; // libraryName → ComponentId

        // Load the shared core function library from document metadata (if available).
        QHash<QString, QString> sharedFnLibrary; // name → bodyStmtsJson
        if (m_canvasDocuments && m_canvasDocuments->hasOpenDocument()) {
            const QJsonArray lib =
                m_canvasDocuments->activeMetadata()
                    .value(QStringLiteral("coreFunctionLibrary")).toArray();
            for (const auto& v : lib) {
                const QJsonObject entry = v.toObject();
                const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
                const QString body = entry.value(QStringLiteral("bodyStmtsJson")).toString();
                if (!name.isEmpty() && !body.isEmpty())
                    sharedFnLibrary.insert(name, body);
            }
        }

        for (const auto& spec : tileSpecs) {
            const Canvas::CanvasBlock* blk = blockById.value(spec.blockId, nullptr);
            using Mode = Canvas::CanvasBlock::CoreFunctionConfig::Mode;
            const bool isCustom = blk
                && blk->hasCoreFunctionConfig()
                && blk->coreFunctionConfig()->mode == Mode::BodyStmts
                && !blk->coreFunctionConfig()->bodyStmtsJson.isEmpty();
            const bool isSharedRef = blk
                && blk->hasCoreFunctionConfig()
                && blk->coreFunctionConfig()->mode == Mode::SharedRef
                && !blk->coreFunctionConfig()->sharedFunctionName.isEmpty();

            // --- Register external kernels for any additional assigned kernels ---
            // The primary kernel (spec.allKernels[0]) was registered above; ensure
            // every extra kernel also has an external declaration before we build the
            // core function and worker args that reference it.
            for (int ki = 1; ki < spec.allKernels.size(); ++ki) {
                const QString& extraKernelId = spec.allKernels[ki];
                if (m_kernelMap.contains(extraKernelId))
                    continue; // already registered (possibly by another tile)
                const KernelAsset* extraKernel = m_kernelRegistry->kernelById(extraKernelId);
                if (!extraKernel) {
                    qCWarning(hlirSyncLog)
                        << "HlirSyncService: extra kernel not found:" << extraKernelId;
                    continue;
                }
                std::vector<hlir::ComponentId> extraArgTypes;
                for (const auto& ep : spec.inputs)  extraArgTypes.push_back(ep.typeId);
                for (const auto& ep : spec.outputs) extraArgTypes.push_back(ep.typeId);
                std::vector<std::string> extraIncludeDirs;
                for (const QString& dir : extraKernel->includeDirs)
                    extraIncludeDirs.push_back(dir.toStdString());
                auto extraResult = m_bridge->addExternalKernel(
                    "kernel_" + extraKernel->id.toStdString(),
                    functionNameFromSig(extraKernel->signature, extraKernel->id).toStdString(),
                    extraKernel->absoluteEntryPath().toStdString(),
                    extraArgTypes,
                    extraIncludeDirs,
                    {});
                if (extraResult)
                    m_kernelMap[extraKernelId] = extraResult.value();
                else
                    qCWarning(hlirSyncLog)
                        << "HlirSyncService: failed to add extra external kernel" << extraKernelId;
            }

            const bool isMultiKernel = (spec.allKernels.size() > 1);

            hlir::ComponentId coreFnId;

            if (isSharedRef) {
                // Shared library function — one registration per unique name.
                const QString fnName = blk->coreFunctionConfig()->sharedFunctionName;
                if (sharedRefCoreFnIds.contains(fnName)) {
                    coreFnId = sharedRefCoreFnIds[fnName];
                } else {
                    const QString bodyStmtsJson = sharedFnLibrary.value(fnName);
                    if (bodyStmtsJson.isEmpty()) {
                        qCWarning(hlirSyncLog)
                            << "HlirSyncService: shared function" << fnName
                            << "not found in library — skipping tile"
                            << QString::fromStdString(spec.tileName);
                        continue;
                    }
                    std::vector<std::string> sharedParams;
                    QString bodyOnlyJson;
                    if (!parseBodyStmtsJson(bodyStmtsJson, sharedParams, bodyOnlyJson)) {
                        qCWarning(hlirSyncLog)
                            << "HlirSyncService: shared function" << fnName
                            << "has no parameters or failed to parse — skipping tile"
                            << QString::fromStdString(spec.tileName);
                        continue;
                    }
                    const std::string regName = "core_shared_" + fnName.toStdString();
                    auto result = m_bridge->addCoreFunctionBody(
                        regName, sharedParams, bodyOnlyJson.toStdString(), {});
                    if (!result) {
                        qCWarning(hlirSyncLog)
                            << "HlirSyncService: failed to add shared core function" << fnName;
                        continue;
                    }
                    coreFnId = result.value();
                    sharedRefCoreFnIds[fnName] = coreFnId;
                }
                m_coreFuncMap[spec.blockId] = coreFnId;
            } else if (isCustom) {
                // Per-tile custom body_stmts core function.
                // If the tile owns a shared function (sharedFunctionName set), register once
                // under "core_shared_{name}" and reuse for all tiles referencing the same name.
                const QString ownedSharedName =
                    blk->coreFunctionConfig()->sharedFunctionName.trimmed();
                if (!ownedSharedName.isEmpty()) {
                    // Owner of a shared function — register as core_shared_{name} once.
                    if (sharedRefCoreFnIds.contains(ownedSharedName)) {
                        coreFnId = sharedRefCoreFnIds[ownedSharedName];
                    } else {
                        std::vector<std::string> sharedParams;
                        QString bodyOnlyJson;
                        if (!parseBodyStmtsJson(blk->coreFunctionConfig()->bodyStmtsJson,
                                                sharedParams, bodyOnlyJson)) {
                            qCWarning(hlirSyncLog)
                                << "HlirSyncService: body_stmts JSON for shared owner"
                                << QString::fromStdString(spec.tileName)
                                << "references no parameters or failed to parse — skipping tile";
                            continue;
                        }
                        const std::string regName = "core_shared_" + ownedSharedName.toStdString();
                        auto result = m_bridge->addCoreFunctionBody(
                            regName, sharedParams, bodyOnlyJson.toStdString(), {});
                        if (!result) {
                            qCWarning(hlirSyncLog)
                                << "HlirSyncService: failed to add shared core function"
                                << ownedSharedName;
                            continue;
                        }
                        coreFnId = result.value();
                        sharedRefCoreFnIds[ownedSharedName] = coreFnId;
                    }
                } else {
                    // Pure per-tile custom body — register under core_{tileName}.
                    std::vector<std::string> customParams;
                    QString bodyOnlyJson;
                    if (!parseBodyStmtsJson(blk->coreFunctionConfig()->bodyStmtsJson,
                                            customParams, bodyOnlyJson)) {
                        qCWarning(hlirSyncLog)
                            << "HlirSyncService: body_stmts JSON for" << QString::fromStdString(spec.tileName)
                            << "references no parameters or failed to parse — skipping tile";
                        continue;
                    }
                    auto result = m_bridge->addCoreFunctionBody(
                        "core_" + spec.tileName,
                        customParams,
                        bodyOnlyJson.toStdString(),
                        {});
                    if (!result) {
                        qCWarning(hlirSyncLog)
                            << "HlirSyncService: failed to add custom core function for"
                            << QString::fromStdString(spec.tileName);
                        continue;
                    }
                    coreFnId = result.value();
                }
                m_coreFuncMap[spec.blockId] = coreFnId;
            } else if (isMultiKernel) {
                // Multi-kernel default: generate a per-tile core function body that calls
                // each assigned kernel in order with all buffer variables as arguments.
                // Named "kernel", "kernel2", "kernel3", ..., then "in0"/"out0" for FIFOs.
                std::vector<std::string> mkParams;
                mkParams.push_back("kernel");
                for (int ki = 1; ki < spec.allKernels.size(); ++ki)
                    mkParams.push_back("kernel" + std::to_string(ki + 1));
                for (int i = 0; i < numInputs;  ++i) mkParams.push_back("in"  + std::to_string(i));
                for (int i = 0; i < numOutputs; ++i) mkParams.push_back("out" + std::to_string(i));

                // Build buffer var names used in acquire/release and kernel call args.
                std::vector<std::string> bufVars;
                for (int i = 0; i < numInputs;  ++i) bufVars.push_back("buf_in"  + std::to_string(i));
                for (int i = 0; i < numOutputs; ++i) bufVars.push_back("buf_out" + std::to_string(i));

                // Build body JSON array.
                QJsonArray bodyArr;
                for (int i = 0; i < numInputs; ++i) {
                    QJsonObject acq;
                    acq[QStringLiteral("type")]       = QStringLiteral("Acquire");
                    acq[QStringLiteral("fifo_param")] = QStringLiteral("in%1").arg(i);
                    acq[QStringLiteral("count")]      = 1;
                    acq[QStringLiteral("local_var")]  = QStringLiteral("buf_in%1").arg(i);
                    bodyArr.append(acq);
                }
                for (int i = 0; i < numOutputs; ++i) {
                    QJsonObject acq;
                    acq[QStringLiteral("type")]       = QStringLiteral("Acquire");
                    acq[QStringLiteral("fifo_param")] = QStringLiteral("out%1").arg(i);
                    acq[QStringLiteral("count")]      = 1;
                    acq[QStringLiteral("local_var")]  = QStringLiteral("buf_out%1").arg(i);
                    bodyArr.append(acq);
                }
                QJsonArray bufVarsJson;
                for (const auto& bv : bufVars) bufVarsJson.append(QString::fromStdString(bv));
                for (int ki = 0; ki < spec.allKernels.size(); ++ki) {
                    QJsonObject kc;
                    kc[QStringLiteral("type")]         = QStringLiteral("KernelCall");
                    kc[QStringLiteral("kernel_param")] = (ki == 0)
                        ? QStringLiteral("kernel")
                        : QStringLiteral("kernel%1").arg(ki + 1);
                    kc[QStringLiteral("args")]         = bufVarsJson;
                    bodyArr.append(kc);
                }
                for (int i = 0; i < numInputs; ++i) {
                    QJsonObject rel;
                    rel[QStringLiteral("type")]       = QStringLiteral("Release");
                    rel[QStringLiteral("fifo_param")] = QStringLiteral("in%1").arg(i);
                    rel[QStringLiteral("count")]      = 1;
                    bodyArr.append(rel);
                }
                for (int i = 0; i < numOutputs; ++i) {
                    QJsonObject rel;
                    rel[QStringLiteral("type")]       = QStringLiteral("Release");
                    rel[QStringLiteral("fifo_param")] = QStringLiteral("out%1").arg(i);
                    rel[QStringLiteral("count")]      = 1;
                    bodyArr.append(rel);
                }
                const std::string mkBodyJson =
                    QJsonDocument(bodyArr).toJson(QJsonDocument::Compact).toStdString();

                auto result = m_bridge->addCoreFunctionBody(
                    "core_" + spec.tileName, mkParams, mkBodyJson, {});
                if (!result) {
                    qCWarning(hlirSyncLog)
                        << "HlirSyncService: failed to add multi-kernel core function for"
                        << QString::fromStdString(spec.tileName);
                    continue;
                }
                coreFnId = result.value();
                m_coreFuncMap[spec.blockId] = coreFnId;
            } else {
                // Default single-kernel acquire/call/release — shared across all default tiles.
                if (sharedDefaultCoreFnId.empty()) {
                    std::vector<hlir::HlirBridge::AcquireSpec> acquires;
                    for (int i = 0; i < numInputs;  ++i)
                        acquires.push_back({"in"  + std::to_string(i), 1, "buf_in"  + std::to_string(i)});
                    for (int i = 0; i < numOutputs; ++i)
                        acquires.push_back({"out" + std::to_string(i), 1, "buf_out" + std::to_string(i)});

                    std::vector<std::string> kernelArgVars;
                    for (int i = 0; i < numInputs;  ++i)
                        kernelArgVars.push_back("buf_in"  + std::to_string(i));
                    for (int i = 0; i < numOutputs; ++i)
                        kernelArgVars.push_back("buf_out" + std::to_string(i));

                    std::vector<hlir::HlirBridge::ReleaseSpec> releases;
                    for (int i = 0; i < numInputs;  ++i)
                        releases.push_back({"in"  + std::to_string(i), 1});
                    for (int i = 0; i < numOutputs; ++i)
                        releases.push_back({"out" + std::to_string(i), 1});

                    auto result = m_bridge->addCoreFunction(
                        "core_" + kernel->id.toStdString(),
                        params,
                        acquires,
                        {"kernel", kernelArgVars},
                        releases,
                        {});
                    if (!result) {
                        qCWarning(hlirSyncLog)
                            << "HlirSyncService: failed to add default core function for"
                            << kernelId;
                        continue;
                    }
                    sharedDefaultCoreFnId = result.value();
                }
                coreFnId = sharedDefaultCoreFnId;
                m_coreFuncMap[spec.blockId] = coreFnId;
            }

            // ---- Worker for this tile ----
            std::vector<hlir::HlirBridge::FunctionArg> fnArgs;
            const QList<Canvas::CanvasBlock::CoreBodyArgSpec> coreArgs =
                blk ? blk->coreBodyArgs() : QList<Canvas::CanvasBlock::CoreBodyArgSpec>{};

            if (!coreArgs.isEmpty()) {
                // User-specified fn_args order via the properties panel.
                QHash<QString, FifoEndpoint> inputByName, outputByName;
                for (const auto& ep : spec.inputs)
                    if (!ep.fifoName.isEmpty()) inputByName[ep.fifoName] = ep;
                for (const auto& ep : spec.outputs)
                    if (!ep.fifoName.isEmpty()) outputByName[ep.fifoName] = ep;

                for (const auto& argSpec : coreArgs) {
                    using Kind = Canvas::CanvasBlock::CoreBodyArgSpec::Kind;
                    switch (argSpec.kind) {
                        case Kind::Kernel: {
                            // Use current kernel if ref matches, otherwise fall back to kernelMap.
                            const hlir::ComponentId kid = (argSpec.ref == kernelId)
                                ? kernelResult.value()
                                : m_kernelMap.value(argSpec.ref);
                            if (!kid.empty())
                                fnArgs.push_back(hlir::HlirBridge::FunctionArg::kernel(kid));
                            else
                                qCWarning(hlirSyncLog)
                                    << "HlirSyncService: coreBodyArgs: kernel" << argSpec.ref
                                    << "not found for tile" << QString::fromStdString(spec.tileName);
                            break;
                        }
                        case Kind::FifoConsumer: {
                            const auto it = inputByName.find(argSpec.ref);
                            if (it != inputByName.end())
                                fnArgs.push_back(it->arg);
                            else
                                qCWarning(hlirSyncLog)
                                    << "HlirSyncService: coreBodyArgs: input FIFO" << argSpec.ref
                                    << "not found for tile" << QString::fromStdString(spec.tileName);
                            break;
                        }
                        case Kind::FifoProducer: {
                            const auto it = outputByName.find(argSpec.ref);
                            if (it != outputByName.end())
                                fnArgs.push_back(it->arg);
                            else
                                qCWarning(hlirSyncLog)
                                    << "HlirSyncService: coreBodyArgs: output FIFO" << argSpec.ref
                                    << "not found for tile" << QString::fromStdString(spec.tileName);
                            break;
                        }
                    }
                }
            } else {
                // Default ordering: all kernels first (in assigned order), then inputs, then outputs.
                for (const auto& kid : spec.allKernels) {
                    const hlir::ComponentId kId = (kid == kernelId)
                        ? kernelResult.value()
                        : m_kernelMap.value(kid);
                    if (!kId.empty())
                        fnArgs.push_back(hlir::HlirBridge::FunctionArg::kernel(kId));
                    else
                        qCWarning(hlirSyncLog)
                            << "HlirSyncService: kernel" << kid << "not in map for tile"
                            << QString::fromStdString(spec.tileName);
                }
                for (const auto& ep : spec.inputs)  fnArgs.push_back(ep.arg);
                for (const auto& ep : spec.outputs) fnArgs.push_back(ep.arg);
            }

            auto workerResult = m_bridge->addWorker(
                "worker_" + spec.tileName,
                coreFnId,
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
    // Create a runtime sequence with Fill/Drain ops driven by Distribute/Collect hubs.
    //
    // Topology for hub-based designs:
    //   DDR ──[Fill pivot wire]──> Distribute hub ──[arm wires]──> SHIM tiles
    //   SHIM tiles ──[FIFO wires]──> Compute tiles
    //   SHIM tiles <──[arm wires]── Collect hub <──[Drain pivot wire]── DDR
    //
    // rt.sequence: one argument per hub (= one DDR buffer), NOT one per SHIM arm.
    // rt.fill/drain: one call per arm, each with a TensorAccessPattern offset.
    //
    // Falls back to the direct DDR↔SHIM path for designs without hubs.

    if (!m_document || !m_bridge)
        return;

    QHash<QString, qint64> constantsMap;
    if (m_symbolsController) {
        for (const auto& sym : m_symbolsController->symbols()) {
            if (sym.kind == SymbolKind::Constant && !sym.name.isEmpty())
                constantsMap.insert(sym.name, sym.constant.value);
        }
    }

    const QLatin1StringView ddrSpecId{"ddr"};
    const auto& items = m_document->items();

    // Helper: parse "MxN" or "N" dimension string into a flat element count.
    // -------------------------------------------------------------------------
    // Pass 1: Find Distribute/Collect hubs attached to DDR blocks.
    //   Distribute hub → one fill group  (DDR → hub → SHIMs → compute)
    //   Collect  hub  → one drain group  (compute → SHIMs → hub → DDR)
    // -------------------------------------------------------------------------
    struct HubGroup {
        Canvas::ObjectId    hubBlockId;
        Canvas::CanvasWire* ddrWire = nullptr; // DDR↔hub pivot wire (total buffer size)
        bool                isFill  = true;    // true=Distribute, false=Collect
        std::string         paramName;

        struct ArmEntry {
            hlir::ComponentId   fifoId;
            hlir::ComponentId   shimTileId;
            Canvas::CanvasWire* fifoWire = nullptr; // SHIM→compute FIFO wire
            Canvas::CanvasWire* armWire  = nullptr; // hub↔SHIM wire (carries tapSymbolRef)
            int                 armIndex = 0;
        };
        QList<ArmEntry> arms;
    };

    // Pre-build hub port-role lookup for direction inference on unannotated wires.
    QHash<Canvas::ObjectId, QHash<Canvas::PortId, Canvas::PortRole>> hubPortRoles;
    for (const auto& item : items) {
        auto* hub = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!hub || !hub->isLinkHub()) continue;
        QHash<Canvas::PortId, Canvas::PortRole> roles;
        for (const auto& port : hub->ports())
            roles[port.id] = port.role;
        hubPortRoles[hub->id()] = roles;
    }

    QList<HubGroup> hubGroups;

    for (const auto& item : items) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire) continue;

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

        const bool aDdr = (blockA->specId() == ddrSpecId);
        const bool bDdr = (blockB->specId() == ddrSpecId);
        if (!aDdr && !bDdr)
            continue; // wire must involve DDR

        // The non-DDR endpoint is the hub or direct SHIM.
        Canvas::CanvasBlock* otherBlock = aDdr ? blockB : blockA;
        const Canvas::CanvasWire::Endpoint& otherEp = aDdr ? epB : epA;

        // Determine fill/drain direction: annotation first, then topology inference.
        bool wireIsFill;
        if (wire->hasFillDrain()) {
            wireIsFill = wire->fillDrain()->isFill;
        } else if (wire->hasObjectFifo()) {
            const auto op = wire->objectFifo()->operation;
            if (op == Canvas::CanvasWire::ObjectFifoOperation::Fill)
                wireIsFill = true;
            else if (op == Canvas::CanvasWire::ObjectFifoOperation::Drain)
                wireIsFill = false;
            else
                continue;
        } else if (otherBlock->isLinkHub() && otherEp.attached.has_value()) {
            // Consumer port on hub → hub receives from DDR → Distribute → fill.
            // Producer port on hub → hub sends to DDR    → Collect   → drain.
            const auto& roles = hubPortRoles.value(otherBlock->id());
            const auto role = roles.value(otherEp.attached->portId, Canvas::PortRole::Dynamic);
            if (role == Canvas::PortRole::Consumer)      wireIsFill = true;
            else if (role == Canvas::PortRole::Producer)  wireIsFill = false;
            else                                          continue;
        } else {
            continue; // unannotated, not a hub — skip
        }

        // Resolve the hub block (or treat a direct SHIM as a 1-arm "hub").
        Canvas::CanvasBlock* hubBlock = nullptr;
        if (otherBlock->isLinkHub()) {
            hubBlock = otherBlock;
        } else {
            const auto parsed = parseTileSpecId(otherBlock->specId());
            if (parsed && parsed->kind == hlir::TileKind::SHIM)
                hubBlock = otherBlock;
        }

        if (!hubBlock)
            continue;

        HubGroup grp;
        grp.hubBlockId = hubBlock->id();
        grp.ddrWire    = wire;
        grp.isFill     = wireIsFill;
        hubGroups.append(grp);
    }

    if (hubGroups.isEmpty())
        return;

    // -------------------------------------------------------------------------
    // Pass 2: For each hub, enumerate its arm wires that connect to SHIM tiles,
    //         then find the FIFO wires those SHIMs produce/consume.
    // -------------------------------------------------------------------------

    // Build a map: SHIM block id → fill/drain FIFO wire and tile id.
    // (Used for direct-DDR-SHIM fallback and hub arm lookup.)
    // Multi-valued: each SHIM may produce/consume multiple FIFO wires.
    QHash<Canvas::ObjectId, QList<Canvas::CanvasWire*>> shimFillFifoWires;  // SHIM → FIFO wires (producer)
    QHash<Canvas::ObjectId, QList<Canvas::CanvasWire*>> shimDrainFifoWires; // SHIM → FIFO wires (consumer)
    for (const auto& item : items) {
        auto* wire = dynamic_cast<Canvas::CanvasWire*>(item.get());
        if (!wire || !wire->hasObjectFifo())
            continue;
        if (wire->objectFifo()->operation != Canvas::CanvasWire::ObjectFifoOperation::Fifo)
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
        const auto parsedA = parseTileSpecId(blockA->specId());
        const auto parsedB = parseTileSpecId(blockB->specId());
        if (parsedA && parsedA->kind == hlir::TileKind::SHIM)
            shimFillFifoWires[blockA->id()].append(wire);   // SHIM is producer
        if (parsedB && parsedB->kind == hlir::TileKind::SHIM)
            shimDrainFifoWires[blockB->id()].append(wire);  // SHIM is consumer
    }
    // Helper: pick the FIFO wire for a given SHIM+armWire combination.
    // If the arm wire has a fifoName set in its FillDrainConfig, match by name.
    // Otherwise, return the first in the list (works for single-FIFO SHIMs).
    const auto pickFifoWire = [&](Canvas::ObjectId shimId, Canvas::CanvasWire* armWire, bool isFill)
        -> Canvas::CanvasWire*
    {
        const auto& fifoList = isFill ? shimFillFifoWires.value(shimId)
                                      : shimDrainFifoWires.value(shimId);
        if (fifoList.isEmpty()) return nullptr;
        if (fifoList.size() == 1) return fifoList[0];
        // Multiple candidates: match by fifoName on the arm wire.
        if (armWire && armWire->hasFillDrain()) {
            const QString& targetName = armWire->fillDrain()->fifoName;
            if (!targetName.isEmpty()) {
                for (auto* fw : fifoList)
                    if (fw->hasObjectFifo() && fw->objectFifo()->name == targetName)
                        return fw;
            }
        }
        return fifoList[0]; // fallback: first available
    };

    // For each hub group, find the arm wires → SHIM → FIFO entries.
    for (auto& grp : hubGroups) {
        auto* hubBlock = dynamic_cast<Canvas::CanvasBlock*>(
            m_document->findItem(grp.hubBlockId));
        const bool hubIsShim = hubBlock && !hubBlock->isLinkHub();

        if (hubIsShim) {
            // Legacy direct DDR↔SHIM: the "hub" IS the SHIM — single arm.
            const hlir::ComponentId shimId = m_tileMap.value(grp.hubBlockId);
            Canvas::CanvasWire* fifoWire = pickFifoWire(grp.hubBlockId, grp.ddrWire, grp.isFill);
            if (!shimId.empty() && fifoWire) {
                const hlir::ComponentId fifoId = m_fifoMap.value(fifoWire->id());
                if (!fifoId.empty())
                    grp.arms.append({fifoId, shimId, fifoWire, 0});
            }
            continue;
        }

        // Real hub: find all wires whose endpoint attaches to this hub block.
        // Those wires go to SHIM tiles — each one is an arm.
        int armIdx = 0;
        for (const auto& armItem : items) {
            auto* armWire = dynamic_cast<Canvas::CanvasWire*>(armItem.get());
            if (!armWire)
                continue;
            const auto& aEp = armWire->a();
            const auto& bEp = armWire->b();

            Canvas::ObjectId shimBlockId;
            if (aEp.attached && aEp.attached->itemId == grp.hubBlockId && bEp.attached)
                shimBlockId = bEp.attached->itemId;
            else if (bEp.attached && bEp.attached->itemId == grp.hubBlockId && aEp.attached)
                shimBlockId = aEp.attached->itemId;
            else
                continue;

            auto* shimBlock = dynamic_cast<Canvas::CanvasBlock*>(
                m_document->findItem(shimBlockId));
            if (!shimBlock)
                continue;
            const auto parsed = parseTileSpecId(shimBlock->specId());
            if (!parsed || parsed->kind != hlir::TileKind::SHIM)
                continue;

            const hlir::ComponentId shimId = m_tileMap.value(shimBlockId);
            if (shimId.empty())
                continue;

            Canvas::CanvasWire* fifoWire = pickFifoWire(shimBlockId, armWire, grp.isFill);
            if (!fifoWire)
                continue;
            const hlir::ComponentId fifoId = m_fifoMap.value(fifoWire->id());
            if (fifoId.empty())
                continue;

            grp.arms.append({fifoId, shimId, fifoWire, armWire, armIdx++});
        }
    }

    // Remove groups with no resolved arms.
    hubGroups.erase(std::remove_if(hubGroups.begin(), hubGroups.end(),
        [](const HubGroup& g) { return g.arms.isEmpty(); }), hubGroups.end());

    if (hubGroups.isEmpty())
        return;

    // -------------------------------------------------------------------------
    // Pass 3: Create runtime, register workers, emit one type/param per hub group.
    // -------------------------------------------------------------------------
    auto runtimeResult = m_bridge->createRuntime("runtime");
    if (!runtimeResult) {
        qCWarning(hlirSyncLog) << "HlirSyncService: failed to create runtime";
        return;
    }

    for (const hlir::ComponentId& workerId : std::as_const(m_workerMap)) {
        if (!workerId.empty())
            m_bridge->runtimeAddWorker(workerId);
    }

    // Resolve type and param name for each hub group (one rt.sequence arg per hub).
    const auto resolveGroupType = [&](HubGroup& grp) {
        hlir::ComponentId typeId;
        QString mainSize;
        QString valueType = QStringLiteral("int32");

        // Value type: prefer the FIFO wire dtype (actual element dtype).
        if (!grp.arms.isEmpty()) {
            if (auto* w = grp.arms[0].fifoWire; w && w->hasObjectFifo()) {
                const auto resolved = resolveType(w->objectFifo().value().type, constantsMap);
                if (!resolved.valueType.isEmpty())
                    valueType = normalizeValueType(resolved.valueType);
            }
        }
        // Total size and sequence type: from DDR pivot wire (FillDrainConfig).
        if (auto* d = grp.ddrWire; d && d->hasFillDrain()) {
            const auto& fd = d->fillDrain().value();
            if (valueType == QStringLiteral("int32") && !fd.valueType.isEmpty())
                valueType = normalizeValueType(fd.valueType);
            mainSize = fd.totalDims.trimmed();
            if (!mainSize.isEmpty()) {
                // Explicit symbol reference takes priority.
                QString effectiveSymRef = (fd.symbolRef.has_value() && !fd.symbolRef->isEmpty())
                    ? *fd.symbolRef : QString();
                // If totalDims looks like a symbol name (not a numeric/x-separated
                // dimension string), treat it as an implicit symbolRef.
                if (effectiveSymRef.isEmpty() && !mainSize.isEmpty()) {
                    const bool looksNumeric = mainSize.at(0).isDigit()
                                             || mainSize.contains(u'x');
                    if (!looksNumeric && m_symbolsController) {
                        for (const auto& sym : m_symbolsController->symbols()) {
                            if (sym.name == mainSize
                                    && sym.kind == SymbolKind::TypeAbstraction) {
                                effectiveSymRef = mainSize;
                                Canvas::CanvasWire::ObjectFifoTypeAbstraction typeAbs;
                                typeAbs.symbolRef  = mainSize;
                                typeAbs.dimensions = sym.type.shapeTokens.join(u'x');
                                typeAbs.valueType  = sym.type.dtype;
                                const auto resolved = resolveType(typeAbs, constantsMap);
                                mainSize = resolved.dimensions;
                                break;
                            }
                        }
                    }
                }
                typeId = effectiveSymRef.isEmpty()
                    ? ensureTensorType(mainSize, valueType)
                    : ensureNamedTensorType(effectiveSymRef, mainSize, valueType);
            }
        }
        // Fallback: use first arm's FIFO dimensions.
        if (typeId.empty() && !grp.arms.isEmpty()) {
            if (auto* w = grp.arms[0].fifoWire; w && w->hasObjectFifo()) {
                const auto resolved = resolveType(w->objectFifo().value().type, constantsMap);
                if (!resolved.dimensions.isEmpty()) {
                    typeId = resolved.symbolName.isEmpty()
                        ? ensureTensorType(resolved.dimensions, valueType)
                        : ensureNamedTensorType(resolved.symbolName, resolved.dimensions, valueType);
                    if (mainSize.isEmpty())
                        mainSize = resolved.dimensions.trimmed();
                }
            }
        }
        return std::make_pair(typeId, mainSize);
    };

    QList<HubGroup*> fillGroups, drainGroups;
    for (auto& grp : hubGroups)
        (grp.isFill ? fillGroups : drainGroups).append(&grp);

    int fillIdx = 0, drainIdx = 0;
    for (auto* grp : fillGroups) {
        auto [typeId, mainSize] = resolveGroupType(*grp);
        if (!typeId.empty())
            m_bridge->runtimeAddInputType(typeId);
        m_bridge->runtimeAddMainSize(mainSize.toStdString());

        // Param name: from DDR pivot wire's FillDrainConfig paramName, then fallback.
        std::string pname = "input_" + std::to_string(fillIdx++);
        if (auto* d = grp->ddrWire; d && d->hasFillDrain()) {
            const QString n = d->fillDrain()->paramName;
            if (!n.isEmpty()) pname = sanitizePythonName(n).toStdString();
        }
        grp->paramName = pname;
        m_bridge->runtimeAddParam(pname);
    }
    for (auto* grp : drainGroups) {
        auto [typeId, mainSize] = resolveGroupType(*grp);
        if (!typeId.empty())
            m_bridge->runtimeAddOutputType(typeId);
        m_bridge->runtimeAddMainSize(mainSize.toStdString());

        std::string pname = "output_" + std::to_string(drainIdx++);
        if (auto* d = grp->ddrWire; d && d->hasFillDrain()) {
            const QString n = d->fillDrain()->paramName;
            if (!n.isEmpty()) pname = sanitizePythonName(n).toStdString();
        }
        grp->paramName = pname;
        m_bridge->runtimeAddParam(pname);
    }

    // -------------------------------------------------------------------------
    // Pass 4: Emit rt.fill / rt.drain — one call per arm with a TAP offset.
    // -------------------------------------------------------------------------
    const auto emitArms = [&](HubGroup* grp, bool isFill) {
        const QString ddrDims = grp->ddrWire && grp->ddrWire->hasFillDrain()
            ? grp->ddrWire->fillDrain()->totalDims.trimmed()
            : QString{};

        for (const auto& arm : grp->arms) {
            const std::string armName = grp->paramName + "_arm" + std::to_string(arm.armIndex);

            hlir::ComponentId tapId;
            // Per-arm TAP takes priority (set on the hub↔SHIM arm wire).
            if (arm.armWire && arm.armWire->hasFillDrain()) {
                const auto& afd = *arm.armWire->fillDrain();
                if (afd.tapSymbolRef.has_value() && !afd.tapSymbolRef->isEmpty())
                    tapId = m_tilerMap.value(*afd.tapSymbolRef);
            }
            // Fall back to group-level TAP on the DDR pivot wire.
            if (tapId.empty() && grp->ddrWire && grp->ddrWire->hasFillDrain()) {
                const auto& fd = *grp->ddrWire->fillDrain();
                if (fd.tapSymbolRef.has_value() && !fd.tapSymbolRef->isEmpty()) {
                    tapId = m_tilerMap.value(*fd.tapSymbolRef);
                } else if (fd.mode == Canvas::CanvasWire::DimensionMode::Matrix
                           && fd.tap.has_value() && !fd.tap->tileDims.isEmpty()) {
                    const QString tilerName = QString::fromStdString(grp->paramName)
                                              + QStringLiteral("_tap");
                    tapId = ensureTensorTiler2D(tilerName, *fd.tap, ddrDims);
                }
            }

            if (isFill)
                m_bridge->runtimeAddFill(armName, arm.fifoId, grp->paramName,
                                          arm.shimTileId, -1, !tapId.empty(), tapId);
            else
                m_bridge->runtimeAddDrain(armName, arm.fifoId, grp->paramName,
                                           arm.shimTileId, -1, !tapId.empty(), tapId);
        }
    };

    for (auto* grp : fillGroups)  emitArms(grp, true);
    for (auto* grp : drainGroups) emitArms(grp, false);

    auto buildResult = m_bridge->runtimeBuild();
    if (!buildResult)
        qCWarning(hlirSyncLog) << "HlirSyncService: failed to build runtime";
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

    {
        std::unordered_set<std::string> seenCoreFns;
        for (const hlir::ComponentId& id : std::as_const(m_coreFuncMap)) {
            if (seenCoreFns.insert(id.value).second)
                m_bridge->remove(id);
        }
    }
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
    m_typeNameByKey.clear();

    for (const hlir::ComponentId& id : std::as_const(m_constantMap))
        m_bridge->remove(id);
    m_constantMap.clear();

    for (const hlir::ComponentId& id : std::as_const(m_tileMap))
        m_bridge->remove(id);
    m_tileMap.clear();
}

} // namespace Aie::Internal
