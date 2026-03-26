// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasController.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasSelectionModel.hpp"
#include "canvas/CanvasView.hpp"

#include <QtCore/QRectF>
#include <QtWidgets/QApplication>

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

Qt::KeyboardModifiers propertiesShortcutModifiers()
{
    return Qt::ControlModifier | Qt::ShiftModifier;
}

} // namespace

TEST(CanvasControllerShortcutTests, PropertiesShortcutSelectsSingleHitItem)
{
    ensureApp();

    Canvas::CanvasDocument document;
    Canvas::CanvasView view;
    view.setDocument(&document);

    Canvas::CanvasSelectionModel selection;
    Canvas::CanvasController controller(&document, &view, &selection);
    view.setController(&controller);
    view.setSelectionModel(&selection);

    auto* firstBlock = document.createBlock(QRectF(0.0, 0.0, 64.0, 64.0), true);
    auto* secondBlock = document.createBlock(QRectF(120.0, 0.0, 64.0, 64.0), true);
    ASSERT_NE(firstBlock, nullptr);
    ASSERT_NE(secondBlock, nullptr);

    selection.setSelectedItem(firstBlock->id());

    controller.onCanvasMousePressed(secondBlock->boundsScene().center(),
                                    Qt::LeftButton,
                                    propertiesShortcutModifiers());

    EXPECT_EQ(selection.selectedItem(), secondBlock->id());
    EXPECT_EQ(selection.selectedItems().size(), 1);
    EXPECT_TRUE(selection.isSelected(secondBlock->id()));
    EXPECT_FALSE(selection.isSelected(firstBlock->id()));
}
