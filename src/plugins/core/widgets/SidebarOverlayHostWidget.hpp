// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtWidgets/QWidget>

#include "core/api/SidebarToolSpec.hpp"

class QVBoxLayout;

namespace Core {

class SidebarModel;

class SidebarOverlayHostWidget final : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(int overlayWidth READ overlayWidth WRITE setOverlayWidth)

public:
	explicit SidebarOverlayHostWidget(SidebarModel* model,
									 SidebarSide side,
									 SidebarFamily family = SidebarFamily::Vertical,
									 QWidget* parent = nullptr);

	bool hasPanels() const noexcept { return m_hasPanels; }
	int overlayWidth() const noexcept { return m_overlayWidth; }
	int panelWidth() const noexcept { return m_panelWidth; }
	void setOverlayWidth(int width);
	void setPanelWidth(int width);
	void setPanelWidthClamped(int w);

signals:
	void hasPanelsChanged(bool hasPanels);
    void panelWidthChanged(int width);

private slots:
	void syncFromModel();

private:
	struct PanelInstance {
		QWidget* chrome = nullptr;
		QWidget* content = nullptr;
	};

	QString desiredExclusiveId() const;
	QStringList desiredAdditiveIds() const;

	PanelInstance ensurePanel(const QString& id);
	void destroyPanel(const QString& id);

	void clearLayout(QVBoxLayout* lay);
	void applyVisibleState(bool visible);
	void updateRailExpandedProperty(bool expanded);
private:
	SidebarModel* m_model = nullptr; // non-owning
	SidebarSide m_side = SidebarSide::Left;
	SidebarFamily m_family = SidebarFamily::Vertical;

	class SidebarFamilyPanelWidget* m_familyPanel = nullptr;
	class SidebarOverlayResizeGrip* m_resizeGrip = nullptr;

	QHash<QString, PanelInstance> m_panels;
	bool m_hasPanels = false;

    int m_overlayWidth = 0;
    int m_panelWidth = 0;
};

} // namespace Core
