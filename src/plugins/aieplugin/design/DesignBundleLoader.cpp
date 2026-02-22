// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/design/DesignBundleLoader.hpp"

#include "aieplugin/AieConstants.hpp"
#include "aieplugin/NpuProfileLoader.hpp"

#include <utils/DocumentBundle.hpp>
#include <utils/filesystem/JsonFileUtils.hpp>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kArchAieMl = u"AIE-ML"_s;
const QString kFamilyAieMl = u"aie-ml"_s;
const QString kFamilyAieMlV2 = u"aie-ml-v2"_s;
const QString kDefaultAieSpecPath = u"aie/spec.json"_s;
const QString kDefaultCanvasDocumentPath = u"canvas/document.json"_s;

QString normalizeToken(const QString& input)
{
    QString out = input.trimmed().toLower();
    out.replace(u'_', u'-');
    return out;
}

QString normalizeRelativePath(const QString& path, const QString& fallback)
{
    QString cleaned = QDir::cleanPath(path.trimmed());
    if (cleaned.isEmpty() || cleaned == QStringLiteral("."))
        cleaned = fallback;
    while (cleaned.startsWith(u'/'))
        cleaned.remove(0, 1);
    return cleaned;
}

} // namespace

DesignBundleLoader::DesignBundleLoader(const Aie::NpuProfileCatalog* catalog)
    : m_catalog(catalog)
{
}

Utils::Result DesignBundleLoader::load(const QString& bundlePath, DesignModel& outModel) const
{
    outModel = DesignModel{};
    if (!m_catalog)
        return Utils::Result::failure(QStringLiteral("NPU profile catalog is not available."));

    QString validateError;
    if (!Utils::DocumentBundle::validate(bundlePath, &validateError))
        return Utils::Result::failure(validateError.isEmpty() ? QStringLiteral("Invalid design bundle.") : validateError);

    const QString normalizedPath = Utils::DocumentBundle::normalizeBundlePath(bundlePath);

    QString error;
    const QJsonObject program = Utils::DocumentBundle::readProgram(normalizedPath, &error);
    if (!error.isEmpty())
        return Utils::Result::failure(error);
    if (program.isEmpty())
        return Utils::Result::failure(QStringLiteral("Program configuration is empty."));

    const QJsonObject legacyDesignState = Utils::DocumentBundle::readDesign(normalizedPath, &error);
    if (!error.isEmpty())
        return Utils::Result::failure(error);

    const QJsonObject manifest = Utils::DocumentBundle::readManifest(normalizedPath, &error);
    if (!error.isEmpty())
        return Utils::Result::failure(error);

    const QJsonObject documents = manifest.value(QStringLiteral("documents")).toObject();
    const QString aieSpecRelativePath = normalizeRelativePath(
        documents.value(QStringLiteral("aieSpec")).toObject().value(QStringLiteral("path")).toString(),
        kDefaultAieSpecPath);
    const QString canvasDocumentRelativePath = normalizeRelativePath(
        documents.value(QStringLiteral("canvas")).toObject().value(QStringLiteral("path")).toString(),
        kDefaultCanvasDocumentPath);

    const QString aieSpecPath = QDir(normalizedPath).filePath(aieSpecRelativePath);
    const QString canvasDocumentPath = QDir(normalizedPath).filePath(canvasDocumentRelativePath);

    QJsonObject aieSpec;
    if (QFileInfo::exists(aieSpecPath)) {
        const QJsonObject loadedSpec = Utils::JsonFileUtils::readObject(aieSpecPath, &error);
        if (!error.isEmpty())
            return Utils::Result::failure(error);
        aieSpec = loadedSpec;
    } else {
        aieSpec.insert(QStringLiteral("deviceFamily"), program.value(QStringLiteral("deviceFamily")));
    }

    const QString deviceFamily = program.value(QStringLiteral("deviceFamily")).toString();
    if (deviceFamily.trimmed().isEmpty())
        return Utils::Result::failure(QStringLiteral("Program config missing deviceFamily."));

    QString arch;
    Utils::Result archResult = resolveArchForDeviceFamily(deviceFamily, arch);
    if (!archResult)
        return archResult;

    const Aie::NpuProfile* profile = nullptr;
    Utils::Result profileResult = resolveProfileForArch(arch, profile);
    if (!profileResult)
        return profileResult;

    TileCounts counts;
    counts.columns = profile->grid.columns;
    counts.shimRows = profile->grid.rows.shim;
    counts.memRows = profile->grid.rows.mem;
    counts.aieRows = profile->grid.rows.aie;

    DesignModel model;
    model.bundlePath = normalizedPath;
    model.name = manifest.value(QStringLiteral("name")).toString();
    if (model.name.trimmed().isEmpty())
        model.name = QFileInfo(normalizedPath).completeBaseName();
    model.deviceFamily = deviceFamily;
    model.aieArch = arch;
    model.deviceId = profile->id;
    model.tiles = counts;
    model.manifest = manifest;
    model.program = program;
    model.aieSpec = aieSpec;
    model.legacyDesignState = legacyDesignState;
    model.canvasPersistenceRelativePath = canvasDocumentRelativePath;
    model.canvasPersistencePath = QDir::cleanPath(canvasDocumentPath);
    model.canvasPersistenceExists = QFileInfo::exists(model.canvasPersistencePath);

    outModel = std::move(model);
    return Utils::Result::success();
}

Utils::Result DesignBundleLoader::resolveArchForDeviceFamily(const QString& deviceFamily, QString& outArch) const
{
    const QString token = normalizeToken(deviceFamily);
    if (token == kFamilyAieMl) {
        outArch = kArchAieMl;
        return Utils::Result::success();
    }

    if (token == kFamilyAieMlV2) {
        return Utils::Result::failure(QStringLiteral("Device family 'AIE-ML v2' is not supported yet."));
    }

    return Utils::Result::failure(QStringLiteral("Unknown device family: %1").arg(deviceFamily));
}

Utils::Result DesignBundleLoader::resolveProfileForArch(const QString& arch, const Aie::NpuProfile*& outProfile) const
{
    if (!m_catalog)
        return Utils::Result::failure(QStringLiteral("NPU profile catalog is not available."));

    const Aie::NpuProfile* profile = selectProfileForArch(arch);
    if (!profile) {
        return Utils::Result::failure(QStringLiteral("No device profile supports architecture: %1").arg(arch));
    }

    outProfile = profile;
    return Utils::Result::success();
}

const Aie::NpuProfile* DesignBundleLoader::selectProfileForArch(const QString& arch) const
{
    if (!m_catalog)
        return nullptr;

    const QString defaultDeviceId = QString::fromLatin1(Aie::kDefaultDeviceId);
    if (const Aie::NpuProfile* preferred = findProfileById(*m_catalog, defaultDeviceId)) {
        if (preferred->aieArch.isEmpty() || archMatches(preferred->aieArch, arch))
            return preferred;
    }

    for (const auto& profile : m_catalog->devices) {
        if (profile.aieArch.isEmpty() || archMatches(profile.aieArch, arch))
            return &profile;
    }

    return nullptr;
}

bool DesignBundleLoader::archMatches(QStringView lhs, QStringView rhs)
{
    return QString::compare(lhs.toString().trimmed(), rhs.toString().trimmed(), Qt::CaseInsensitive) == 0;
}

} // namespace Aie::Internal
