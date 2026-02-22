// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "core/state/CoreUiState.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QTemporaryDir>
#include <QtWidgets/QApplication>

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        static int argc = 1;
        static char arg0[] = "core-uistate-tests";
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

TEST(CoreUiStateTests, PersistsSidebarPanelWidthBySideAndFamily)
{
    ensureApp();

    QTemporaryDir stateDir;
    ASSERT_TRUE(stateDir.isValid());

    Core::Internal::CoreUiState state(makeTestEnvironment(stateDir.path()));
    state.setSidebarPanelWidth(Core::SidebarSide::Left, Core::SidebarFamily::Vertical, 420);
    state.setSidebarPanelWidth(Core::SidebarSide::Right, Core::SidebarFamily::Horizontal, 288);

    Core::Internal::CoreUiState restored(makeTestEnvironment(stateDir.path()));
    EXPECT_EQ(restored.sidebarPanelWidth(Core::SidebarSide::Left,
                                         Core::SidebarFamily::Vertical,
                                         320),
              420);
    EXPECT_EQ(restored.sidebarPanelWidth(Core::SidebarSide::Right,
                                         Core::SidebarFamily::Horizontal,
                                         320),
              288);
    EXPECT_EQ(restored.sidebarPanelWidth(Core::SidebarSide::Left,
                                         Core::SidebarFamily::Horizontal,
                                         345),
              345);
}

TEST(CoreUiStateTests, PersistsMainWindowGeometryBlob)
{
    ensureApp();

    QTemporaryDir stateDir;
    ASSERT_TRUE(stateDir.isValid());

    const QByteArray geometryBlob("geometry-test-data");

    Core::Internal::CoreUiState state(makeTestEnvironment(stateDir.path()));
    state.setMainWindowGeometry(geometryBlob);

    Core::Internal::CoreUiState restored(makeTestEnvironment(stateDir.path()));
    EXPECT_EQ(restored.mainWindowGeometry(), geometryBlob);
}
