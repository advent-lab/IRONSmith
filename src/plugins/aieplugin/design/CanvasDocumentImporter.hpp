// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utils/Result.hpp>
#include <QtCore/QJsonObject>

namespace Aie::Internal {

class AieService;

class CanvasDocumentImporter final
{
public:
    explicit CanvasDocumentImporter(AieService* service);

    Utils::Result applyProfile(const QString& deviceId) const;
    Utils::Result importLegacyDesignState(const QJsonObject& designState) const;

private:
    Utils::Result applyDesignState(const QJsonObject& designState) const;

    AieService* m_service = nullptr;
};

} // namespace Aie::Internal
