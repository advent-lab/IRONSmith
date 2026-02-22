// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "codeeditor/internal/CodeEditorServiceImpl.hpp"

#include "codeeditor/CodeEditorGlobal.hpp"
#include "codeeditor/Constants.hpp"

#include <core/api/ISidebarRegistry.hpp>
#include <core/ui/IUiHost.hpp>
#include <utils/Comparisons.hpp>
#include <utils/PathUtils.hpp>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QSaveFile>
#include <QtCore/QUuid>
#include <QtCore/QtGlobal>

#include <algorithm>
#include <utility>

namespace CodeEditor::Internal {

namespace {

#if defined(Q_OS_WIN)
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

constexpr int kMinZoomLevel = -8;
constexpr int kMaxZoomLevel = 24;

QString pathLookupKey(const QString& absolutePath)
{
#if defined(Q_OS_WIN)
    return absolutePath.toLower();
#else
    return absolutePath;
#endif
}

bool pathsEqual(const QString& lhs, const QString& rhs)
{
    return QString::compare(lhs, rhs, kPathCase) == 0;
}

bool containsWatchedPath(const QStringList& watchedPaths, const QString& path)
{
    for (const QString& watchedPath : watchedPaths) {
        if (pathsEqual(watchedPath, path))
            return true;
    }

    return false;
}

QString watchedPathMatch(const QStringList& watchedPaths, const QString& path)
{
    for (const QString& watchedPath : watchedPaths) {
        if (pathsEqual(watchedPath, path))
            return watchedPath;
    }

    return {};
}

} // namespace

CodeEditorServiceImpl::CodeEditorServiceImpl(QObject* parent)
    : CodeEditor::Api::ICodeEditorService(parent)
{
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &CodeEditorServiceImpl::handleWatchedFileChanged);
}

void CodeEditorServiceImpl::setUiHost(Core::IUiHost* uiHost)
{
    if (!uiHost) {
        qCWarning(ceditorlog) << "CodeEditorServiceImpl::setUiHost: uiHost is null";
        return;
    }

    m_uiHost = uiHost;
}

Utils::Result CodeEditorServiceImpl::openFile(const CodeEditor::Api::CodeEditorOpenRequest& request,
                                              CodeEditor::Api::CodeEditorSessionHandle& outHandle)
{
    outHandle = {};

    const QString absolutePath = normalizeAbsolutePath(request.filePath);
    if (absolutePath.isEmpty())
        return Utils::Result::failure(QStringLiteral("CodeEditor: file path is empty."));

    const QFileInfo info(absolutePath);
    if (!info.exists() || !info.isFile()) {
        return Utils::Result::failure(
            QStringLiteral("CodeEditor: file does not exist: %1").arg(absolutePath));
    }

    const QString pathKey = pathLookupKey(absolutePath);
    const QString existingId = m_sessionIdByPath.value(pathKey);
    if (!existingId.isEmpty()) {
        if (const auto* existing = sessionById(existingId)) {
            outHandle = existing->handle;
            if (request.activate || !m_active.isValid()) {
                const Utils::Result activateResult = setActiveFileById(existingId);
                if (!activateResult)
                    return activateResult;
            }
            if (request.activate)
                revealCodeSidebar();

            qCInfo(ceditorlog) << "CodeEditor: reused file session:" << outHandle.filePath
                               << outHandle.languageId;
            return Utils::Result::success();
        }
    }

    QString text;
    const Utils::Result readResult = readTextFile(absolutePath, text, Constants::kSessionOpenMaxBytes);
    if (!readResult)
        return readResult;

    SessionState state;
    state.handle.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state.handle.filePath = absolutePath;
    state.handle.languageId = resolveLanguageId(request.languageHint, absolutePath);
    state.forcedReadOnly = request.readOnly;
    state.handle.readOnly = state.forcedReadOnly || !info.isWritable();
    state.persistedText = text;
    state.currentText = text;
    state.dirty = false;

    const QString sessionId = state.handle.id;
    m_sessionIdByPath.insert(pathLookupKey(state.handle.filePath), sessionId);
    m_sessionsById.insert(sessionId, state);
    m_openOrder.push_back(sessionId);

    watchPath(state.handle.filePath);

    outHandle = state.handle;
    emit fileOpened(outHandle);

    if (request.activate || !m_active.isValid()) {
        const Utils::Result activateResult = setActiveFileById(sessionId);
        if (!activateResult)
            return activateResult;
    }
    if (request.activate)
        revealCodeSidebar();

    qCInfo(ceditorlog) << "CodeEditor: opened file session:" << outHandle.filePath << outHandle.languageId;
    return Utils::Result::success();
}

