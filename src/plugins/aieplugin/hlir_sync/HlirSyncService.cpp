// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/HlirSyncService.hpp"
#include "aieplugin/hlir_sync/DesignVerifier.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasWire.hpp"
#include "hlir_cpp_bridge/HlirBridge.hpp"
#include "code_gen_bridge/CodeGenBridge.hpp"

#include <QtCore/QDir>
#include <QtCore/QLoggingCategory>
#include <QtCore/QSet>
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

    // Remove stale components (FIFOs before tiles to satisfy bridge dependency order)
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

    // Sync FIFOs
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

        QString fifoName;
        int depth = 2;
        hlir::ComponentId typeId = defaultTypeId;

        if (wire->hasObjectFifo()) {
            const auto& cfg = wire->objectFifo().value();
            fifoName = cfg.name;
            depth = cfg.depth;
            // Normalise "i32" → "int32" so the Python DataType enum accepts it
            const QString vt = (cfg.type.valueType == QStringLiteral("i32"))
                                   ? QStringLiteral("int32")
                                   : cfg.type.valueType;
            typeId = ensureTensorType(cfg.type.dimensions, vt);
        } else {
            fifoName = QStringLiteral("fifo_%1_to_%2")
                           .arg(producerSpecId, consumerSpecId);
        }

        if (typeId.empty())
            continue;

        const hlir::ComponentId producerCompId = m_tileMap.value(blockA->id());
        const hlir::ComponentId consumerCompId = m_tileMap.value(blockB->id());
        if (producerCompId.empty() || consumerCompId.empty())
            continue;

        const hlir::ComponentId existingId = m_fifoMap.value(wire->id());

        auto result = m_bridge->addFifo(
            fifoName.toStdString(),
            typeId,
            depth,
            producerCompId,
            {consumerCompId},
            existingId);

        if (result) {
            m_fifoMap[wire->id()] = result.value();
        } else {
            qCWarning(hlirSyncLog) << "HlirSyncService: failed to sync FIFO" << fifoName;
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
    // Run verification and emit verificationFinished() with a pass/fail summary.
    if (!m_document) {
        emit verificationFinished(false, tr("No design is open."));
        return;
    }

    const auto issues = runVerification();

    if (DesignVerifier::hasErrors(issues)) {
        QString msg = tr("Verification failed:\n\n");
        for (const auto& issue : issues)
            if (issue.severity == VerificationIssue::Severity::Error)
                msg += QStringLiteral("• ") + issue.message + u'\n';
        emit verificationFinished(false, msg);
    } else {
        const DesignStats stats = collectStats({m_document});
        const QString msg = tr("Design verification passed.\n\n"
                               "Design summary:\n"
                               "  \u2022 SHIM tiles: %1\n"
                               "  \u2022 MEM tiles:  %2\n"
                               "  \u2022 AIE tiles:  %3\n"
                               "  \u2022 FIFOs:      %4\n"
                               "  \u2022 Fills:      %5\n"
                               "  \u2022 Drains:     %6")
            .arg(stats.shimTiles).arg(stats.memTiles).arg(stats.aieTiles)
            .arg(stats.fifos).arg(stats.fills).arg(stats.drains);
        emit verificationFinished(true, msg);
    }
}

void HlirSyncService::generateCode()
{
    // Sync canvas, build HLIR, export to GUI XML, and run code generation.
    if (!m_bridge || !m_document) {
        emit codeGenFinished(false, tr("No design is open."));
        return;
    }

    // Verify the design before generating code
    const auto issues = runVerification();
    if (DesignVerifier::hasErrors(issues)) {
        QString msg = tr("Cannot generate code — design verification failed.\n\nIssues:\n");
        for (const auto& issue : issues)
            if (issue.severity == VerificationIssue::Severity::Error)
                msg += QStringLiteral("• ") + issue.message + u'\n';
        emit codeGenFinished(false, msg);
        return;
    }

    // specIds are assigned lazily by the grid host, so sync here to capture the current layout
    syncCanvas();
    buildRuntime();

    // Validate the HLIR program
    auto buildResult = m_bridge->build();
    if (!buildResult) {
        QStringList errors;
        for (const auto& diag : buildResult.error())
            errors << QString::fromStdString(diag.message);
        emit codeGenFinished(false, errors.join(u'\n'));
        return;
    }

    // Export to GUI XML — "_gui.xml" suffix triggers the XMLTransformer step in main.py
    QDir().mkpath(m_outputDir);
    const QString xmlPath = m_outputDir + QStringLiteral("/design_gui.xml");
    auto exportResult = m_bridge->exportToGuiXml(xmlPath.toStdString());
    if (!exportResult) {
        emit codeGenFinished(false, tr("Failed to export HLIR to XML."));
        return;
    }

    // Run Python code generation
    codegen::CodeGenBridge codegenBridge;
    codegen::CodeGenOptions options;
    options.outputDir = m_outputDir.toStdString();

    auto genResult = codegenBridge.runCodeGen(
        std::filesystem::path(xmlPath.toStdWString()), options);

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
    // Create a runtime sequence with Fill/Drain ops for shim-connected FIFOs.
    if (!m_document || !m_bridge)
        return;

    const hlir::ComponentId defaultTypeId = m_typeMap.value(QStringLiteral("data_ty"));

    // Classify wires by shim endpoint role
    struct ShimFifoEntry {
        hlir::ComponentId fifoId;
        hlir::ComponentId shimTileId;
    };

    QList<ShimFifoEntry> fillEntries;
    QList<ShimFifoEntry> drainEntries;

    const auto& items = m_document->items();
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

        const auto parsedA = parseTileSpecId(blockA->specId());
        const auto parsedB = parseTileSpecId(blockB->specId());
        if (!parsedA || !parsedB)
            continue;

        // Producer is SHIM → Fill (DDR → shim → AIE)
        if (parsedA->kind == hlir::TileKind::SHIM) {
            const hlir::ComponentId shimTileId = m_tileMap.value(blockA->id());
            if (!shimTileId.empty())
                fillEntries.append({fifoId, shimTileId});
        }

        // Consumer is SHIM → Drain (AIE → shim → DDR)
        if (parsedB->kind == hlir::TileKind::SHIM) {
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
    // Remove all tracked HLIR components in dependency order (FIFOs → types → tiles).
    if (!m_bridge)
        return;

    // Remove in dependency order: FIFOs first, then types, then tiles
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
