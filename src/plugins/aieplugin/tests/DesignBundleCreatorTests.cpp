// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/design/DesignBundleCreator.hpp"

#include <utils/DocumentBundle.hpp>
#include <utils/filesystem/JsonFileUtils.hpp>

#include <QtCore/QDir>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

using Aie::Internal::DesignBundleCreateRequest;
using Aie::Internal::DesignBundleCreateResult;
using Aie::Internal::DesignBundleCreator;
using Aie::Internal::ExistingBundlePolicy;

TEST(DesignBundleCreatorTests, CreatesBundleWithRequestedDeviceFamily)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    DesignBundleCreateRequest request;
    request.name = QStringLiteral("MyDesign");
    request.location = temp.path();
    request.deviceFamily = QStringLiteral("aie-ml");

    DesignBundleCreateResult created;
    const Utils::Result result = DesignBundleCreator::create(request,
                                                             ExistingBundlePolicy::FailIfExists,
                                                             created);
    ASSERT_TRUE(result.ok) << result.errors.join("\n").toStdString();
    EXPECT_FALSE(created.bundlePath.isEmpty());
    EXPECT_EQ(created.displayName, QStringLiteral("MyDesign"));
    EXPECT_TRUE(QFileInfo::exists(created.bundlePath));

    QString readError;
    const QJsonObject program = Utils::DocumentBundle::readProgram(created.bundlePath, &readError);
    ASSERT_TRUE(readError.isEmpty()) << readError.toStdString();
    EXPECT_EQ(program.value(QStringLiteral("deviceFamily")).toString(), QStringLiteral("aie-ml"));

    const QJsonObject manifest = Utils::DocumentBundle::readManifest(created.bundlePath, &readError);
    ASSERT_TRUE(readError.isEmpty()) << readError.toStdString();
    const QJsonObject documents = manifest.value(QStringLiteral("documents")).toObject();
    EXPECT_EQ(documents.value(QStringLiteral("aieSpec")).toObject().value(QStringLiteral("path")).toString(),
              QStringLiteral("aie/spec.json"));
    EXPECT_EQ(documents.value(QStringLiteral("canvas")).toObject().value(QStringLiteral("path")).toString(),
              QStringLiteral("canvas/document.json"));

    const QString specPath = QDir(created.bundlePath).filePath(QStringLiteral("aie/spec.json"));
    const QJsonObject spec = Utils::JsonFileUtils::readObject(specPath, &readError);
    ASSERT_TRUE(readError.isEmpty()) << readError.toStdString();
    EXPECT_EQ(spec.value(QStringLiteral("deviceFamily")).toString(), QStringLiteral("aie-ml"));
}

TEST(DesignBundleCreatorTests, CreateCopyUsesUniqueBundleName)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    DesignBundleCreateRequest request;
    request.name = QStringLiteral("MyDesign");
    request.location = temp.path();
    request.deviceFamily = QStringLiteral("aie-ml");

    DesignBundleCreateResult initial;
    const Utils::Result first = DesignBundleCreator::create(request,
                                                            ExistingBundlePolicy::FailIfExists,
                                                            initial);
    ASSERT_TRUE(first.ok) << first.errors.join("\n").toStdString();

    DesignBundleCreateResult copy;
    const Utils::Result second = DesignBundleCreator::create(request,
                                                             ExistingBundlePolicy::CreateCopy,
                                                             copy);
    ASSERT_TRUE(second.ok) << second.errors.join("\n").toStdString();
    EXPECT_TRUE(copy.createdCopy);
    EXPECT_NE(copy.bundlePath, initial.bundlePath);
    EXPECT_TRUE(QFileInfo::exists(copy.bundlePath));
}

TEST(DesignBundleCreatorTests, ReplaceOverwritesExistingBundle)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    DesignBundleCreateRequest request;
    request.name = QStringLiteral("MyDesign");
    request.location = temp.path();
    request.deviceFamily = QStringLiteral("aie-ml");

    DesignBundleCreateResult initial;
    const Utils::Result first = DesignBundleCreator::create(request,
                                                            ExistingBundlePolicy::FailIfExists,
                                                            initial);
    ASSERT_TRUE(first.ok) << first.errors.join("\n").toStdString();

    request.deviceFamily = QStringLiteral("aie-ml-v2");
    DesignBundleCreateResult replaced;
    const Utils::Result second = DesignBundleCreator::create(request,
                                                             ExistingBundlePolicy::ReplaceExisting,
                                                             replaced);
    ASSERT_TRUE(second.ok) << second.errors.join("\n").toStdString();
    EXPECT_TRUE(replaced.replacedExisting);
    EXPECT_EQ(replaced.bundlePath, initial.bundlePath);

    QString readError;
    const QJsonObject program = Utils::DocumentBundle::readProgram(replaced.bundlePath, &readError);
    ASSERT_TRUE(readError.isEmpty()) << readError.toStdString();
    EXPECT_EQ(program.value(QStringLiteral("deviceFamily")).toString(), QStringLiteral("aie-ml-v2"));
}
