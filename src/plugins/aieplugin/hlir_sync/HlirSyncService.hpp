// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/hlir_sync/DesignVerifier.hpp"
#include "canvas/CanvasTypes.hpp"
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

/// Keeps an HlirBridge in sync with the active CanvasDocument (tiles + FIFOs).
class HlirSyncService : public QObject
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

    /// Run all design rule checks and return the collected issues.
    QList<VerificationIssue> runVerification() const;

    /// Remove all tracked HLIR components in dependency order (FIFOs → types → tiles).
    void resetTrackedComponents();

    /// Diff the canvas against tracked state and update the bridge accordingly.
    void syncCanvas();

    /// Register split and join hub blocks as HLIR split/join operations.
    void syncSplitsAndJoins();

    /// Build a Runtime: shim-producer wires → Fill, shim-consumer wires → Drain.
    void buildRuntime();

    std::unique_ptr<hlir::HlirBridge> m_bridge;

    // Maps canvas ObjectId → HLIR ComponentId
    QHash<Canvas::ObjectId, hlir::ComponentId> m_tileMap;
    QHash<Canvas::ObjectId, hlir::ComponentId> m_fifoMap;
    QHash<Canvas::ObjectId, hlir::ComponentId> m_splitJoinMap;

    // Maps "dimensions|valueType" → HLIR ComponentId (tensor type cache)
    QHash<QString, hlir::ComponentId> m_typeMap;

    QPointer<Canvas::CanvasDocument> m_document;
    QString m_outputDir;
};

} // namespace Aie::Internal
