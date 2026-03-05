// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/kernels/KernelCatalog.hpp"

#include <utils/Result.hpp>

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QVector>

namespace Aie::Internal {

class AIEPLUGIN_EXPORT KernelRegistryService final : public QObject
{
    Q_OBJECT

public:
    struct KernelCreateRequest final {
        QString id;
        QString name;
        QString signature;
        QString description;
        QStringList tags;
    };

    explicit KernelRegistryService(QObject* parent = nullptr);

    const QVector<KernelAsset>& kernels() const { return m_kernels; }
    const KernelAsset* kernelById(const QString& kernelId) const;

    QString builtInRoot() const { return m_builtInRoot; }
    QString globalUserRoot() const { return m_globalUserRoot; }
    QString workspaceUserRoot() const { return m_workspaceUserRoot; }
    QString workspaceRoot() const { return m_workspaceRoot; }

    void setBuiltInRoot(const QString& rootPath);
    void setGlobalUserRoot(const QString& rootPath);
    void setWorkspaceRoot(const QString& workspaceRoot);

    Utils::Result reload();

    Utils::Result copyKernelToScope(const QString& kernelId,
                                    KernelSourceScope scope,
                                    KernelAsset* outCopiedKernel = nullptr);
    Utils::Result createKernelInScope(const KernelCreateRequest& request,
                                      KernelSourceScope scope,
                                      KernelAsset* outCreatedKernel = nullptr);

    static QString detectBuiltInRoot();
    static QString detectGlobalUserRoot();
    static QString workspaceKernelRootForWorkspace(const QString& workspaceRoot);

signals:
    void kernelsChanged();
    void warningsUpdated(const QStringList& warnings);
    void rootsChanged();

private:
    static QString cleanPath(const QString& path);

    Utils::Result ensureRootDirectory(const QString& path) const;
    Utils::Result copyDirectoryRecursively(const QString& sourceDir, const QString& targetDir) const;

    QString rootForScope(KernelSourceScope scope) const;

    QVector<KernelAsset> m_kernels;
    QStringList m_warnings;

    QString m_builtInRoot;
    QString m_globalUserRoot;
    QString m_workspaceRoot;
    QString m_workspaceUserRoot;
};

} // namespace Aie::Internal
