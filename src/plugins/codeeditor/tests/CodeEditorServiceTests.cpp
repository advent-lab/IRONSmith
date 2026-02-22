// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "codeeditor/CodeEditorTextView.hpp"
#include "codeeditor/api/ICodeEditorService.hpp"
#include "codeeditor/internal/CodeEditorServiceImpl.hpp"
#include "codeeditor/state/CodeEditorWorkspaceState.hpp"
#include "codeeditor/style/CodeEditorStyleManager.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtGui/QColor>
#include <QtGui/QPalette>
#include <QtTest/QSignalSpy>
#include <QtWidgets/QApplication>
#include <QtWidgets/QPlainTextEdit>

namespace {

QApplication* ensureApp()
{
    static QApplication* app = []() {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

        static int argc = 1;
        static char arg0[] = "codeeditor-service-tests";
        static char* argv[] = {arg0, nullptr};
        return new QApplication(argc, argv);
    }();
    return app;
}

QString writeFile(const QString& dir, const QString& name, const QString& content)
{
    const QString path = QDir(dir).filePath(name);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    EXPECT_EQ(file.write(content.toUtf8()), content.toUtf8().size());
    file.close();
    return path;
}

} // namespace

TEST(CodeEditorStyleManagerTests, LoadsDefaultThemeAndLanguageFallbacks)
{
    ensureApp();

    const auto styleManager = CodeEditor::Style::CodeEditorStyleManager::loadDefault();
    const auto& surface = styleManager.surfaceColors();

    EXPECT_TRUE(surface.paper.isValid());
    EXPECT_TRUE(surface.text.isValid());
    EXPECT_TRUE(surface.marginBaseBackground.isValid());
    EXPECT_TRUE(surface.lineNumberBackground.isValid());
    EXPECT_EQ(surface.paper, surface.lineNumberBackground);

    EXPECT_TRUE(styleManager.hasLanguagePalette(QStringLiteral("cpp")));
    EXPECT_GT(styleManager.resolvedStyleCount(QStringLiteral("cpp")), 0);
    EXPECT_EQ(styleManager.resolvedStyleCount(QStringLiteral("c")),
              styleManager.resolvedStyleCount(QStringLiteral("cpp")));
}

TEST(CodeEditorStyleManagerTests, AppliesSurfacePaletteToPlainTextEditor)
{
    ensureApp();

    const auto styleManager = CodeEditor::Style::CodeEditorStyleManager::loadDefault();
    QPlainTextEdit editor;

    styleManager.applyEditorView(&editor);

    const QPalette palette = editor.palette();
    const auto& surface = styleManager.surfaceColors();
    EXPECT_EQ(palette.color(QPalette::Base), surface.paper);
    EXPECT_EQ(palette.color(QPalette::Text), surface.text);
    EXPECT_EQ(palette.color(QPalette::Highlight), surface.selectionBackground);
    EXPECT_EQ(palette.color(QPalette::HighlightedText), surface.selectionForeground);
}

TEST(CodeEditorStyleManagerTests, LoadsThemeFromJsonFile)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString themeJson = QStringLiteral(R"({
        "surface": {
            "paper": "#111111",
            "text": "#EEEEEE",
            "lineNumberBackground": "#191919"
        },
        "languageFallbacks": {
            "c": "cpp"
        },
        "languages": {
            "cpp": {
                "styles": [
                    {"id": 1, "foreground": "#00FF00"}
                ]
            }
        }
    })");
    const QString themePath = writeFile(tempDir.path(), QStringLiteral("theme.json"), themeJson);

    const auto styleManager = CodeEditor::Style::CodeEditorStyleManager::loadFromJsonFile(themePath);
    const auto& surface = styleManager.surfaceColors();
    EXPECT_EQ(surface.paper, QColor(QStringLiteral("#111111")));
    EXPECT_EQ(surface.text, QColor(QStringLiteral("#EEEEEE")));
    EXPECT_EQ(surface.lineNumberBackground, QColor(QStringLiteral("#191919")));
    EXPECT_EQ(styleManager.resolvedStyleCount(QStringLiteral("c")), 1);
}

