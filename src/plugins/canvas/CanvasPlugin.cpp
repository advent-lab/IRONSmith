// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/CanvasGlobal.hpp"

#include <extensionsystem/IPlugin.hpp>
#include <utils/Result.hpp>

#include <QStringList>
#include <QLoggingCategory>
#include <array>
#include <QtCore/QSignalBlocker>
#include <QtCore/QtGlobal>
#include <QtGui/QActionGroup>

#include "canvas/internal/CanvasHostImpl.hpp"
#include "canvas/document/CanvasDocumentServiceImpl.hpp"
#include "extensionsystem/PluginManager.hpp"
#include "canvas/internal/CanvasGridHostImpl.hpp"
#include "canvas/internal/CanvasStyleHostImpl.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasTypes.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasController.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/Tools.hpp"

#include "core/CoreConstants.hpp"
#include "core/ui/IUiHost.hpp"

Q_LOGGING_CATEGORY(canvaslog, "ironsmith.canvas")

namespace Canvas::Internal {

class CanvasPlugin final : public ExtensionSystem::IPlugin {
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "org.ironsmith.plugin" FILE "Canvas.json")

public:
	CanvasPlugin() = default;

	Utils::Result initialize(const QStringList &arguments, ExtensionSystem::PluginManager &manager) override;

	void extensionsInitialized(ExtensionSystem::PluginManager& manager) override;
	ShutdownFlag aboutToShutdown() override;

private:
    void connectRibbonActions(Core::IUiHost* uiHost);
    void syncRibbonState(CanvasController* controller);
    void setCanvasActionsEnabled(bool enabled);
    void applyZoom(CanvasView* view, double factor);
    void zoomToFit(CanvasView* view, CanvasDocument* doc);
    void clearWireOverrides(CanvasDocument* doc);
    void setWireArrows(CanvasDocument* doc, bool enabled);

	    QPointer<CanvasHostImpl> m_host;
	    QPointer<CanvasGridHostImpl> m_gridHost;
	    QPointer<CanvasStyleHostImpl> m_styleHost;
        QPointer<CanvasDocumentServiceImpl> m_documentService;

    struct RibbonActions final {
        QPointer<QAction> select;
        QPointer<QAction> pan;
        QPointer<QAction> link;
        QPointer<QAction> linkSplit;
        QPointer<QAction> linkJoin;
        QPointer<QAction> linkBroadcast;
        QPointer<QAction> linkFifo;
        QPointer<QAction> linkForwardFifo;

        QPointer<QAction> autoRoute;
        QPointer<QAction> clearOverrides;
        QPointer<QAction> wireArrows;

        QPointer<QAction> zoomIn;
        QPointer<QAction> zoomOut;
        QPointer<QAction> zoomFit;
        QPointer<QAction> resetView;
    } m_actions;
};


Utils::Result CanvasPlugin::initialize(const QStringList &arguments, ExtensionSystem::PluginManager &manager) {
	Q_UNUSED(arguments);
	Q_UNUSED(manager);

	qCInfo(canvaslog) << "CanvasPlugin: initialize...";
	qRegisterMetaType<Canvas::ObjectId>("Canvas::ObjectId");
	qRegisterMetaType<Canvas::PortId>("Canvas::PortId");
    qRegisterMetaType<Canvas::Api::CanvasDocumentHandle>("Canvas::Api::CanvasDocumentHandle");
    qRegisterMetaType<Canvas::Api::CanvasDocumentCloseReason>("Canvas::Api::CanvasDocumentCloseReason");
	m_host = new CanvasHostImpl();
	if (!m_host) {
		qCInfo(canvaslog) << "Failed to create CanvasHostImpl";
		return Utils::Result::failure("Failed to create CanvasHostImpl");
	}

	ExtensionSystem::PluginManager::addObject(m_host);

	m_styleHost = new CanvasStyleHostImpl();
	ExtensionSystem::PluginManager::addObject(m_styleHost);

    m_documentService = new CanvasDocumentServiceImpl(this);
    ExtensionSystem::PluginManager::addObject(m_documentService);
	return Utils::Result::success();
}

void CanvasPlugin::extensionsInitialized(ExtensionSystem::PluginManager &manager) {
	qCInfo(canvaslog) << "CanvasPlugin: extensionsInitialized";
	if (!m_host) {
		qCWarning(canvaslog) << "CanvasPlugin: CanvasHost is null";
		return;
	}

	m_host->wireIntoApplication(manager);
    if (m_documentService)
        m_documentService->setCanvasHost(m_host);

	if (!m_gridHost) {
		auto* view = qobject_cast<CanvasView*>(m_host->viewWidget());
		m_gridHost = new CanvasGridHostImpl(m_host->document(), view, m_styleHost, this);
		ExtensionSystem::PluginManager::addObject(m_gridHost);
	}

    if (auto* uiHost = manager.getObject<Core::IUiHost>()) {
        connectRibbonActions(uiHost);
    } else {
        qCWarning(canvaslog) << "CanvasPlugin: IUiHost not available for ribbon actions";
    }
}

