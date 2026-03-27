// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/symbol_table/SymbolsController.hpp"
#include "aieplugin/symbol_table/SymbolsPanel.hpp"

#include "canvas/api/ICanvasDocumentService.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QToolButton>

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
        static int argc = 1;
        static char arg0[] = "symbols-panel-tests";
        static char* argv[] = { arg0, nullptr };
        return new QApplication(argc, argv);
    }();
    return app;
}

class FakeCanvasDocumentService final : public Canvas::Api::ICanvasDocumentService
{
public:
    Utils::Result createDocument(const Canvas::Api::CanvasDocumentCreateRequest&,
                                 Canvas::Api::CanvasDocumentHandle&) override
    {
        return Utils::Result::failure(QStringLiteral("unused"));
    }

    Utils::Result openDocument(const Canvas::Api::CanvasDocumentOpenRequest&,
                               Canvas::Api::CanvasDocumentHandle&) override
    {
        return Utils::Result::failure(QStringLiteral("unused"));
    }

    Utils::Result saveDocument(const Canvas::Api::CanvasDocumentHandle&) override
    {
        return Utils::Result::success();
    }

    Utils::Result closeDocument(const Canvas::Api::CanvasDocumentHandle&,
                                Canvas::Api::CanvasDocumentCloseReason reason) override
    {
        if (!m_hasOpenDocument)
            return Utils::Result::success();
        m_hasOpenDocument = false;
        const auto handle = m_handle;
        m_handle = {};
        m_metadata = {};
        emit documentClosed(handle, reason);
        return Utils::Result::success();
    }

    Canvas::Api::CanvasDocumentHandle activeDocument() const override
    {
        return m_handle;
    }

    QJsonObject activeMetadata() const override
    {
        return m_metadata;
    }

    Utils::Result updateActiveMetadata(const QJsonObject& metadata) override
    {
        if (!m_hasOpenDocument)
            return Utils::Result::failure(QStringLiteral("No active document."));
        m_metadata = metadata;
        return Utils::Result::success();
    }

    bool hasOpenDocument() const override
    {
        return m_hasOpenDocument;
    }

    bool isDirty() const override
    {
        return false;
    }

    void open(const QJsonObject& metadata = {})
    {
        m_hasOpenDocument = true;
        m_handle.id = QStringLiteral("doc");
        m_handle.bundlePath = QStringLiteral("/tmp/test.ironsmith");
        m_handle.persistencePath = QStringLiteral("/tmp/test.ironsmith/canvas/document.json");
        m_metadata = metadata;
        emit documentOpened(m_handle);
    }

private:
    Canvas::Api::CanvasDocumentHandle m_handle;
    QJsonObject m_metadata;
    bool m_hasOpenDocument = false;
};

QGroupBox* findGroupBoxByTitle(QWidget& parent, const QString& title)
{
    const auto groups = parent.findChildren<QGroupBox*>();
    for (auto* group : groups) {
        if (group && group->title() == title)
            return group;
    }
    return nullptr;
}

QLineEdit* findLineEditByPlaceholder(QWidget& parent, const QString& placeholder, int occurrence = 0)
{
    int seen = 0;
    const auto edits = parent.findChildren<QLineEdit*>();
    for (auto* edit : edits) {
        if (!edit || edit->placeholderText() != placeholder)
            continue;
        if (seen == occurrence)
            return edit;
        ++seen;
    }
    return nullptr;
}

QPushButton* findPushButtonByText(QWidget& parent, const QString& text)
{
    const auto buttons = parent.findChildren<QPushButton*>();
    for (auto* button : buttons) {
        if (button && button->text() == text)
            return button;
    }
    return nullptr;
}

void fireEditingFinished(QLineEdit* edit)
{
    ASSERT_NE(edit, nullptr);
    const bool invoked = QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
    ASSERT_TRUE(invoked);
}

} // namespace

