// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/state/AieSidebarState.hpp"

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kPanelOpenKeyPrefix = u"aie/sidebar/open/"_s;

} // namespace

AieSidebarState::AieSidebarState()
    : m_env(makeEnvironment())
{
}

AieSidebarState::AieSidebarState(Utils::Environment environment)
    : m_env(std::move(environment))
{
}

Utils::Environment AieSidebarState::makeEnvironment()
{
    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    return Utils::Environment(cfg);
}

bool AieSidebarState::panelOpen(const QString& panelId) const
{
    return m_env.setting(Utils::EnvironmentScope::Global, panelOpenKey(panelId), false).toBool();
}

void AieSidebarState::setPanelOpen(const QString& panelId, bool open)
{
    m_env.setSetting(Utils::EnvironmentScope::Global, panelOpenKey(panelId), open);
}

QString AieSidebarState::panelOpenKey(const QString& panelId)
{
    return kPanelOpenKeyPrefix + panelId;
}

} // namespace Aie::Internal