Utils::Result CodeEditorServiceImpl::closeFile(const CodeEditor::Api::CodeEditorSessionHandle& handle,
                                               CodeEditor::Api::CodeEditorCloseReason reason)
{
    if (!handle.isValid())
        return Utils::Result::failure(QStringLiteral("CodeEditor: invalid session handle."));

    SessionState* state = mutableSessionById(handle.id);
    if (!state)
        return Utils::Result::failure(QStringLiteral("CodeEditor: session is not active."));

    const CodeEditor::Api::CodeEditorSessionHandle closed = state->handle;
    if (state->view) {
        state->currentText = state->view->text();
        state->view->deleteLater();
        state->view = nullptr;
    }

    m_sessionIdByPath.remove(pathLookupKey(closed.filePath));
    m_sessionsById.remove(handle.id);
    m_openOrder.erase(std::remove(m_openOrder.begin(), m_openOrder.end(), handle.id), m_openOrder.end());
    unwatchPathIfUnused(closed.filePath);

    emit fileClosed(closed, reason);

    if (m_active.isValid() && m_active.id == closed.id) {
        if (m_openOrder.isEmpty()) {
            setActiveFileById(QString());
        } else {
            setActiveFileById(m_openOrder.constLast());
        }
    }

    return Utils::Result::success();
}

Utils::Result CodeEditorServiceImpl::closeAllFiles(CodeEditor::Api::CodeEditorCloseReason reason)
{
    const QVector<CodeEditor::Api::CodeEditorSessionHandle> sessions = openFiles();
    Utils::Result result = Utils::Result::success();
    for (const auto& handle : sessions) {
        const Utils::Result closed = closeFile(handle, reason);
        if (!closed) {
            for (const QString& error : closed.errors)
                result.addError(error);
        }
    }
    return result;
}

Utils::Result CodeEditorServiceImpl::saveFile(const CodeEditor::Api::CodeEditorSessionHandle& handle)
{
    if (!handle.isValid())
        return Utils::Result::failure(QStringLiteral("CodeEditor: invalid session handle."));

    SessionState* state = mutableSessionById(handle.id);
    if (!state)
        return Utils::Result::failure(QStringLiteral("CodeEditor: session is not active."));

    const QFileInfo fileInfo(state->handle.filePath);
    state->handle.readOnly = state->forcedReadOnly || !fileInfo.isWritable();

    if (state->view)
        state->view->setReadOnlyMode(state->handle.readOnly);

    if (state->handle.readOnly) {
        return Utils::Result::failure(
            QStringLiteral("CodeEditor: file is read-only: %1").arg(state->handle.filePath));
    }

    if (state->view)
        state->currentText = state->view->text();

    const Utils::Result writeResult = writeTextFile(state->handle.filePath, state->currentText);
    if (!writeResult)
        return writeResult;

    state->persistedText = state->currentText;
    updateDirtyState(*state, false);
    watchPath(state->handle.filePath);
    qCInfo(ceditorlog) << "CodeEditor: saved file session:" << state->handle.filePath;
    return Utils::Result::success();
}

Utils::Result CodeEditorServiceImpl::saveAllFiles()
{
    const QVector<CodeEditor::Api::CodeEditorSessionHandle> sessions = openFiles();
    Utils::Result result = Utils::Result::success();
    for (const auto& handle : sessions) {
        const Utils::Result saved = saveFile(handle);
        if (!saved) {
            for (const QString& error : saved.errors)
                result.addError(error);
        }
    }
    return result;
}

Utils::Result CodeEditorServiceImpl::setActiveFile(const CodeEditor::Api::CodeEditorSessionHandle& handle)
{
    if (!handle.isValid())
        return Utils::Result::failure(QStringLiteral("CodeEditor: invalid session handle."));
    return setActiveFileById(handle.id);
}