TEST(CodeEditorServiceTests, OpenFileCreatesSingleSessionPerPath)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString path = writeFile(tempDir.path(), QStringLiteral("main.cpp"), QStringLiteral("int main() { return 0; }\n"));

    CodeEditor::Internal::CodeEditorServiceImpl service;

    CodeEditor::Api::CodeEditorOpenRequest request;
    request.filePath = path;
    request.activate = true;

    CodeEditor::Api::CodeEditorSessionHandle first;
    const Utils::Result openFirst = service.openFile(request, first);
    ASSERT_TRUE(openFirst.ok) << openFirst.errors.join("\n").toStdString();
    ASSERT_TRUE(first.isValid());

    CodeEditor::Api::CodeEditorSessionHandle second;
    const Utils::Result openSecond = service.openFile(request, second);
    ASSERT_TRUE(openSecond.ok) << openSecond.errors.join("\n").toStdString();

    EXPECT_EQ(first.id, second.id);
    EXPECT_EQ(service.openFiles().size(), 1);
    EXPECT_TRUE(service.hasOpenFile());
    EXPECT_EQ(service.activeFile().id, first.id);
    EXPECT_FALSE(service.isDirty(first));
}

TEST(CodeEditorServiceTests, EditingMarksDirtyAndSaveClearsDirty)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString path = writeFile(tempDir.path(), QStringLiteral("kernel.cpp"), QStringLiteral("void kernel() {}\n"));

    CodeEditor::Internal::CodeEditorServiceImpl service;

    CodeEditor::Api::CodeEditorOpenRequest request;
    request.filePath = path;
    request.activate = true;

    CodeEditor::Api::CodeEditorSessionHandle handle;
    const Utils::Result openResult = service.openFile(request, handle);
    ASSERT_TRUE(openResult.ok) << openResult.errors.join("\n").toStdString();

    auto* view = qobject_cast<CodeEditor::CodeEditorTextView*>(service.widgetForSession(handle));
    ASSERT_NE(view, nullptr);

    QSignalSpy dirtySpy(&service, &CodeEditor::Api::ICodeEditorService::fileDirtyStateChanged);

    view->appendText(QStringLiteral("// modified"));
    QCoreApplication::processEvents();

    EXPECT_TRUE(service.isDirty(handle));
    EXPECT_GE(dirtySpy.count(), 1);

    const Utils::Result saveResult = service.saveFile(handle);
    ASSERT_TRUE(saveResult.ok) << saveResult.errors.join("\n").toStdString();
    EXPECT_FALSE(service.isDirty(handle));

    QFile verify(path);
    ASSERT_TRUE(verify.open(QIODevice::ReadOnly));
    const QString diskText = QString::fromUtf8(verify.readAll());
    EXPECT_TRUE(diskText.contains(QStringLiteral("modified")));
}

TEST(CodeEditorServiceTests, UpdateFilePathTracksRenamedSession)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString oldPath = writeFile(tempDir.path(), QStringLiteral("a.cpp"), QStringLiteral("int a = 1;\n"));
    const QString newPath = QDir(tempDir.path()).filePath(QStringLiteral("b.cpp"));

    CodeEditor::Internal::CodeEditorServiceImpl service;

    CodeEditor::Api::CodeEditorOpenRequest request;
    request.filePath = oldPath;

    CodeEditor::Api::CodeEditorSessionHandle handle;
    const Utils::Result openResult = service.openFile(request, handle);
    ASSERT_TRUE(openResult.ok) << openResult.errors.join("\n").toStdString();

    ASSERT_TRUE(QFile::rename(oldPath, newPath));

    QSignalSpy pathSpy(&service, &CodeEditor::Api::ICodeEditorService::filePathChanged);
    const Utils::Result updateResult = service.updateFilePath(handle, newPath);
    ASSERT_TRUE(updateResult.ok) << updateResult.errors.join("\n").toStdString();

    const auto sessions = service.openFiles();
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(QDir::cleanPath(sessions.front().filePath), QDir::cleanPath(newPath));
    EXPECT_GE(pathSpy.count(), 1);
}

TEST(CodeEditorServiceTests, CloseAllFilesClearsActiveState)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString pathA = writeFile(tempDir.path(), QStringLiteral("a.cpp"), QStringLiteral("int a = 1;\n"));
    const QString pathB = writeFile(tempDir.path(), QStringLiteral("b.cpp"), QStringLiteral("int b = 2;\n"));

    CodeEditor::Internal::CodeEditorServiceImpl service;

    CodeEditor::Api::CodeEditorOpenRequest requestA;
    requestA.filePath = pathA;
    requestA.activate = true;

    CodeEditor::Api::CodeEditorOpenRequest requestB;
    requestB.filePath = pathB;
    requestB.activate = true;

    CodeEditor::Api::CodeEditorSessionHandle handleA;
    CodeEditor::Api::CodeEditorSessionHandle handleB;

    ASSERT_TRUE(service.openFile(requestA, handleA).ok);
    ASSERT_TRUE(service.openFile(requestB, handleB).ok);

    ASSERT_TRUE(service.hasOpenFile());
    ASSERT_EQ(service.openFiles().size(), 2);

    const Utils::Result closeAll = service.closeAllFiles(CodeEditor::Api::CodeEditorCloseReason::WorkspaceChanged);
    ASSERT_TRUE(closeAll.ok) << closeAll.errors.join("\n").toStdString();

    EXPECT_FALSE(service.hasOpenFile());
    EXPECT_TRUE(service.openFiles().isEmpty());
    EXPECT_FALSE(service.activeFile().isValid());
}

