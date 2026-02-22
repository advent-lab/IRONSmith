// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/CorePlugin.hpp"

#include <QtGui/QAction>
#include <QtCore/QDebug>
#include <QElapsedTimer>

#include <extensionsystem/PluginManager.hpp>

#include "core/CoreGlobal.hpp"
#include "core/CoreImpl.hpp"
#include "core/CoreConstants.hpp"
#include "core/CommandRibbon.hpp"
#include "core/HeaderInfoService.hpp"
#include "core/ui/IUiHost.hpp"
#include "core/ui/IconLoader.hpp"
#include "core/widgets/InfoBarWidget.hpp"

// Sidebar test wiring (Core-internal)
#include "core/SidebarRegistryImpl.hpp"
#include "core/widgets/ToolRailWidget.hpp"
#include "core/ui/UiStyle.hpp"

namespace Core {

CorePlugin::CorePlugin(QObject* parent)
	: ExtensionSystem::IPlugin(parent)
{
}

CorePlugin::~CorePlugin()
{
	if (!m_core)
		return;

	if (m_headerInfo)
		ExtensionSystem::PluginManager::removeObject(static_cast<Core::IHeaderInfo*>(m_headerInfo));

	if (auto* ui = m_core->uiHost())
		ExtensionSystem::PluginManager::removeObject(ui);

}

Utils::Result CorePlugin::initialize(const QStringList& arguments,
									 ExtensionSystem::PluginManager& manager)
{
	Q_UNUSED(arguments);
	Q_UNUSED(manager);

	qCInfo(corelog) << "Initializing...";
	if (m_core)
		return Utils::Result::failure("CorePlugin initialized twice.");

	m_core = new CoreImpl(this);

	auto* ui = m_core->uiHost();
	if (!ui)
		return Utils::Result::failure("CorePlugin failed to create IUiHost.");

	ExtensionSystem::PluginManager::addObject(ui);

	qCInfo(corelog) << "setting up command ribbon actions.";
	QElapsedTimer timer;
	timer.start();
	setupCommandRibbonActions(ui);
	qint64 ns = timer.nsecsElapsed();
	qCInfo(corelog) << "command ribbon actions setup complete: avg =" << (ns/1e9) << " secs.";
	if (auto* bar = ui->playgroundTopBar()) {
		m_headerInfo = new Internal::HeaderInfoService(this);
		m_headerInfo->bindInfoBar(bar);
		ExtensionSystem::PluginManager::addObject(static_cast<Core::IHeaderInfo*>(m_headerInfo));
	}

	return Utils::Result::success();
}

void CorePlugin::extensionsInitialized(ExtensionSystem::PluginManager& manager)
{
	Q_UNUSED(manager);

	qCInfo(corelog) << "extensionsInitialized()...";
	if (m_core)
		m_core->open();
}

void CorePlugin::setupCommandRibbonActions(Core::IUiHost* uiHost)
{
	if (!uiHost)
		return;

	struct RibbonUpdateBatchGuard final
	{
		Core::IUiHost* host = nullptr;
		~RibbonUpdateBatchGuard()
		{
			if (host)
				host->endRibbonUpdateBatch();
		}
	};

	uiHost->beginRibbonUpdateBatch();
	const RibbonUpdateBatchGuard batchGuard{uiHost};

	uiHost->addMenuTab(Constants::RIBBON_TAB_HOME, "Home");
	uiHost->addMenuTab(Constants::RIBBON_TAB_VIEW, "View");
	uiHost->addMenuTab(Constants::RIBBON_TAB_OUTPUT, "Output");

	uiHost->setActiveMenuTab(Constants::RIBBON_TAB_HOME);

	setupHomePageCommands(uiHost);
	setupViewPageCommands(uiHost);
	setupOutputPageCommands(uiHost);
}


void CorePlugin::setupHomePageCommands(Core::IUiHost* uiHost)
{
	if (!uiHost)
		return;

	uiHost->ensureRibbonGroup(Constants::RIBBON_TAB_HOME, Constants::RIBBON_TAB_HOME_PROJECT_GROUP, "Project");
	uiHost->ensureRibbonGroup(Constants::RIBBON_TAB_HOME, Constants::RIBBON_TAB_HOME_CANVAS_GROUP, "Canvas");
	uiHost->ensureRibbonGroup(Constants::RIBBON_TAB_HOME, Constants::RIBBON_TAB_HOME_WIRES_GROUP, "Wires");
	uiHost->ensureRibbonGroup(Constants::RIBBON_TAB_HOME, Constants::RIBBON_TAB_HOME_VIEW_GROUP, "View");

	auto* actNew    = new QAction(tr("New Design"), this);
	auto* actOpen   = new QAction(tr("Open…"), this);

	actNew->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/file_new_icon.svg"), QSize(28, 28)));
	actOpen->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/opened_folder.svg"), QSize(28, 28)));

	connect(actNew, &QAction::triggered, this, [] {
		qCWarning(corelog) << "New Design triggered (no project service bound yet).";
	});

	RibbonPresentation projectPres;
	projectPres.size = RibbonVisualSize::Large;
	projectPres.iconPlacement = RibbonIconPlacement::AboveText;
	projectPres.iconPx = 28;

	auto projectRoot = RibbonNode::makeRow("project_root");
	projectRoot->addCommand(Constants::PROJECT_NEW_ITEMID, actNew, RibbonControlType::Button, projectPres);
	projectRoot->addCommand(Constants::PROJECT_OPEN_ITEMID, actOpen, RibbonControlType::Button, projectPres);

	if (const auto res = uiHost->setRibbonGroupLayout(Constants::RIBBON_TAB_HOME,
	                                                  Constants::RIBBON_TAB_HOME_PROJECT_GROUP,
	                                                  std::move(projectRoot));
	    !res) {
		qCWarning(corelog) << res.error;
	}

	auto* actSelect = new QAction(tr("Select"), this);
	auto* actPan = new QAction(tr("Pan"), this);
	auto* actLink = new QAction(tr("Link"), this);
	auto* actSplit = new QAction(tr("Split"), this);
	auto* actJoin = new QAction(tr("Join"), this);
	auto* actBroadcast = new QAction(tr("Broadcast"), this);
	auto* actFifo = new QAction(tr("FIFO"), this);
	auto* actForwardFifo = new QAction(tr("FWD FIFO"), this);

	actSelect->setCheckable(true);
	actPan->setCheckable(true);
	actLink->setCheckable(true);
	actSplit->setCheckable(true);
	actJoin->setCheckable(true);
	actBroadcast->setCheckable(true);
	actFifo->setCheckable(true);
	actForwardFifo->setCheckable(true);

	actSelect->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/select_hand_pointer_icon.svg"), QSize(20, 20)));
	actPan->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/pan_icon.svg"), QSize(20, 20)));
	actLink->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/link_icon.svg"), QSize(28, 28)));
	actSplit->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/split_link_icon.svg"), QSize(20, 20)));
	actJoin->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/merge_link_icon.svg"), QSize(20, 20)));
	actBroadcast->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/broadcast_link_icon.svg"), QSize(20, 20)));
	actFifo->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/fifo_icon.svg"), QSize(20, 20)));
	actForwardFifo->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/forward_fifo_icon.svg"), QSize(20, 20)));

	actFifo->setToolTip(tr("FIFO Link Mode: create ObjectFIFO links"));
	actForwardFifo->setToolTip(tr("Forward FIFO Mode: create FWD ObjectFIFO links"));

	RibbonPresentation smallPres;
	smallPres.size = RibbonVisualSize::Small;
	smallPres.iconPlacement = RibbonIconPlacement::AboveText;
	smallPres.iconPx = 20;

	RibbonPresentation linkPres = smallPres;
	linkPres.size = RibbonVisualSize::Large;
	linkPres.iconPx = 28;

	auto canvasRoot = RibbonNode::makeColumn("canvas_root");
	auto& canvasTopRow = canvasRoot->addRow("canvas_top_row");
	canvasTopRow.addCommand(Constants::CANVAS_SELECT_ITEMID, actSelect, RibbonControlType::ToggleButton, smallPres);
	canvasTopRow.addCommand(Constants::CANVAS_PAN_ITEMID, actPan, RibbonControlType::ToggleButton, smallPres);
	canvasTopRow.addCommand(Constants::CANVAS_LINK_ITEMID, actLink, RibbonControlType::ToggleButton, linkPres);

	auto& canvasLinkRow = canvasRoot->addRow("canvas_link_row");
	canvasLinkRow.addCommand(Constants::CANVAS_LINK_SPLIT_ITEMID, actSplit, RibbonControlType::ToggleButton, smallPres);
	canvasLinkRow.addCommand(Constants::CANVAS_LINK_JOIN_ITEMID, actJoin, RibbonControlType::ToggleButton, smallPres);
	canvasLinkRow.addCommand(Constants::CANVAS_LINK_BROADCAST_ITEMID, actBroadcast, RibbonControlType::ToggleButton, smallPres);
	canvasLinkRow.addCommand(Constants::CANVAS_LINK_FIFO_ITEMID, actFifo, RibbonControlType::ToggleButton, smallPres);
	canvasLinkRow.addCommand(Constants::CANVAS_LINK_FORWARD_FIFO_ITEMID, actForwardFifo, RibbonControlType::ToggleButton, smallPres);

	if (const auto res = uiHost->setRibbonGroupLayout(Constants::RIBBON_TAB_HOME,
	                                                  Constants::RIBBON_TAB_HOME_CANVAS_GROUP,
	                                                  std::move(canvasRoot));
	    !res) {
		qCWarning(corelog) << res.error;
	}

	auto* actAutoRoute = new QAction(tr("Auto Route"), this);
	auto* actClearOverrides = new QAction(tr("Clear Overrides"), this);
	auto* actToggleArrows = new QAction(tr("Wire Arrows"), this);

	actToggleArrows->setCheckable(true);
	actAutoRoute->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/auto_route_icon.svg"), QSize(20, 20)));

	auto wiresRoot = RibbonNode::makeRow("wires_root");
	wiresRoot->addCommand(Constants::CANVAS_WIRE_AUTO_ROUTE_ITEMID, actAutoRoute, RibbonControlType::Button, smallPres);
	wiresRoot->addCommand(Constants::CANVAS_WIRE_CLEAR_OVERRIDES_ITEMID, actClearOverrides, RibbonControlType::Button, smallPres);
	wiresRoot->addCommand(Constants::CANVAS_WIRE_TOGGLE_ARROWS_ITEMID, actToggleArrows, RibbonControlType::ToggleButton, smallPres);

	if (const auto res = uiHost->setRibbonGroupLayout(Constants::RIBBON_TAB_HOME,
	                                                  Constants::RIBBON_TAB_HOME_WIRES_GROUP,
	                                                  std::move(wiresRoot));
	    !res) {
		qCWarning(corelog) << res.error;
	}
	auto* actZoomIn = new QAction(tr("Zoom In"), this);
	auto* actZoomOut = new QAction(tr("Zoom Out"), this);
	auto* actZoomFit = new QAction(tr("Zoom to Fit"), this);
	auto* actResetView = new QAction(tr("Reset View"), this);

	actZoomIn->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/zoom_in_icon.svg"), QSize(20, 20)));
	actZoomOut->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/zoom_out_icon.svg"), QSize(20, 20)));
	actZoomFit->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/zoom_fit_icon.svg"), QSize(20, 20)));
	actResetView->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/reset_icon.svg"), QSize(20, 20)));

	auto viewRoot = RibbonNode::makeRow("view_root");
	viewRoot->addCommand(Constants::CANVAS_VIEW_ZOOM_IN_ITEMID, actZoomIn, RibbonControlType::Button, smallPres);
	viewRoot->addCommand(Constants::CANVAS_VIEW_ZOOM_OUT_ITEMID, actZoomOut, RibbonControlType::Button, smallPres);
	viewRoot->addCommand(Constants::CANVAS_VIEW_ZOOM_FIT_ITEMID, actZoomFit, RibbonControlType::Button, smallPres);
	viewRoot->addCommand(Constants::CANVAS_VIEW_RESET_ITEMID, actResetView, RibbonControlType::Button, smallPres);

	if (const auto res = uiHost->setRibbonGroupLayout(Constants::RIBBON_TAB_HOME,
	                                                  Constants::RIBBON_TAB_HOME_VIEW_GROUP,
	                                                  std::move(viewRoot));
	    !res) {
		qCWarning(corelog) << res.error;
	}
}

void CorePlugin::setupViewPageCommands(Core::IUiHost* uiHost)
{
	Q_UNUSED(uiHost);
}

void CorePlugin::setupOutputPageCommands(Core::IUiHost* uiHost)
{
	Q_UNUSED(uiHost);
}

} // namespace Core
