// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QObject>

#include "core/CoreGlobal.hpp"

class QMainWindow;
class QWidget;

namespace Core {
namespace Internal { class MainWindow; }

class CORE_EXPORT ICore : public QObject
{
	Q_OBJECT

public:
	explicit ICore(QObject* parent = nullptr) : QObject(parent) {}
	~ICore() override = default;

	virtual void setCentralWidget(QWidget* widget) = 0;

	virtual void open() = 0;

signals:
	void coreAboutToOpen();
	void coreOpened();
};

} // namespace Core
