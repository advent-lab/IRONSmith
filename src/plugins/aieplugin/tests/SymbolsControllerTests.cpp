// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/symbol_table/SymbolsController.hpp"

#include "canvas/api/ICanvasDocumentService.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace {

QCoreApplication* ensureCoreApp()
{
    if (auto* existing = QCoreApplication::instance())
        return existing;

    static int argc = 1;
    static char appName[] = "AiePluginTests";
    static char* argv[] = {appName, nullptr};
    static QCoreApplication app(argc, argv);
    return &app;
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

} // namespace

TEST(SymbolsControllerTests, CreatesAndPersistsSymbolsIntoDocumentMetadata)
{
    ensureCoreApp();

    FakeCanvasDocumentService documentService;
    Aie::Internal::SymbolsController controller;
    controller.setCanvasDocumentService(&documentService);
    documentService.open(QJsonObject{{QStringLiteral("deviceId"), QStringLiteral("phoenix")}});

    QString constantId;
    const Utils::Result constantResult = controller.createConstant(&constantId);
    ASSERT_TRUE(constantResult.ok) << constantResult.errors.join("\n").toStdString();

    auto constant = *controller.symbolById(constantId);
    constant.name = QStringLiteral("N");
    constant.constant.value = 1024;
    const Utils::Result updateConstant = controller.updateSymbol(constant);
    ASSERT_TRUE(updateConstant.ok) << updateConstant.errors.join("\n").toStdString();

    QString typeId;
    const Utils::Result typeResult = controller.createTypeAbstraction(&typeId);
    ASSERT_TRUE(typeResult.ok) << typeResult.errors.join("\n").toStdString();

    auto type = *controller.symbolById(typeId);
    type.name = QStringLiteral("in_ty");
    type.type.shapeTokens = {QStringLiteral("N")};
    type.type.dtype = QStringLiteral("int32");
    const Utils::Result updateType = controller.updateSymbol(type);
    ASSERT_TRUE(updateType.ok) << updateType.errors.join("\n").toStdString();

    const QJsonObject metadata = documentService.activeMetadata();
    EXPECT_EQ(metadata.value(QStringLiteral("schema")).toString(), QStringLiteral("aie.spec/1"));
    const QJsonObject symbols = metadata.value(QStringLiteral("symbols")).toObject();
    ASSERT_FALSE(symbols.isEmpty());
    EXPECT_EQ(symbols.value(QStringLiteral("schema")).toString(), QStringLiteral("aie.symbols/1"));
    EXPECT_EQ(symbols.value(QStringLiteral("schemaVersion")).toInt(), 1);
    EXPECT_EQ(controller.symbols().size(), 2);
}

TEST(SymbolsControllerTests, RenamingConstantUpdatesDependentTypeDimensions)
{
    ensureCoreApp();

    FakeCanvasDocumentService documentService;
    Aie::Internal::SymbolsController controller;
    controller.setCanvasDocumentService(&documentService);
    documentService.open();

    QString constantId;
    ASSERT_TRUE(controller.createConstant(&constantId).ok);
    auto constant = *controller.symbolById(constantId);
    constant.name = QStringLiteral("N");
    constant.constant.value = 256;
    ASSERT_TRUE(controller.updateSymbol(constant).ok);

    QString typeId;
    ASSERT_TRUE(controller.createTypeAbstraction(&typeId).ok);
    auto type = *controller.symbolById(typeId);
    type.name = QStringLiteral("out_ty");
    type.type.shapeTokens = {QStringLiteral("N")};
    ASSERT_TRUE(controller.updateSymbol(type).ok);

    constant = *controller.symbolById(constantId);
    constant.name = QStringLiteral("M");
    const Utils::Result renameResult = controller.updateSymbol(constant);
    ASSERT_TRUE(renameResult.ok) << renameResult.errors.join("\n").toStdString();

    const auto* renamedType = controller.symbolById(typeId);
    ASSERT_NE(renamedType, nullptr);
    ASSERT_EQ(renamedType->type.shapeTokens.size(), 1);
    EXPECT_EQ(renamedType->type.shapeTokens.front(), QStringLiteral("M"));
}

TEST(SymbolsControllerTests, RejectsRemovingConstantWhenTypesStillReferenceIt)
{
    ensureCoreApp();

    FakeCanvasDocumentService documentService;
    Aie::Internal::SymbolsController controller;
    controller.setCanvasDocumentService(&documentService);
    documentService.open();

    QString constantId;
    ASSERT_TRUE(controller.createConstant(&constantId).ok);
    auto constant = *controller.symbolById(constantId);
    constant.name = QStringLiteral("N");
    constant.constant.value = 64;
    ASSERT_TRUE(controller.updateSymbol(constant).ok);

    QString typeId;
    ASSERT_TRUE(controller.createTypeAbstraction(&typeId).ok);
    auto type = *controller.symbolById(typeId);
    type.name = QStringLiteral("buffer_ty");
    type.type.shapeTokens = {QStringLiteral("N")};
    ASSERT_TRUE(controller.updateSymbol(type).ok);

    const Utils::Result removeResult = controller.removeSymbol(constantId);
    EXPECT_FALSE(removeResult.ok);
    EXPECT_TRUE(removeResult.errors.join("\n").contains(QStringLiteral("referenced")));
}

TEST(SymbolsControllerTests, CreatesAndPersistsTensorAccessPatterns)
{
    ensureCoreApp();

    FakeCanvasDocumentService documentService;
    Aie::Internal::SymbolsController controller;
    controller.setCanvasDocumentService(&documentService);
    documentService.open();

    QString tapId;
    const Utils::Result createResult = controller.createTensorAccessPattern(&tapId);
    ASSERT_TRUE(createResult.ok) << createResult.errors.join("\n").toStdString();

    auto tap = *controller.symbolById(tapId);
    tap.name = QStringLiteral("tap_main");
    tap.tap.rows = 8;
    tap.tap.cols = 16;
    tap.tap.offset = 2;
    tap.tap.sizes = {4, 2, 2};
    tap.tap.strides = {16, 8, 1};
    tap.tap.showRepetitions = false;
    const Utils::Result updateResult = controller.updateSymbol(tap);
    ASSERT_TRUE(updateResult.ok) << updateResult.errors.join("\n").toStdString();

    const QJsonObject symbols = documentService.activeMetadata().value(QStringLiteral("symbols")).toObject();
    const QJsonArray entries = symbols.value(QStringLiteral("entries")).toArray();
    ASSERT_EQ(entries.size(), 1);
    const QJsonObject entry = entries.first().toObject();
    EXPECT_EQ(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("tap"));
    EXPECT_EQ(entry.value(QStringLiteral("rows")).toInt(), 8);
    EXPECT_EQ(entry.value(QStringLiteral("cols")).toInt(), 16);
    EXPECT_EQ(entry.value(QStringLiteral("offset")).toInt(), 2);
    EXPECT_FALSE(entry.value(QStringLiteral("showRepetitions")).toBool(true));
}
