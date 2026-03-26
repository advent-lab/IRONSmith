// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/CanvasController.hpp"

#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasCommands.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasItem.hpp"
#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/Tools.hpp"
#include "canvas/services/CanvasHitTestService.hpp"
#include "canvas/controllers/CanvasDragController.hpp"
#include "canvas/controllers/CanvasInteractionHelpers.hpp"
#include "canvas/controllers/CanvasLinkingController.hpp"
#include "canvas/controllers/CanvasContextMenuController.hpp"
#include "canvas/controllers/CanvasSelectionController.hpp"

#include <QtCore/QString>
#include <QtCore/QtGlobal>
#include <algorithm>
#include <utility>

namespace {

QPointF wheelPanDeltaView(const QPoint& angleDelta, const QPoint& pixelDelta, Qt::KeyboardModifiers mods)
{
    QPoint delta = pixelDelta.isNull() ? angleDelta : pixelDelta;
    if (delta.isNull())
        return {};

    if (mods.testFlag(Qt::ShiftModifier)) {
        if (delta.x() == 0)
            delta.setX(delta.y());
        delta.setY(0);
    }

    return QPointF(delta);
}

bool isPropertiesShortcut(Qt::KeyboardModifiers mods)
{
    const Qt::KeyboardModifiers relevantModifiers =
        mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier);
    return relevantModifiers == (Qt::ControlModifier | Qt::ShiftModifier);
}

} // namespace

