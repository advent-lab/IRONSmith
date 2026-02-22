// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "projectexplorer/ProjectExplorerDataSource.hpp"
#include "projectexplorer/api/ProjectExplorerTypes.hpp"
#include "projectexplorer/api/ProjectExplorerMetaTypes.hpp"

#include <utils/DocumentBundle.hpp>
#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <memory>

using ProjectExplorer::ProjectEntryKind;
using ProjectExplorer::ProjectEntryList;
using ProjectExplorer::Internal::ProjectExplorerDataSource;

namespace {

ProjectEntryKind kindForPath(const ProjectEntryList& entries, const QString& path)
{
    for (const auto& entry : entries) {
        if (entry.path == path)
            return entry.kind;
    }
    return ProjectEntryKind::Unknown;
}

bool containsPath(const ProjectEntryList& entries, const QString& path)
{
    for (const auto& entry : entries) {
        if (entry.path == path)
            return true;
    }
    return false;
}

} // namespace

TEST(ProjectExplorerDataSourceTests, ScansDirectoryEntries)
{
    ProjectExplorer::registerProjectExplorerMetaTypes();

    int argc = 0;
    char** argv = nullptr;
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance())
        app = std::make_unique<QCoreApplication>(argc, argv);

    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    QTemporaryDir settingsDir;
    ASSERT_TRUE(settingsDir.isValid());

    QDir root(temp.path());
    ASSERT_TRUE(root.mkpath(QStringLiteral("docs")));

    QFile readme(root.filePath(QStringLiteral("docs/readme.md")));
    ASSERT_TRUE(readme.open(QIODevice::WriteOnly));
    readme.write("test");
    readme.close();

    QFile design(root.filePath(QStringLiteral("design.graphml")));
    ASSERT_TRUE(design.open(QIODevice::WriteOnly));
    design.write("graph");
    design.close();

    const QString bundlePath = root.filePath(QStringLiteral("bundle.ironsmith"));
    Utils::DocumentBundle::BundleInit init;
    init.name = QStringLiteral("Bundle");
    init.program = QJsonObject{};
    init.design = QJsonObject{};
    const Utils::Result bundleCreated = Utils::DocumentBundle::create(bundlePath, init);
    ASSERT_TRUE(bundleCreated.ok) << bundleCreated.errors.join("\n").toStdString();

    QString label;
    ProjectEntryList entries;

    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    cfg.globalConfigRootOverride = settingsDir.path();
    ProjectExplorerDataSource source{Utils::Environment(cfg)};
    QSignalSpy labelSpy(&source, &ProjectExplorerDataSource::rootLabelChanged);
    QSignalSpy entriesSpy(&source, &ProjectExplorerDataSource::entriesChanged);
    QObject::connect(&source, &ProjectExplorerDataSource::rootLabelChanged,
                     [&](const QString& l) { label = l; });
    QObject::connect(&source, &ProjectExplorerDataSource::entriesChanged,
                     [&](const ProjectEntryList& e) { entries = e; });

    source.setRootPath(temp.path());

    if (labelSpy.isEmpty())
        ASSERT_TRUE(labelSpy.wait(1000));
    if (entriesSpy.isEmpty())
        ASSERT_TRUE(entriesSpy.wait(1000));

    EXPECT_EQ(label, QFileInfo(temp.path()).fileName());

    EXPECT_TRUE(containsPath(entries, QStringLiteral("docs")));
    EXPECT_TRUE(containsPath(entries, QStringLiteral("docs/readme.md")));
    EXPECT_TRUE(containsPath(entries, QStringLiteral("design.graphml")));
    EXPECT_TRUE(containsPath(entries, QStringLiteral("bundle.ironsmith")));
    EXPECT_FALSE(containsPath(entries, QStringLiteral("bundle.ironsmith/manifest.json")));

    EXPECT_EQ(kindForPath(entries, QStringLiteral("docs")), ProjectEntryKind::Folder);
    EXPECT_EQ(kindForPath(entries, QStringLiteral("docs/readme.md")), ProjectEntryKind::Asset);
    EXPECT_EQ(kindForPath(entries, QStringLiteral("design.graphml")), ProjectEntryKind::Design);
    EXPECT_EQ(kindForPath(entries, QStringLiteral("bundle.ironsmith")), ProjectEntryKind::Design);
}
