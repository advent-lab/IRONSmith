// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/document/CanvasDocumentServiceImpl.hpp"

#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/api/ICanvasHost.hpp"
#include "canvas/document/CanvasDocumentJsonSerializer.hpp"

#include <utils/DocumentBundle.hpp>
#include <utils/async/AsyncTask.hpp>
#include <utils/filesystem/JsonFileUtils.hpp>

#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QLoggingCategory>
#include <QtCore/QUuid>

Q_LOGGING_CATEGORY(canvasdoclog, "ironsmith.canvas.document")

namespace Canvas::Internal {

namespace {

constexpr int kContentAutosaveDebounceMs = 700;
constexpr int kViewAutosaveDebounceMs = 1800;

QString cleanedPath(const QString& path)
{
    return QDir::cleanPath(path.trimmed());
}

Utils::Result ensureParentDirectory(const QString& filePath)
{
    const QFileInfo info(filePath);
    QDir directory = info.dir();
    if (directory.exists())
        return Utils::Result::success();
    if (!directory.mkpath(QStringLiteral("."))) {
        return Utils::Result::failure(QStringLiteral("Failed to create directory: %1")
                                          .arg(directory.absolutePath()));
    }
    return Utils::Result::success();
}

void clearCanvasDocument(CanvasDocument& document)
{
    QVector<ObjectId> ids;
    ids.reserve(static_cast<int>(document.items().size()));
    for (const auto& item : document.items()) {
        if (item)
            ids.push_back(item->id());
    }
    for (const auto& id : ids)
        document.removeItem(id);
}

} // namespace

CanvasDocumentServiceImpl::CanvasDocumentServiceImpl(QObject* parent)
    : Canvas::Api::ICanvasDocumentService(parent)
    , m_contentSaveDebounce(this)
    , m_viewSaveDebounce(this)
{
    m_contentSaveDebounce.setDelayMs(kContentAutosaveDebounceMs);
    m_contentSaveDebounce.setAction([this]() {
        if (!m_contentDirty)
            return;
        requestAutosave();
    });

    m_viewSaveDebounce.setDelayMs(kViewAutosaveDebounceMs);
    m_viewSaveDebounce.setAction([this]() {
        if (!m_viewDirty)
            return;
        if (m_contentDirty) {
            m_contentSaveDebounce.trigger();
            return;
        }
        requestAutosave();
    });
}

void CanvasDocumentServiceImpl::setCanvasHost(Canvas::Api::ICanvasHost* host)
{
    if (m_host == host)
        return;

    detachFromCanvasObjects();
    m_host = host;
    attachToCanvasObjects();
}

Utils::Result CanvasDocumentServiceImpl::createDocument(const Canvas::Api::CanvasDocumentCreateRequest& request,
                                                        Canvas::Api::CanvasDocumentHandle& outHandle)
{
    outHandle = {};
    if (!m_document)
        return Utils::Result::failure(QStringLiteral("Canvas document is not available."));

    const QString bundlePath = normalizeBundlePath(request.bundlePath);
    if (bundlePath.isEmpty())
        return Utils::Result::failure(QStringLiteral("Bundle path is empty."));

    const QString persistencePath = resolvePersistencePath(bundlePath, request.persistenceRelativePath);
    if (persistencePath.isEmpty())
        return Utils::Result::failure(QStringLiteral("Unable to resolve persistence path."));

    const Utils::Result parentResult = ensureParentDirectory(persistencePath);
    if (!parentResult)
        return parentResult;

    if (hasOpenDocument()) {
        const Utils::Result closeResult = closeDocument(m_activeHandle, Canvas::Api::CanvasDocumentCloseReason::OpenReplaced);
        if (!closeResult)
            return closeResult;
    }

    m_loading = true;
    if (!request.initializeFromCurrentCanvas) {
        const Utils::Result loadResult = CanvasDocumentJsonSerializer::deserialize(request.specification,
                                                                                   *m_document,
                                                                                   m_view);
        if (!loadResult) {
            m_loading = false;
            return loadResult;
        }
    }
    m_loading = false;

    Canvas::Api::CanvasDocumentHandle handle;
    handle.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    handle.bundlePath = bundlePath;
    handle.persistencePath = persistencePath;
    m_activeHandle = handle;
    m_activeMetadata = request.metadata;
    m_contentDirty = true;
    m_viewDirty = false;
    m_autosaveRequested = false;
    m_changeRevision = 1;

    if (request.activate && m_host)
        m_host->setCanvasActive(true);

    const Utils::Result saveResult = saveActiveNow();
    if (!saveResult)
        return saveResult;

    outHandle = m_activeHandle;
    emit documentOpened(m_activeHandle);
    return Utils::Result::success();
}

Utils::Result CanvasDocumentServiceImpl::openDocument(const Canvas::Api::CanvasDocumentOpenRequest& request,
                                                      Canvas::Api::CanvasDocumentHandle& outHandle)
{
    outHandle = {};
    if (!m_document)
        return Utils::Result::failure(QStringLiteral("Canvas document is not available."));

    const QString bundlePath = normalizeBundlePath(request.bundlePath);
    if (bundlePath.isEmpty())
        return Utils::Result::failure(QStringLiteral("Bundle path is empty."));

    const QString persistencePath = resolvePersistencePath(bundlePath, request.persistencePath);
    if (persistencePath.isEmpty())
        return Utils::Result::failure(QStringLiteral("Unable to resolve persistence path."));
    if (!QFileInfo::exists(persistencePath))
        return Utils::Result::failure(QStringLiteral("Canvas document does not exist: %1").arg(persistencePath));

    QString readError;
    const QJsonObject json = Utils::JsonFileUtils::readObject(persistencePath, &readError);
    if (!readError.isEmpty())
        return Utils::Result::failure(readError);

    if (hasOpenDocument()) {
        const Utils::Result closeResult = closeDocument(m_activeHandle, Canvas::Api::CanvasDocumentCloseReason::OpenReplaced);
        if (!closeResult)
            return closeResult;
    }

    m_loading = true;
    QJsonObject metadata;
    const Utils::Result loadResult = CanvasDocumentJsonSerializer::deserialize(json, *m_document, m_view, &metadata);
    m_loading = false;
    if (!loadResult)
        return loadResult;

    Canvas::Api::CanvasDocumentHandle handle;
    handle.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    handle.bundlePath = bundlePath;
    handle.persistencePath = persistencePath;
    m_activeHandle = handle;
    m_activeMetadata = metadata;
    m_contentDirty = false;
    m_viewDirty = false;
    m_autosaveRequested = false;
    m_changeRevision = 0;

    if (request.activate && m_host)
        m_host->setCanvasActive(true);

    outHandle = m_activeHandle;
    emit documentOpened(m_activeHandle);
    emit documentDirtyChanged(m_activeHandle, false);
    return Utils::Result::success();
}

Utils::Result CanvasDocumentServiceImpl::saveDocument(const Canvas::Api::CanvasDocumentHandle& handle)
{
    return saveIfHandleMatches(handle);
}

Utils::Result CanvasDocumentServiceImpl::closeDocument(const Canvas::Api::CanvasDocumentHandle& handle,
                                                       Canvas::Api::CanvasDocumentCloseReason reason)
{
    if (!hasOpenDocument())
        return Utils::Result::success();

    if (!handleMatches(handle, m_activeHandle))
        return Utils::Result::failure(QStringLiteral("Canvas document handle is not active."));

    m_contentSaveDebounce.cancel();
    m_viewSaveDebounce.cancel();

    if (hasPendingChanges()) {
        const Utils::Result saveResult = saveActiveNow();
        if (!saveResult) {
            qCWarning(canvasdoclog).noquote()
                << "CanvasDocumentService: save before close failed:"
                << saveResult.errors.join("\n");
        }
    }

    m_loading = true;
    clearCanvasDocument(*m_document);
    if (m_view) {
        m_view->setZoom(1.0);
        m_view->setPan(QPointF());
    }
    m_loading = false;

    if (m_host)
        m_host->setCanvasActive(false);

    const Canvas::Api::CanvasDocumentHandle closedHandle = m_activeHandle;
    m_activeHandle = {};
    m_activeMetadata = {};
    m_contentDirty = false;
    m_viewDirty = false;
    m_autosaveRequested = false;
    m_changeRevision = 0;

    emit documentClosed(closedHandle, reason);
    return Utils::Result::success();
}

Canvas::Api::CanvasDocumentHandle CanvasDocumentServiceImpl::activeDocument() const
{
    return m_activeHandle;
}

bool CanvasDocumentServiceImpl::hasOpenDocument() const
{
    return m_activeHandle.isValid();
}

bool CanvasDocumentServiceImpl::isDirty() const
{
    return hasPendingChanges();
}

void CanvasDocumentServiceImpl::attachToCanvasObjects()
{
    if (!m_host)
        return;

    m_document = m_host->document();
    m_view = qobject_cast<CanvasView*>(m_host->viewWidget());
    if (m_document) {
        connect(m_document, &CanvasDocument::changed,
                this, &CanvasDocumentServiceImpl::markContentDirty);
    }
    if (m_view) {
        connect(m_view, &CanvasView::zoomChanged,
                this, [this](double) { markViewDirty(); });
        connect(m_view, &CanvasView::panChanged,
                this, [this](const QPointF&) { markViewDirty(); });
    }
}

void CanvasDocumentServiceImpl::detachFromCanvasObjects()
{
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    if (m_view)
        disconnect(m_view, nullptr, this, nullptr);
    m_document = nullptr;
    m_view = nullptr;
}

void CanvasDocumentServiceImpl::markContentDirty()
{
    if (!hasOpenDocument() || m_loading)
        return;

    const bool wasDirty = hasPendingChanges();
    ++m_changeRevision;
    m_contentDirty = true;
    emitDirtyStateIfChanged(wasDirty);

    m_contentSaveDebounce.trigger();
}

void CanvasDocumentServiceImpl::markViewDirty()
{
    if (!hasOpenDocument() || m_loading)
        return;

    const bool wasDirty = hasPendingChanges();
    ++m_changeRevision;
    m_viewDirty = true;
    emitDirtyStateIfChanged(wasDirty);

    if (m_contentDirty)
        return;
    m_viewSaveDebounce.trigger();
}

Utils::Result CanvasDocumentServiceImpl::saveActiveNow()
{
    const Utils::Result flushResult = flushAutosave(true);
    if (!flushResult)
        return flushResult;

    SaveSnapshot snapshot;
    const Utils::Result snapshotResult = buildSaveSnapshot(snapshot);
    if (!snapshotResult)
        return snapshotResult;

    const Utils::Result writeResult = writeSnapshot(snapshot);
    if (!writeResult)
        return writeResult;

    const bool wasDirty = hasPendingChanges();
    m_contentDirty = false;
    m_viewDirty = false;
    emit documentSaved(m_activeHandle, snapshot.persistencePath);
    emitDirtyStateIfChanged(wasDirty);
    return Utils::Result::success();
}

Utils::Result CanvasDocumentServiceImpl::saveIfHandleMatches(const Canvas::Api::CanvasDocumentHandle& handle)
{
    if (!hasOpenDocument())
        return Utils::Result::failure(QStringLiteral("No canvas document is active."));
    if (!handleMatches(handle, m_activeHandle))
        return Utils::Result::failure(QStringLiteral("Canvas document handle is not active."));
    m_contentSaveDebounce.cancel();
    m_viewSaveDebounce.cancel();
    return saveActiveNow();
}

QString CanvasDocumentServiceImpl::resolvePersistencePath(const QString& bundlePath,
                                                          const QString& requestedPath) const
{
    if (bundlePath.trimmed().isEmpty())
        return {};

    const QString cleanedRequested = requestedPath.trimmed();
    if (cleanedRequested.isEmpty())
        return QDir(bundlePath).filePath(QStringLiteral("canvas/document.json"));

    const QFileInfo info(cleanedRequested);
    if (info.isAbsolute())
        return cleanedPath(info.absoluteFilePath());

    return cleanedPath(QDir(bundlePath).filePath(cleanedRequested));
}

QString CanvasDocumentServiceImpl::normalizeBundlePath(const QString& path)
{
    const QString cleaned = cleanedPath(path);
    if (cleaned.isEmpty())
        return {};
    return cleanedPath(Utils::DocumentBundle::normalizeBundlePath(cleaned));
}

bool CanvasDocumentServiceImpl::handleMatches(const Canvas::Api::CanvasDocumentHandle& lhs,
                                              const Canvas::Api::CanvasDocumentHandle& rhs)
{
    if (!lhs.isValid() || !rhs.isValid())
        return false;
    return lhs.id == rhs.id
           && cleanedPath(lhs.bundlePath) == cleanedPath(rhs.bundlePath)
           && cleanedPath(lhs.persistencePath) == cleanedPath(rhs.persistencePath);
}

bool CanvasDocumentServiceImpl::hasPendingChanges() const
{
    return m_contentDirty || m_viewDirty;
}

Utils::Result CanvasDocumentServiceImpl::buildSaveSnapshot(SaveSnapshot& outSnapshot) const
{
    outSnapshot = {};
    if (!hasOpenDocument())
        return Utils::Result::failure(QStringLiteral("No canvas document is active."));
    if (!m_document)
        return Utils::Result::failure(QStringLiteral("Canvas document is not available."));

    outSnapshot.handle = m_activeHandle;
    outSnapshot.persistencePath = m_activeHandle.persistencePath;
    outSnapshot.revision = m_changeRevision;
    outSnapshot.payload = CanvasDocumentJsonSerializer::serialize(*m_document,
                                                                  m_view,
                                                                  m_activeMetadata);
    return Utils::Result::success();
}

Utils::Result CanvasDocumentServiceImpl::writeSnapshot(const SaveSnapshot& snapshot)
{
    const Utils::Result parentResult = ensureParentDirectory(snapshot.persistencePath);
    if (!parentResult)
        return parentResult;

    return Utils::JsonFileUtils::writeObjectAtomic(snapshot.persistencePath,
                                                   snapshot.payload,
                                                   QJsonDocument::Indented);
}

void CanvasDocumentServiceImpl::requestAutosave()
{
    if (!hasOpenDocument())
        return;

    m_autosaveRequested = true;
    if (m_autosaveInFlight)
        return;

    SaveSnapshot snapshot;
    const Utils::Result snapshotResult = buildSaveSnapshot(snapshot);
    if (!snapshotResult) {
        m_autosaveRequested = false;
        qCWarning(canvasdoclog).noquote()
            << "CanvasDocumentService: autosave snapshot failed:"
            << snapshotResult.errors.join("\n");
        emit autosaveIdle();
        return;
    }

    m_autosaveRequested = false;
    startAutosave(std::move(snapshot));
}

void CanvasDocumentServiceImpl::startAutosave(SaveSnapshot snapshot)
{
    m_autosaveInFlight = true;
    Utils::Async::run<Utils::Result>(this,
                                     [snapshot]() {
                                         return writeSnapshot(snapshot);
                                     },
                                     [this, snapshot](Utils::Result writeResult) mutable {
                                         handleAutosaveFinished(snapshot, std::move(writeResult));
                                     });
}

void CanvasDocumentServiceImpl::handleAutosaveFinished(const SaveSnapshot& snapshot,
                                                       Utils::Result writeResult)
{
    m_autosaveInFlight = false;

    if (!writeResult) {
        qCWarning(canvasdoclog).noquote()
            << "CanvasDocumentService: autosave failed:"
            << writeResult.errors.join("\n");
    } else if (handleMatches(snapshot.handle, m_activeHandle) && snapshot.revision == m_changeRevision) {
        const bool wasDirty = hasPendingChanges();
        m_contentDirty = false;
        m_viewDirty = false;
        emit documentSaved(m_activeHandle, snapshot.persistencePath);
        emitDirtyStateIfChanged(wasDirty);
    }

    if (m_autosaveRequested && hasOpenDocument()) {
        SaveSnapshot nextSnapshot;
        const Utils::Result snapshotResult = buildSaveSnapshot(nextSnapshot);
        if (!snapshotResult) {
            m_autosaveRequested = false;
            qCWarning(canvasdoclog).noquote()
                << "CanvasDocumentService: queued autosave snapshot failed:"
                << snapshotResult.errors.join("\n");
            emit autosaveIdle();
            return;
        }

        m_autosaveRequested = false;
        startAutosave(std::move(nextSnapshot));
        return;
    }

    emit autosaveIdle();
}

Utils::Result CanvasDocumentServiceImpl::flushAutosave(bool discardPendingRequest)
{
    if (discardPendingRequest)
        m_autosaveRequested = false;

    if (!m_autosaveInFlight)
        return Utils::Result::success();

    QEventLoop loop;
    const QMetaObject::Connection connection = connect(this,
                                                       &CanvasDocumentServiceImpl::autosaveIdle,
                                                       &loop,
                                                       &QEventLoop::quit);
    while (m_autosaveInFlight)
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    disconnect(connection);
    return Utils::Result::success();
}

void CanvasDocumentServiceImpl::emitDirtyStateIfChanged(bool previousDirty)
{
    const bool currentDirty = hasPendingChanges();
    if (previousDirty == currentDirty)
        return;
    emit documentDirtyChanged(m_activeHandle, currentDirty);
}

} // namespace Canvas::Internal