namespace Canvas {

CanvasController::CanvasController(CanvasDocument* doc, CanvasView* view, CanvasSelectionModel* selection, QObject* parent)
	: QObject(parent)
	, m_doc(doc)
	, m_view(view)
{
	m_selectionController = std::make_unique<Controllers::CanvasSelectionController>(m_doc, m_view, selection);
	m_dragController = std::make_unique<Controllers::CanvasDragController>(m_doc, m_view, m_selectionController.get());
	m_linkingController = std::make_unique<Controllers::CanvasLinkingController>(m_doc, m_view,
	                                                                             m_selectionController.get(),
	                                                                             m_dragController.get());
    m_contextMenuController = std::make_unique<Controllers::CanvasContextMenuController>(m_doc,
                                                                                          m_view,
                                                                                          m_selectionController.get(),
                                                                                          this);
}

CanvasController::~CanvasController() = default;

const QSet<ObjectId>& CanvasController::selectedItems() const noexcept
{
    if (!m_selectionController) {
        static const QSet<ObjectId> kEmpty;
        return kEmpty;
    }
    return m_selectionController->selectedItems();
}

CanvasController::LinkingMode CanvasController::linkingMode() const noexcept
{
    return m_linkingController ? m_linkingController->linkingMode() : LinkingMode::Normal;
}

bool CanvasController::isLinkingInProgress() const noexcept
{
    return m_linkingController && m_linkingController->isLinkingInProgress();
}

bool CanvasController::isEndpointDragActive() const noexcept
{
    return m_dragController && m_dragController->isEndpointDragActive();
}

bool CanvasController::isBoundProducerPlacementActive() const noexcept
{
    return m_boundProducerPlacementActive;
}

ObjectId CanvasController::linkStartItem() const noexcept
{
    return m_linkingController ? m_linkingController->linkStartItem() : ObjectId{};
}

PortId CanvasController::linkStartPort() const noexcept
{
    return m_linkingController ? m_linkingController->linkStartPort() : PortId{};
}

QPointF CanvasController::linkPreviewScene() const noexcept
{
    return m_linkingController ? m_linkingController->linkPreviewScene() : QPointF();
}

void CanvasController::setMode(Mode mode)
{
	if (m_mode == mode)
		return;

	m_mode = mode;
	if (m_mode != Mode::Linking) {
		if (linkingMode() != LinkingMode::Normal) {
			setLinkingMode(LinkingMode::Normal);
		} else if (m_linkingController) {
			m_linkingController->resetLinkingSession();
		}
	}
	emit modeChanged(m_mode);
}

void CanvasController::setLinkingMode(LinkingMode mode)
{
	if (linkingMode() == mode)
		return;
	if (m_linkingController)
		m_linkingController->setLinkingMode(mode);
	emit linkingModeChanged(mode);
}

QPointF CanvasController::sceneToView(const QPointF& scenePos) const
{
    if (!m_view)
        return {};
    return Tools::sceneToView(scenePos, m_view->pan(), m_view->zoom());
}

void CanvasController::beginPanning(const QPointF& viewPos)
{
    if (!m_view)
        return;

    m_panning = true;
    m_lastViewPos = viewPos;
    m_panStart = m_view->pan();
    m_modeBeforePan = m_mode;
    setMode(Mode::Panning);

    clearTransientDragState();
}

void CanvasController::updatePanning(const QPointF& viewPos)
{
    if (!m_view || !m_panning)
        return;

    const QPointF delta = viewPos - m_lastViewPos;
    const double zoom = m_view->zoom();
    if (zoom <= 0.0)
        return;
    const QPointF deltaScene(delta.x() / zoom, delta.y() / zoom);
    m_view->setPan(m_view->pan() + deltaScene);
    m_lastViewPos = viewPos;
}

void CanvasController::endPanning()
{
    if (!m_panning)
        return;

    m_panning = false;
    setMode(m_modeBeforePan);
}

void CanvasController::updateHoveredWire(const QPointF& scenePos)
{
    if (!m_view || !m_doc) {
        clearHoveredWire();
        return;
    }

    CanvasRenderContext ctx;
    if (m_view)
        ctx = Controllers::Detail::buildRenderContext(m_doc, m_view);

    CanvasItem* hit = Services::hitTestItem(*m_doc, scenePos, &ctx);
    if (auto* wire = dynamic_cast<CanvasWire*>(hit))
        m_view->setHoveredWire(wire->id());
    else
        m_view->clearHoveredWire();
}

void CanvasController::clearHoveredWire()
{
    if (m_view)
        m_view->clearHoveredWire();
}

void CanvasController::clearTransientDragState()
{
    if (m_dragController)
        m_dragController->clearTransientState();
    if (m_selectionController)
        m_selectionController->clearMarqueeSelection();
}

void CanvasController::syncBoundProducerPlacementUiState()
{
    const bool active = m_contextMenuController &&
                        m_contextMenuController->hasPendingBoundProducerPlacement();
    if (m_boundProducerPlacementActive == active)
        return;

    if (m_view) {
        if (active)
            m_view->setCursor(Qt::CrossCursor);
        else
            m_view->unsetCursor();
    }
    m_boundProducerPlacementActive = active;
    emit boundProducerPlacementChanged(active);
}

void CanvasController::onCanvasMousePressed(const QPointF& scenePos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods)
{
    if (!m_view)
        return;

    const QPointF viewPos = sceneToView(scenePos);

    if (buttons.testFlag(Qt::MiddleButton)) {
        beginPanning(viewPos);
        return;
    }
    if (m_mode == Mode::Panning && buttons.testFlag(Qt::LeftButton)) {
        beginPanning(viewPos);
        return;
    }

    if (!buttons.testFlag(Qt::LeftButton) || !m_doc)
        return;

    const bool propertiesShortcut = isPropertiesShortcut(mods);

    if (m_contextMenuController && m_contextMenuController->hasPendingBoundProducerPlacement()) {
        m_contextMenuController->tryPlacePendingBoundProducer(scenePos);
        syncBoundProducerPlacementUiState();
        return;
    }

    if (m_dragController && m_dragController->beginPendingEndpoint(scenePos, viewPos))
        return;

    if (m_mode == Mode::Normal) {
        const double radiusScene = Canvas::Constants::kPortHitRadiusPx / std::max(m_view->zoom(), 0.25);
        if (auto hitPort = m_doc->hitTestPort(scenePos, radiusScene)) {
            if (m_selectionController)
                m_selectionController->selectPort(*hitPort);
            return;
        }
    }

    if (m_linkingController) {
        const auto result = m_linkingController->handleLinkingPress(scenePos, m_mode);
        if (result == Controllers::CanvasLinkingController::LinkingPressResult::RequestLinkingModeReset) {
            setLinkingMode(LinkingMode::Normal);
            return;
        }
        if (result == Controllers::CanvasLinkingController::LinkingPressResult::Handled)
            return;
    }

    CanvasRenderContext ctx;
    const CanvasRenderContext* ctxPtr = nullptr;
    if (m_view) {
        ctx = Controllers::Detail::buildRenderContext(m_doc, m_view);
        ctxPtr = &ctx;
    }
    CanvasItem* hit = Services::hitTestItem(*m_doc, scenePos, ctxPtr);

    if (m_mode == Mode::Normal && !hit) {
        if (propertiesShortcut)
            return;
        if (m_selectionController)
            m_selectionController->beginMarqueeSelection(scenePos, mods);
        return;
    }

    if (hit) {
        if (m_selectionController) {
            m_selectionController->clearSelectedPort();
            if (propertiesShortcut)
                m_selectionController->selectItem(hit->id());
            else if (mods.testFlag(Qt::ControlModifier))
                m_selectionController->toggleSelection(hit->id());
            else if (mods.testFlag(Qt::ShiftModifier))
                m_selectionController->addToSelection(hit->id());
            else
                m_selectionController->selectItem(hit->id());
        }
    } else {
        if (!mods.testFlag(Qt::ControlModifier) && !mods.testFlag(Qt::ShiftModifier)) {
            if (m_selectionController)
                m_selectionController->clearSelection();
        }
    }

    if (auto* wire = dynamic_cast<CanvasWire*>(hit)) {
        if (!mods.testFlag(Qt::ControlModifier) && !mods.testFlag(Qt::ShiftModifier)) {
            if (m_dragController)
                m_dragController->beginWireSegmentDrag(wire, scenePos);
            if (m_dragController && m_dragController->isWireSegmentDragActive())
                return;
        }
    }

    auto* blk = dynamic_cast<CanvasBlock*>(hit);
    if (blk && blk->isMovable() && !mods.testFlag(Qt::ControlModifier) && !mods.testFlag(Qt::ShiftModifier))
        if (m_dragController)
            m_dragController->beginBlockDrag(blk, scenePos);
}

void CanvasController::onCanvasMouseMoved(const QPointF& scenePos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods)
{
    Q_UNUSED(mods);

    if (!m_view)
        return;

    syncBoundProducerPlacementUiState();

    if (m_dragController && m_dragController->updatePendingEndpoint(scenePos, buttons)) {
        clearHoveredWire();
        return;
    }

    if (m_selectionController && m_selectionController->isMarqueeActive() && buttons.testFlag(Qt::LeftButton)) {
        clearHoveredWire();
        m_selectionController->updateMarqueeSelection(scenePos);
        return;
    }

    if (m_linkingController) {
        m_linkingController->updateLinkingHoverAndPreview(scenePos,
                                                          m_mode,
                                                          m_panning,
                                                          m_dragController && m_dragController->isEndpointDragActive());
    }

    if (m_panning) {
        clearHoveredWire();
        const bool allowPan = buttons.testFlag(Qt::MiddleButton) ||
                              (m_mode == Mode::Panning && buttons.testFlag(Qt::LeftButton));
        if (allowPan)
            updatePanning(sceneToView(scenePos));
        else
            endPanning();
        return;
    }

    if (m_dragController && m_dragController->isWireSegmentDragActive()) {
        clearHoveredWire();
        m_dragController->updateWireSegmentDrag(scenePos);
        return;
    }

    if (m_dragController && m_dragController->isEndpointDragActive()) {
        clearHoveredWire();
        m_dragController->updateEndpointDrag(scenePos);
        return;
    }

    if (m_mode == Mode::Linking) {
        clearHoveredWire();
        return;
    }

    if (m_dragController && m_dragController->isBlockDragActive() && buttons.testFlag(Qt::LeftButton)) {
        clearHoveredWire();
        m_dragController->updateBlockDrag(scenePos);
        return;
    }

    if (!buttons.testFlag(Qt::LeftButton))
        updateHoveredWire(scenePos);
    else
        clearHoveredWire();
}

void CanvasController::onCanvasMouseReleased(const QPointF& scenePos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods)
{
    Q_UNUSED(mods);

    if (!m_view)
        return;

    if (m_panning) {
        const bool allowPan = buttons.testFlag(Qt::MiddleButton) ||
                              (m_mode == Mode::Panning && buttons.testFlag(Qt::LeftButton));
        if (!allowPan)
            endPanning();
    }

    if (!m_doc) {
        clearTransientDragState();
        if (m_linkingController)
            m_linkingController->resetLinkingSession();
        return;
    }

    if (m_dragController && m_dragController->hasPendingEndpoint()) {
        if (m_mode == Mode::Normal) {
            if (m_dragController->pendingEndpointPort().has_value()) {
                if (m_selectionController)
                    m_selectionController->selectPort(*m_dragController->pendingEndpointPort());
            } else if (m_selectionController) {
                m_selectionController->clearSelectedPort();
            }
        } else if (m_mode == Mode::Linking && m_linkingController) {
            const auto result = m_linkingController->handleLinkingPress(scenePos, m_mode);
            if (result == Controllers::CanvasLinkingController::LinkingPressResult::RequestLinkingModeReset)
                setLinkingMode(LinkingMode::Normal);
        }
        m_dragController->clearPendingEndpoint();
        return;
    }

    if (m_selectionController && m_selectionController->isMarqueeActive()) {
        m_selectionController->endMarqueeSelection(scenePos);
        return;
    }

    if (m_dragController && m_dragController->isWireSegmentDragActive()) {
        m_dragController->endWireSegmentDrag();
        return;
    }

    if (m_dragController && m_dragController->isEndpointDragActive()) {
        m_dragController->endEndpointDrag(scenePos);
        return;
    }

	    if (m_dragController && m_dragController->isBlockDragActive())
	        m_dragController->endBlockDrag();
}

void CanvasController::onCanvasContextMenuRequested(const QPointF& scenePos,
                                                    const QPoint& globalPos,
                                                    Qt::KeyboardModifiers mods)
{
    if (m_contextMenuController) {
        m_contextMenuController->showContextMenu(scenePos, globalPos, mods);
        syncBoundProducerPlacementUiState();
    }
}


void CanvasController::onCanvasWheel(const QPointF& scenePos, const QPoint& angleDelta, const QPoint& pixelDelta, Qt::KeyboardModifiers mods)
{
	if (!m_view)
		return;

	if (mods.testFlag(Qt::ControlModifier)) {
		int dy = angleDelta.y();
		if (dy == 0)
			dy = pixelDelta.y();
		if (dy == 0)
			return;

		const double factor = dy > 0 ? Constants::kZoomStep : (1.0 / Constants::kZoomStep);

		const double oldZoom = m_view->zoom();
		const QPointF oldPan = m_view->pan();

		const double newZoom = Tools::clampZoom(oldZoom * factor);
		m_view->setZoom(newZoom);

		const QPointF panNew = ((scenePos + oldPan) * oldZoom / newZoom) - scenePos;
		m_view->setPan(panNew);
		return;
	}

	const QPointF deltaView = wheelPanDeltaView(angleDelta, pixelDelta, mods);
	if (deltaView.isNull())
		return;

	const double zoom = m_view->zoom();
	if (zoom <= 0.0)
		return;

	m_view->setPan(m_view->pan() + QPointF(deltaView.x() / zoom, deltaView.y() / zoom));
}

void CanvasController::onCanvasKeyPressed(int key, Qt::KeyboardModifiers mods)
{
    if (!m_doc)
        return;

    if (key == Qt::Key_Escape) {
        if (m_contextMenuController &&
            m_contextMenuController->hasPendingBoundProducerPlacement()) {
            m_contextMenuController->clearPendingBoundProducerPlacement();
            syncBoundProducerPlacementUiState();
            return;
        }
		if (m_panning) {
			m_modeBeforePan = Mode::Normal;
			return;
		}
		if (m_mode == Mode::Linking && linkingMode() != LinkingMode::Normal) {
			setLinkingMode(LinkingMode::Normal);
			return;
		}
		setMode(Mode::Normal);
		return;
	}

	if (mods.testFlag(Qt::ControlModifier) && mods.testFlag(Qt::ShiftModifier) && key == Qt::Key_L) {
		if (m_panning) {
			m_modeBeforePan = Mode::Linking;
			return;
		}
		setMode(Mode::Linking);
		return;
	}

    if (mods.testFlag(Qt::ControlModifier)) {
		if (m_mode == Mode::Linking) {
			if (key == Qt::Key_S) {
				setLinkingMode(LinkingMode::Split);
				return;
			}
			if (key == Qt::Key_J) {
				setLinkingMode(LinkingMode::Join);
				return;
			}
			if (key == Qt::Key_B) {
				setLinkingMode(LinkingMode::Broadcast);
				return;
			}
			if (key == Qt::Key_F) {
				if (mods.testFlag(Qt::ShiftModifier))
					setLinkingMode(LinkingMode::ForwardFifo);
				else
					setLinkingMode(LinkingMode::Fifo);
				return;
			}
		}
        if (key == Qt::Key_Z) {
            if (mods.testFlag(Qt::ShiftModifier))
                m_doc->commands().redo();
            else
                m_doc->commands().undo();
            return;
        }
        if (key == Qt::Key_Y) {
            m_doc->commands().redo();
            return;
        }
    }

    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        if (m_selectionController && m_selectionController->hasSelectedPort()) {
            if (m_doc->commands().execute(std::make_unique<DeletePortCommand>(m_selectionController->selectedPortItem(),
                                                                             m_selectionController->selectedPortId()))) {
                m_selectionController->clearSelectedPort();
            }
            return;
        }
        if (selectedItems().isEmpty())
            return;

        QSet<ObjectId> deletion = selectedItems();
        for (const auto& id : selectedItems()) {
            auto* item = m_doc->findItem(id);
            if (!item) {
                deletion.remove(id);
                continue;
            }
            if (auto* block = dynamic_cast<CanvasBlock*>(item)) {
                if (!block->isDeletable()) {
                    deletion.remove(id);
                    continue;
                }
                if (block->isLinkHub()) {
                    for (const auto& it : m_doc->items()) {
                        auto* wire = dynamic_cast<CanvasWire*>(it.get());
                        if (wire && wire->attachesTo(id))
                            deletion.remove(wire->id());
                    }
                }
            }
        }

        if (deletion.isEmpty())
            return;

        std::vector<ObjectId> ordered;
        ordered.reserve(deletion.size());
        for (const auto& id : deletion)
            ordered.push_back(id);
        std::ranges::sort(ordered);

        auto batch = std::make_unique<CompositeCommand>(QStringLiteral("Delete Items"));
        for (const auto& id : ordered)
            batch->add(std::make_unique<DeleteItemCommand>(id));

        if (!batch->empty() && m_doc->commands().execute(std::move(batch))) {
            if (m_selectionController)
                m_selectionController->clearSelection();
        }
    }
}

} // namespace Canvas
