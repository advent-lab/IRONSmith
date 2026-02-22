// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QString>

namespace Aie::Internal {

class AieWorkspaceState final
{
public:
    AieWorkspaceState();
    explicit AieWorkspaceState(Utils::Environment environment);

    QString activeBundlePathForRoot(const QString& rootPath) const;
    void setActiveBundlePathForRoot(const QString& rootPath, const QString& bundlePath);
    void clearRoot(const QString& rootPath);

    static Utils::Environment makeEnvironment();

private:
    Utils::Environment m_env;
};

} // namespace Aie::Internal
