// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QString>

namespace Aie::Internal {

class AieSidebarState final
{
public:
    AieSidebarState();
    explicit AieSidebarState(Utils::Environment environment);

    bool panelOpen(const QString& panelId) const;
    void setPanelOpen(const QString& panelId, bool open);

    static Utils::Environment makeEnvironment();

private:
    static QString panelOpenKey(const QString& panelId);

    Utils::Environment m_env;
};

} // namespace Aie::Internal
