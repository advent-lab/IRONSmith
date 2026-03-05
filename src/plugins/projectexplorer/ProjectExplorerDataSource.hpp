// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"
#include "projectexplorer/api/ProjectExplorerTypes.hpp"

#include <utils/EnvironmentQtPolicy.hpp>
#include <utils/filesystem/PathPatternMatcher.hpp>
#include <utils/filesystem/RecursiveFileSystemWatcher.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <atomic>

namespace ProjectExplorer::Internal {

class PROJECTEXPLORER_EXPORT ProjectExplorerDataSource final : public QObject
{
    Q_OBJECT

public:
    explicit ProjectExplorerDataSource(QObject* parent = nullptr);
    ProjectExplorerDataSource(Utils::Environment environment, QObject* parent = nullptr);

    QString rootPath() const;
    void setRootPath(const QString& path);

    void refresh();
    static ProjectExplorer::ProjectEntryList scanEntries(const QString& rootPath,
                                                         const Utils::PathPatternMatcher& matcher);

signals:
    void rootLabelChanged(const QString& label);
    void rootPathChanged(const QString& path);
    void entriesChanged(const ProjectExplorer::ProjectEntryList& entries);

private:
    static Utils::Environment makeEnvironment();
    static ProjectExplorer::ProjectEntryKind classifyPath(const QString& relPath, bool isDir);
    static QStringList loadIgnoreFile(const QString& rootPath, const QString& fileName);
    QStringList buildIgnorePatterns() const;
    void initialize();

    Utils::Environment m_env;
    Utils::RecursiveFileSystemWatcher* m_watcher = nullptr;
    QString m_rootPath;
    Utils::PathPatternMatcher m_ignoreMatcher;
    QStringList m_ignorePatterns;
    bool m_useGitIgnore = true;
    bool m_useIronIgnore = true;
    std::atomic<quint64> m_scanGeneration{0};
};

} // namespace ProjectExplorer::Internal
