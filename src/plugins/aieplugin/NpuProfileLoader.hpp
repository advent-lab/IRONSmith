// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/NpuProfile.hpp"

#include <utils/Result.hpp>

#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace Aie {

AIEPLUGIN_EXPORT Utils::Result loadProfileCatalogFromJson(const QByteArray& data, NpuProfileCatalog& out);
AIEPLUGIN_EXPORT Utils::Result loadProfileCatalogFromFile(const QString& path, NpuProfileCatalog& out);

AIEPLUGIN_EXPORT const NpuProfile* findProfileById(const NpuProfileCatalog& catalog, const QString& id);

} // namespace Aie