Utils::Result CodeEditorServiceImpl::updateFilePath(const CodeEditor::Api::CodeEditorSessionHandle& handle,
                                                    const QString& newFilePath)
{
    if (!handle.isValid())
        return Utils::Result::failure(QStringLiteral("CodeEditor: invalid session handle."));

    SessionState* state = mutableSessionById(handle.id);
    if (!state)
        return Utils::Result::failure(QStringLiteral("CodeEditor: session is not active."));

    const QString newAbsolutePath = normalizeAbsolutePath(newFilePath);
    if (newAbsolutePath.isEmpty())
        return Utils::Result::failure(QStringLiteral("CodeEditor: new file path is empty."));

    const QString oldAbsolutePath = state->handle.filePath;
    if (QString::compare(oldAbsolutePath, newAbsolutePath, kPathCase) == 0)
        return Utils::Result::success();

    const QString newPathKey = pathLookupKey(newAbsolutePath);
    const QString existingId = m_sessionIdByPath.value(newPathKey);
    if (!existingId.isEmpty() && existingId != state->handle.id) {
        return Utils::Result::failure(
            QStringLiteral("CodeEditor: target path already open: %1").arg(newAbsolutePath));
    }

    m_sessionIdByPath.remove(pathLookupKey(oldAbsolutePath));
    m_sessionIdByPath.insert(newPathKey, state->handle.id);

    state->handle.filePath = newAbsolutePath;
    state->handle.languageId = resolveLanguageId(QString(), newAbsolutePath);
    state->handle.readOnly = state->forcedReadOnly || !QFileInfo(newAbsolutePath).isWritable();

    if (state->view) {
        state->view->setPathHint(newAbsolutePath);
        state->view->setLanguageId(state->handle.languageId);
        state->view->setReadOnlyMode(state->handle.readOnly);
    }

    unwatchPathIfUnused(oldAbsolutePath);
    watchPath(newAbsolutePath);

    if (m_active.isValid() && m_active.id == state->handle.id) {
        m_active = state->handle;
        emit activeFileChanged(m_active);
    }

    emit filePathChanged(state->handle, oldAbsolutePath, newAbsolutePath);
    return Utils::Result::success();
}

CodeEditor::Api::CodeEditorSessionHandle CodeEditorServiceImpl::activeFile() const
{
    return m_active;
}

bool CodeEditorServiceImpl::hasOpenFile() const
{
    return !m_openOrder.isEmpty();
}

QVector<CodeEditor::Api::CodeEditorSessionHandle> CodeEditorServiceImpl::openFiles() const
{
    QVector<CodeEditor::Api::CodeEditorSessionHandle> sessions;
    sessions.reserve(m_openOrder.size());
    for (const QString& id : m_openOrder) {
        if (const auto* state = sessionById(id))
            sessions.push_back(state->handle);
    }
    return sessions;
}

bool CodeEditorServiceImpl::isDirty(const CodeEditor::Api::CodeEditorSessionHandle& handle) const
{
    if (!handle.isValid())
        return false;

    const SessionState* state = sessionById(handle.id);
    return state ? state->dirty : false;
}

QWidget* CodeEditorServiceImpl::widgetForSession(const Api::CodeEditorSessionHandle& handle) const
{
    if (!handle.isValid())
        return nullptr;

    return const_cast<CodeEditorServiceImpl*>(this)->ensureView(handle.id);
}

QWidget* CodeEditorServiceImpl::createQuickView(const CodeEditor::Api::CodeEditorQuickViewRequest& request,
                                                QWidget* parent)
{
    auto* view = new CodeEditorTextView(parent);
    view->setReadOnlyMode(true);
    view->setZoomLevel(m_globalZoomLevel);

    const QString absolutePath = normalizeAbsolutePath(request.filePath);
    const QString languageId = resolveLanguageId(request.languageHint, absolutePath);
    view->setLanguageId(languageId);
    view->setPathHint(absolutePath);

    QString text;
    const Utils::Result result = readTextFile(absolutePath, text, Constants::kQuickViewMaxBytes);
    if (!result) {
        view->setText(QStringLiteral("Unable to open '%1'.\n\n%2")
                          .arg(absolutePath, result.errors.join(QStringLiteral("\n"))));
        return view;
    }

    view->setText(text);
    return view;
}

bool CodeEditorServiceImpl::supportsLanguage(const QString& languageId) const
{
    const QString id = languageId.trimmed().toLower();
    if (id.isEmpty())
        return false;

    return Constants::kSupportedLanguages.contains(id);
}

int CodeEditorServiceImpl::zoomLevel() const
{
    return m_globalZoomLevel;
}

void CodeEditorServiceImpl::setZoomLevel(int level)
{
    const int clampedLevel = std::clamp(level, kMinZoomLevel, kMaxZoomLevel);
    if (m_globalZoomLevel == clampedLevel)
        return;

    m_globalZoomLevel = clampedLevel;
    for (auto& state : m_sessionsById) {
        if (state.view)
            state.view->setZoomLevel(m_globalZoomLevel);
    }

    emit zoomLevelChanged(m_globalZoomLevel);
}

