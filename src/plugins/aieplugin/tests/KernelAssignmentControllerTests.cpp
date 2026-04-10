// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/kernels/KernelAssignmentController.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/api/ICanvasHost.hpp"

#include <QtWidgets/QApplication>
#include <QtCore/QRectF>
#include <QtCore/QTemporaryDir>

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        int argc = 0;
        char** argv = nullptr;
        return new QApplication(argc, argv);
    }();
    return app;
}

class StubCanvasHost final : public Canvas::Api::ICanvasHost
{
public:
    using Canvas::Api::ICanvasHost::ICanvasHost;

    QWidget* viewWidget() const override { return m_viewWidget; }
    Canvas::CanvasDocument* document() const override { return m_document; }
    Canvas::CanvasController* controller() const override { return nullptr; }

    void setCanvasActive(bool active) override
    {
        if (m_canvasActive == active)
            return;

        m_canvasActive = active;
        emit canvasActiveChanged(m_canvasActive);
    }

    bool canvasActive() const override { return m_canvasActive; }

    void setEmptyStateText(const QString& title, const QString& message) override
    {
        Q_UNUSED(title);
        Q_UNUSED(message);
    }

    void setDocument(Canvas::CanvasDocument* document)
    {
        m_document = document;
    }

    void setViewWidget(QWidget* viewWidget)
    {
        m_viewWidget = viewWidget;
    }

private:
    QWidget* m_viewWidget = nullptr;
    Canvas::CanvasDocument* m_document = nullptr;
    bool m_canvasActive = true;
};

Canvas::CanvasBlock* makeBlock(Canvas::CanvasDocument& doc, const QString& specId)
{
    auto* block = doc.createBlock(QRectF(0.0, 0.0, 64.0, 64.0), false);
    if (!block)
        return nullptr;

    block->setSpecId(specId);
    return block;
}

Aie::Internal::KernelAsset createKernel(Aie::Internal::KernelRegistryService& registry, const QString& rootPath)
{
    registry.setBuiltInRoot(QString());
    registry.setGlobalUserRoot(rootPath);

    Aie::Internal::KernelRegistryService::KernelCreateRequest request;
    request.id = QStringLiteral("test_kernel");
    request.name = QStringLiteral("Test Kernel");
    request.signature = QStringLiteral("void test_kernel();");
    request.description = QStringLiteral("Kernel used by unit tests.");

    Aie::Internal::KernelAsset created;
    const Utils::Result result =
        registry.createKernelInScope(request, Aie::Internal::KernelSourceScope::Global, &created);
    EXPECT_TRUE(result.ok) << result.errors.join('\n').toStdString();
    return created;
}

QPointF stereotypeHoverPoint(const Canvas::CanvasBlock& block)
{
    const QString stereotype = block.stereotype().trimmed();
    if (stereotype.isEmpty())
        return block.boundsScene().center();

    QFont font = QApplication::font();
    font.setPointSizeF(Canvas::Constants::kBlockStereotypePointSize);
    font.setBold(false);
    font.setItalic(true);

    const QFontMetricsF metrics(font);
    const QSizeF size = metrics.size(Qt::TextSingleLine, stereotype);
    const QRectF bounds = block.boundsScene();

    const double x = bounds.center().x() - (size.width() * 0.5);
    const double y = bounds.top() - Canvas::Constants::kBlockStereotypeOffsetY - size.height();
    return QPointF(x + (size.width() * 0.5), y + (size.height() * 0.5));
}

} // namespace

TEST(KernelAssignmentControllerTests, AssignKernelToTileAcceptsOnlyAieTiles)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    Aie::Internal::KernelRegistryService registry;
    const Aie::Internal::KernelAsset kernel = createKernel(registry, tempDir.path());
    ASSERT_FALSE(kernel.id.isEmpty());

    Aie::Internal::KernelAssignmentController controller;
    controller.setRegistry(&registry);

    const Utils::Result shimResult =
        controller.assignKernelToTile(QStringLiteral("shim0_0"), kernel.id);
    EXPECT_FALSE(shimResult.ok);

    const Utils::Result memResult =
        controller.assignKernelToTile(QStringLiteral("mem0_0"), kernel.id);
    EXPECT_FALSE(memResult.ok);

    const Utils::Result aieResult =
        controller.assignKernelToTile(QStringLiteral("aie0_0"), kernel.id);
    EXPECT_TRUE(aieResult.ok) << aieResult.errors.join('\n').toStdString();
    EXPECT_EQ(controller.assignments().value(QStringLiteral("aie0_0")), QStringList{kernel.id});
}

TEST(KernelAssignmentControllerTests, RehydrateAssignmentsIgnoresNonAieTiles)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    Aie::Internal::KernelRegistryService registry;
    const Aie::Internal::KernelAsset kernel = createKernel(registry, tempDir.path());
    ASSERT_FALSE(kernel.id.isEmpty());

    Canvas::CanvasDocument document;
    Canvas::CanvasBlock* aieBlock = makeBlock(document, QStringLiteral("aie0_0"));
    Canvas::CanvasBlock* shimBlock = makeBlock(document, QStringLiteral("shim0_0"));
    Canvas::CanvasBlock* memBlock = makeBlock(document, QStringLiteral("mem0_0"));
    ASSERT_NE(aieBlock, nullptr);
    ASSERT_NE(shimBlock, nullptr);
    ASSERT_NE(memBlock, nullptr);

    const QString stereotype = QStringLiteral("<<kernel: %1>>").arg(kernel.id);
    aieBlock->setStereotype(stereotype);
    shimBlock->setStereotype(stereotype);
    memBlock->setStereotype(stereotype);

    StubCanvasHost host;
    host.setDocument(&document);

    Aie::Internal::KernelAssignmentController controller;
    controller.setRegistry(&registry);
    controller.setCanvasHost(&host);
    controller.rehydrateAssignmentsFromCanvas();

    const QHash<QString, QStringList> assignments = controller.assignments();
    EXPECT_EQ(assignments.size(), 1);
    EXPECT_EQ(assignments.value(QStringLiteral("aie0_0")), QStringList{kernel.id});
    EXPECT_FALSE(assignments.contains(QStringLiteral("shim0_0")));
    EXPECT_FALSE(assignments.contains(QStringLiteral("mem0_0")));
}

TEST(KernelAssignmentControllerTests, HoveringKernelStereotypeRestoresPointingHandCursor)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    Aie::Internal::KernelRegistryService registry;
    const Aie::Internal::KernelAsset kernel = createKernel(registry, tempDir.path());
    ASSERT_FALSE(kernel.id.isEmpty());

    Canvas::CanvasDocument document;
    Canvas::CanvasBlock* block = makeBlock(document, QStringLiteral("aie0_0"));
    ASSERT_NE(block, nullptr);
    block->setStereotype(QStringLiteral("<<kernel: %1>>").arg(kernel.id));

    Canvas::CanvasView view;
    view.setDocument(&document);

    StubCanvasHost host;
    host.setDocument(&document);
    host.setViewWidget(&view);

    Aie::Internal::KernelAssignmentController controller;
    controller.setRegistry(&registry);
    controller.setCanvasHost(&host);

    const QPointF hoverPoint = stereotypeHoverPoint(*block);
    emit view.canvasMouseMoved(hoverPoint, Qt::NoButton, Qt::NoModifier);
    EXPECT_EQ(view.cursor().shape(), Qt::PointingHandCursor);

    view.unsetCursor();
    emit view.canvasMouseMoved(hoverPoint, Qt::NoButton, Qt::NoModifier);
    EXPECT_EQ(view.cursor().shape(), Qt::PointingHandCursor);
}
