// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "utils/Result.hpp"
#include "utils/UtilsGlobal.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace Utils::JsonFileUtils {

UTILS_EXPORT Result writeObjectAtomic(const QString& path,
                                      const QJsonObject& object,
                                      QJsonDocument::JsonFormat format = QJsonDocument::Indented);

UTILS_EXPORT QJsonObject readObject(const QString& path, QString* error = nullptr);

} // namespace Utils::JsonFileUtils

