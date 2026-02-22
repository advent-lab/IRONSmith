// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

#include <memory>

#include "core/CommandRibbon.hpp"

class QWidget;

namespace Core {
class ISidebarRegistry;
class InfoBarWidget;

class IUiHost : public QObject
{
	Q_OBJECT

public:
	explicit IUiHost(QObject* parent = nullptr) : QObject(parent) {}
	~IUiHost() override = default;

	virtual bool addMenuTab(QString id, QString title) = 0;
	virtual bool setActiveMenuTab(QString id) = 0;
	virtual QString activeMenuTab() const = 0;

	virtual bool ensureRibbonPage(QString pageId, QString title) = 0;
	virtual bool ensureRibbonGroup(QString pageId, QString groupId, QString title) = 0;

	virtual RibbonResult setRibbonGroupLayout(QString pageId,
											  QString groupId,
											  std::unique_ptr<RibbonNode> root) = 0;
	virtual void beginRibbonUpdateBatch() = 0;
	virtual void endRibbonUpdateBatch() = 0;

	virtual QAction* ribbonCommand(QString pageId, QString groupId, QString itemId) = 0;
	virtual RibbonResult addRibbonCommand(QString pageId,
										 QString groupId,
										 QString itemId,
										 QAction* action,
										 RibbonControlType type = RibbonControlType::Button,
										 RibbonPresentation pres = {}) = 0;

	virtual RibbonResult addRibbonSeparator(QString pageId, QString groupId, QString itemId = {}) = 0;
	virtual RibbonResult addRibbonStretch(QString pageId, QString groupId, QString itemId = {}) = 0;

	virtual ISidebarRegistry* sidebarRegistry() const = 0;
	virtual void setLeftSidebar(QWidget* w) = 0;
	virtual void setRightSidebar(QWidget* w) = 0;

	virtual void setPlaygroundTopBar(QWidget* w) = 0;
	virtual void setPlaygroundBottomBar(QWidget* w) = 0;
	virtual InfoBarWidget* playgroundTopBar() const = 0;
	virtual InfoBarWidget* playgroundBottomBar() const = 0;

	virtual void setPlaygroundCenterBase(QWidget* w) = 0;
	virtual QWidget* playgroundOverlayHost() const = 0;

signals:
	void activeMenuTabChanged(const QString& id);
};

} // namespace Core
