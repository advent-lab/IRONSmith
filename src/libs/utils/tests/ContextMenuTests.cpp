// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "utils/contextmenu/ContextMenu.hpp"

#include <QtWidgets/QApplication>
#include <QtTest/QSignalSpy>

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        static int argc = 1;
        static char arg0[] = "utils-contextmenu-tests";
        static char* argv[] = { arg0, nullptr };
        return new QApplication(argc, argv);
    }();
    return app;
}

} // namespace

TEST(ContextMenuTests, SetActionsBuildsMenuActionsFromSpecs)
{
    ensureApp();

    Utils::ContextMenu menu;

    QList<Utils::ContextMenuAction> specs;
    specs.push_back(Utils::ContextMenuAction::item(QStringLiteral("action.first"), QStringLiteral("First")));
    specs.push_back(Utils::ContextMenuAction::separatorAction());
    auto second = Utils::ContextMenuAction::item(QStringLiteral("action.second"), QStringLiteral("Second"));
    second.enabled = false;
    second.checkable = true;
    second.checked = true;
    specs.push_back(second);

    menu.setActions(specs);

    EXPECT_EQ(menu.actionsSpec().size(), 3);
    const QList<QAction*> menuActions = menu.actions();
    ASSERT_EQ(menuActions.size(), 3);

    EXPECT_EQ(menuActions.at(0)->text(), QStringLiteral("First"));
    EXPECT_FALSE(menuActions.at(0)->isSeparator());

    EXPECT_TRUE(menuActions.at(1)->isSeparator());

    EXPECT_EQ(menuActions.at(2)->text(), QStringLiteral("Second"));
    EXPECT_FALSE(menuActions.at(2)->isEnabled());
    EXPECT_TRUE(menuActions.at(2)->isCheckable());
    EXPECT_TRUE(menuActions.at(2)->isChecked());
}

TEST(ContextMenuTests, TriggeredActionEmitsActionId)
{
    ensureApp();

    Utils::ContextMenu menu;
    menu.setActions({Utils::ContextMenuAction::item(QStringLiteral("action.id"), QStringLiteral("Action"))});

    QSignalSpy spy(&menu, &Utils::ContextMenu::actionTriggered);
    ASSERT_EQ(menu.actions().size(), 1);
    menu.actions().front()->trigger();

    ASSERT_EQ(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    ASSERT_EQ(args.size(), 1);
    EXPECT_EQ(args.at(0).toString(), QStringLiteral("action.id"));
}

TEST(ContextMenuTests, TriggeredActionWithEmptyIdDoesNotEmit)
{
    ensureApp();

    Utils::ContextMenu menu;
    menu.setActions({Utils::ContextMenuAction::item(QString(), QStringLiteral("Action"))});

    QSignalSpy spy(&menu, &Utils::ContextMenu::actionTriggered);
    ASSERT_EQ(menu.actions().size(), 1);
    menu.actions().front()->trigger();

    EXPECT_EQ(spy.count(), 0);
}
