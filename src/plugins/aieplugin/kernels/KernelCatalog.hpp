// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utils/Result.hpp>

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

namespace Aie::Internal {

enum class KernelSourceScope : unsigned char {
    BuiltIn,
    Global,
    Workspace
};

struct KernelAsset final {
    QString id;
    QString name;
    QString version;
    QString language;
    QString description;
    QString signature;

    QString entryFile;
    QStringList files;
    QStringList tags;

    KernelSourceScope scope = KernelSourceScope::BuiltIn;
    QString rootPath;
    QString directoryPath;

    QJsonObject metadata;

    bool isValid() const;
    QString absoluteEntryPath() const;
};

struct KernelCatalogScanRequest final {
    QString builtInRoot;
    QString globalRoot;
    QString workspaceRoot;
};

QString kernelScopeName(KernelSourceScope scope);

Utils::Result scanKernelCatalog(const KernelCatalogScanRequest& request,
                                QVector<KernelAsset>& outKernels,
                                QStringList* outWarnings = nullptr);

const KernelAsset* findKernelById(const QVector<KernelAsset>& kernels, const QString& kernelId);

} // namespace Aie::Internal
