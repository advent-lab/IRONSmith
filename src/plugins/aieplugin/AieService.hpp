// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/api/IAieHost.hpp"
#include "aieplugin/NpuProfileLoader.hpp"

#include <QtCore/QHash>
#include <QtCore/QPointer>

#include "canvas/api/ICanvasHost.hpp"
#include "canvas/api/ICanvasGridHost.hpp"
#include "canvas/api/ICanvasStyleHost.hpp"

namespace Aie {

class AieCanvasCoordinator;

namespace Internal {

class AieService final : public IAieHost
{
    Q_OBJECT

public:
    explicit AieService(QObject* parent = nullptr);

    AieCanvasCoordinator* coordinator() const override;
    QString profileId() const override;
    QStringList profileIds() const override;

    Utils::Result loadProfileCatalog(const QString& path) override;
    Utils::Result setProfileId(const QString& id) override;

    const NpuProfileCatalog& catalog() const { return m_catalog; }

    void setGridHost(Canvas::Api::ICanvasGridHost* host);
    void setStyleHost(Canvas::Api::ICanvasStyleHost* host);
    void setCanvasHost(Canvas::Api::ICanvasHost* host);
    Canvas::Api::ICanvasHost* canvasHost() const { return m_canvasHost; }
    void setBaseStyles(const QHash<QString, Canvas::Api::CanvasBlockStyle>& styles);

private:
    void ensureCoordinator();
    void applyBaseStyles();

    QPointer<AieCanvasCoordinator> m_coordinator;
    QPointer<Canvas::Api::ICanvasHost> m_canvasHost;
    QPointer<Canvas::Api::ICanvasGridHost> m_gridHost;
    QPointer<Canvas::Api::ICanvasStyleHost> m_styleHost;

    NpuProfileCatalog m_catalog;
    QString m_profileId;
    QHash<QString, Canvas::Api::CanvasBlockStyle> m_baseStyles;
};

} // namespace Internal
} // namespace Aie