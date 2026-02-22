// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <functional>

#include "core/api/SidebarToolSpec.hpp"

class QWidget;

namespace Core {

class ISidebarRegistry : public QObject
{
	Q_OBJECT

public:
	using QObject::QObject;
	~ISidebarRegistry() override = default;

	using PanelFactory = std::function<QWidget*(QWidget* parent)>;
	virtual bool registerTool(const SidebarToolSpec& spec,
							  PanelFactory factory,
							  QString* errorOut = nullptr) = 0;

	virtual bool unregisterTool(const QString& id, QString* errorOut = nullptr) = 0;
	virtual bool isToolOpen(const QString& id) const = 0;
public slots:
	virtual void requestShowTool(const QString& id) = 0;
	virtual void requestHideTool(const QString& id) = 0;

signals:
	void toolRegistered(const QString& id);
	void toolUnregistered(const QString& id);
	void toolOpenStateChanged(const QString& id, bool open);
};

} // namespace Core
