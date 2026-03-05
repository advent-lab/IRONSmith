// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/design/DesignState.hpp"

#include <utils/Result.hpp>

namespace Canvas {
class CanvasDocument;
class CanvasView;
}

namespace Aie::Internal {

AIEPLUGIN_EXPORT Utils::Result buildDesignStateFromCanvas(Canvas::CanvasDocument& doc,
                                                          Canvas::CanvasView* view,
                                                          const QJsonObject& metadata,
                                                          DesignState& out);

AIEPLUGIN_EXPORT Utils::Result applyDesignStateToCanvas(const DesignState& state,
                                                        Canvas::CanvasDocument& doc,
                                                        Canvas::CanvasView* view);

} // namespace Aie::Internal
