// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/api/ICanvasDocumentService.hpp"

#include <utils/Result.hpp>
#include <utils/async/DebouncedInvoker.hpp>

#include <QtCore/QPointer>
#include <QtCore/QJsonObject>

namespace Canvas {
class CanvasDocument;
class CanvasView;
namespace Api {
class ICanvasHost;
} // namespace Api
} // namespace Canvas

namespace Canvas::Internal {

class CanvasDocumentServiceImpl final : public Canvas::Api::ICanvasDocumentService
{
    Q_OBJECT

public:
    explicit CanvasDocumentServiceImpl(QObject* parent = nullptr);

    void setCanvasHost(Canvas::Api::ICanvasHost* host);

    Utils::Result createDocument(const Canvas::Api::CanvasDocumentCreateRequest& request,
                                 Canvas::Api::CanvasDocumentHandle& outHandle) override;
    Utils::Result openDocument(const Canvas::Api::CanvasDocumentOpenRequest& request,
                               Canvas::Api::CanvasDocumentHandle& outHandle) override;
    Utils::Result saveDocument(const Canvas::Api::CanvasDocumentHandle& handle) override;
    Utils::Result closeDocument(const Canvas::Api::CanvasDocumentHandle& handle,
                                Canvas::Api::CanvasDocumentCloseReason reason) override;

    Canvas::Api::CanvasDocumentHandle activeDocument() const override;
    bool hasOpenDocument() const override;
    bool isDirty() const override;

private:
    struct SaveSnapshot final {
        Canvas::Api::CanvasDocumentHandle handle;
        QString persistencePath;
        QJsonObject payload;
        quint64 revision = 0;
    };

    void attachToCanvasObjects();
    void detachFromCanvasObjects();
    void markContentDirty();
    void markViewDirty();
    bool hasPendingChanges() const;
    void emitDirtyStateIfChanged(bool previousDirty);
    Utils::Result buildSaveSnapshot(SaveSnapshot& outSnapshot) const;
    static Utils::Result writeSnapshot(const SaveSnapshot& snapshot);
    void requestAutosave();
    void startAutosave(SaveSnapshot snapshot);
    void handleAutosaveFinished(const SaveSnapshot& snapshot, Utils::Result writeResult);
    Utils::Result flushAutosave(bool discardPendingRequest);

    Utils::Result saveActiveNow();
    Utils::Result saveIfHandleMatches(const Canvas::Api::CanvasDocumentHandle& handle);
    QString resolvePersistencePath(const QString& bundlePath,
                                   const QString& requestedPath) const;
    static QString normalizeBundlePath(const QString& path);
    static bool handleMatches(const Canvas::Api::CanvasDocumentHandle& lhs,
                              const Canvas::Api::CanvasDocumentHandle& rhs);

    QPointer<Canvas::Api::ICanvasHost> m_host;
    QPointer<Canvas::CanvasDocument> m_document;
    QPointer<Canvas::CanvasView> m_view;

    Canvas::Api::CanvasDocumentHandle m_activeHandle;
    QJsonObject m_activeMetadata;

    Utils::Async::DebouncedInvoker m_contentSaveDebounce;
    Utils::Async::DebouncedInvoker m_viewSaveDebounce;
    bool m_contentDirty = false;
    bool m_viewDirty = false;
    bool m_loading = false;
    bool m_autosaveInFlight = false;
    bool m_autosaveRequested = false;
    quint64 m_changeRevision = 0;

signals:
    void autosaveIdle();
};

} // namespace Canvas::Internal
