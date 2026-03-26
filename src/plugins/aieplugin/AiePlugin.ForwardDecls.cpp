// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace ExtensionSystem {
class PluginManager;
}

namespace Core {
class IUiHost;
class ISidebarRegistry;
class IHeaderInfo;
}

namespace Canvas::Api {
class ICanvasGridHost;
class ICanvasHost;
class ICanvasDocumentService;
class ICanvasStyleHost;
}

namespace ProjectExplorer {
class IProjectExplorer;
}

namespace CodeEditor::Api {
class ICodeEditorService;
}

namespace Aie::Internal {
class AieService;
class AiePanelState;
class AiePropertiesShortcutController;
class DesignOpenController;
class DesignBundleLoader;
class CanvasDocumentImporter;
class KernelRegistryService;
class KernelAssignmentController;
class KernelToolboxController;
class SymbolsController;
}
