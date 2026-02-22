// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "projectexplorer/state/ProjectExplorerSidebarState.hpp"

namespace ProjectExplorer::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kPanelOpenKey = u"projectExplorer/panelOpen"_s;

} // namespace

ProjectExplorerSidebarState::ProjectExplorerSidebarState()
    : m_env(makeEnvironment())
{
}

ProjectExplorerSidebarState::ProjectExplorerSidebarState(Utils::Environment environment)
    : m_env(std::move(environment))
{
}

Utils::Environment ProjectExplorerSidebarState::makeEnvironment()
{
    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    return Utils::Environment(cfg);
}

bool ProjectExplorerSidebarState::panelOpen() const
{
    return m_env.setting(Utils::EnvironmentScope::Global, kPanelOpenKey, false).toBool();
}

void ProjectExplorerSidebarState::setPanelOpen(bool open)
{
    m_env.setSetting(Utils::EnvironmentScope::Global, kPanelOpenKey, open);
}

} // namespace ProjectExplorer::Internal
