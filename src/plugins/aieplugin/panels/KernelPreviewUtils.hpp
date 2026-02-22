// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/kernels/KernelCatalog.hpp"

#include <QtCore/QString>

namespace CodeEditor::Api {
class ICodeEditorService;
}

namespace Aie::Internal {
class KernelPreviewDialog;
}

namespace Aie::Internal::KernelPreview {

QString loadCodeText(const KernelAsset& kernel);
QString loadCodePreview(const KernelAsset& kernel);
QString formatMetadataText(const KernelAsset& kernel);
void initializeDialogContent(KernelPreviewDialog& dialog,
                             const KernelAsset& kernel,
                             CodeEditor::Api::ICodeEditorService* codeEditorService);

} // namespace Aie::Internal::KernelPreview
