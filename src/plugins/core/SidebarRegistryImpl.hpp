// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/api/ISidebarRegistry.hpp"
#include "core/CoreGlobal.hpp"

namespace Core {

class SidebarModel;

class CORE_EXPORT SidebarRegistryImpl final : public ISidebarRegistry
{
	Q_OBJECT

public:
	explicit SidebarRegistryImpl(QObject* parent = nullptr);
	~SidebarRegistryImpl() override = default;

	bool registerTool(const SidebarToolSpec& spec,
					  PanelFactory factory,
					  QString* errorOut = nullptr) override;

	bool unregisterTool(const QString& id, QString* errorOut = nullptr) override;
	bool isToolOpen(const QString& id) const override;

public slots:
	void requestShowTool(const QString& id) override;
	void requestHideTool(const QString& id) override;

public:
	SidebarModel* model() noexcept { return m_model; }
	const SidebarModel* model() const noexcept { return m_model; }

private:
	SidebarModel* m_model = nullptr; // owned (QObject child)
};

} // namespace Core
