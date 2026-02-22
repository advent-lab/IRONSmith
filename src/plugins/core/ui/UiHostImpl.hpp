// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/ui/IUiHost.hpp"
#include "core/api/SidebarToolSpec.hpp"

#include <QtCore/QHash>
#include <QtCore/QTimer>

namespace Core {

class CommandRibbon;
class CommandRibbonWidget;
class FrameWidget;
class PlaygroundWidget;
class GlobalMenuBar;
class SidebarRegistryImpl;
class ISidebarRegistry;
class SidebarOverlayHostWidget;
namespace Internal { class CoreUiState; }

class UiHostImpl final : public IUiHost
{
	Q_OBJECT

public:
	explicit UiHostImpl(FrameWidget* frame, QObject* parent = nullptr);
    ~UiHostImpl() override;

	bool addMenuTab(QString id, QString title) override;
	bool setActiveMenuTab(QString id) override;
	QString activeMenuTab() const override;

	ISidebarRegistry* sidebarRegistry() const override;

	bool ensureRibbonPage(QString pageId, QString title) override;
	bool ensureRibbonGroup(QString pageId, QString groupId, QString title) override;
	RibbonResult setRibbonGroupLayout(QString pageId,
									 QString groupId,
									 std::unique_ptr<RibbonNode> root) override;
	void beginRibbonUpdateBatch() override;
	void endRibbonUpdateBatch() override;

	QAction* ribbonCommand(QString pageId, QString groupId, QString itemId) override;
	RibbonResult addRibbonCommand(QString pageId,
								 QString groupId,
								 QString itemId,
								 QAction* action,
								 RibbonControlType type,
								 RibbonPresentation pres) override;
	RibbonResult addRibbonSeparator(QString pageId, QString groupId, QString itemId) override;
	RibbonResult addRibbonStretch(QString pageId, QString groupId, QString itemId) override;

	void setLeftSidebar(QWidget* w) override;
	void setRightSidebar(QWidget* w) override;

	void setPlaygroundTopBar(QWidget* w) override;
	void setPlaygroundBottomBar(QWidget* w) override;
	InfoBarWidget* playgroundTopBar() const override;
	InfoBarWidget* playgroundBottomBar() const override;

	void setPlaygroundCenterBase(QWidget* w) override;
	QWidget* playgroundOverlayHost() const override;

private:
	static void replaceSingleChild(QWidget* host, QWidget* child);
    void restoreAndTrackOverlayHost(class SidebarOverlayHostWidget* host,
                                    SidebarSide side,
                                    SidebarFamily family);
    void flushSidebarPanelWidthState();

private:
	FrameWidget* m_frame = nullptr;
	GlobalMenuBar* m_menuModel = nullptr;
	CommandRibbon* m_ribbonModel = nullptr;
	CommandRibbonWidget* m_ribbonWidget = nullptr;
	PlaygroundWidget* m_playground = nullptr;
	SidebarRegistryImpl* m_sidebarRegistry = nullptr;
    std::unique_ptr<Internal::CoreUiState> m_uiState;
    QHash<QString, int> m_pendingSidebarWidths;
    QTimer m_sidebarWidthSaveTimer;
};

} // namespace Core