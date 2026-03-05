// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/NpuProfile.hpp"

#include "canvas/api/CanvasGridTypes.hpp"
#include "utils/ui/GridSpec.hpp"

#include <utils/Result.hpp>

#include <QtCore/QVector>

namespace Aie {

struct CanvasGridModel final {
    Utils::GridSpec gridSpec;
    QVector<Canvas::Api::CanvasBlockSpec> blocks;
};

AIEPLUGIN_EXPORT Utils::Result buildCanvasGridModel(const NpuProfile& profile, CanvasGridModel& out);

} // namespace Aie