Utils::Result CodeEditorServiceImpl::setActiveFileById(const QString& id)
{
    if (id.isEmpty()) {
        if (m_active.isValid()) {
            m_active = {};
            emit activeFileChanged(m_active);
        }
        return Utils::Result::success();
    }

    const SessionState* state = sessionById(id);
    if (!state)
        return Utils::Result::failure(QStringLiteral("CodeEditor: session is not active."));

    if (m_active.isValid() && m_active.id == id)
        return Utils::Result::success();

    m_active = state->handle;
    emit activeFileChanged(m_active);
    return Utils::Result::success();
}

void CodeEditorServiceImpl::revealCodeSidebar()
{
    if (!m_uiHost)
        return;

    Core::ISidebarRegistry* sidebarRegistry = m_uiHost->sidebarRegistry();
    if (!sidebarRegistry)
        return;

    sidebarRegistry->requestShowTool(QString::fromLatin1(Constants::kSidebarToolId));
}

QString CodeEditorServiceImpl::normalizeAbsolutePath(const QString& path)
{
    const QString cleaned = Utils::PathUtils::normalizePath(path);
    if (cleaned.isEmpty())
        return {};

    return QDir::cleanPath(QFileInfo(cleaned).absoluteFilePath());
}

void CodeEditorServiceImpl::updateDirtyState(SessionState& state, bool dirty)
{
    if (state.dirty == dirty)
        return;

    state.dirty = dirty;
    emit fileDirtyStateChanged(state.handle, dirty);
}

void CodeEditorServiceImpl::connectSessionView(const QString& sessionId, CodeEditorTextView* view)
{
    if (!view)
        return;

    connect(view, &CodeEditorTextView::textEdited, this, [this, sessionId]() {
        SessionState* state = mutableSessionById(sessionId);
        if (!state || !state->view)
            return;

        state->currentText = state->view->text();
        updateDirtyState(*state, state->currentText != state->persistedText);
    });

    connect(view, &QObject::destroyed, this, [this, sessionId]() {
        SessionState* state = mutableSessionById(sessionId);
        if (!state)
            return;
        state->view = nullptr;
    });

    connect(view, &CodeEditorTextView::zoomLevelChanged, this, [this](int level) {
        setZoomLevel(level);
    });
}

CodeEditorTextView* CodeEditorServiceImpl::ensureView(const QString& sessionId)
{
    SessionState* state = mutableSessionById(sessionId);
    if (!state)
        return nullptr;

    if (state->view)
        return state->view;

    auto* view = new CodeEditorTextView();
    view->setPathHint(state->handle.filePath);
    view->setLanguageId(state->handle.languageId);
    view->setReadOnlyMode(state->handle.readOnly);
    view->setText(state->currentText);
    view->setZoomLevel(m_globalZoomLevel);
    state->view = view;

    connectSessionView(sessionId, view);
    return view;
}

CodeEditorServiceImpl::SessionState* CodeEditorServiceImpl::mutableSessionById(const QString& id)
{
    auto it = m_sessionsById.find(id);
    if (it == m_sessionsById.end())
        return nullptr;
    return &it.value();
}

const CodeEditorServiceImpl::SessionState* CodeEditorServiceImpl::sessionById(const QString& id) const
{
    const auto it = m_sessionsById.constFind(id);
    if (it == m_sessionsById.cend())
        return nullptr;
    return &it.value();
}

void CodeEditorServiceImpl::watchPath(const QString& absolutePath)
{
    const QString cleaned = normalizeAbsolutePath(absolutePath);
    if (cleaned.isEmpty())
        return;

    const QFileInfo info(cleaned);
    if (!info.exists() || !info.isFile())
        return;

    const QStringList watched = m_fileWatcher.files();
    if (!containsWatchedPath(watched, cleaned))
        m_fileWatcher.addPath(cleaned);
}

void CodeEditorServiceImpl::unwatchPathIfUnused(const QString& absolutePath)
{
    const QString cleaned = normalizeAbsolutePath(absolutePath);
    if (cleaned.isEmpty())
        return;

    for (const auto& state : std::as_const(m_sessionsById)) {
        if (pathsEqual(state.handle.filePath, cleaned))
            return;
    }

    const QString matchedPath = watchedPathMatch(m_fileWatcher.files(), cleaned);
    if (!matchedPath.isEmpty())
        m_fileWatcher.removePath(matchedPath);
}

