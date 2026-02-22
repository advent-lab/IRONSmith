// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "aieplugin/design/DesignBundleLoader.hpp"
#include "aieplugin/design/DesignModel.hpp"
#include "aieplugin/AieConstants.hpp"
#include "aieplugin/NpuProfileLoader.hpp"

#include <utils/DocumentBundle.hpp>

#include <QtCore/QDir>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

using Aie::NpuProfileCatalog;
using Aie::Internal::DesignBundleLoader;
using Aie::Internal::DesignModel;

namespace {

NpuProfileCatalog makeCatalog()
{
    const QByteArray json = R"JSON(
{
  "schemaVersion": 1,
  "devices": [
    {
      "id": "amd-xdna1-phoenix",
      "name": "Phoenix",
      "vendor": "AMD",
      "family": "XDNA1",
      "aieArch": "AIE-ML",
      "grid": {
        "columns": 5,
        "rows": { "shim": 1, "mem": 1, "aie": 4 },
        "rowOrderBottomToTop": ["shim", "mem", "aie"]
      },
      "tiles": {
        "coordinateSystem": "col_row",
        "shim": { "rows": [0], "cols": [0, 1, 2, 3], "virtualCols": [] },
        "mem": { "rows": [1], "cols": [0, 1, 2, 3, 4], "virtualCols": [] },
        "aie": { "rows": [2, 3, 4, 5], "cols": [0, 1, 2, 3, 4], "virtualCols": [] }
      }
    }
  ]
}
)JSON";

    NpuProfileCatalog catalog;
    const Utils::Result result = Aie::loadProfileCatalogFromJson(json, catalog);
    EXPECT_TRUE(result.ok) << result.errors.join("\n").toStdString();
    return catalog;
}

} // namespace

TEST(DesignBundleLoaderTests, LoadsBundleAndResolvesTopology)
{
    NpuProfileCatalog catalog = makeCatalog();
    DesignBundleLoader loader(&catalog);

    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    const QString bundlePath = QDir(temp.path()).filePath(QStringLiteral("Design.ironsmith"));

    Utils::DocumentBundle::BundleInit init;
    init.name = QStringLiteral("Design");
    init.program = QJsonObject{{QStringLiteral("deviceFamily"), QStringLiteral("aie-ml")}};
    init.design = QJsonObject{};

    const Utils::Result created = Utils::DocumentBundle::create(bundlePath, init);
    ASSERT_TRUE(created.ok) << created.errors.join("\n").toStdString();

    DesignModel model;
    const Utils::Result loaded = loader.load(bundlePath, model);
    ASSERT_TRUE(loaded.ok) << loaded.errors.join("\n").toStdString();

    EXPECT_EQ(model.deviceId, QString::fromLatin1(Aie::kDefaultDeviceId));
    EXPECT_EQ(model.aieArch, QStringLiteral("AIE-ML"));
    EXPECT_EQ(model.tiles.columns, 5);
    EXPECT_EQ(model.tiles.shimRows, 1);
    EXPECT_EQ(model.tiles.memRows, 1);
    EXPECT_EQ(model.tiles.aieRows, 4);
    EXPECT_EQ(model.canvasPersistenceRelativePath, QStringLiteral("canvas/document.json"));
    EXPECT_FALSE(model.canvasPersistenceExists);
    EXPECT_EQ(model.aieSpec.value(QStringLiteral("deviceFamily")).toString(), QStringLiteral("aie-ml"));
}

TEST(DesignBundleLoaderTests, RejectsUnsupportedFamily)
{
    NpuProfileCatalog catalog = makeCatalog();
    DesignBundleLoader loader(&catalog);

    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    const QString bundlePath = QDir(temp.path()).filePath(QStringLiteral("Design.ironsmith"));

    Utils::DocumentBundle::BundleInit init;
    init.name = QStringLiteral("Design");
    init.program = QJsonObject{{QStringLiteral("deviceFamily"), QStringLiteral("aie-ml-v2")}};
    init.design = QJsonObject{};

    const Utils::Result created = Utils::DocumentBundle::create(bundlePath, init);
    ASSERT_TRUE(created.ok) << created.errors.join("\n").toStdString();

    DesignModel model;
    const Utils::Result loaded = loader.load(bundlePath, model);
    EXPECT_FALSE(loaded.ok);
}