ExtensionSystem::IPlugin::ShutdownFlag CanvasPlugin::aboutToShutdown() {
	if (m_host) {
		ExtensionSystem::PluginManager::removeObject(m_host);
		m_host = nullptr;
	}
	if (m_gridHost) {
		ExtensionSystem::PluginManager::removeObject(m_gridHost);
		m_gridHost = nullptr;
	}
	if (m_styleHost) {
		ExtensionSystem::PluginManager::removeObject(m_styleHost);
		m_styleHost = nullptr;
	}
    if (m_documentService) {
        ExtensionSystem::PluginManager::removeObject(m_documentService);
        m_documentService = nullptr;
    }
	return ShutdownFlag::SynchronousShutdown;
}

void CanvasPlugin::connectRibbonActions(Core::IUiHost* uiHost)
{
    if (!uiHost || !m_host)
        return;

    auto* controller = m_host->controller();
    auto* view = qobject_cast<CanvasView*>(m_host->viewWidget());
    auto* doc = m_host->document();
    if (!controller || !view || !doc)
        return;

    auto fetch = [uiHost](const char* group, const char* item) -> QAction* {
        return uiHost->ribbonCommand(Core::Constants::RIBBON_TAB_HOME, group, item);
    };

    m_actions.select = fetch(Core::Constants::RIBBON_TAB_HOME_CANVAS_GROUP, Core::Constants::CANVAS_SELECT_ITEMID);
    m_actions.pan = fetch(Core::Constants::RIBBON_TAB_HOME_CANVAS_GROUP, Core::Constants::CANVAS_PAN_ITEMID);
    m_actions.link = fetch(Core::Constants::RIBBON_TAB_HOME_CANVAS_GROUP, Core::Constants::CANVAS_LINK_ITEMID);
    m_actions.linkSplit = fetch(Core::Constants::RIBBON_TAB_HOME_CANVAS_GROUP, Core::Constants::CANVAS_LINK_SPLIT_ITEMID);
    m_actions.linkJoin = fetch(Core::Constants::RIBBON_TAB_HOME_CANVAS_GROUP, Core::Constants::CANVAS_LINK_JOIN_ITEMID);
    m_actions.linkBroadcast = fetch(Core::Constants::RIBBON_TAB_HOME_CANVAS_GROUP, Core::Constants::CANVAS_LINK_BROADCAST_ITEMID);
    m_actions.linkFifo = fetch(Core::Constants::RIBBON_TAB_HOME_CANVAS_GROUP, Core::Constants::CANVAS_LINK_FIFO_ITEMID);
    m_actions.linkForwardFifo = fetch(Core::Constants::RIBBON_TAB_HOME_CANVAS_GROUP,
                                      Core::Constants::CANVAS_LINK_FORWARD_FIFO_ITEMID);

    m_actions.autoRoute = fetch(Core::Constants::RIBBON_TAB_HOME_WIRES_GROUP, Core::Constants::CANVAS_WIRE_AUTO_ROUTE_ITEMID);
    m_actions.clearOverrides = fetch(Core::Constants::RIBBON_TAB_HOME_WIRES_GROUP, Core::Constants::CANVAS_WIRE_CLEAR_OVERRIDES_ITEMID);
    m_actions.wireArrows = fetch(Core::Constants::RIBBON_TAB_HOME_WIRES_GROUP, Core::Constants::CANVAS_WIRE_TOGGLE_ARROWS_ITEMID);

    m_actions.zoomIn = fetch(Core::Constants::RIBBON_TAB_HOME_VIEW_GROUP, Core::Constants::CANVAS_VIEW_ZOOM_IN_ITEMID);
    m_actions.zoomOut = fetch(Core::Constants::RIBBON_TAB_HOME_VIEW_GROUP, Core::Constants::CANVAS_VIEW_ZOOM_OUT_ITEMID);
    m_actions.zoomFit = fetch(Core::Constants::RIBBON_TAB_HOME_VIEW_GROUP, Core::Constants::CANVAS_VIEW_ZOOM_FIT_ITEMID);
    m_actions.resetView = fetch(Core::Constants::RIBBON_TAB_HOME_VIEW_GROUP, Core::Constants::CANVAS_VIEW_RESET_ITEMID);

    auto* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    for (auto* act : {m_actions.select.data(),
                      m_actions.pan.data(),
                      m_actions.link.data(),
                      m_actions.linkSplit.data(),
                      m_actions.linkJoin.data(),
                      m_actions.linkBroadcast.data(),
                      m_actions.linkFifo.data(),
                      m_actions.linkForwardFifo.data()}) {
        if (act) {
            act->setCheckable(true);
            modeGroup->addAction(act);
        }
    }

    if (m_actions.select) {
        connect(m_actions.select, &QAction::triggered, this, [controller]() {
            controller->setMode(CanvasController::Mode::Normal);
        });
    }
    if (m_actions.pan) {
        connect(m_actions.pan, &QAction::triggered, this, [controller]() {
            controller->setMode(CanvasController::Mode::Panning);
        });
    }
    const std::array<std::pair<QAction*, CanvasController::LinkingMode>, 6> linkActions{{
        {m_actions.link.data(), CanvasController::LinkingMode::Normal},
        {m_actions.linkSplit.data(), CanvasController::LinkingMode::Split},
        {m_actions.linkJoin.data(), CanvasController::LinkingMode::Join},
        {m_actions.linkBroadcast.data(), CanvasController::LinkingMode::Broadcast},
        {m_actions.linkFifo.data(), CanvasController::LinkingMode::Fifo},
        {m_actions.linkForwardFifo.data(), CanvasController::LinkingMode::ForwardFifo}
    }};
    for (const auto& [action, linkMode] : linkActions) {
        if (!action)
            continue;
        connect(action, &QAction::triggered, this, [controller, linkMode]() {
            controller->setLinkingMode(linkMode);
            controller->setMode(CanvasController::Mode::Linking);
        });
    }

    if (m_actions.autoRoute) {
        connect(m_actions.autoRoute, &QAction::triggered, this, [this, doc]() {
            clearWireOverrides(doc);
        });
    }
    if (m_actions.clearOverrides) {
        connect(m_actions.clearOverrides, &QAction::triggered, this, [this, doc]() {
            clearWireOverrides(doc);
        });
    }
    if (m_actions.wireArrows) {
        connect(m_actions.wireArrows, &QAction::toggled, this, [this, doc](bool enabled) {
            setWireArrows(doc, enabled);
        });
    }

    if (m_actions.zoomIn) {
        connect(m_actions.zoomIn, &QAction::triggered, this, [this, view]() {
            applyZoom(view, Canvas::Constants::kZoomStep);
        });
    }
    if (m_actions.zoomOut) {
        connect(m_actions.zoomOut, &QAction::triggered, this, [this, view]() {
            applyZoom(view, 1.0 / Canvas::Constants::kZoomStep);
        });
    }
    if (m_actions.zoomFit) {
        connect(m_actions.zoomFit, &QAction::triggered, this, [this, view, doc]() {
            zoomToFit(view, doc);
        });
    }
    if (m_actions.resetView) {
        connect(m_actions.resetView, &QAction::triggered, this, [view]() {
            view->setZoom(1.0);
            view->setPan(QPointF());
        });
    }

    auto updateState = [this, controller]() { syncRibbonState(controller); };
    connect(controller, &CanvasController::modeChanged, this, updateState);
    connect(controller, &CanvasController::linkingModeChanged, this, updateState);
    syncRibbonState(controller);

    setCanvasActionsEnabled(m_host->canvasActive());
    connect(m_host, &Canvas::Api::ICanvasHost::canvasActiveChanged, this,
            [this](bool active) { setCanvasActionsEnabled(active); });
}