TEST(CodeEditorServiceTests, ReadOnlyOpenRequestDisallowsSave)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString path = writeFile(tempDir.path(), QStringLiteral("readonly.cpp"), QStringLiteral("int v = 7;\n"));

    CodeEditor::Internal::CodeEditorServiceImpl service;

    CodeEditor::Api::CodeEditorOpenRequest request;
    request.filePath = path;
    request.activate = true;
    request.readOnly = true;

    CodeEditor::Api::CodeEditorSessionHandle handle;
    const Utils::Result openResult = service.openFile(request, handle);
    ASSERT_TRUE(openResult.ok) << openResult.errors.join("\n").toStdString();
    ASSERT_TRUE(handle.readOnly);

    const Utils::Result saveResult = service.saveFile(handle);
    EXPECT_FALSE(saveResult.ok);
    EXPECT_TRUE(saveResult.errors.join("\n").contains(QStringLiteral("read-only")));
}

TEST(CodeEditorServiceTests, ClosingActiveSessionPromotesPreviousSession)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString pathA = writeFile(tempDir.path(), QStringLiteral("a.cpp"), QStringLiteral("int a = 1;\n"));
    const QString pathB = writeFile(tempDir.path(), QStringLiteral("b.cpp"), QStringLiteral("int b = 2;\n"));

    CodeEditor::Internal::CodeEditorServiceImpl service;

    CodeEditor::Api::CodeEditorOpenRequest requestA;
    requestA.filePath = pathA;
    requestA.activate = true;

    CodeEditor::Api::CodeEditorOpenRequest requestB;
    requestB.filePath = pathB;
    requestB.activate = true;

    CodeEditor::Api::CodeEditorSessionHandle handleA;
    CodeEditor::Api::CodeEditorSessionHandle handleB;

    ASSERT_TRUE(service.openFile(requestA, handleA).ok);
    ASSERT_TRUE(service.openFile(requestB, handleB).ok);
    ASSERT_EQ(service.activeFile().id, handleB.id);

    const Utils::Result closeResult = service.closeFile(handleB, CodeEditor::Api::CodeEditorCloseReason::UserClosed);
    ASSERT_TRUE(closeResult.ok) << closeResult.errors.join("\n").toStdString();

    ASSERT_TRUE(service.activeFile().isValid());
    EXPECT_EQ(service.activeFile().id, handleA.id);
}

TEST(CodeEditorServiceTests, UpdateFilePathRejectsPathAlreadyOpenInAnotherSession)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString pathA = writeFile(tempDir.path(), QStringLiteral("a.cpp"), QStringLiteral("int a = 1;\n"));
    const QString pathB = writeFile(tempDir.path(), QStringLiteral("b.cpp"), QStringLiteral("int b = 2;\n"));

    CodeEditor::Internal::CodeEditorServiceImpl service;

    CodeEditor::Api::CodeEditorOpenRequest requestA;
    requestA.filePath = pathA;
    requestA.activate = false;

    CodeEditor::Api::CodeEditorOpenRequest requestB;
    requestB.filePath = pathB;
    requestB.activate = false;

    CodeEditor::Api::CodeEditorSessionHandle handleA;
    CodeEditor::Api::CodeEditorSessionHandle handleB;

    ASSERT_TRUE(service.openFile(requestA, handleA).ok);
    ASSERT_TRUE(service.openFile(requestB, handleB).ok);

    const Utils::Result updateResult = service.updateFilePath(handleA, pathB);
    EXPECT_FALSE(updateResult.ok);
    EXPECT_TRUE(updateResult.errors.join("\n").contains(QStringLiteral("already open")));
}

