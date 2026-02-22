// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

#include "core/GlobalMenuBar.hpp" // required for QPointer<GlobalMenuBar>

class QHBoxLayout;

namespace Core {

class GlobalMenuBarWidget final : public QWidget
{
	Q_OBJECT

public:
	explicit GlobalMenuBarWidget(QWidget* parent = nullptr);

	void setModel(GlobalMenuBar* model);
	GlobalMenuBar* model() const { return m_model; }

signals:
	// Kept for compatibility; currently mirrors active selection.
	void itemTriggered(const QString& id);

private slots:
	void rebuild();

private:
	void clearButtons();

private:
	QPointer<GlobalMenuBar> m_model;
	QHBoxLayout* m_layout = nullptr;
};

} // namespace Core