void CanvasPlugin::syncRibbonState(CanvasController* controller)
{
    if (!controller)
        return;

    const auto mode = controller->mode();
    const auto linkMode = controller->linkingMode();

    if (m_actions.select) {
        QSignalBlocker block(m_actions.select);
        m_actions.select->setChecked(mode == CanvasController::Mode::Normal);
    }
    if (m_actions.pan) {
        QSignalBlocker block(m_actions.pan);
        m_actions.pan->setChecked(mode == CanvasController::Mode::Panning);
    }

    const bool linking = (mode == CanvasController::Mode::Linking);
    const std::array<std::pair<QAction*, CanvasController::LinkingMode>, 6> linkActions{{
        {m_actions.link.data(), CanvasController::LinkingMode::Normal},
        {m_actions.linkSplit.data(), CanvasController::LinkingMode::Split},
        {m_actions.linkJoin.data(), CanvasController::LinkingMode::Join},
        {m_actions.linkBroadcast.data(), CanvasController::LinkingMode::Broadcast},
        {m_actions.linkFifo.data(), CanvasController::LinkingMode::Fifo},
        {m_actions.linkForwardFifo.data(), CanvasController::LinkingMode::ForwardFifo}
    }};
    for (const auto& [action, candidateMode] : linkActions) {
        if (!action)
            continue;
        QSignalBlocker block(action);
        action->setChecked(linking && linkMode == candidateMode);
    }
}

