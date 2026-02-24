// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QString>

namespace Aie::Internal {

class AIEPLUGIN_EXPORT AieKernelsPanelState final
{
public:
    struct Snapshot final {
        QString searchText;
        QString selectedKernelId;
        int scrollValue = 0;
        bool hasPersistedValue = false;
    };

    AieKernelsPanelState();
    explicit AieKernelsPanelState(Utils::Environment environment);

    Snapshot stateForWorkspaceRoot(const QString& workspaceRoot) const;
    void setStateForWorkspaceRoot(const QString& workspaceRoot, const Snapshot& snapshot);

    static Utils::Environment makeEnvironment();

private:
    static QString normalizedRootKey(const QString& workspaceRoot);

    Utils::Environment m_env;
};

} // namespace Aie::Internal
