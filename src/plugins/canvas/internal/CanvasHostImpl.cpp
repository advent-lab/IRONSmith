// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/internal/CanvasHostImpl.hpp"

#include "canvas/CanvasController.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasSelectionModel.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/utils/CanvasLinkingMode.hpp"

#include <extensionsystem/PluginManager.hpp>
#include <core/ui/IUiHost.hpp>
#include <core/StatusBarField.hpp>
#include <core/widgets/InfoBarWidget.hpp>

#include <QtCore/QString>
#include <QShortcut>

namespace Canvas::Internal {

CanvasHostImpl::CanvasHostImpl(QObject* parent)
	: Canvas::Api::ICanvasHost(parent)
{
    m_emptyTitle = QStringLiteral("No design open.");
    m_emptyMessage = QStringLiteral("Create or open a design to start.");
}


void CanvasHostImpl::wireIntoApplication(ExtensionSystem::PluginManager& manager)
{
	if (m_view)
		return;

	m_uiHost = manager.getObject<Core::IUiHost>();
	if (!m_uiHost)
		return;

	m_document = new CanvasDocument(this);
	m_document->setStatusText(QStringLiteral("Canvas"));

	m_view = new CanvasView();
	m_view->setDocument(m_document);
    applyEmptyState();

	m_selection = new CanvasSelectionModel(this);
	m_view->setSelectionModel(m_selection);
	m_controller = new CanvasController(m_document, m_view, m_selection, this);
	m_view->setController(m_controller);

	if (auto* bar = m_uiHost ? m_uiHost->playgroundBottomBar() : nullptr) {
		auto* mode = bar->ensureField(QStringLiteral("mode"));
		if (mode) {
			mode->setLabel(QStringLiteral("MODE"));
			mode->setSide(Core::StatusBarField::Side::Left);
			mode->setValue(QStringLiteral("NORMAL"));
			m_modeField = mode;
		}

		auto* zoom = bar->ensureField(QStringLiteral("canvas_zoom"));
		if (zoom) {
			zoom->setLabel(QStringLiteral("ZOOM"));
			zoom->setSide(Core::StatusBarField::Side::Left);
			m_zoomField = zoom;
		}

		auto* pan = bar->ensureField(QStringLiteral("canvas_pan"));
		if (pan) {
			pan->setLabel(QStringLiteral("PAN"));
			pan->setSide(Core::StatusBarField::Side::Left);
			m_panField = pan;
		}

		auto* sel = bar->ensureField(QStringLiteral("canvas_selection"));
		if (sel) {
			sel->setLabel(QStringLiteral("SEL"));
			sel->setSide(Core::StatusBarField::Side::Left);
			m_selectionField = sel;
		}
	}

	auto modeLabel = [](CanvasController::Mode mode) -> QString {
		switch (mode) {
			case CanvasController::Mode::Normal:  return QStringLiteral("NORMAL");
			case CanvasController::Mode::Panning: return QStringLiteral("PANNING");
			case CanvasController::Mode::Linking: return QStringLiteral("LINKING");
		}
		return QStringLiteral("NORMAL");
	};
	auto modeValue = [modeLabel](CanvasController::Mode mode,
	                                           CanvasController::LinkingMode linkingMode,
                                               bool boundProducerPlacementActive) -> QString {
		const QString base = modeLabel(mode);
        if (boundProducerPlacementActive)
            return base + QStringLiteral("|BOUND_PRODUCER");
		if (mode != CanvasController::Mode::Linking)
			return base;
		const QString sub = Support::linkingModeLabel(linkingMode);
		if (sub.isEmpty())
			return base;
		return base + QStringLiteral("|") + sub;
	};

	if (m_controller) {
		auto updateMode = [this, modeValue]() {
			if (m_modeField && m_controller)
				m_modeField->setValue(modeValue(m_controller->mode(),
                                               m_controller->linkingMode(),
                                               m_controller->isBoundProducerPlacementActive()));
		};
		connect(m_controller, &CanvasController::modeChanged, this,
		        [updateMode](CanvasController::Mode) { updateMode(); });
		connect(m_controller, &CanvasController::linkingModeChanged, this,
		        [updateMode](CanvasController::LinkingMode) { updateMode(); });
        connect(m_controller, &CanvasController::boundProducerPlacementChanged, this,
                [updateMode](bool) { updateMode(); });
		updateMode();
	}

	if (m_view) {
		auto formatZoom = [](double zoom) -> QString {
			const double pct = zoom * 100.0;
			return QString::number(pct, 'f', 0) + QLatin1String("%");
		};
		auto formatPan = [](const QPointF& pan) -> QString {
			return QString::number(pan.x(), 'f', 1) + QLatin1String(", ")
			       + QString::number(pan.y(), 'f', 1);
		};

		auto updateZoom = [this, formatZoom]() {
			if (m_zoomField && m_view)
				m_zoomField->setValue(formatZoom(m_view->displayZoom()));
		};
		auto updatePan = [this, formatPan]() {
			if (m_panField && m_view)
				m_panField->setValue(formatPan(m_view->pan()));
		};

		connect(m_view, &CanvasView::zoomChanged, this,
		        [updateZoom](double) { updateZoom(); });
		connect(m_view, &CanvasView::panChanged, this,
		        [updatePan](const QPointF&) { updatePan(); });

		updateZoom();
		updatePan();
	}

	if (m_selection) {
		auto updateSel = [this]() {
			if (!m_selectionField || !m_selection)
				return;
			m_selectionField->setValue(QString::number(m_selection->selectedItems().size()));
		};
		connect(m_selection, &CanvasSelectionModel::selectedItemsChanged, this,
		        [updateSel]() { updateSel(); });
		connect(m_selection, &CanvasSelectionModel::selectedItemChanged, this,
		        [updateSel](ObjectId) { updateSel(); });
		updateSel();
	}

	connect(m_view, &CanvasView::canvasMousePressed, m_controller, &CanvasController::onCanvasMousePressed);
	connect(m_view, &CanvasView::canvasMouseMoved, m_controller, &CanvasController::onCanvasMouseMoved);
	connect(m_view, &CanvasView::canvasMouseReleased, m_controller, &CanvasController::onCanvasMouseReleased);
    connect(m_view, &CanvasView::canvasContextMenuRequested,
            m_controller, &CanvasController::onCanvasContextMenuRequested);
	connect(m_view, &CanvasView::canvasWheel, m_controller, &CanvasController::onCanvasWheel);
	connect(m_view, &CanvasView::canvasKeyPressed, m_controller, &CanvasController::onCanvasKeyPressed);

    auto* cancelBoundProducerShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_view);
    cancelBoundProducerShortcut->setContext(Qt::WindowShortcut);
    connect(cancelBoundProducerShortcut, &QShortcut::activated, this, [this]() {
        if (m_controller)
            m_controller->cancelBoundProducerPlacement();
    });

