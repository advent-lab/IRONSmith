// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace CodeEditor::Internal {

class CodeEditorWorkspaceState final
{
public:
    struct Snapshot final {
        bool panelOpen = false;
        int zoomLevel = 0;
        QStringList openFiles;
        QString activeFilePath;
    };

    CodeEditorWorkspaceState();
    explicit CodeEditorWorkspaceState(Utils::Environment environment);

    Snapshot loadForRoot(const QString& rootPath) const;
    void saveForRoot(const QString& rootPath, const Snapshot& snapshot);
    void clearForRoot(const QString& rootPath);

    static Utils::Environment makeEnvironment();

private:
    Utils::Environment m_env;
};

} // namespace CodeEditor::Internal
