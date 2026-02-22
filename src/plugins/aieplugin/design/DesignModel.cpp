// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/design/DesignModel.hpp"

namespace Aie::Internal {

bool DesignModel::hasDesignState() const
{
    return !legacyDesignState.isEmpty();
}

} // namespace Aie::Internal