	m_uiHost->setPlaygroundCenterBase(m_view);
	m_view->setFocus(Qt::OtherFocusReason);
}

QWidget* CanvasHostImpl::viewWidget() const
{
	return m_view;
}

CanvasDocument* CanvasHostImpl::document() const
{
	return m_document;
}

CanvasController* CanvasHostImpl::controller() const
{
	return m_controller;
}

void CanvasHostImpl::setCanvasActive(bool active)
{
    if (m_canvasActive == active)
        return;
    m_canvasActive = active;
    applyEmptyState();
    emit canvasActiveChanged(m_canvasActive);
}

bool CanvasHostImpl::canvasActive() const
{
    return m_canvasActive;
}

void CanvasHostImpl::setEmptyStateText(const QString& title, const QString& message)
{
    const QString cleanedTitle = title.trimmed();
    const QString cleanedMessage = message.trimmed();
    if (m_emptyTitle == cleanedTitle && m_emptyMessage == cleanedMessage)
        return;
    m_emptyTitle = cleanedTitle;
    m_emptyMessage = cleanedMessage;
    applyEmptyState();
}

void CanvasHostImpl::applyEmptyState()
{
    if (!m_view)
        return;
    const bool showEmpty = !m_canvasActive;
    m_view->setEmptyStateVisible(showEmpty);
    if (showEmpty)
        m_view->setEmptyStateText(m_emptyTitle, m_emptyMessage);
    m_view->update();
}

} // namespace Canvas::Internal
