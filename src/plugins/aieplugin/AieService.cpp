// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/AieService.hpp"

#include "aieplugin/AieCanvasCoordinator.hpp"
#include "aieplugin/NpuProfileCanvasMapper.hpp"

namespace Aie::Internal {

AieService::AieService(QObject* parent)
    : IAieHost(parent)
{
}

AieCanvasCoordinator* AieService::coordinator() const
{
    return m_coordinator;
}

QString AieService::profileId() const
{
    return m_profileId;
}

QStringList AieService::profileIds() const
{
    QStringList ids;
    ids.reserve(static_cast<int>(m_catalog.devices.size()));
    for (const auto& profile : m_catalog.devices)
        ids.push_back(profile.id);
    return ids;
}

Utils::Result AieService::loadProfileCatalog(const QString& path)
{
    NpuProfileCatalog catalog;
    const Utils::Result result = loadProfileCatalogFromFile(path, catalog);
    if (!result)
        return result;

    m_catalog = std::move(catalog);
    emit catalogChanged();
    return Utils::Result::success();
}

Utils::Result AieService::setProfileId(const QString& id)
{
    ensureCoordinator();

    const NpuProfile* profile = findProfileById(m_catalog, id);
    if (!profile)
        return Utils::Result::failure(QStringLiteral("Profile not found: %1").arg(id));

    CanvasGridModel model;
    const Utils::Result buildResult = buildCanvasGridModel(*profile, model);
    if (!buildResult)
        return buildResult;

    m_profileId = id;
    if (m_coordinator)
        m_coordinator->setBaseModel(model);
    applyBaseStyles();
    if (m_coordinator)
        m_coordinator->flushApply();
    emit profileIdChanged(m_profileId);
    return Utils::Result::success();
}

void AieService::setGridHost(Canvas::Api::ICanvasGridHost* host)
{
    if (m_gridHost == host)
        return;
    m_gridHost = host;
    ensureCoordinator();
    if (m_coordinator)
        m_coordinator->setGridHost(host);
}

void AieService::setStyleHost(Canvas::Api::ICanvasStyleHost* host)
{
    if (m_styleHost == host)
        return;
    m_styleHost = host;
    ensureCoordinator();
    if (m_coordinator)
        m_coordinator->setStyleHost(host);
    applyBaseStyles();
}

void AieService::setCanvasHost(Canvas::Api::ICanvasHost* host)
{
    if (m_canvasHost == host)
        return;
    m_canvasHost = host;
    ensureCoordinator();
    if (m_coordinator)
        m_coordinator->setCanvasHost(host);
}

void AieService::setBaseStyles(const QHash<QString, Canvas::Api::CanvasBlockStyle>& styles)
{
    m_baseStyles = styles;
    applyBaseStyles();
}

void AieService::ensureCoordinator()
{
    if (m_coordinator)
        return;
    m_coordinator = new AieCanvasCoordinator(this);
    if (m_canvasHost)
        m_coordinator->setCanvasHost(m_canvasHost);
    if (m_gridHost)
        m_coordinator->setGridHost(m_gridHost);
    if (m_styleHost)
        m_coordinator->setStyleHost(m_styleHost);
}

void AieService::applyBaseStyles()
{
    if (m_coordinator && !m_baseStyles.isEmpty())
        m_coordinator->setBaseStyles(m_baseStyles);
}

} // namespace Aie::Internal