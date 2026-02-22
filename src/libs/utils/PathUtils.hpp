// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "utils/UtilsGlobal.hpp"

#include <QtCore/Qt>
#include <QtCore/QString>
#include <QtCore/QStringView>

namespace Utils::PathUtils {

UTILS_EXPORT QString normalizePath(QStringView path);
UTILS_EXPORT QString cleanPath(QStringView path);
UTILS_EXPORT QString basename(QStringView path);
UTILS_EXPORT QString extension(QStringView path);
UTILS_EXPORT QString stem(QStringView path);

UTILS_EXPORT bool hasExtension(QStringView path,
                               QStringView ext,
                               Qt::CaseSensitivity cs = Qt::CaseInsensitive);
UTILS_EXPORT QString ensureExtension(QStringView path, QStringView ext);

UTILS_EXPORT QString sanitizeFileName(QStringView name);

} // namespace Utils::PathUtils
