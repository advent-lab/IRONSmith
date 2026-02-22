// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/SidebarRegistryImpl.hpp"

#include <QtCore/QDebug>

#include "core/SidebarModel.hpp"

namespace Core {

SidebarRegistryImpl::SidebarRegistryImpl(QObject* parent)
	: ISidebarRegistry(parent)
{
	m_model = new SidebarModel(this);

	connect(m_model, &SidebarModel::toolRegistered,
			this, &ISidebarRegistry::toolRegistered);

	connect(m_model, &SidebarModel::toolUnregistered,
			this, &ISidebarRegistry::toolUnregistered);

	connect(m_model, &SidebarModel::toolOpenStateChanged,
			this, &ISidebarRegistry::toolOpenStateChanged);
}

bool SidebarRegistryImpl::registerTool(const SidebarToolSpec& spec,
								   PanelFactory factory,
								   QString* errorOut)
{
	SidebarToolSpec normalized = spec;

	normalized.rail = (normalized.region == SidebarRegion::Additive) ? SidebarRail::Bottom : SidebarRail::Top;

	return m_model->registerTool(normalized, std::move(factory), errorOut);
}

bool SidebarRegistryImpl::unregisterTool(const QString& id, QString* errorOut)
{
	return m_model->unregisterTool(id, errorOut);
}

bool SidebarRegistryImpl::isToolOpen(const QString& id) const
{
	return m_model->isOpen(id);
}

void SidebarRegistryImpl::requestShowTool(const QString& id)
{
	QString err;
	if (!m_model->requestShowTool(id, &err))
		qCWarning(corelog) << "Sidebar requestShowTool failed:" << err;
}

void SidebarRegistryImpl::requestHideTool(const QString& id)
{
	QString err;
	if (!m_model->requestHideTool(id, &err))
		qCWarning(corelog) << "Sidebar requestHideTool failed:" << err;
}

} // namespace Core