void CodeEditorServiceImpl::handleWatchedFileChanged(const QString& path)
{
    const QString cleanedPath = normalizeAbsolutePath(path);
    if (cleanedPath.isEmpty())
        return;

    QVector<CodeEditor::Api::CodeEditorSessionHandle> affected;
    for (const auto& state : std::as_const(m_sessionsById)) {
        if (pathsEqual(state.handle.filePath, cleanedPath))
            affected.push_back(state.handle);
    }
    if (affected.isEmpty())
        return;

    const QFileInfo info(cleanedPath);
    if (!info.exists() || !info.isFile()) {
        for (const auto& handle : affected)
            closeFile(handle, CodeEditor::Api::CodeEditorCloseReason::FileDeleted);
        return;
    }

    QString text;
    const Utils::Result readResult = readTextFile(cleanedPath, text, Constants::kSessionOpenMaxBytes);
    if (!readResult) {
        qCWarning(ceditorlog).noquote()
            << QStringLiteral("CodeEditor: failed to reload modified file '%1': %2")
                   .arg(cleanedPath, readResult.errors.join(QStringLiteral("; ")));
        watchPath(cleanedPath);
        return;
    }

    for (const auto& handle : affected) {
        SessionState* state = mutableSessionById(handle.id);
        if (!state)
            continue;

        state->handle.readOnly = state->forcedReadOnly || !info.isWritable();
        if (state->view)
            state->view->setReadOnlyMode(state->handle.readOnly);

        if (state->dirty) {
            qCWarning(ceditorlog).noquote()
                << QStringLiteral("CodeEditor: on-disk file changed while session is dirty, skipping reload: %1")
                       .arg(state->handle.filePath);
            continue;
        }

        state->persistedText = text;
        state->currentText = text;
        if (state->view)
            state->view->setText(text);
        updateDirtyState(*state, false);
    }

    watchPath(cleanedPath);
}

QString CodeEditorServiceImpl::resolveLanguageId(const QString& hint, const QString& absolutePath)
{
    const QString cleanedHint = hint.trimmed().toLower();
    if (!cleanedHint.isEmpty())
        return cleanedHint;

    const QString suffix = Utils::PathUtils::extension(absolutePath).toLower();
    if (Utils::isOneOf(suffix, "c", "h"))
        return Constants::kLanguageIdC;

    if (Utils::isOneOf(suffix, "cc", "cpp", "cxx", "hpp", "hh", "hxx"))
        return Constants::kLanguageIdCpp;

    if (suffix == "py")
        return Constants::kLanguageIdPython;

    if (suffix == "json")
        return Constants::kLanguageIdJson;

    if (suffix == "xml")
        return Constants::kLanguageIdXml;

    return Constants::kLanguageIdText;
}

Utils::Result CodeEditorServiceImpl::readTextFile(const QString& absolutePath,
                                                  QString& outText,
                                                  quint64 maxBytes)
{
    outText.clear();

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly))
        return Utils::Result::failure(QStringLiteral("Failed to open file: %1").arg(absolutePath));

    if (maxBytes > 0) {
        const qint64 size = file.size();
        if (size >= 0 && static_cast<quint64>(size) > maxBytes) {
            return Utils::Result::failure(
                QStringLiteral("File exceeds supported size (%1 bytes): %2").arg(maxBytes).arg(absolutePath));
        }
    }

    const QByteArray bytes = file.read(maxBytes + 1);
    if (maxBytes > 0 && static_cast<quint64>(bytes.size()) > maxBytes) {
        return Utils::Result::failure(
            QStringLiteral("File exceeds supported size (%1 bytes): %2").arg(maxBytes).arg(absolutePath));
    }

    outText = QString::fromUtf8(bytes);
    return Utils::Result::success();
}

Utils::Result CodeEditorServiceImpl::writeTextFile(const QString& absolutePath, const QString& text)
{
    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly))
        return Utils::Result::failure(QStringLiteral("Failed to open file for writing: %1").arg(absolutePath));

    const QByteArray encoded = text.toUtf8();
    const qint64 written = file.write(encoded);
    if (written != encoded.size()) {
        file.cancelWriting();
        return Utils::Result::failure(QStringLiteral("Failed to write file: %1").arg(absolutePath));
    }

    if (!file.commit())
        return Utils::Result::failure(QStringLiteral("Failed to commit file write: %1").arg(absolutePath));

    return Utils::Result::success();
}

} // namespace CodeEditor::Internal
