// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/CorePlugin.hpp"

#include <QtGui/QAction>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMenu>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>
#include <QtWidgets/QWidgetAction>
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

// The creation of the command ribbon should be delegated to a special component that oversees all actionable items through corresponding action item interface:
// i.e., ActionManager -> IActionManager | ex: ProjectGroupActions -> IActionItem
// please see qtc's implementation (solid reference): https://github.com/qt-creator/qt-creator/tree/master/src/plugins/coreplugin/actionmanager
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
	uiHost->addMenuTab(Constants::RIBBON_TAB_OUTPUT, "Output");

	uiHost->setActiveMenuTab(Constants::RIBBON_TAB_HOME);

	setupHomePageCommands(uiHost);
	setupOutputPageCommands(uiHost);
}

void CorePlugin::setupHomePageCommands(Core::IUiHost* uiHost)
{
	if (!uiHost)
		return;

	uiHost->ensureRibbonGroup(Constants::RIBBON_TAB_HOME, Constants::RIBBON_TAB_HOME_PROJECT_GROUP, "Project");
	uiHost->ensureRibbonGroup(Constants::RIBBON_TAB_HOME, Constants::RIBBON_TAB_HOME_CANVAS_GROUP, "Canvas");
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
	auto* actDistribute = new QAction(tr("Distribute"), this);
	auto* actCollect = new QAction(tr("Collect"), this);
	auto* actFifo = new QAction(tr("FIFO"), this);
	auto* actForwardFifo = new QAction(tr("FWD FIFO"), this);
	auto* actLinkingMenu = new QAction(tr("Linking"), this);
	auto* actMovementPatternsMenu = new QAction(tr("Movement Patterns"), this);
	auto* actDdrTransfersMenu = new QAction(tr("DDR Transfers"), this);

	actSelect->setCheckable(true);
	actPan->setCheckable(true);
	actLink->setCheckable(true);
	actSplit->setCheckable(true);
	actJoin->setCheckable(true);
	actBroadcast->setCheckable(true);
	actDistribute->setCheckable(true);
	actCollect->setCheckable(true);
	actFifo->setCheckable(true);
	actForwardFifo->setCheckable(true);

	actSelect->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/select_hand_pointer_icon.svg"), QSize(20, 20)));
	actPan->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/pan_icon.svg"), QSize(20, 20)));
	actLink->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/link_icon.svg"), QSize(28, 28)));
	actSplit->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/split_mesh_icon.svg"), QSize(20, 20)));
	actJoin->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/join_mesh_icon.svg"), QSize(20, 20)));
	actBroadcast->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/broadcast_mesh_icon.svg"), QSize(20, 20)));
	actDistribute->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/distribute_mesh_icon.svg"), QSize(20, 20)));
	actCollect->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/collect_mesh_icon.svg"), QSize(20, 20)));
	actFifo->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/fifo_icon.svg"), QSize(20, 20)));
	actForwardFifo->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/forward_fifo_icon.svg"), QSize(20, 20)));
	actLinkingMenu->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/link_icon.svg"), QSize(20, 20)));
	actMovementPatternsMenu->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/split_mesh_icon.svg"), QSize(20, 20)));
	actDdrTransfersMenu->setIcon(Ui::IconLoader::load(QStringLiteral(":/ui/icons/svg/distribute_mesh_icon.svg"), QSize(20, 20)));

	actFifo->setToolTip(tr("FIFO Link Mode: create ObjectFIFO links"));
	actForwardFifo->setToolTip(tr("Forward FIFO Mode: create FWD ObjectFIFO links"));
	actLinkingMenu->setToolTip(tr("Linking tools"));
	actMovementPatternsMenu->setToolTip(tr("Split, join, and broadcast patterns"));
	actDdrTransfersMenu->setToolTip(tr("DDR transfer patterns"));

	actLink->setData(QString::fromLatin1(Constants::CANVAS_LINK_ITEMID));
	actSplit->setData(QString::fromLatin1(Constants::CANVAS_LINK_SPLIT_ITEMID));
	actJoin->setData(QString::fromLatin1(Constants::CANVAS_LINK_JOIN_ITEMID));
	actBroadcast->setData(QString::fromLatin1(Constants::CANVAS_LINK_BROADCAST_ITEMID));
	actDistribute->setData(QString::fromLatin1(Constants::CANVAS_LINK_DISTRIBUTE_ITEMID));
	actCollect->setData(QString::fromLatin1(Constants::CANVAS_LINK_COLLECT_ITEMID));
	actFifo->setData(QString::fromLatin1(Constants::CANVAS_LINK_FIFO_ITEMID));
	actForwardFifo->setData(QString::fromLatin1(Constants::CANVAS_LINK_FORWARD_FIFO_ITEMID));

	auto buildHorizontalDropDownMenu = [this](std::initializer_list<QAction*> actions) {
		auto* menu = new QMenu();
		menu->setObjectName("RibbonDropDownMenu");

		for (QAction* action : actions) {
			if (!action)
				continue;
			action->setParent(menu);
		}

		auto* host = new QWidget(menu);
		host->setObjectName("RibbonDropDownMenuHost");
		auto* row = new QHBoxLayout(host);
		row->setContentsMargins(6, 6, 6, 6);
		row->setSpacing(6);

		for (QAction* action : actions) {
			if (!action)
				continue;

			auto* button = new QToolButton(host);
			button->setObjectName("RibbonDropDownMenuButton");
			button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
			button->setAutoRaise(true);
			button->setDefaultAction(action);
			button->setIconSize(QSize(20, 20));
			button->setCheckable(action->isCheckable());
			row->addWidget(button);

			connect(button, &QToolButton::clicked, menu, [menu]() {
				menu->close();
			});
		}

		auto* widgetAction = new QWidgetAction(menu);
		widgetAction->setDefaultWidget(host);
		menu->addAction(widgetAction);
		return menu;
	};

	actLinkingMenu->setMenu(buildHorizontalDropDownMenu({actLink, actFifo, actForwardFifo}));
	actMovementPatternsMenu->setMenu(buildHorizontalDropDownMenu({actSplit, actJoin, actBroadcast}));
	actDdrTransfersMenu->setMenu(buildHorizontalDropDownMenu({actDistribute, actCollect}));

	RibbonPresentation smallPres;
	smallPres.size = RibbonVisualSize::Small;
	smallPres.iconPlacement = RibbonIconPlacement::AboveText;
	smallPres.iconPx = 20;

	RibbonPresentation menuPres = smallPres;
	menuPres.size = RibbonVisualSize::Small;
	menuPres.iconPlacement = RibbonIconPlacement::TextOnly;
	menuPres.iconPx = 0;

	auto canvasRoot = RibbonNode::makeRow("canvas_root");
	auto& canvasLeft = canvasRoot->addRow("canvas_left");
	canvasLeft.addCommand(Constants::CANVAS_SELECT_ITEMID, actSelect, RibbonControlType::ToggleButton, smallPres);
	canvasLeft.addCommand(Constants::CANVAS_PAN_ITEMID, actPan, RibbonControlType::ToggleButton, smallPres);
	canvasRoot->addSeparator("canvas_section_separator");
	auto& canvasRight = canvasRoot->addColumn("canvas_right");
	canvasRight.addCommand(Constants::CANVAS_LINKING_MENU_ITEMID,
	                       actLinkingMenu,
	                       RibbonControlType::DropDownButton,
	                       menuPres);
	canvasRight.addCommand(Constants::CANVAS_MOVEMENT_PATTERNS_MENU_ITEMID,
	                       actMovementPatternsMenu,
	                       RibbonControlType::DropDownButton,
	                       menuPres);
	canvasRight.addCommand(Constants::CANVAS_DDR_TRANSFERS_MENU_ITEMID,
	                       actDdrTransfersMenu,
	                       RibbonControlType::DropDownButton,
	                       menuPres);

	if (const auto res = uiHost->setRibbonGroupLayout(Constants::RIBBON_TAB_HOME,
	                                                  Constants::RIBBON_TAB_HOME_CANVAS_GROUP,
	                                                  std::move(canvasRoot));
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

void CorePlugin::setupOutputPageCommands(Core::IUiHost* uiHost)
{
	Q_UNUSED(uiHost);
}

} // namespace Core
