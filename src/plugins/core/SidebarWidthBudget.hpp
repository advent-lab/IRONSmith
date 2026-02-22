// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/api/SidebarToolSpec.hpp"

#include <algorithm>
#include <optional>

namespace Core::Internal {

struct SidebarWidthBudgetInput
{
    int availablePanelsPx = 0;
    int minPanelPx = 0;

    bool leftVisible = false;
    bool rightVisible = false;

    int leftPx = 0;
    int rightPx = 0;

    // Side currently being expanded by user drag.
    // We prefer shrinking the opposite side first.
    std::optional<SidebarSide> changedSide;
};

struct SidebarWidthBudgetResult
{
    int leftPx = 0;
    int rightPx = 0;
    int overflowPx = 0;
};

inline SidebarWidthBudgetResult resolveSidebarWidthBudget(const SidebarWidthBudgetInput& in)
{
    SidebarWidthBudgetResult out;
    out.leftPx = std::max(0, in.leftPx);
    out.rightPx = std::max(0, in.rightPx);

    const int minLeft = in.leftVisible ? std::max(0, in.minPanelPx) : 0;
    const int minRight = in.rightVisible ? std::max(0, in.minPanelPx) : 0;
    const int allowed = std::max(0, in.availablePanelsPx);

    int overflow = (out.leftPx + out.rightPx) - allowed;
    if (overflow <= 0)
        return out;

    auto shrinkSide = [&](SidebarSide side, int delta) -> int {
        if (delta <= 0)
            return 0;

        int* width = nullptr;
        int minWidth = 0;
        if (side == SidebarSide::Left) {
            width = &out.leftPx;
            minWidth = minLeft;
        } else {
            width = &out.rightPx;
            minWidth = minRight;
        }

        const int capacity = std::max(0, *width - minWidth);
        const int cut = std::min(capacity, delta);
        *width -= cut;
        return cut;
    };

    SidebarSide first = SidebarSide::Left;
    if (in.changedSide.has_value()) {
        first = (in.changedSide.value() == SidebarSide::Left) ? SidebarSide::Right : SidebarSide::Left;
    } else {
        first = (out.leftPx >= out.rightPx) ? SidebarSide::Left : SidebarSide::Right;
    }
    const SidebarSide second = (first == SidebarSide::Left) ? SidebarSide::Right : SidebarSide::Left;

    overflow -= shrinkSide(first, overflow);
    overflow -= shrinkSide(second, overflow);
    out.overflowPx = std::max(0, overflow);
    return out;
}

} // namespace Core::Internal

