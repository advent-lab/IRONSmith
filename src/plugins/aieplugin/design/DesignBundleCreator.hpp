// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"

#include <utils/Result.hpp>

#include <QtCore/QString>

namespace Aie::Internal {

enum class ExistingBundlePolicy : unsigned char {
    FailIfExists,
    ReplaceExisting,
    CreateCopy
};

struct DesignBundleCreateRequest final {
    QString name;
    QString location;
    QString deviceFamily;
};

struct DesignBundleCreateResult final {
    QString bundlePath;
    QString displayName;
    bool replacedExisting = false;
    bool createdCopy = false;
};

class AIEPLUGIN_EXPORT DesignBundleCreator final
{
public:
    static Utils::Result validateRequest(const DesignBundleCreateRequest& request);
    static QString resolveBundlePath(const QString& location, const QString& name);

    static Utils::Result create(const DesignBundleCreateRequest& request,
                                ExistingBundlePolicy policy,
                                DesignBundleCreateResult& outResult);

private:
    static Utils::Result ensureLocationExists(const QString& location);
    static Utils::Result removeExistingBundle(const QString& bundlePath);
    static QString uniqueBundlePath(const QString& existingPath);
    static bool containsPathSeparators(const QString& text);
};

} // namespace Aie::Internal
