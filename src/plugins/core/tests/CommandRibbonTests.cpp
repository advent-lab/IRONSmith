// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "core/CommandRibbon.hpp"

#include <QtCore/QStringList>

namespace {

TEST(CommandRibbonTests, GroupLayoutChangePropagatesToRibbonStructureChanged)
{
    Core::CommandRibbon ribbon;
    int structureChanges = 0;
    QObject::connect(&ribbon, &Core::CommandRibbon::structureChanged, [&structureChanges]() {
        ++structureChanges;
    });

    auto* page = ribbon.ensurePage("home", "Home");
    ASSERT_NE(page, nullptr);
    auto* group = page->ensureGroup("project", "Project");
    ASSERT_NE(group, nullptr);

    structureChanges = 0;
    auto root = Core::RibbonNode::makeRow("project_root");
    root->addSeparator("sep");
    EXPECT_TRUE(group->setLayout(std::move(root)));
    EXPECT_EQ(structureChanges, 1);
}

TEST(CommandRibbonTests, UpdateBatchCoalescesModelSignals)
{
    Core::CommandRibbon ribbon;

    int structureChanges = 0;
    QStringList activePageChanges;
    QObject::connect(&ribbon, &Core::CommandRibbon::structureChanged, [&structureChanges]() {
        ++structureChanges;
    });
    QObject::connect(&ribbon, &Core::CommandRibbon::activePageChanged, [&activePageChanges](const QString& id) {
        activePageChanges.push_back(id);
    });

    ribbon.beginUpdateBatch();
    auto* home = ribbon.ensurePage("home", "Home");
    ASSERT_NE(home, nullptr);
    auto* view = ribbon.ensurePage("view", "View");
    ASSERT_NE(view, nullptr);

    auto* group = home->ensureGroup("project", "Project");
    ASSERT_NE(group, nullptr);
    auto root = Core::RibbonNode::makeRow("project_root");
    root->addSeparator("sep");
    EXPECT_TRUE(group->setLayout(std::move(root)));
    EXPECT_TRUE(ribbon.setActivePageId("view"));

    EXPECT_EQ(structureChanges, 0);
    EXPECT_TRUE(activePageChanges.isEmpty());

    ribbon.endUpdateBatch();

    EXPECT_EQ(structureChanges, 1);
    ASSERT_EQ(activePageChanges.size(), 1);
    EXPECT_EQ(activePageChanges.front(), "view");
}

TEST(CommandRibbonTests, NestedUpdateBatchDefersSignalsUntilOutermostEnd)
{
    Core::CommandRibbon ribbon;

    int structureChanges = 0;
    QObject::connect(&ribbon, &Core::CommandRibbon::structureChanged, [&structureChanges]() {
        ++structureChanges;
    });

    ribbon.beginUpdateBatch();
    ribbon.beginUpdateBatch();
    EXPECT_NE(ribbon.ensurePage("home", "Home"), nullptr);
    EXPECT_EQ(structureChanges, 0);

    ribbon.endUpdateBatch();
    EXPECT_EQ(structureChanges, 0);

    ribbon.endUpdateBatch();
    EXPECT_EQ(structureChanges, 1);
}

} // namespace
