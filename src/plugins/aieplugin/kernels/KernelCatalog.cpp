// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/kernels/KernelCatalog.hpp"

#include <utils/PathUtils.hpp>
#include <utils/filesystem/JsonFileUtils.hpp>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>

#include <algorithm>

namespace Aie::Internal {

namespace {

struct KernelFileSource final {
    KernelSourceScope scope = KernelSourceScope::BuiltIn;
    QString rootPath;
};

QString cleanPath(const QString& path)
{
    return Utils::PathUtils::normalizePath(path);
}

QString cleanRelativePath(const QString& path)
{
    QString cleaned = cleanPath(path);
    while (cleaned.startsWith('/'))
        cleaned.remove(0, 1);
    return cleaned;
}

QString cleanedId(const QString& id)
{
    return cleanPath(id.trimmed());
}

bool isCppFile(const QString& fileName)
{
    const QString ext = Utils::PathUtils::extension(fileName).toLower();
    return ext == QStringLiteral("cpp")
           || ext == QStringLiteral("cxx")
           || ext == QStringLiteral("cc")
           || ext == QStringLiteral("c");
}

QString selectEntryFile(const QJsonObject& metadata,
                        const QStringList& files,
                        const QString& kernelDir,
                        QStringList& warnings,
                        const QString& id)
{
    const QString declaredEntry = cleanRelativePath(metadata.value(QStringLiteral("entry")).toString());
    if (!declaredEntry.isEmpty()) {
        const QString absolute = QDir(kernelDir).filePath(declaredEntry);
        if (QFileInfo::exists(absolute) && QFileInfo(absolute).isFile())
            return declaredEntry;

        warnings.push_back(QStringLiteral("Kernel '%1' entry file '%2' does not exist.")
                               .arg(id, declaredEntry));
    }

    for (const QString& file : files) {
        const QString absolute = QDir(kernelDir).filePath(file);
        if (!QFileInfo::exists(absolute) || !QFileInfo(absolute).isFile())
            continue;
        if (isCppFile(file))
            return file;
    }

    for (const QString& file : files) {
        const QString absolute = QDir(kernelDir).filePath(file);
        if (QFileInfo::exists(absolute) && QFileInfo(absolute).isFile())
            return file;
    }

    return {};
}

QStringList parseFiles(const QJsonObject& metadata)
{
    QStringList files;
    const QJsonValue filesValue = metadata.value(QStringLiteral("files"));
    if (filesValue.isArray()) {
        const QJsonArray fileArray = filesValue.toArray();
        for (const QJsonValue& entry : fileArray) {
            if (!entry.isString())
                continue;
            const QString relative = cleanRelativePath(entry.toString());
            if (!relative.isEmpty())
                files.push_back(relative);
        }
    }

    const QString entryFile = cleanRelativePath(metadata.value(QStringLiteral("entry")).toString());
    if (!entryFile.isEmpty() && !files.contains(entryFile))
        files.push_back(entryFile);

    files.removeDuplicates();
    return files;
}

QStringList parseTags(const QJsonObject& metadata)
{
    QStringList tags;
    const QJsonValue tagsValue = metadata.value(QStringLiteral("tags"));
    if (!tagsValue.isArray())
        return tags;

    const QJsonArray tagsArray = tagsValue.toArray();
    for (const QJsonValue& tagValue : tagsArray) {
        if (!tagValue.isString())
            continue;

        const QString tag = tagValue.toString().trimmed();
        if (!tag.isEmpty())
            tags.push_back(tag);
    }

    tags.removeDuplicates();
    return tags;
}

QString fallbackName(const QString& id)
{
    QString out = id;
    out.replace('_', ' ');
    out.replace('-', ' ');
    return out.trimmed();
}

Utils::Result parseKernel(const QFileInfo& directory,
                          const KernelFileSource& source,
                          KernelAsset& outKernel,
                          QStringList& warnings)
{
    const QString kernelJsonPath = directory.absoluteFilePath() + QStringLiteral("/kernel.json");
    if (!QFileInfo::exists(kernelJsonPath)) {
        warnings.push_back(QStringLiteral("Skipping '%1': kernel.json not found.")
                               .arg(directory.absoluteFilePath()));
        return Utils::Result::success();
    }

    QString readError;
    const QJsonObject metadata = Utils::JsonFileUtils::readObject(kernelJsonPath, &readError);
    if (!readError.isEmpty()) {
        warnings.push_back(QStringLiteral("Skipping '%1': %2")
                               .arg(kernelJsonPath, readError));
        return Utils::Result::success();
    }

    const QString id = cleanedId(metadata.value(QStringLiteral("id")).toString(directory.fileName()));
    if (id.isEmpty()) {
        warnings.push_back(QStringLiteral("Skipping '%1': kernel id is empty.")
                               .arg(directory.absoluteFilePath()));
        return Utils::Result::success();
    }

    const QStringList files = parseFiles(metadata);
    const QString entryFile = selectEntryFile(metadata, files, directory.absoluteFilePath(), warnings, id);
    if (entryFile.isEmpty()) {
        warnings.push_back(QStringLiteral("Skipping kernel '%1': no readable entry file found.")
                               .arg(id));
        return Utils::Result::success();
    }

    KernelAsset kernel;
    kernel.id = id;
    kernel.name = metadata.value(QStringLiteral("name")).toString().trimmed();
    if (kernel.name.isEmpty())
        kernel.name = fallbackName(id);
    kernel.version = metadata.value(QStringLiteral("version")).toString().trimmed();
    kernel.language = metadata.value(QStringLiteral("language")).toString().trimmed();
    if (kernel.language.isEmpty())
        kernel.language = QStringLiteral("cpp");
    kernel.description = metadata.value(QStringLiteral("description")).toString().trimmed();
    kernel.signature = metadata.value(QStringLiteral("signature")).toString().trimmed();
    kernel.entryFile = entryFile;
    kernel.files = files;
    kernel.tags = parseTags(metadata);
    kernel.scope = source.scope;
    kernel.rootPath = source.rootPath;
    kernel.directoryPath = directory.absoluteFilePath();
    kernel.metadata = metadata;

    outKernel = std::move(kernel);
    return Utils::Result::success();
}

Utils::Result scanRoot(const KernelFileSource& source,
                       QHash<QString, KernelAsset>& merged,
                       QStringList& warnings)
{
    if (source.rootPath.isEmpty())
        return Utils::Result::success();

    const QDir rootDir(source.rootPath);
    if (!rootDir.exists())
        return Utils::Result::success();

    const QFileInfoList kernelDirectories =
        rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& directory : kernelDirectories) {
        KernelAsset kernel;
        const Utils::Result parseResult = parseKernel(directory, source, kernel, warnings);
        if (!parseResult)
            return parseResult;
        if (!kernel.isValid())
            continue;

        if (merged.contains(kernel.id)) {
            warnings.push_back(QStringLiteral("Kernel '%1' from '%2' overrides previous definition.")
                                   .arg(kernel.id, directory.absoluteFilePath()));
        }
        merged.insert(kernel.id, std::move(kernel));
    }