TEST(SymbolsPanelTests, PanelInteractionsUpdateControllerWithoutCrashing)
{
    ensureApp();

    FakeCanvasDocumentService documentService;
    Aie::Internal::SymbolsController controller;
    controller.setCanvasDocumentService(&documentService);
    documentService.open();

    Aie::Internal::SymbolsPanel panel(&controller);
    QApplication::processEvents();

    auto* addConstantButton = panel.findChild<QPushButton*>(QStringLiteral("AieSymbolsPrimaryButton"));
    ASSERT_NE(addConstantButton, nullptr);
    addConstantButton->click();
    QApplication::processEvents();

    auto* constantGroup = findGroupBoxByTitle(panel, QStringLiteral("Constant"));
    ASSERT_NE(constantGroup, nullptr);
    auto* constantNameEdit = findLineEditByPlaceholder(*constantGroup, QStringLiteral("Identifier"));
    auto* constantValueEdit = findLineEditByPlaceholder(*constantGroup, QStringLiteral("Integral value"));
    ASSERT_NE(constantNameEdit, nullptr);
    ASSERT_NE(constantValueEdit, nullptr);

    constantNameEdit->setText(QStringLiteral("N"));
    fireEditingFinished(constantNameEdit);
    constantValueEdit->setText(QStringLiteral("1024"));
    fireEditingFinished(constantValueEdit);
    QApplication::processEvents();

    auto* addTypeButton = findPushButtonByText(panel, QStringLiteral("New Type"));
    ASSERT_NE(addTypeButton, nullptr);
    addTypeButton->click();
    QApplication::processEvents();

    auto* typeGroup = findGroupBoxByTitle(panel, QStringLiteral("Type Abstraction"));
    ASSERT_NE(typeGroup, nullptr);
    auto* typeNameEdit = findLineEditByPlaceholder(*typeGroup, QStringLiteral("Identifier"));
    auto* axis0Edit = findLineEditByPlaceholder(*typeGroup, QStringLiteral("Literal or constant"), 0);
    ASSERT_NE(typeNameEdit, nullptr);
    ASSERT_NE(axis0Edit, nullptr);

    typeNameEdit->setText(QStringLiteral("in_ty"));
    fireEditingFinished(typeNameEdit);
    axis0Edit->setText(QStringLiteral("N"));
    fireEditingFinished(axis0Edit);

    auto* dtypeCombo = typeGroup->findChild<QComboBox*>();
    ASSERT_NE(dtypeCombo, nullptr);
    dtypeCombo->setCurrentText(QStringLiteral("int32"));

    auto* rankSpin = typeGroup->findChild<QSpinBox*>();
    ASSERT_NE(rankSpin, nullptr);
    rankSpin->setValue(2);
    QApplication::processEvents();

    auto* refreshedAxis0Edit = findLineEditByPlaceholder(*typeGroup, QStringLiteral("Literal or constant"), 0);
    auto* axis1Edit = findLineEditByPlaceholder(*typeGroup, QStringLiteral("Literal or constant"), 1);
    ASSERT_NE(refreshedAxis0Edit, nullptr);
    ASSERT_NE(axis1Edit, nullptr);
    axis1Edit->setText(QStringLiteral("1"));
    fireEditingFinished(axis1Edit);
    QApplication::processEvents();

    const QVector<Aie::Internal::SymbolRecord> symbols = controller.symbols();
    ASSERT_EQ(symbols.size(), 2);
    EXPECT_EQ(symbols.at(0).name, QStringLiteral("N"));
    EXPECT_EQ(symbols.at(0).constant.value, 1024);
    EXPECT_EQ(symbols.at(1).name, QStringLiteral("in_ty"));
    EXPECT_EQ(refreshedAxis0Edit->text(), QStringLiteral("N"));
    EXPECT_EQ(symbols.at(1).type.shapeTokens, (QStringList{QStringLiteral("N"), QStringLiteral("1")}));
}

TEST(SymbolsPanelTests, TapEditorCreatesAndUpdatesTapSymbols)
{
    ensureApp();

    FakeCanvasDocumentService documentService;
    Aie::Internal::SymbolsController controller;
    controller.setCanvasDocumentService(&documentService);
    documentService.open();

    Aie::Internal::SymbolsPanel panel(&controller);
    QApplication::processEvents();

    auto* addTapButton = findPushButtonByText(panel, QStringLiteral("New TAP"));
    ASSERT_NE(addTapButton, nullptr);
    addTapButton->click();
    QApplication::processEvents();

    auto* tapGroup = findGroupBoxByTitle(panel, QStringLiteral("Tensor Access Pattern"));
    ASSERT_NE(tapGroup, nullptr);
    auto* tapNameEdit = findLineEditByPlaceholder(*tapGroup, QStringLiteral("Identifier"));
    ASSERT_NE(tapNameEdit, nullptr);
    tapNameEdit->setText(QStringLiteral("tap_main"));
    fireEditingFinished(tapNameEdit);

    const auto spinBoxes = tapGroup->findChildren<QSpinBox*>();
    ASSERT_GE(spinBoxes.size(), 3);
    spinBoxes[0]->setValue(8);
    spinBoxes[1]->setValue(32);
    spinBoxes[2]->setValue(3);
    QApplication::processEvents();

    const QVector<Aie::Internal::SymbolRecord> symbols = controller.symbols();
    ASSERT_EQ(symbols.size(), 1);
    ASSERT_EQ(symbols.at(0).kind, Aie::Internal::SymbolKind::TensorAccessPattern);
    EXPECT_EQ(symbols.at(0).name, QStringLiteral("tap_main"));
    EXPECT_EQ(symbols.at(0).tap.rows, 8);
    EXPECT_EQ(symbols.at(0).tap.cols, 32);
    EXPECT_EQ(symbols.at(0).tap.offset, 3);
    EXPECT_EQ(symbols.at(0).tap.sizes, (QVector<int>{4, 4, 4}));
    EXPECT_EQ(symbols.at(0).tap.strides, (QVector<int>{16, 64, 1}));
}

TEST(SymbolsPanelTests, PanelWrapsContentInScrollAreaToPreserveUsableWidth)
{
    ensureApp();

    FakeCanvasDocumentService documentService;
    Aie::Internal::SymbolsController controller;
    controller.setCanvasDocumentService(&documentService);
    documentService.open();

    Aie::Internal::SymbolsPanel panel(&controller);

    auto* scrollArea = panel.findChild<QScrollArea*>(QStringLiteral("AieSymbolsScrollArea"));
    ASSERT_NE(scrollArea, nullptr);
    EXPECT_TRUE(scrollArea->widgetResizable());
    ASSERT_NE(scrollArea->widget(), nullptr);
    EXPECT_GE(scrollArea->widget()->minimumWidth(), 420);
    EXPECT_GE(scrollArea->widget()->minimumHeight(), 760);
}
