// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/state/AieKernelsPanelState.hpp"

#include <utils/PathUtils.hpp>

#include <QtCore/QJsonObject>
#include <QtCore/QtGlobal>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kStateName = u"aie/kernelsPanelState"_s;
const QString kRootsKey = u"roots"_s;
const QString kGlobalRootKey = u"__global__"_s;

const QString kSearchTextKey = u"searchText"_s;
const QString kSelectedKernelIdKey = u"selectedKernelId"_s;
const QString kScrollValueKey = u"scrollValue"_s;

} // namespace

AieKernelsPanelState::AieKernelsPanelState()
    : m_env(makeEnvironment())
{
}

AieKernelsPanelState::AieKernelsPanelState(Utils::Environment environment)
    : m_env(std::move(environment))
{
}

Utils::Environment AieKernelsPanelState::makeEnvironment()
{
    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    return Utils::Environment(cfg);
}

AieKernelsPanelState::Snapshot AieKernelsPanelState::stateForWorkspaceRoot(const QString& workspaceRoot) const
{
    Snapshot snapshot;

    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status != Utils::DocumentLoadResult::Status::Ok)
        return snapshot;

    const QJsonObject rootsObject = loaded.object.value(kRootsKey).toObject();
    const QJsonObject rootObject = rootsObject.value(normalizedRootKey(workspaceRoot)).toObject();
    snapshot.hasPersistedValue = !rootObject.isEmpty();

    snapshot.searchText = rootObject.value(kSearchTextKey).toString();
    snapshot.selectedKernelId = rootObject.value(kSelectedKernelIdKey).toString().trimmed();
    snapshot.scrollValue = qMax(0, rootObject.value(kScrollValueKey).toInt(0));
    return snapshot;
}

void AieKernelsPanelState::setStateForWorkspaceRoot(const QString& workspaceRoot,
                                                    const Snapshot& snapshot)
{
    QJsonObject document;
    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status == Utils::DocumentLoadResult::Status::Ok)
        document = loaded.object;

    QJsonObject rootsObject = document.value(kRootsKey).toObject();

    QJsonObject rootObject;
    if (!snapshot.searchText.isEmpty())
        rootObject.insert(kSearchTextKey, snapshot.searchText);

    const QString selectedKernelId = snapshot.selectedKernelId.trimmed();
    if (!selectedKernelId.isEmpty())
        rootObject.insert(kSelectedKernelIdKey, selectedKernelId);

    if (snapshot.scrollValue > 0)
        rootObject.insert(kScrollValueKey, snapshot.scrollValue);

    const QString rootKey = normalizedRootKey(workspaceRoot);
    if (rootObject.isEmpty())
        rootsObject.remove(rootKey);
    else
        rootsObject.insert(rootKey, rootObject);

    document.insert(kRootsKey, rootsObject);
    m_env.saveState(Utils::EnvironmentScope::Global, kStateName, document);
}

QString AieKernelsPanelState::normalizedRootKey(const QString& workspaceRoot)
{
    const QString normalized = Utils::PathUtils::normalizePath(workspaceRoot);
    return normalized.isEmpty() ? kGlobalRootKey : normalized;
}

} // namespace Aie::Internal
