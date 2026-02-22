// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/CoreGlobal.hpp"
#include "core/api/SidebarToolSpec.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QByteArray>

QT_BEGIN_NAMESPACE
class QMainWindow;
QT_END_NAMESPACE

namespace Core::Internal {

class CORE_EXPORT CoreUiState final
{
public:
    CoreUiState();
    explicit CoreUiState(Utils::Environment environment);

    int sidebarPanelWidth(SidebarSide side, SidebarFamily family, int fallback) const;
    void setSidebarPanelWidth(SidebarSide side, SidebarFamily family, int width);

    QByteArray mainWindowGeometry() const;
    void setMainWindowGeometry(const QByteArray& geometry);

    static Utils::Environment makeEnvironment();

private:
    static QString sidebarWidthKey(SidebarSide side, SidebarFamily family);

    Utils::Environment m_env;
};

} // namespace Core::Internal
