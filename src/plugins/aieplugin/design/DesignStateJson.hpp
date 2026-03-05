// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/design/DesignState.hpp"

#include <utils/Result.hpp>

#include <QtCore/QJsonObject>

namespace Aie::Internal {

AIEPLUGIN_EXPORT Utils::Result parseDesignState(const QJsonObject& json, DesignState& out);
AIEPLUGIN_EXPORT QJsonObject serializeDesignState(const DesignState& state);

} // namespace Aie::Internal
