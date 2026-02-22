// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/utils/CanvasLinkingMode.hpp"

namespace Canvas::Support {

bool isHubLinkingMode(CanvasController::LinkingMode mode)
{
    return mode == CanvasController::LinkingMode::Split ||
           mode == CanvasController::LinkingMode::Join ||
           mode == CanvasController::LinkingMode::Broadcast;
}

bool linkingModeUsesObjectFifo(CanvasController::LinkingMode mode)
{
    return mode == CanvasController::LinkingMode::Fifo ||
           mode == CanvasController::LinkingMode::ForwardFifo;
}

QString linkingModeLabel(CanvasController::LinkingMode mode)
{
    switch (mode) {
        case CanvasController::LinkingMode::Split: return QStringLiteral("SPLIT");
        case CanvasController::LinkingMode::Join: return QStringLiteral("JOIN");
        case CanvasController::LinkingMode::Broadcast: return QStringLiteral("BROADCAST");
        case CanvasController::LinkingMode::Fifo: return QStringLiteral("FIFO");
        case CanvasController::LinkingMode::ForwardFifo: return QStringLiteral("FWD_FIFO");
        case CanvasController::LinkingMode::Normal: return QString();
    }
    return QString();
}

std::optional<LinkHubKind> linkHubKindForMode(CanvasController::LinkingMode mode)
{
    switch (mode) {
        case CanvasController::LinkingMode::Split: return LinkHubKind::Split;
        case CanvasController::LinkingMode::Join: return LinkHubKind::Join;
        case CanvasController::LinkingMode::Broadcast: return LinkHubKind::Broadcast;
        case CanvasController::LinkingMode::Normal:
        case CanvasController::LinkingMode::Fifo:
        case CanvasController::LinkingMode::ForwardFifo:
            return std::nullopt;
    }
    return std::nullopt;
}

LinkWireRole linkStartWireRole(CanvasController::LinkingMode mode)
{
    return mode == CanvasController::LinkingMode::Join
        ? LinkWireRole::Consumer
        : LinkWireRole::Producer;
}

LinkWireRole linkFinishWireRole(CanvasController::LinkingMode mode)
{
    return mode == CanvasController::LinkingMode::Join
        ? LinkWireRole::Producer
        : LinkWireRole::Consumer;
}

} // namespace Canvas::Support
