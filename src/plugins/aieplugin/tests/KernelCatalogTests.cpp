// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/kernels/KernelCatalog.hpp"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

namespace {

bool writeTextFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (file.write(contents) != contents.size())
        return false;
    return true;
}

bool writeKernel(const QString& root,
                 const QString& folder,
                 const QString& id,
                 const QString& name,
                 const QString& entry,
                 const QString& signature = QStringLiteral("void kernel();"))
{
    QDir rootDir(root);
    if (!rootDir.mkpath(folder))
        return false;

    const QString kernelDir = rootDir.filePath(folder);
    const QString sourcePath = QDir(kernelDir).filePath(entry);
    if (!writeTextFile(sourcePath, QByteArrayLiteral("void kernel() {}\n")))
        return false;

    QJsonObject kernelJson;
    kernelJson.insert(QStringLiteral("id"), id);
    kernelJson.insert(QStringLiteral("name"), name);
    kernelJson.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    kernelJson.insert(QStringLiteral("language"), QStringLiteral("cpp"));
    kernelJson.insert(QStringLiteral("signature"), signature);
    kernelJson.insert(QStringLiteral("description"), QStringLiteral("test kernel"));
    kernelJson.insert(QStringLiteral("entry"), entry);
    kernelJson.insert(QStringLiteral("files"), QJsonArray{entry});
    kernelJson.insert(QStringLiteral("tags"), QJsonArray{QStringLiteral("math")});

    const QByteArray bytes = QJsonDocument(kernelJson).toJson(QJsonDocument::Indented);
    return writeTextFile(QDir(kernelDir).filePath(QStringLiteral("kernel.json")), bytes);
}

} // namespace

TEST(KernelCatalogTests, MergesByScopePrecedence)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    const QString builtInRoot = QDir(temp.path()).filePath(QStringLiteral("builtin"));
    const QString globalRoot = QDir(temp.path()).filePath(QStringLiteral("global"));
    const QString workspaceRoot = QDir(temp.path()).filePath(QStringLiteral("workspace"));

    ASSERT_TRUE(writeKernel(builtInRoot,
                            QStringLiteral("gemm_bf16"),
                            QStringLiteral("gemm_bf16"),
                            QStringLiteral("Builtin GEMM"),
                            QStringLiteral("gemm_bf16.cpp")));
    ASSERT_TRUE(writeKernel(globalRoot,
                            QStringLiteral("gemm_bf16"),
                            QStringLiteral("gemm_bf16"),
                            QStringLiteral("Global GEMM"),
                            QStringLiteral("gemm_global.cpp")));
    ASSERT_TRUE(writeKernel(workspaceRoot,
                            QStringLiteral("gemm_bf16"),
                            QStringLiteral("gemm_bf16"),
                            QStringLiteral("Workspace GEMM"),
                            QStringLiteral("gemm_workspace.cpp")));
    ASSERT_TRUE(writeKernel(builtInRoot,
                            QStringLiteral("fft_fp32"),
                            QStringLiteral("fft_fp32"),
                            QStringLiteral("FFT"),
                            QStringLiteral("fft_fp32.cpp")));

    Aie::Internal::KernelCatalogScanRequest request;
    request.builtInRoot = builtInRoot;
    request.globalRoot = globalRoot;
    request.workspaceRoot = workspaceRoot;

    QVector<Aie::Internal::KernelAsset> kernels;
    QStringList warnings;
    const Utils::Result result = Aie::Internal::scanKernelCatalog(request, kernels, &warnings);
    ASSERT_TRUE(result.ok) << result.errors.join("\n").toStdString();

    ASSERT_EQ(kernels.size(), 2);

    const Aie::Internal::KernelAsset* gemm = Aie::Internal::findKernelById(kernels, QStringLiteral("gemm_bf16"));
    ASSERT_NE(gemm, nullptr);
    EXPECT_EQ(gemm->scope, Aie::Internal::KernelSourceScope::Workspace);
    EXPECT_EQ(gemm->name, QStringLiteral("Workspace GEMM"));
    EXPECT_TRUE(gemm->absoluteEntryPath().endsWith(QStringLiteral("gemm_workspace.cpp")));

    const Aie::Internal::KernelAsset* fft = Aie::Internal::findKernelById(kernels, QStringLiteral("fft_fp32"));
    ASSERT_NE(fft, nullptr);
    EXPECT_EQ(fft->scope, Aie::Internal::KernelSourceScope::BuiltIn);

    EXPECT_FALSE(warnings.isEmpty());
}

TEST(KernelCatalogTests, SkipsInvalidKernelFolders)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    const QString builtInRoot = QDir(temp.path()).filePath(QStringLiteral("builtin"));
    QDir().mkpath(QDir(builtInRoot).filePath(QStringLiteral("broken_kernel")));

    Aie::Internal::KernelCatalogScanRequest request;
    request.builtInRoot = builtInRoot;

    QVector<Aie::Internal::KernelAsset> kernels;
    QStringList warnings;
    const Utils::Result result = Aie::Internal::scanKernelCatalog(request, kernels, &warnings);
    ASSERT_TRUE(result.ok) << result.errors.join("\n").toStdString();

    EXPECT_TRUE(kernels.isEmpty());
    EXPECT_FALSE(warnings.isEmpty());
}
