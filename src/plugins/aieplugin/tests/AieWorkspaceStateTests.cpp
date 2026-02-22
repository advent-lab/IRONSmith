// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/state/AieWorkspaceState.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QTemporaryDir>
#include <QtWidgets/QApplication>

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        static int argc = 1;
        static char arg0[] = "aie-workspacestate-tests";
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

TEST(AieWorkspaceStateTests, PersistsActiveBundlePerWorkspaceRoot)
{
    ensureApp();

    QTemporaryDir stateDir;
    ASSERT_TRUE(stateDir.isValid());

    Aie::Internal::AieWorkspaceState state{makeTestEnvironment(stateDir.path())};

    state.setActiveBundlePathForRoot(QStringLiteral("/workspace/A"),
                                     QStringLiteral("/workspace/A/a.ironsmith"));
    state.setActiveBundlePathForRoot(QStringLiteral("/workspace/B"),
                                     QStringLiteral("/workspace/B/b.ironsmith"));

    EXPECT_EQ(state.activeBundlePathForRoot(QStringLiteral("/workspace/A")),
              QStringLiteral("/workspace/A/a.ironsmith"));
    EXPECT_EQ(state.activeBundlePathForRoot(QStringLiteral("/workspace/B")),
              QStringLiteral("/workspace/B/b.ironsmith"));

    state.clearRoot(QStringLiteral("/workspace/A"));
    EXPECT_TRUE(state.activeBundlePathForRoot(QStringLiteral("/workspace/A")).isEmpty());
    EXPECT_EQ(state.activeBundlePathForRoot(QStringLiteral("/workspace/B")),
              QStringLiteral("/workspace/B/b.ironsmith"));
}