TEST(CodeEditorServiceTests, ZoomLevelSyncsAcrossOpenAndFutureSessions)
{
    ensureApp();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString pathA = writeFile(tempDir.path(), QStringLiteral("a.cpp"), QStringLiteral("int a = 1;\n"));
    const QString pathB = writeFile(tempDir.path(), QStringLiteral("b.cpp"), QStringLiteral("int b = 2;\n"));
    const QString pathC = writeFile(tempDir.path(), QStringLiteral("c.cpp"), QStringLiteral("int c = 3;\n"));

    CodeEditor::Internal::CodeEditorServiceImpl service;

    CodeEditor::Api::CodeEditorOpenRequest openA;
    openA.filePath = pathA;
    openA.activate = true;
    CodeEditor::Api::CodeEditorSessionHandle handleA;
    ASSERT_TRUE(service.openFile(openA, handleA).ok);

    CodeEditor::Api::CodeEditorOpenRequest openB;
    openB.filePath = pathB;
    openB.activate = true;
    CodeEditor::Api::CodeEditorSessionHandle handleB;
    ASSERT_TRUE(service.openFile(openB, handleB).ok);

    auto* viewA = qobject_cast<CodeEditor::CodeEditorTextView*>(service.widgetForSession(handleA));
    auto* viewB = qobject_cast<CodeEditor::CodeEditorTextView*>(service.widgetForSession(handleB));
    ASSERT_NE(viewA, nullptr);
    ASSERT_NE(viewB, nullptr);

    viewA->zoomInEditor(3);
    QCoreApplication::processEvents();

    EXPECT_EQ(service.zoomLevel(), 3);
    EXPECT_EQ(viewB->zoomLevel(), 3);

    CodeEditor::Api::CodeEditorOpenRequest openC;
    openC.filePath = pathC;
    openC.activate = false;
    CodeEditor::Api::CodeEditorSessionHandle handleC;
    ASSERT_TRUE(service.openFile(openC, handleC).ok);

    auto* viewC = qobject_cast<CodeEditor::CodeEditorTextView*>(service.widgetForSession(handleC));
    ASSERT_NE(viewC, nullptr);
    EXPECT_EQ(viewC->zoomLevel(), 3);

    service.setZoomLevel(-2);
    EXPECT_EQ(viewA->zoomLevel(), -2);
    EXPECT_EQ(viewB->zoomLevel(), -2);
    EXPECT_EQ(viewC->zoomLevel(), -2);
}

TEST(CodeEditorWorkspaceStateTests, PersistsPerRootWorkspaceSnapshot)
{
    ensureApp();

    QTemporaryDir stateDir;
    ASSERT_TRUE(stateDir.isValid());

    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    cfg.globalConfigRootOverride = stateDir.path();

    CodeEditor::Internal::CodeEditorWorkspaceState state{Utils::Environment(cfg)};

    CodeEditor::Internal::CodeEditorWorkspaceState::Snapshot snapshotA;
    snapshotA.panelOpen = true;
    snapshotA.zoomLevel = 4;
    snapshotA.openFiles = {QStringLiteral("/tmp/workspaceA/main.cpp"),
                           QStringLiteral("/tmp/workspaceA/kernel.cpp")};
    snapshotA.activeFilePath = QStringLiteral("/tmp/workspaceA/kernel.cpp");

    CodeEditor::Internal::CodeEditorWorkspaceState::Snapshot snapshotB;
    snapshotB.panelOpen = false;
    snapshotB.zoomLevel = -1;
    snapshotB.openFiles = {QStringLiteral("/tmp/workspaceB/only.cpp")};
    snapshotB.activeFilePath = QStringLiteral("/tmp/workspaceB/only.cpp");

    state.saveForRoot(QStringLiteral("/tmp/workspaceA"), snapshotA);
    state.saveForRoot(QStringLiteral("/tmp/workspaceB"), snapshotB);

    const auto loadedA = state.loadForRoot(QStringLiteral("/tmp/workspaceA"));
    EXPECT_TRUE(loadedA.panelOpen);
    EXPECT_EQ(loadedA.zoomLevel, 4);
    EXPECT_EQ(loadedA.openFiles.size(), 2);
    EXPECT_EQ(loadedA.activeFilePath, QStringLiteral("/tmp/workspaceA/kernel.cpp"));

    const auto loadedB = state.loadForRoot(QStringLiteral("/tmp/workspaceB"));
    EXPECT_FALSE(loadedB.panelOpen);
    EXPECT_EQ(loadedB.zoomLevel, -1);
    EXPECT_EQ(loadedB.openFiles.size(), 1);
    EXPECT_EQ(loadedB.activeFilePath, QStringLiteral("/tmp/workspaceB/only.cpp"));
}
