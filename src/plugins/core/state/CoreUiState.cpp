// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/state/CoreUiState.hpp"

#include <QtCore/QVariant>

namespace Core::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kMainWindowGeometryKey = u"core/mainWindow/geometry"_s;

const QHash<Core::SidebarSide, QString> kSideTokens = {
    {Core::SidebarSide::Left, u"left"_s},
    {Core::SidebarSide::Right, u"right"_s},
};

const QHash<Core::SidebarFamily, QString> kFamilyTokens = {
    {Core::SidebarFamily::Vertical, u"vertical"_s},
    {Core::SidebarFamily::Horizontal, u"horizontal"_s},
};

} // namespace

CoreUiState::CoreUiState()
    : m_env(makeEnvironment())
{
}

CoreUiState::CoreUiState(Utils::Environment environment)
    : m_env(std::move(environment))
{
}

Utils::Environment CoreUiState::makeEnvironment()
{
    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    return Utils::Environment(cfg);
}

QString CoreUiState::sidebarWidthKey(SidebarSide side, SidebarFamily family)
{
    const QString sideToken = kSideTokens.value(side);
    const QString familyToken = kFamilyTokens.value(family);
    if (sideToken.isEmpty() || familyToken.isEmpty())
        return {};

    return QStringLiteral("core/sidebarPanels/%1/%2/width").arg(sideToken, familyToken);
}

int CoreUiState::sidebarPanelWidth(SidebarSide side, SidebarFamily family, int fallback) const
{
    const QString key = sidebarWidthKey(side, family);
    if (key.isEmpty())
        return fallback;

    return m_env.setting(Utils::EnvironmentScope::Global, key, fallback).toInt();
}

void CoreUiState::setSidebarPanelWidth(SidebarSide side, SidebarFamily family, int width)
{
    const QString key = sidebarWidthKey(side, family);
    if (key.isEmpty())
        return;

    m_env.setSetting(Utils::EnvironmentScope::Global, key, width);
}

QByteArray CoreUiState::mainWindowGeometry() const
{
    return m_env.setting(Utils::EnvironmentScope::Global, kMainWindowGeometryKey, QByteArray()).toByteArray();
}

void CoreUiState::setMainWindowGeometry(const QByteArray& geometry)
{
    if (geometry.isEmpty())
        return;
    m_env.setSetting(Utils::EnvironmentScope::Global, kMainWindowGeometryKey, geometry);
}

} // namespace Core::Internal
