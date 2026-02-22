// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

class QStackedWidget;

namespace Core {

class CommandRibbon;
class CommandRibbonPage;
class CommandRibbonGroup;
class RibbonNode;

class CommandRibbonWidget final : public QWidget
{
	Q_OBJECT

public:
	explicit CommandRibbonWidget(QWidget* parent = nullptr);

	void setModel(CommandRibbon* model);
	CommandRibbon* model() const { return m_model; }

private slots:
	void scheduleRebuild();
	void rebuildAll();
	void syncActivePage();

private:
	QWidget* buildPageWidget(CommandRibbonPage* page);
	QWidget* buildGroupWidget(CommandRibbonGroup* group);
	QWidget* buildGroupDividerWidget(QWidget* parent);

	QWidget* buildNodeWidget(const RibbonNode& node, QWidget* parent);
	QWidget* buildLeafCommandWidget(const RibbonNode& node, QWidget* parent);
	QWidget* buildLeafWidgetFactoryWidget(const RibbonNode& node, QWidget* parent);
	QWidget* buildSeparatorWidget(Qt::Orientation parentLayoutOrientation, QWidget* parent);

private:
	QPointer<CommandRibbon> m_model;
	QStackedWidget* m_stack = nullptr;

	QHash<QString, int> m_pageIndex;
	bool m_rebuildScheduled = false;
};

} // namespace Core
