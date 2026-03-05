// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <core/StatusBarField.hpp>

class QHBoxLayout;

namespace Core {

class StatusBarFieldWidget;

class CORE_EXPORT InfoBarWidget final : public QWidget
{
	Q_OBJECT

public:
	explicit InfoBarWidget(QWidget* parent = nullptr);
	~InfoBarWidget() override = default;

	void setField(StatusBarField* field);

	StatusBarField* field(const QString& id) const noexcept;
	bool hasField(const QString& id) const noexcept;

	StatusBarField* ensureField(const QString& id);

	void removeField(const QString& id);
	void clear();

signals:
	void fieldAdded(StatusBarField* field);
	void fieldRemoved(const QString& id);

private slots:
	void onFieldSideChanged(StatusBarField::Side side);

private:
	void addFieldWidget(StatusBarField* field);
	void removeFieldWidget(const QString& id);

	QHBoxLayout* m_root = nullptr;
	QHBoxLayout* m_left = nullptr;
	QHBoxLayout* m_right = nullptr;

	QHash<QString, StatusBarField*> m_fields;
	QHash<QString, StatusBarFieldWidget*> m_fieldWidgets;
};

} // namespace Core
