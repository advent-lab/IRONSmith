// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/state/AieKernelAssignmentState.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QTemporaryDir>
#include <QtWidgets/QApplication>

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        static int argc = 1;
        static char arg0[] = "aie-kernelassignmentstate-tests";
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

TEST(AieKernelAssignmentStateTests, PersistsReassignmentConfirmationPreference)
{
    ensureApp();

    QTemporaryDir stateDir;
    ASSERT_TRUE(stateDir.isValid());

    {
        Aie::Internal::AieKernelAssignmentState state(makeTestEnvironment(stateDir.path()));
        EXPECT_TRUE(state.confirmReassignment());
        state.setConfirmReassignment(false);
    }

    {
        Aie::Internal::AieKernelAssignmentState restored(makeTestEnvironment(stateDir.path()));
        EXPECT_FALSE(restored.confirmReassignment());
        restored.setConfirmReassignment(true);
    }

    {
        Aie::Internal::AieKernelAssignmentState restored(makeTestEnvironment(stateDir.path()));
        EXPECT_TRUE(restored.confirmReassignment());
    }
}
