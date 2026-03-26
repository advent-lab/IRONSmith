// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/state/AieKernelAssignmentState.hpp"

#include <QtCore/QJsonObject>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kStateName = u"aie/kernelAssignmentState"_s;
const QString kConfirmReassignmentKey = u"confirmReassignment"_s;

} // namespace

AieKernelAssignmentState::AieKernelAssignmentState()
    : m_env(makeEnvironment())
{
}

AieKernelAssignmentState::AieKernelAssignmentState(Utils::Environment environment)
    : m_env(std::move(environment))
{
}

Utils::Environment AieKernelAssignmentState::makeEnvironment()
{
    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    return Utils::Environment(cfg);
}

bool AieKernelAssignmentState::confirmReassignment() const
{
    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status != Utils::DocumentLoadResult::Status::Ok)
        return true;

    const QJsonValue value = loaded.object.value(kConfirmReassignmentKey);
    return value.isBool() ? value.toBool(true) : true;
}

void AieKernelAssignmentState::setConfirmReassignment(bool enabled)
{
    QJsonObject document;
    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status == Utils::DocumentLoadResult::Status::Ok)
        document = loaded.object;

    document.insert(kConfirmReassignmentKey, enabled);
    m_env.saveState(Utils::EnvironmentScope::Global, kStateName, document);
}

} // namespace Aie::Internal
