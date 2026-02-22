// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/KernelPreviewUtils.hpp"

#include "aieplugin/panels/KernelPreviewDialog.hpp"

#include "codeeditor/api/CodeEditorTypes.hpp"
#include "codeeditor/api/ICodeEditorService.hpp"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryFile>

#include <limits>

namespace Aie::Internal::KernelPreview {

namespace {

QString readTextFile(const QString& path, qint64 maxBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QByteArray bytes = file.read(maxBytes > 0 ? maxBytes : std::numeric_limits<qint64>::max());
    return QString::fromUtf8(bytes);
}

QString fallbackSnippet(const KernelAsset& kernel)
{
    if (!kernel.signature.trimmed().isEmpty())
        return kernel.signature.trimmed();
    if (!kernel.description.trimmed().isEmpty())
        return kernel.description.trimmed();
    return QStringLiteral("No signature available.");
}

} // namespace

QString loadCodeText(const KernelAsset& kernel)
{
    const QString code = readTextFile(kernel.absoluteEntryPath(), 512 * 1024);
    if (!code.isEmpty())
        return code;

    return QStringLiteral("Unable to read code file: %1")
        .arg(kernel.absoluteEntryPath());
}

QString loadCodePreview(const KernelAsset& kernel)
{
    const QString fullCode = loadCodeText(kernel);
    if (fullCode.isEmpty())
        return fallbackSnippet(kernel);

    const QStringList lines = fullCode.split('\n');
    QStringList previewLines;
    previewLines.reserve(3);
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        previewLines.push_back(trimmed);
        if (previewLines.size() >= 3)
            break;
    }

    if (previewLines.isEmpty())
        return fallbackSnippet(kernel);

    return previewLines.join(QStringLiteral("\n"));
}

QString formatMetadataText(const KernelAsset& kernel)
{
    QJsonObject metadata = kernel.metadata;
    metadata.insert(QStringLiteral("_scope"), kernelScopeName(kernel.scope));
    metadata.insert(QStringLiteral("_directory"), kernel.directoryPath);
    metadata.insert(QStringLiteral("_entryAbsolutePath"), kernel.absoluteEntryPath());

    const QJsonDocument doc(metadata);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

void initializeDialogContent(KernelPreviewDialog& dialog,
                             const KernelAsset& kernel,
                             CodeEditor::Api::ICodeEditorService* codeEditorService)
{
    const QString metadataText = formatMetadataText(kernel);

    if (codeEditorService) {
        CodeEditor::Api::CodeEditorQuickViewRequest codeRequest;
        codeRequest.filePath = kernel.absoluteEntryPath();
        codeRequest.languageHint = kernel.language;
        if (QWidget* quickView = codeEditorService->createQuickView(codeRequest, &dialog))
            dialog.setCodeWidget(quickView);
        else
            dialog.setCodeText(loadCodeText(kernel));

        bool metadataQuickViewReady = false;
        QTemporaryFile metadataFile;
        metadataFile.setFileTemplate(QStringLiteral("ironsmith-kernel-metadata-XXXXXX.json"));
        if (metadataFile.open()) {
            const QByteArray metadataBytes = metadataText.toUtf8();
            if (metadataFile.write(metadataBytes) == metadataBytes.size()) {
                metadataFile.flush();

                CodeEditor::Api::CodeEditorQuickViewRequest metadataRequest;
                metadataRequest.filePath = metadataFile.fileName();
                metadataRequest.languageHint = QStringLiteral("json");
                if (QWidget* metadataView = codeEditorService->createQuickView(metadataRequest, &dialog)) {
                    dialog.setMetadataWidget(metadataView);
                    metadataQuickViewReady = true;
                }
            }
        }

        if (!metadataQuickViewReady)
            dialog.setMetadataText(metadataText);
        return;
    }

    dialog.setCodeText(loadCodeText(kernel));
    dialog.setMetadataText(metadataText);
}

} // namespace Aie::Internal::KernelPreview
