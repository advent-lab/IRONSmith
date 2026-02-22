// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "utils/filesystem/JsonFileUtils.hpp"

#include <QtCore/QFile>
#include <QtCore/QJsonParseError>
#include <QtCore/QSaveFile>

namespace Utils::JsonFileUtils {

Result writeObjectAtomic(const QString& path, const QJsonObject& object, QJsonDocument::JsonFormat format)
{
    const QString cleanedPath = path.trimmed();
    if (cleanedPath.isEmpty())
        return Result::failure(QStringLiteral("JSON output path is empty."));

    QSaveFile file(cleanedPath);
    if (!file.open(QIODevice::WriteOnly))
        return Result::failure(QStringLiteral("Failed to open file for writing: %1").arg(cleanedPath));

    const QJsonDocument doc(object);
    if (file.write(doc.toJson(format)) < 0) {
        const QString error = file.errorString();
        file.cancelWriting();
        return Result::failure(QStringLiteral("Failed to write JSON file: %1 (%2)").arg(cleanedPath, error));
    }

    if (!file.commit())
        return Result::failure(QStringLiteral("Failed to commit JSON file: %1 (%2)")
                                   .arg(cleanedPath, file.errorString()));
    return Result::success();
}

QJsonObject readObject(const QString& path, QString* error)
{
    const QString cleanedPath = path.trimmed();
    if (cleanedPath.isEmpty()) {
        if (error)
            *error = QStringLiteral("JSON input path is empty.");
        return {};
    }

    QFile file(cleanedPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Failed to open JSON file: %1 (%2)")
                         .arg(cleanedPath, file.errorString());
        }
        return {};
    }

    const QByteArray bytes = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Failed to parse JSON file: %1 (%2)")
                         .arg(cleanedPath, parseError.errorString());
        }
        return {};
    }

    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("JSON document is not an object: %1").arg(cleanedPath);
        return {};
    }

    if (error)
        error->clear();
    return doc.object();
}

} // namespace Utils::JsonFileUtils

