// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/state/AieKernelsPanelState.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtWidgets/QApplication>
#include <QtCore/QTemporaryDir>

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        static int argc = 1;
        static char arg0[] = "aie-kernelspanelstate-tests";
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

TEST(AieKernelsPanelStateTests, PersistsPanelStatePerWorkspaceRoot)
{
    ensureApp();

    QTemporaryDir stateDir;
    ASSERT_TRUE(stateDir.isValid());

    Aie::Internal::AieKernelsPanelState state{makeTestEnvironment(stateDir.path())};

    Aie::Internal::AieKernelsPanelState::Snapshot workspaceA;
    workspaceA.searchText = QStringLiteral("fir");
    workspaceA.selectedKernelId = QStringLiteral("fir.filter");
    workspaceA.scrollValue = 88;

    Aie::Internal::AieKernelsPanelState::Snapshot workspaceB;
    workspaceB.searchText = QStringLiteral("conv");
    workspaceB.selectedKernelId = QStringLiteral("conv2d");
    workspaceB.scrollValue = 11;

    Aie::Internal::AieKernelsPanelState::Snapshot global;
    global.searchText = QStringLiteral("default");
    global.selectedKernelId = QStringLiteral("builtin.add");
    global.scrollValue = 5;

    state.setStateForWorkspaceRoot(QStringLiteral("/workspace/A"), workspaceA);
    state.setStateForWorkspaceRoot(QStringLiteral("/workspace/B"), workspaceB);
    state.setStateForWorkspaceRoot(QString(), global);

    const auto loadedA = state.stateForWorkspaceRoot(QStringLiteral("/workspace/A"));
    EXPECT_EQ(loadedA.searchText, workspaceA.searchText);
    EXPECT_EQ(loadedA.selectedKernelId, workspaceA.selectedKernelId);
    EXPECT_EQ(loadedA.scrollValue, workspaceA.scrollValue);

    const auto loadedB = state.stateForWorkspaceRoot(QStringLiteral("/workspace/B"));
    EXPECT_EQ(loadedB.searchText, workspaceB.searchText);
    EXPECT_EQ(loadedB.selectedKernelId, workspaceB.selectedKernelId);
    EXPECT_EQ(loadedB.scrollValue, workspaceB.scrollValue);

    const auto loadedGlobal = state.stateForWorkspaceRoot(QString());
    EXPECT_EQ(loadedGlobal.searchText, global.searchText);
    EXPECT_EQ(loadedGlobal.selectedKernelId, global.selectedKernelId);
    EXPECT_EQ(loadedGlobal.scrollValue, global.scrollValue);
}

TEST(AieKernelsPanelStateTests, EmptySnapshotClearsRootState)
{
    ensureApp();

    QTemporaryDir stateDir;
    ASSERT_TRUE(stateDir.isValid());

    Aie::Internal::AieKernelsPanelState state{makeTestEnvironment(stateDir.path())};

    Aie::Internal::AieKernelsPanelState::Snapshot snapshot;
    snapshot.searchText = QStringLiteral("fir");
    snapshot.selectedKernelId = QStringLiteral("fir.filter");
    snapshot.scrollValue = 64;

    state.setStateForWorkspaceRoot(QStringLiteral("/workspace/A"), snapshot);

    Aie::Internal::AieKernelsPanelState::Snapshot empty;
    state.setStateForWorkspaceRoot(QStringLiteral("/workspace/A"), empty);

    const auto loaded = state.stateForWorkspaceRoot(QStringLiteral("/workspace/A"));
    EXPECT_TRUE(loaded.searchText.isEmpty());
    EXPECT_TRUE(loaded.selectedKernelId.isEmpty());
    EXPECT_EQ(loaded.scrollValue, 0);
}
