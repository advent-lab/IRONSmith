// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/AiePropertiesShortcutController.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/api/ICanvasHost.hpp"
#include "core/api/ISidebarRegistry.hpp"
#include "core/api/SidebarToolSpec.hpp"

#include <QtCore/QRectF>
#include <QtWidgets/QApplication>

namespace {

constexpr auto kPropertiesPanelId = "IRONSmith.AieProperties";

QApplication* ensureApp()
{
    static QApplication* app = []() {
        int argc = 0;
        char** argv = nullptr;
        return new QApplication(argc, argv);
    }();
    return app;
}

Qt::KeyboardModifiers propertiesShortcutModifiers()
{
    return Qt::ControlModifier | Qt::ShiftModifier;
}

class StubCanvasHost final : public Canvas::Api::ICanvasHost
{
public:
    using Canvas::Api::ICanvasHost::ICanvasHost;

    QWidget* viewWidget() const override { return m_viewWidget; }
    Canvas::CanvasDocument* document() const override { return m_document; }
    Canvas::CanvasController* controller() const override { return nullptr; }

    void setCanvasActive(bool active) override { m_canvasActive = active; }
    bool canvasActive() const override { return m_canvasActive; }

    void setEmptyStateText(const QString& title, const QString& message) override
    {
        Q_UNUSED(title);
        Q_UNUSED(message);
    }

    void setViewWidget(QWidget* viewWidget) { m_viewWidget = viewWidget; }
    void setDocument(Canvas::CanvasDocument* document) { m_document = document; }

private:
    QWidget* m_viewWidget = nullptr;
    Canvas::CanvasDocument* m_document = nullptr;
    bool m_canvasActive = true;
};

class StubSidebarRegistry final : public Core::ISidebarRegistry
{
public:
    using Core::ISidebarRegistry::ISidebarRegistry;

    bool registerTool(const Core::SidebarToolSpec&, PanelFactory, QString*) override { return true; }
    bool unregisterTool(const QString&, QString*) override { return true; }
    bool isToolOpen(const QString&) const override { return false; }

    void requestShowTool(const QString& id) override
    {
        ++requestShowCount;
        lastRequestedId = id;
    }

    void requestHideTool(const QString& id) override
    {
        Q_UNUSED(id);
    }

    int requestShowCount = 0;
    QString lastRequestedId;
};

} // namespace

TEST(AiePropertiesShortcutControllerTests, ShortcutOpensPropertiesForCanvasItem)
{
    ensureApp();

    Canvas::CanvasDocument document;
    auto* block = document.createBlock(QRectF(12.0, 18.0, 64.0, 48.0), true);
    ASSERT_NE(block, nullptr);

    Canvas::CanvasView view;
    view.setDocument(&document);

    StubCanvasHost host;
    host.setDocument(&document);
    host.setViewWidget(&view);

    StubSidebarRegistry sidebarRegistry;

    Aie::Internal::AiePropertiesShortcutController controller;
    controller.setCanvasHost(&host);
    controller.setSidebarRegistry(&sidebarRegistry);

    emit view.canvasMousePressed(block->boundsScene().center(),
                                 Qt::LeftButton,
                                 propertiesShortcutModifiers());

    EXPECT_EQ(sidebarRegistry.requestShowCount, 1);
    EXPECT_EQ(sidebarRegistry.lastRequestedId, QString::fromLatin1(kPropertiesPanelId));
}

TEST(AiePropertiesShortcutControllerTests, ShortcutIgnoresEmptyCanvasHits)
{
    ensureApp();

    Canvas::CanvasDocument document;
    Canvas::CanvasView view;
    view.setDocument(&document);

    StubCanvasHost host;
    host.setDocument(&document);
    host.setViewWidget(&view);

    StubSidebarRegistry sidebarRegistry;

    Aie::Internal::AiePropertiesShortcutController controller;
    controller.setCanvasHost(&host);
    controller.setSidebarRegistry(&sidebarRegistry);

    emit view.canvasMousePressed(QPointF(320.0, 240.0),
                                 Qt::LeftButton,
                                 propertiesShortcutModifiers());

    EXPECT_EQ(sidebarRegistry.requestShowCount, 0);
    EXPECT_TRUE(sidebarRegistry.lastRequestedId.isEmpty());
}
