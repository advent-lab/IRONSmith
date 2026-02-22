// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/api/CanvasStyleTypes.hpp"

#include <QtCore/QHash>
#include <QtCore/QString>

namespace Aie::Internal {

QHash<QString, Canvas::Api::CanvasBlockStyle> createDefaultBlockStyles();

} // namespace Aie::Internal