// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/state/AieWorkspaceState.hpp"

#include <utils/PathUtils.hpp>

#include <QtCore/QJsonObject>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kStateName = u"aie/workspaceState"_s;
const QString kRootsKey = u"roots"_s;
const QString kActiveBundlePathKey = u"activeBundlePath"_s;

QString normalizedPath(const QString& path)
{
    return Utils::PathUtils::normalizePath(path);
}

} // namespace

AieWorkspaceState::AieWorkspaceState()
    : m_env(makeEnvironment())
{
}

AieWorkspaceState::AieWorkspaceState(Utils::Environment environment)
    : m_env(std::move(environment))
{
}

Utils::Environment AieWorkspaceState::makeEnvironment()
{
    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    return Utils::Environment(cfg);
}

QString AieWorkspaceState::activeBundlePathForRoot(const QString& rootPath) const
{
    const QString root = normalizedPath(rootPath);
    if (root.isEmpty())
        return {};

    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status != Utils::DocumentLoadResult::Status::Ok)
        return {};

    const QJsonObject rootsObject = loaded.object.value(kRootsKey).toObject();
    const QJsonObject rootObject = rootsObject.value(root).toObject();
    return normalizedPath(rootObject.value(kActiveBundlePathKey).toString());
}

void AieWorkspaceState::setActiveBundlePathForRoot(const QString& rootPath, const QString& bundlePath)
{
    const QString root = normalizedPath(rootPath);
    if (root.isEmpty())
        return;

    const QString normalizedBundlePath = normalizedPath(bundlePath);

    QJsonObject document;
    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status == Utils::DocumentLoadResult::Status::Ok)
        document = loaded.object;

    QJsonObject rootsObject = document.value(kRootsKey).toObject();
    QJsonObject rootObject = rootsObject.value(root).toObject();

    if (normalizedBundlePath.isEmpty())
        rootObject.remove(kActiveBundlePathKey);
    else
        rootObject.insert(kActiveBundlePathKey, normalizedBundlePath);

    rootsObject.insert(root, rootObject);
    document.insert(kRootsKey, rootsObject);

    m_env.saveState(Utils::EnvironmentScope::Global, kStateName, document);
}

void AieWorkspaceState::clearRoot(const QString& rootPath)
{
    const QString root = normalizedPath(rootPath);
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

} // namespace Aie::Internal
