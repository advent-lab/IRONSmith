// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "codeeditor/state/CodeEditorWorkspaceState.hpp"

#include <utils/PathUtils.hpp>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QSet>

#include <algorithm>

namespace CodeEditor::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kStateName = u"codeEditor/workspaceState"_s;
const QString kRootsKey = u"roots"_s;
const QString kPanelOpenKey = u"panelOpen"_s;
const QString kZoomLevelKey = u"zoomLevel"_s;
const QString kOpenFilesKey = u"openFiles"_s;
const QString kActiveFilePathKey = u"activeFilePath"_s;

constexpr int kMinZoomLevel = -8;
constexpr int kMaxZoomLevel = 24;

QString normalizedRootPath(const QString& rootPath)
{
    return Utils::PathUtils::normalizePath(rootPath);
}

QString normalizedFilePath(const QString& filePath)
{
    return Utils::PathUtils::normalizePath(filePath);
}

int clampZoomLevel(int zoomLevel)
{
    return std::clamp(zoomLevel, kMinZoomLevel, kMaxZoomLevel);
}

} // namespace

CodeEditorWorkspaceState::CodeEditorWorkspaceState()
    : m_env(makeEnvironment())
{
}

CodeEditorWorkspaceState::CodeEditorWorkspaceState(Utils::Environment environment)
    : m_env(std::move(environment))
{
}

Utils::Environment CodeEditorWorkspaceState::makeEnvironment()
{
    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    return Utils::Environment(cfg);
}

CodeEditorWorkspaceState::Snapshot CodeEditorWorkspaceState::loadForRoot(const QString& rootPath) const
{
    Snapshot snapshot;

    const QString root = normalizedRootPath(rootPath);
    if (root.isEmpty())
        return snapshot;

    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status != Utils::DocumentLoadResult::Status::Ok)
        return snapshot;

    const QJsonObject rootsObject = loaded.object.value(kRootsKey).toObject();
    const QJsonObject rootObject = rootsObject.value(root).toObject();
    if (rootObject.isEmpty())
        return snapshot;

    snapshot.panelOpen = rootObject.value(kPanelOpenKey).toBool(false);
    snapshot.zoomLevel = clampZoomLevel(rootObject.value(kZoomLevelKey).toInt(0));
    snapshot.activeFilePath = normalizedFilePath(rootObject.value(kActiveFilePathKey).toString());

    const QJsonArray openFilesArray = rootObject.value(kOpenFilesKey).toArray();
    snapshot.openFiles.reserve(openFilesArray.size());
    QSet<QString> seenFiles;
    for (const QJsonValue& value : openFilesArray) {
        const QString filePath = normalizedFilePath(value.toString());
        if (filePath.isEmpty() || seenFiles.contains(filePath))
            continue;
        seenFiles.insert(filePath);
        snapshot.openFiles.push_back(filePath);
    }

    if (!snapshot.activeFilePath.isEmpty() && !seenFiles.contains(snapshot.activeFilePath))
        snapshot.activeFilePath.clear();

    return snapshot;
}

void CodeEditorWorkspaceState::saveForRoot(const QString& rootPath, const Snapshot& snapshot)
{
    const QString root = normalizedRootPath(rootPath);
    if (root.isEmpty())
        return;

    QJsonObject document;
    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status == Utils::DocumentLoadResult::Status::Ok)
        document = loaded.object;

    QJsonObject rootsObject = document.value(kRootsKey).toObject();

    QJsonObject rootObject;
    rootObject.insert(kPanelOpenKey, snapshot.panelOpen);
    rootObject.insert(kZoomLevelKey, clampZoomLevel(snapshot.zoomLevel));

    const QString normalizedActivePath = normalizedFilePath(snapshot.activeFilePath);
    if (!normalizedActivePath.isEmpty())
        rootObject.insert(kActiveFilePathKey, normalizedActivePath);
    else
        rootObject.remove(kActiveFilePathKey);

    QJsonArray openFilesArray;
    QSet<QString> seenFiles;
    for (const QString& filePath : snapshot.openFiles) {
        const QString normalizedPath = normalizedFilePath(filePath);
        if (normalizedPath.isEmpty() || seenFiles.contains(normalizedPath))
            continue;
        seenFiles.insert(normalizedPath);
        openFilesArray.push_back(normalizedPath);
    }
    rootObject.insert(kOpenFilesKey, openFilesArray);

    rootsObject.insert(root, rootObject);
    document.insert(kRootsKey, rootsObject);

    m_env.saveState(Utils::EnvironmentScope::Global, kStateName, document);
}

void CodeEditorWorkspaceState::clearForRoot(const QString& rootPath)
{
    const QString root = normalizedRootPath(rootPath);
    if (root.isEmpty())
        return;

    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status != Utils::DocumentLoadResult::Status::Ok)
        return;

    QJsonObject document = loaded.object;
    QJsonObject rootsObject = document.value(kRootsKey).toObject();
    if (!rootsObject.contains(root))
        return;

    rootsObject.remove(root);
    document.insert(kRootsKey, rootsObject);
    m_env.saveState(Utils::EnvironmentScope::Global, kStateName, document);
}

} // namespace CodeEditor::Internal
