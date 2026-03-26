// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/state/AieSidebarState.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QTemporaryDir>
#include <QtWidgets/QApplication>

namespace {

constexpr auto kLayoutPanelId = "IRONSmith.AieGridTools";
constexpr auto kKernelsPanelId = "IRONSmith.Kernels";
constexpr auto kPropertiesPanelId = "IRONSmith.AieProperties";
constexpr auto kSymbolsPanelId = "IRONSmith.Symbols";

QApplication* ensureApp()
{
    static QApplication* app = []() {
        static int argc = 1;
        static char arg0[] = "aie-sidebarstate-tests";
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

TEST(AieSidebarStateTests, PersistsPanelOpenFlagsPerPanelId)
{
    ensureApp();

    QTemporaryDir stateDir;
    ASSERT_TRUE(stateDir.isValid());

    {
        Aie::Internal::AieSidebarState state(makeTestEnvironment(stateDir.path()));
        EXPECT_FALSE(state.panelOpen(QString::fromLatin1(kLayoutPanelId)));
        EXPECT_FALSE(state.panelOpen(QString::fromLatin1(kKernelsPanelId)));
        EXPECT_FALSE(state.panelOpen(QString::fromLatin1(kPropertiesPanelId)));
        EXPECT_FALSE(state.panelOpen(QString::fromLatin1(kSymbolsPanelId)));

        state.setPanelOpen(QString::fromLatin1(kLayoutPanelId), true);
        state.setPanelOpen(QString::fromLatin1(kKernelsPanelId), false);
        state.setPanelOpen(QString::fromLatin1(kPropertiesPanelId), true);
        state.setPanelOpen(QString::fromLatin1(kSymbolsPanelId), true);
    }

    {
        Aie::Internal::AieSidebarState restored(makeTestEnvironment(stateDir.path()));
        EXPECT_TRUE(restored.panelOpen(QString::fromLatin1(kLayoutPanelId)));
        EXPECT_FALSE(restored.panelOpen(QString::fromLatin1(kKernelsPanelId)));
        EXPECT_TRUE(restored.panelOpen(QString::fromLatin1(kPropertiesPanelId)));
        EXPECT_TRUE(restored.panelOpen(QString::fromLatin1(kSymbolsPanelId)));

        restored.setPanelOpen(QString::fromLatin1(kKernelsPanelId), true);
    }

    {
        Aie::Internal::AieSidebarState restored(makeTestEnvironment(stateDir.path()));
        EXPECT_TRUE(restored.panelOpen(QString::fromLatin1(kLayoutPanelId)));
        EXPECT_TRUE(restored.panelOpen(QString::fromLatin1(kKernelsPanelId)));
        EXPECT_TRUE(restored.panelOpen(QString::fromLatin1(kPropertiesPanelId)));
        EXPECT_TRUE(restored.panelOpen(QString::fromLatin1(kSymbolsPanelId)));
    }
}
