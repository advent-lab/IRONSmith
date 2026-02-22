// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasController.hpp"
#include "canvas/CanvasGlobal.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"
#include "canvas/utils/CanvasLinkWireStyle.hpp"

#include <QtCore/QString>

#include <optional>

namespace Canvas::Support {

CANVAS_EXPORT bool isHubLinkingMode(CanvasController::LinkingMode mode);
CANVAS_EXPORT bool linkingModeUsesObjectFifo(CanvasController::LinkingMode mode);
CANVAS_EXPORT QString linkingModeLabel(CanvasController::LinkingMode mode);
CANVAS_EXPORT std::optional<LinkHubKind> linkHubKindForMode(CanvasController::LinkingMode mode);
CANVAS_EXPORT LinkWireRole linkStartWireRole(CanvasController::LinkingMode mode);
CANVAS_EXPORT LinkWireRole linkFinishWireRole(CanvasController::LinkingMode mode);

} // namespace Canvas::Support