void CanvasPlugin::setCanvasActionsEnabled(bool enabled)
{
    const auto setEnabled = [enabled](QPointer<QAction>& action) {
        if (action)
            action->setEnabled(enabled);
    };

    setEnabled(m_actions.select);
    setEnabled(m_actions.pan);
    setEnabled(m_actions.link);
    setEnabled(m_actions.linkSplit);
    setEnabled(m_actions.linkJoin);
    setEnabled(m_actions.linkBroadcast);
    setEnabled(m_actions.linkFifo);
    setEnabled(m_actions.linkForwardFifo);
    setEnabled(m_actions.autoRoute);
    setEnabled(m_actions.clearOverrides);
    setEnabled(m_actions.wireArrows);
    setEnabled(m_actions.zoomIn);
    setEnabled(m_actions.zoomOut);
    setEnabled(m_actions.zoomFit);
    setEnabled(m_actions.resetView);
}

void CanvasPlugin::applyZoom(CanvasView* view, double factor)
{
    if (!view)
        return;

    const double oldZoom = view->zoom();
    const double newZoom = Tools::clampZoom(oldZoom * factor);
    if (qFuzzyCompare(oldZoom, newZoom))
        return;

    const QPointF viewCenter(view->width() * 0.5, view->height() * 0.5);
    const QPointF sceneCenter = view->viewToScene(viewCenter);
    const QPointF oldPan = view->pan();

    const QPointF panNew = ((sceneCenter + oldPan) * oldZoom / newZoom) - sceneCenter;
    view->setZoom(newZoom);
    view->setPan(panNew);
}

void CanvasPlugin::zoomToFit(CanvasView* view, CanvasDocument* doc)
{
    if (!view || !doc)
        return;

    QRectF bounds;
    bool hasBounds = false;
    for (const auto& it : doc->items()) {
        if (!it)
            continue;
        const QRectF r = it->boundsScene();
        if (!hasBounds) {
            bounds = r;
            hasBounds = true;
        } else {
            bounds = bounds.united(r);
        }
    }

    if (!hasBounds || bounds.isEmpty())
        return;

    const QSizeF viewSize(view->width(), view->height());
    if (viewSize.width() <= 1.0 || viewSize.height() <= 1.0)
        return;

    const double pad = 32.0;
    bounds = bounds.adjusted(-pad, -pad, pad, pad);

    const double zx = viewSize.width() / std::max(1.0, bounds.width());
    const double zy = viewSize.height() / std::max(1.0, bounds.height());
    const double targetZoom = Tools::clampZoom(std::min(zx, zy));

    const QPointF center = bounds.center();
    const QPointF viewCenter(view->width() * 0.5, view->height() * 0.5);
    const QPointF pan = (viewCenter / targetZoom) - center;
    view->setZoom(targetZoom);
    view->setPan(pan);
    view->setDisplayZoomBaseline(targetZoom);
}

void CanvasPlugin::clearWireOverrides(CanvasDocument* doc)
{
    if (!doc)
        return;

    bool changed = false;
    for (const auto& it : doc->items()) {
        auto* wire = dynamic_cast<CanvasWire*>(it.get());
        if (!wire || !wire->hasRouteOverride())
            continue;
        wire->clearRouteOverride();
        changed = true;
    }

    if (changed)
        doc->notifyChanged();
}

void CanvasPlugin::setWireArrows(CanvasDocument* doc, bool enabled)
{
    if (!doc)
        return;

    bool changed = false;
    for (const auto& it : doc->items()) {
        auto* wire = dynamic_cast<CanvasWire*>(it.get());
        if (!wire)
            continue;

        WireArrowPolicy next = WireArrowPolicy::None;
        if (enabled) {
            CanvasPort aMeta;
            CanvasPort bMeta;
            const auto& a = wire->a();
            const auto& b = wire->b();
            if (a.attached.has_value() && b.attached.has_value() &&
                doc->getPort(a.attached->itemId, a.attached->portId, aMeta) &&
                doc->getPort(b.attached->itemId, b.attached->portId, bMeta)) {
                const bool aConsumer = aMeta.role == PortRole::Consumer;
                const bool bConsumer = bMeta.role == PortRole::Consumer;
                if (aConsumer && !bConsumer)
                    next = WireArrowPolicy::Start;
                else if (bConsumer && !aConsumer)
                    next = WireArrowPolicy::End;
                else
                    next = wire->arrowPolicy();
            } else {
                next = wire->arrowPolicy();
            }
        }

        if (wire->arrowPolicy() != next) {
            wire->setArrowPolicy(next);
            changed = true;
        }
    }

    if (changed)
        doc->notifyChanged();
}

} // Canvas::Internal

#include "CanvasPlugin.moc"
