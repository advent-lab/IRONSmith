// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"

#include <utils/Result.hpp>

#include <QtCore/QDir>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace ProjectExplorer::Internal {

class PROJECTEXPLORER_EXPORT ProjectExplorerFileSystemService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY rootPathChanged)

public:
    enum class Operation {
        Open,
        Rename,
        Delete,
        Duplicate,
        NewFolder,
        NewDesign,
        ImportAsset,
        Reveal
    };
    Q_ENUM(Operation)

    explicit ProjectExplorerFileSystemService(QObject* parent = nullptr);

    QString rootPath() const;
    void setRootPath(const QString& path);

    Utils::Result openPath(const QString& relPath);
    Utils::Result revealPath(const QString& relPath);

    Utils::Result renamePath(const QString& relPath, const QString& newName, QString* outNewRelPath = nullptr);
    Utils::Result removePath(const QString& relPath);
    Utils::Result duplicatePath(const QString& relPath, QString* outNewRelPath = nullptr);

    Utils::Result createFolder(const QString& parentRelPath, const QString& name, QString* outNewRelPath = nullptr);
    Utils::Result createDesign(const QString& parentRelPath, const QString& name, QString* outNewRelPath = nullptr);
    Utils::Result importAssets(const QString& parentRelPath,
                               const QStringList& sourcePaths,
                               QStringList* outNewRelPaths = nullptr);

signals:
    void rootPathChanged(const QString& path);
    void operationCompleted(ProjectExplorerFileSystemService::Operation op, const QString& path, const QString& newPath);
    void operationFailed(ProjectExplorerFileSystemService::Operation op, const QString& path, const QString& error);
    void refreshRequested();

private:
    QString absolutePathFor(const QString& relPath) const;
    QString resolveTargetDirectory(const QString& relPath) const;

    static Utils::Result copyRecursively(const QString& source, const QString& dest);

    Utils::Result ensureRoot(QString* error) const;

    QString m_rootPath;
};

} // namespace ProjectExplorer::Internal
