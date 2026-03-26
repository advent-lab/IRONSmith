// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

namespace Aie::Internal {

class AIEPLUGIN_EXPORT AieKernelAssignmentState final
{
public:
    AieKernelAssignmentState();
    explicit AieKernelAssignmentState(Utils::Environment environment);

    bool confirmReassignment() const;
    void setConfirmReassignment(bool enabled);

    static Utils::Environment makeEnvironment();

private:
    Utils::Environment m_env;
};

} // namespace Aie::Internal
