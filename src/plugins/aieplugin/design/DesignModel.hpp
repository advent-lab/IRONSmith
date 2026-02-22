// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace Aie::Internal {

struct TileCounts final {
    int columns = 0;
    int shimRows = 0;
    int memRows = 0;
    int aieRows = 0;

    int totalRows() const { return shimRows + memRows + aieRows; }
};

struct DesignModel final {
    QString bundlePath;
    QString name;
    QString deviceFamily;
    QString aieArch;
    QString deviceId;

    TileCounts tiles;

    QJsonObject manifest;
    QJsonObject program;
    QJsonObject aieSpec;
    QJsonObject legacyDesignState;

    QString canvasPersistenceRelativePath;
    QString canvasPersistencePath;
    bool canvasPersistenceExists = false;

    bool hasDesignState() const;
};

} // namespace Aie::Internal
