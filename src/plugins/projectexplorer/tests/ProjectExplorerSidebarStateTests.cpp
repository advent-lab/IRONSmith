// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "projectexplorer/state/ProjectExplorerSidebarState.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QTemporaryDir>
#include <QtWidgets/QApplication>

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        static int argc = 1;
        static char arg0[] = "projectexplorer-sidebarstate-tests";
        static char* argv[] = { arg0, nullptr };
        return new QApplication(argc, argv);
    }();
    return app;
}

Utils::Environment makeTestEnvironment(const QString& root)
{
    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    cfg.globalConfigRootOverride = root;
    return Utils::Environment(cfg);
}

} // namespace

TEST(ProjectExplorerSidebarStateTests, PersistsPanelOpenFlag)
{
    ensureApp();

    QTemporaryDir stateDir;
    ASSERT_TRUE(stateDir.isValid());

    {
        ProjectExplorer::Internal::ProjectExplorerSidebarState state(makeTestEnvironment(stateDir.path()));
        EXPECT_FALSE(state.panelOpen());
        state.setPanelOpen(true);
    }

    {
        ProjectExplorer::Internal::ProjectExplorerSidebarState restored(makeTestEnvironment(stateDir.path()));
        EXPECT_TRUE(restored.panelOpen());
        restored.setPanelOpen(false);
    }

    {
        ProjectExplorer::Internal::ProjectExplorerSidebarState restored(makeTestEnvironment(stateDir.path()));
        EXPECT_FALSE(restored.panelOpen());
    }
}
