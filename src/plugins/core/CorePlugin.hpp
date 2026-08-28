// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QPointer>
#include <QtCore/QStringList>

#include "extensionsystem/IPlugin.hpp"

namespace Core {
class CoreImpl;
class IUiHost;
namespace Internal {
class HeaderInfoService;
}

class CorePlugin final : public ExtensionSystem::IPlugin
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "org.ironsmith.plugin" FILE "Core.json")

public:
	explicit CorePlugin(QObject* parent = nullptr);
	~CorePlugin() override;

	Utils::Result initialize(const QStringList& arguments,
							 ExtensionSystem::PluginManager& manager) override;

	void extensionsInitialized(ExtensionSystem::PluginManager& manager) override;

private:
	void setupCommandRibbonActions(Core::IUiHost* uiHost);
	void setupHomePageCommands(Core::IUiHost* uiHost);
	void setupOutputPageCommands(Core::IUiHost* uiHost);

	QPointer<CoreImpl> m_core;
	QPointer<Internal::HeaderInfoService> m_headerInfo;
};

} // namespace Core
