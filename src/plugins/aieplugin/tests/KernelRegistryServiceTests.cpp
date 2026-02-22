// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/kernels/KernelRegistryService.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>

TEST(KernelRegistryServiceTests, CreatesWorkspaceKernelScaffold)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    const QString workspaceRoot = QDir(temp.path()).filePath(QStringLiteral("workspace"));
    const QString globalRoot = QDir(temp.path()).filePath(QStringLiteral("global"));
    ASSERT_TRUE(QDir().mkpath(workspaceRoot));
    ASSERT_TRUE(QDir().mkpath(globalRoot));

    Aie::Internal::KernelRegistryService registry;
    registry.setBuiltInRoot(QString());
    registry.setGlobalUserRoot(globalRoot);
    registry.setWorkspaceRoot(workspaceRoot);

    Aie::Internal::KernelRegistryService::KernelCreateRequest request;
    request.id = QStringLiteral("custom_add");
    request.name = QStringLiteral("Custom Add");
    request.signature = QStringLiteral("void custom_add(const int* in, int* out, int count);");
    request.description = QStringLiteral("Workspace local test kernel.");
    request.tags = QStringList{QStringLiteral("custom"), QStringLiteral("math")};

    Aie::Internal::KernelAsset createdKernel;
    const Utils::Result createResult = registry.createKernelInScope(request,
                                                                    Aie::Internal::KernelSourceScope::Workspace,
                                                                    &createdKernel);
    ASSERT_TRUE(createResult.ok) << createResult.errors.join("\n").toStdString();

    EXPECT_EQ(createdKernel.id, QStringLiteral("custom_add"));
    EXPECT_EQ(createdKernel.scope, Aie::Internal::KernelSourceScope::Workspace);
    EXPECT_TRUE(QFileInfo::exists(createdKernel.directoryPath + QStringLiteral("/kernel.json")));
    EXPECT_TRUE(QFileInfo::exists(createdKernel.absoluteEntryPath()));

    const Aie::Internal::KernelAsset* loaded = registry.kernelById(QStringLiteral("custom_add"));
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->scope, Aie::Internal::KernelSourceScope::Workspace);
    EXPECT_EQ(loaded->name, QStringLiteral("Custom Add"));
}

TEST(KernelRegistryServiceTests, RejectsBuiltInScopeForCreation)
{
    Aie::Internal::KernelRegistryService registry;

    Aie::Internal::KernelRegistryService::KernelCreateRequest request;
    request.id = QStringLiteral("invalid_scope_kernel");
    request.signature = QStringLiteral("void invalid_scope_kernel();");

    const Utils::Result createResult = registry.createKernelInScope(
        request, Aie::Internal::KernelSourceScope::BuiltIn);

    EXPECT_FALSE(createResult.ok);
}

