// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/hlir_sync/DesignVerifier.hpp"
#include "canvas/CanvasTypes.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/api/ICanvasDocumentService.hpp"
#include "hlir_cpp_bridge/HlirTypes.hpp"

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

#include <memory>
#include <optional>

namespace Canvas {
class CanvasDocument;
} // namespace Canvas

namespace hlir {
class HlirBridge;
} // namespace hlir

namespace Aie::Internal {

class KernelRegistryService;
class SymbolsController;

/// Keeps an HlirBridge in sync with the active CanvasDocument (tiles + FIFOs).
class AIEPLUGIN_EXPORT HlirSyncService : public QObject
{
    Q_OBJECT

public:
    explicit HlirSyncService(QObject* parent = nullptr);
    ~HlirSyncService() override;

    /// Attach to a canvas document, replacing any previous design state.
    void attachDocument(Canvas::CanvasDocument* doc, const QString& outputBaseDir);

    /// Detach from the current document and remove all tracked bridge components.
    void detachDocument();

    /// The base output directory set by the most recent attachDocument() call.
    const QString& outputDir() const { return m_outputDir; }

    /// Full path of the generated Python script for the current design,
    /// e.g. <outputDir>/generated_MatrixVectorMul.py.
    /// Returns an empty string when no document is attached.
    QString generatedScriptPath() const;

    /// Set the kernel registry used to look up kernel assets during code generation.
    void setKernelRegistry(KernelRegistryService* registry);

    /// Set the symbol table controller. When set, TypeAbstraction symbol references on
    /// wires are resolved to named tensor types during sync.
    void setSymbolsController(SymbolsController* controller);

    /// Set the canvas document service. Used to look up the shared core function library
    /// stored in document metadata when resolving SharedRef core function configs.
    void setCanvasDocumentService(Canvas::Api::ICanvasDocumentService* service);

    /// Disable the per-step animation delay (250 ms sleep). Call in test fixtures.
    static void setAnimateSteps(bool enabled) { s_animateSteps = enabled; }
    static bool animateSteps() { return s_animateSteps; }

public slots:
    /// Run all design rule checks and emit verificationFinished() with the result.
    void verifyDesign();

    /// Sync, build, and export the HLIR program; emits codeGenFinished().
    void generateCode();

signals:
    /// Emitted once when a multi-step operation begins (opens a new log entry).
    void runStarted();
    /// Emitted after each individual step completes (appended to the current log entry).
    void stepLogged(bool ok, const QString& label);
    void verificationFinished(bool passed, const QString& message);
    void codeGenFinished(bool success, const QString& message);

private slots:
    void onCanvasChanged();

private:
    struct ParsedTileSpec {
        hlir::TileKind kind;
        int col = 0;
        int row = 0;
    };

    /// Parse a specId ("shim0_0", "mem1_2", "aie0_3") into tile kind and coordinates.
    std::optional<ParsedTileSpec> parseTileSpecId(const QString& specId) const;

    /// Get or create a named tensor type (e.g. "data_ty").
    hlir::ComponentId ensureNamedTensorType(const QString& name,
                                            const QString& dimensions,
                                            const QString& valueType);

    /// Get or create a tensor type for the given dimensions and dtype.
    /// Name is derived from both (e.g. "type_int32_1024").
    hlir::ComponentId ensureTensorType(const QString& dimensions, const QString& valueType);

    /// Register a TensorTiler2D (group_tiler) for the given TAP config.
    /// Returns the ComponentId, or empty on failure.
    hlir::ComponentId ensureTensorTiler2D(const QString& name,
                                          const Canvas::CanvasWire::TensorTilerConfig& tap,
                                          const QString& totalDims);

    /// Register a TAP (TensorAccessPattern or TensorTiler2D) from symbol table.
    /// Returns the ComponentId, or empty on failure.
    hlir::ComponentId ensureTap(const QString& name,
                                int rows,
                                int cols,
                                int offset,
                                const QVector<int>& sizes,
                                const QVector<int>& strides,
                                bool useTiler2D,
                                const QString& tensorDims,
                                const QString& tileDims,
                                const QString& tileCounts,
                                const QString& patternRepeat);

    struct ResolvedType {
        QString dimensions;
        QString valueType;
        QString symbolName; // empty if literal (no symbol ref)
    };

    /// Resolve a wire's type abstraction to concrete dims/dtype/symbolName.
    /// If symbolRef is set and found in the symbol table, shape tokens are resolved
    /// using the provided constants map; otherwise, literal values are returned as-is.
    ResolvedType resolveType(const Canvas::CanvasWire::ObjectFifoTypeAbstraction& typeAbs,
                             const QHash<QString, qint64>& constants) const;

    /// Run all design rule checks and return the collected issues.
    QList<VerificationIssue> runVerification() const;

    /// Remove all tracked HLIR components in dependency order.
    void resetTrackedComponents();

    /// Diff the canvas against tracked state and update the bridge accordingly.
    void syncCanvas();

    /// Register split and join hub blocks as HLIR split/join operations.
    void syncSplitsAndJoins();

    /// For each COMPUTE tile with a kernel stereotype, generate an external kernel
    /// declaration, a core body function, and a worker binding.
    void buildWorkers();

    /// Build a Runtime: shim-producer wires → Fill, shim-consumer wires → Drain.
    void buildRuntime();

    std::unique_ptr<hlir::HlirBridge> m_bridge;

    // Maps canvas ObjectId → HLIR ComponentId
    QHash<Canvas::ObjectId, hlir::ComponentId> m_tileMap;
    QHash<Canvas::ObjectId, hlir::ComponentId> m_fifoMap;
    QHash<Canvas::ObjectId, hlir::ComponentId> m_splitJoinMap;
    QHash<Canvas::ObjectId, hlir::ComponentId> m_workerMap;

    // One external kernel per unique kernel ID (shared across all tiles using it).
    QHash<QString, hlir::ComponentId> m_kernelMap;   // kernelId → external kernel ComponentId
    // Per-tile core function: default tiles sharing a kernel share one entry; custom body_stmts
    // tiles each get their own. Keyed by block ObjectId so workers can look up their fn.
    QHash<Canvas::ObjectId, hlir::ComponentId> m_coreFuncMap; // blockId → core fn ComponentId

    // Maps constant symbol name → HLIR ComponentId
    QHash<QString, hlir::ComponentId> m_constantMap;
    // Maps "dimensions|valueType" → HLIR ComponentId (tensor type cache)
    QHash<QString, hlir::ComponentId> m_typeMap;
    // Maps "dimensions|valueType" → already-registered named type key, so ensureTensorType
    // can reuse a named type instead of generating a duplicate anonymous one.
    QHash<QString, QString> m_typeNameByKey;
    // Maps tiler name → HLIR ComponentId (TensorTiler2D cache)
    QHash<QString, hlir::ComponentId> m_tilerMap;
    // Maps hub block ObjectId → branch type ComponentId (split/join/forward branch element type)
    QHash<Canvas::ObjectId, hlir::ComponentId> m_branchTypeMap;

    QPointer<Canvas::CanvasDocument> m_document;
    QPointer<KernelRegistryService> m_kernelRegistry;
    QPointer<SymbolsController> m_symbolsController;
    QPointer<Canvas::Api::ICanvasDocumentService> m_canvasDocuments;
    QString m_outputDir;
    bool m_syncInProgress = false;

    static bool s_animateSteps;
};

} // namespace Aie::Internal
