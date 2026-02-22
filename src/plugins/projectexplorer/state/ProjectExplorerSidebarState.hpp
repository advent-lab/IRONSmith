// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

namespace ProjectExplorer::Internal {

class PROJECTEXPLORER_EXPORT ProjectExplorerSidebarState final
{
public:
    ProjectExplorerSidebarState();
    explicit ProjectExplorerSidebarState(Utils::Environment environment);

    bool panelOpen() const;
    void setPanelOpen(bool open);

    static Utils::Environment makeEnvironment();

private:
    Utils::Environment m_env;
};

} // namespace ProjectExplorer::Internal
