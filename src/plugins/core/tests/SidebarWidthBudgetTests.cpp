// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "core/SidebarWidthBudget.hpp"

namespace {

using Core::SidebarSide;
using Core::Internal::SidebarWidthBudgetInput;
using Core::Internal::resolveSidebarWidthBudget;

TEST(SidebarWidthBudgetTests, LeavesWidthsUnchangedWhenWithinBudget)
{
    SidebarWidthBudgetInput input;
    input.availablePanelsPx = 760;
    input.minPanelPx = 220;
    input.leftVisible = true;
    input.rightVisible = true;
    input.leftPx = 320;
    input.rightPx = 360;

    const auto result = resolveSidebarWidthBudget(input);
    EXPECT_EQ(result.leftPx, 320);
    EXPECT_EQ(result.rightPx, 360);
    EXPECT_EQ(result.overflowPx, 0);
}

TEST(SidebarWidthBudgetTests, ShrinksOppositeSideFirstForUserDrag)
{
    SidebarWidthBudgetInput input;
    input.availablePanelsPx = 760;
    input.minPanelPx = 220;
    input.leftVisible = true;
    input.rightVisible = true;
    input.leftPx = 460;
    input.rightPx = 360;
    input.changedSide = SidebarSide::Left;

    const auto result = resolveSidebarWidthBudget(input);
    EXPECT_EQ(result.leftPx, 460);
    EXPECT_EQ(result.rightPx, 300);
    EXPECT_EQ(result.overflowPx, 0);
}

TEST(SidebarWidthBudgetTests, FallsBackToChangedSideWhenOppositeHitsMinimum)
{
    SidebarWidthBudgetInput input;
    input.availablePanelsPx = 680;
    input.minPanelPx = 220;
    input.leftVisible = true;
    input.rightVisible = true;
    input.leftPx = 520;
    input.rightPx = 240;
    input.changedSide = SidebarSide::Left;

    const auto result = resolveSidebarWidthBudget(input);
    EXPECT_EQ(result.leftPx, 460);
    EXPECT_EQ(result.rightPx, 220);
    EXPECT_EQ(result.overflowPx, 0);
}

TEST(SidebarWidthBudgetTests, ReportsUnavoidableOverflowWhenBothSidesAtMinimum)
{
    SidebarWidthBudgetInput input;
    input.availablePanelsPx = 360;
    input.minPanelPx = 220;
    input.leftVisible = true;
    input.rightVisible = true;
    input.leftPx = 220;
    input.rightPx = 220;

    const auto result = resolveSidebarWidthBudget(input);
    EXPECT_EQ(result.leftPx, 220);
    EXPECT_EQ(result.rightPx, 220);
    EXPECT_EQ(result.overflowPx, 80);
}

} // namespace