    return Utils::Result::success();
}

} // namespace

bool KernelAsset::isValid() const
{
    return !id.trimmed().isEmpty()
           && !name.trimmed().isEmpty()
           && !entryFile.trimmed().isEmpty()
           && !directoryPath.trimmed().isEmpty();
}

QString KernelAsset::absoluteEntryPath() const
{
    if (directoryPath.trimmed().isEmpty() || entryFile.trimmed().isEmpty())
        return {};
    return cleanPath(QDir(directoryPath).filePath(entryFile));
}

QString kernelScopeName(KernelSourceScope scope)
{
    switch (scope) {
        case KernelSourceScope::BuiltIn:
            return QStringLiteral("built-in");
        case KernelSourceScope::Global:
            return QStringLiteral("global");
        case KernelSourceScope::Workspace:
            return QStringLiteral("workspace");
    }
    return QStringLiteral("unknown");
}

Utils::Result scanKernelCatalog(const KernelCatalogScanRequest& request,
                                QVector<KernelAsset>& outKernels,
                                QStringList* outWarnings)
{
    outKernels.clear();

    QStringList warnings;

    QHash<QString, KernelAsset> merged;
    const QVector<KernelFileSource> sources{
        KernelFileSource{KernelSourceScope::BuiltIn, cleanPath(request.builtInRoot)},
        KernelFileSource{KernelSourceScope::Global, cleanPath(request.globalRoot)},
        KernelFileSource{KernelSourceScope::Workspace, cleanPath(request.workspaceRoot)}
    };

    for (const KernelFileSource& source : sources) {
        const Utils::Result rootResult = scanRoot(source, merged, warnings);
        if (!rootResult)
            return rootResult;
    }

    outKernels.reserve(merged.size());
    for (auto it = merged.begin(); it != merged.end(); ++it)
        outKernels.push_back(it.value());

    std::sort(outKernels.begin(), outKernels.end(), [](const KernelAsset& lhs, const KernelAsset& rhs) {
        const int byName = QString::compare(lhs.name, rhs.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return QString::compare(lhs.id, rhs.id, Qt::CaseInsensitive) < 0;
    });

    if (outWarnings)
        *outWarnings = warnings;
    return Utils::Result::success();
}

const KernelAsset* findKernelById(const QVector<KernelAsset>& kernels, const QString& kernelId)
{
    const QString wanted = cleanedId(kernelId);
    if (wanted.isEmpty())
        return nullptr;

    for (const KernelAsset& kernel : kernels) {
        if (cleanedId(kernel.id) == wanted)
            return &kernel;
    }

    return nullptr;
}

} // namespace Aie::Internal
