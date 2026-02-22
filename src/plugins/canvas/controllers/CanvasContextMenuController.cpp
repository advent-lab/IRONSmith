// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/controllers/CanvasContextMenuController.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasCommands.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasItem.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/Tools.hpp"
#include "canvas/controllers/CanvasInteractionHelpers.hpp"
#include "canvas/controllers/CanvasSelectionController.hpp"
#include "canvas/services/CanvasHitTestService.hpp"
#include "canvas/utils/CanvasPortBindings.hpp"
#include "canvas/utils/CanvasGeometry.hpp"
#include "canvas/utils/CanvasPortUsage.hpp"

#include <QtCore/QtGlobal>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Canvas::Controllers {

namespace {

using Utils::ContextMenuAction;

const QString kActionUndo = QStringLiteral("canvas.context.undo");
const QString kActionRedo = QStringLiteral("canvas.context.redo");
const QString kActionResetView = QStringLiteral("canvas.context.view.reset");
const QString kActionFrameAll = QStringLiteral("canvas.context.view.frameAll");
const QString kActionFrameSelection = QStringLiteral("canvas.context.view.frameSelection");
const QString kActionToggleWireAnnotations = QStringLiteral("canvas.context.view.toggleWireAnnotations");
const QString kActionClearSelection = QStringLiteral("canvas.context.selection.clear");
const QString kActionDeleteSelection = QStringLiteral("canvas.context.selection.delete");

const QString kActionAddBlock = QStringLiteral("canvas.context.create.block");
const QString kActionAddHubSplit = QStringLiteral("canvas.context.create.hub.split");
const QString kActionAddHubJoin = QStringLiteral("canvas.context.create.hub.join");
const QString kActionAddHubBroadcast = QStringLiteral("canvas.context.create.hub.broadcast");

const QString kActionDeleteItem = QStringLiteral("canvas.context.item.delete");
const QString kActionToggleMovable = QStringLiteral("canvas.context.block.toggleMovable");
const QString kActionToggleShowPorts = QStringLiteral("canvas.context.block.toggleShowPorts");
const QString kActionAddPort = QStringLiteral("canvas.context.block.addPort");

const QString kActionDeleteWire = QStringLiteral("canvas.context.wire.delete");
const QString kActionClearWireRoute = QStringLiteral("canvas.context.wire.clearRoute");

const QString kActionDeletePort = QStringLiteral("canvas.context.port.delete");
const QString kActionCreateBoundProducer = QStringLiteral("canvas.context.port.createBoundProducer");

const QString kActionHubKindSplit = QStringLiteral("canvas.context.hub.kind.split");
const QString kActionHubKindJoin = QStringLiteral("canvas.context.hub.kind.join");
const QString kActionHubKindBroadcast = QStringLiteral("canvas.context.hub.kind.broadcast");

ContextMenuAction actionItem(const QString& id, const QString& text, bool enabled = true)
{
    ContextMenuAction action = ContextMenuAction::item(id, text);
    action.enabled = enabled;
    return action;
}

ContextMenuAction checkItem(const QString& id, const QString& text, bool checked, bool enabled = true)
{
    ContextMenuAction action = ContextMenuAction::item(id, text);
    action.checkable = true;
    action.checked = checked;
    action.enabled = enabled;
    return action;
}

QSet<ObjectId> allItemIds(const CanvasDocument& document)
{
    QSet<ObjectId> ids;
    for (const auto& item : document.items()) {
        if (item)
            ids.insert(item->id());
    }
    return ids;
}

} // namespace

CanvasContextMenuController::CanvasContextMenuController(CanvasDocument* doc,
                                                         CanvasView* view,
                                                         CanvasSelectionController* selection,
                                                         QObject* parent)
    : QObject(parent)
    , m_doc(doc)
    , m_view(view)
    , m_selection(selection)
{
    m_menu = new Utils::ContextMenu(view ? view : qobject_cast<QWidget*>(parent));
    m_menu->setObjectName(QStringLiteral("CanvasContextMenu"));
    connect(m_menu, &Utils::ContextMenu::actionTriggered,
            this, &CanvasContextMenuController::handleMenuAction);
}

void CanvasContextMenuController::showContextMenu(const QPointF& scenePos,
                                                  const QPoint& globalPos,
                                                  Qt::KeyboardModifiers mods)
{
    if (!m_doc || !m_view || !m_menu)
        return;

    const MenuTarget target = resolveTarget(scenePos, globalPos, mods);
    populateMenu(target);
    if (m_actions.isEmpty())
        return;

    m_activeTarget = target;
    m_menu->setActions(m_actions);
    m_menu->exec(globalPos);
    m_activeTarget.reset();
}

bool CanvasContextMenuController::tryPlacePendingBoundProducer(const QPointF& scenePos)
{
    if (!m_pendingBoundProducerPlacement.has_value() || !m_doc || !m_view)
        return false;

    const auto edge = Detail::edgeCandidateAt(m_doc, m_view, scenePos);
    if (!edge.has_value())
        return false;

    Support::BoundProducerPlacementRequest request;
    request.consumerItemId = m_pendingBoundProducerPlacement->consumerItemId;
    request.consumerPortId = m_pendingBoundProducerPlacement->consumerPortId;
    request.producerEdge = *edge;

    const auto created = Support::createBoundProducerPort(*m_doc, request);
    if (!created.has_value()) {
        CanvasPort consumerMeta;
        if (!m_doc->getPort(request.consumerItemId, request.consumerPortId, consumerMeta))
            m_pendingBoundProducerPlacement.reset();
        return false;
    }

    if (m_selection)
        m_selection->selectPort(PortRef{created->producerItemId, created->producerPortId});
    return true;
}

bool CanvasContextMenuController::hasPendingBoundProducerPlacement() const noexcept
{
    return m_pendingBoundProducerPlacement.has_value();
}

void CanvasContextMenuController::clearPendingBoundProducerPlacement()
{
    m_pendingBoundProducerPlacement.reset();
}

void CanvasContextMenuController::appendSeparator()
{
    if (m_actions.isEmpty() || m_actions.back().isSeparator)
        return;
    m_actions.push_back(ContextMenuAction::separatorAction());
}

void CanvasContextMenuController::appendEditActions(bool canUndo, bool canRedo)
{
    m_actions.push_back(actionItem(kActionUndo, QStringLiteral("Undo"), canUndo));
    m_actions.push_back(actionItem(kActionRedo, QStringLiteral("Redo"), canRedo));
}

void CanvasContextMenuController::appendEmptyCanvasActions()
{
    m_actions.push_back(actionItem(kActionAddBlock, QStringLiteral("Add Block")));
    m_actions.push_back(actionItem(kActionAddHubSplit, QStringLiteral("Add Link Hub (Split)")));
    m_actions.push_back(actionItem(kActionAddHubJoin, QStringLiteral("Add Link Hub (Join)")));
    m_actions.push_back(actionItem(kActionAddHubBroadcast, QStringLiteral("Add Link Hub (Broadcast)")));
    appendSeparator();
    m_actions.push_back(actionItem(kActionFrameAll,
                                   QStringLiteral("Frame All"),
                                   !m_doc->items().empty()));
    m_actions.push_back(actionItem(kActionResetView, QStringLiteral("Reset View")));
}

void CanvasContextMenuController::appendSelectionActions()
{
    const bool hasSelection = m_selection && !m_selection->selectedItems().isEmpty();
    m_actions.push_back(actionItem(kActionFrameSelection, QStringLiteral("Frame Selection"), hasSelection));
    appendSeparator();
    m_actions.push_back(actionItem(kActionDeleteSelection, QStringLiteral("Delete Selected"), hasSelection));
    m_actions.push_back(actionItem(kActionClearSelection, QStringLiteral("Clear Selection"), hasSelection));
}

void CanvasContextMenuController::appendBlockActions(CanvasBlock* block, bool linkHub)
{
    if (!block)
        return;

    m_actions.push_back(actionItem(kActionAddPort, QStringLiteral("Add Port")));
    m_actions.push_back(checkItem(kActionToggleMovable,
                                  QStringLiteral("Lock Position"),
                                  !block->isMovable()));
    m_actions.push_back(checkItem(kActionToggleShowPorts,
                                  QStringLiteral("Show Ports"),
                                  block->showPorts()));
    m_actions.push_back(actionItem(kActionDeleteItem,
                                   linkHub ? QStringLiteral("Delete Link Hub") : QStringLiteral("Delete Block"),
                                   block->isDeletable()));

    if (linkHub) {
        appendSeparator();
        const auto kind = hubKindForBlock(*block);
        m_actions.push_back(checkItem(kActionHubKindSplit,
                                      QStringLiteral("Hub Type: Split"),
                                      kind.has_value() && *kind == Support::LinkHubKind::Split));
        m_actions.push_back(checkItem(kActionHubKindJoin,
                                      QStringLiteral("Hub Type: Join"),
                                      kind.has_value() && *kind == Support::LinkHubKind::Join));
        m_actions.push_back(checkItem(kActionHubKindBroadcast,
                                      QStringLiteral("Hub Type: Broadcast"),
                                      kind.has_value() && *kind == Support::LinkHubKind::Broadcast));
    }

    appendSeparator();
    m_actions.push_back(actionItem(kActionFrameSelection, QStringLiteral("Frame Selection")));
}

void CanvasContextMenuController::appendWireActions(ObjectId wireId)
{
    auto* wire = dynamic_cast<CanvasWire*>(m_doc->findItem(wireId));
    m_actions.push_back(actionItem(kActionDeleteWire, QStringLiteral("Delete Wire"), wire != nullptr));
    m_actions.push_back(actionItem(kActionClearWireRoute,
                                   QStringLiteral("Clear Manual Route"),
                                   wire && wire->hasRouteOverride()));
}

void CanvasContextMenuController::appendPortActions(ObjectId itemId, PortId portId)
{
    auto* block = findBlock(itemId);
    const auto port = findPort(itemId, portId);
    const bool canDelete = block && port.has_value();
    m_actions.push_back(actionItem(kActionDeletePort, QStringLiteral("Delete Port"), canDelete));

    const bool canCreateBoundProducer = block &&
                                        port.has_value() &&
                                        port->role != PortRole::Producer;
    m_actions.push_back(actionItem(kActionCreateBoundProducer,
                                   QStringLiteral("Create Bound Producer (Click Edge)"),
                                   canCreateBoundProducer));
}

void CanvasContextMenuController::populateMenu(const MenuTarget& target)
{
    m_actions.clear();
    appendEditActions(m_doc && m_doc->commands().canUndo(),
                      m_doc && m_doc->commands().canRedo());
    appendSeparator();

    switch (target.kind) {
        case TargetKind::Empty:
            appendEmptyCanvasActions();
            break;
        case TargetKind::Selection:
            appendSelectionActions();
            break;
        case TargetKind::Block:
            appendBlockActions(findBlock(target.itemId), false);
            break;
        case TargetKind::LinkHub:
            appendBlockActions(findBlock(target.itemId), true);
            break;
        case TargetKind::Wire:
            appendWireActions(target.itemId);
            break;
        case TargetKind::Port:
            appendPortActions(target.itemId, target.portId);
            break;
    }

    appendSeparator();
    m_actions.push_back(checkItem(kActionToggleWireAnnotations,
                                  QStringLiteral("Show All Wire Annotations"),
                                  m_view && m_view->showAllWireAnnotations(),
                                  m_view != nullptr));
}

CanvasContextMenuController::MenuTarget CanvasContextMenuController::resolveTarget(const QPointF& scenePos,
                                                                                    const QPoint& globalPos,
                                                                                    Qt::KeyboardModifiers mods)
{
    MenuTarget out;
    out.scenePos = scenePos;
    out.globalPos = globalPos;

    if (!m_doc || !m_view)
        return out;

    const double radiusScene = Constants::kPortHitRadiusPx / std::max(m_view->zoom(), 0.25);
    if (const auto hitPort = m_doc->hitTestPort(scenePos, radiusScene); hitPort.has_value()) {
        out.kind = TargetKind::Port;
        out.itemId = hitPort->itemId;
        out.portId = hitPort->portId;
        if (m_selection)
            m_selection->selectPort(*hitPort);
        return out;
    }

    CanvasRenderContext ctx = Detail::buildRenderContext(m_doc, m_view);
    CanvasItem* hitItem = Services::hitTestItem(*m_doc, scenePos, &ctx);

    const bool additiveModifier = mods.testFlag(Qt::ControlModifier) || mods.testFlag(Qt::ShiftModifier);
    if (hitItem) {
        const bool selectedHit = m_selection && m_selection->isSelected(hitItem->id());
        if (m_selection && !selectedHit && !additiveModifier)
            m_selection->selectItem(hitItem->id());

        if (m_selection && m_selection->selectedItems().size() > 1 && selectedHit) {
            out.kind = TargetKind::Selection;
            return out;
        }

        out.itemId = hitItem->id();
        if (dynamic_cast<CanvasWire*>(hitItem)) {
            out.kind = TargetKind::Wire;
            return out;
        }

        if (auto* block = dynamic_cast<CanvasBlock*>(hitItem)) {
            out.kind = block->isLinkHub() ? TargetKind::LinkHub : TargetKind::Block;
            return out;
        }
    }

    if (m_selection && m_selection->selectedItems().size() > 1) {
        out.kind = TargetKind::Selection;
        return out;
    }

    out.kind = TargetKind::Empty;
    return out;
}

CanvasBlock* CanvasContextMenuController::findBlock(ObjectId itemId) const
{
    if (!m_doc || itemId.isNull())
        return nullptr;
    return dynamic_cast<CanvasBlock*>(m_doc->findItem(itemId));
}

std::optional<CanvasPort> CanvasContextMenuController::findPort(ObjectId itemId, PortId portId) const
{
    if (!m_doc || itemId.isNull() || portId.isNull())
        return std::nullopt;
    CanvasPort port;
    if (!m_doc->getPort(itemId, portId, port))
        return std::nullopt;
    return port;
}

std::optional<Support::LinkHubKind> CanvasContextMenuController::hubKindForBlock(const CanvasBlock& block) const
{
    if (!block.isLinkHub())
        return std::nullopt;

    const auto* symbol = dynamic_cast<const BlockContentSymbol*>(block.content());
    if (!symbol)
        return std::nullopt;

    const QString value = symbol->symbol().trimmed();
    if (value == Support::linkHubStyle(Support::LinkHubKind::Split).symbol)
        return Support::LinkHubKind::Split;
    if (value == Support::linkHubStyle(Support::LinkHubKind::Join).symbol)
        return Support::LinkHubKind::Join;
    if (value == Support::linkHubStyle(Support::LinkHubKind::Broadcast).symbol)
        return Support::LinkHubKind::Broadcast;
    return std::nullopt;
}

bool CanvasContextMenuController::executeDeletePort(ObjectId itemId, PortId portId)
{
    if (!m_doc || itemId.isNull() || portId.isNull())
        return false;

    const bool executed = m_doc->commands().execute(std::make_unique<DeletePortCommand>(itemId, portId));
    if (executed && m_selection)
        m_selection->clearSelectedPort();
    return executed;
}

bool CanvasContextMenuController::executeDeleteItems(const QSet<ObjectId>& ids)
{
    if (!m_doc || ids.isEmpty())
        return false;

    QSet<ObjectId> deletion = ids;
    for (const auto& id : ids) {
        auto* item = m_doc->findItem(id);
        if (!item) {
            deletion.remove(id);
            continue;
        }

        auto* block = dynamic_cast<CanvasBlock*>(item);
        if (!block)
            continue;

        if (!block->isDeletable()) {
            deletion.remove(id);
            continue;
        }

        if (!block->isLinkHub())
            continue;

        for (const auto& it : m_doc->items()) {
            auto* wire = dynamic_cast<CanvasWire*>(it.get());
            if (wire && wire->attachesTo(id))
                deletion.remove(wire->id());
        }
    }

    if (deletion.isEmpty())
        return false;

    std::vector<ObjectId> ordered;
    ordered.reserve(static_cast<size_t>(deletion.size()));
    for (const auto& id : deletion)
        ordered.push_back(id);
    std::ranges::sort(ordered);

    auto batch = std::make_unique<CompositeCommand>(QStringLiteral("Delete Items"));
    for (const auto& id : ordered)
        batch->add(std::make_unique<DeleteItemCommand>(id));

    if (batch->empty())
        return false;
    const bool executed = m_doc->commands().execute(std::move(batch));
    if (executed && m_selection) {
        m_selection->clearSelection();
        m_selection->clearSelectedPort();
    }
    return executed;
}

bool CanvasContextMenuController::executeDeleteSingleItem(ObjectId itemId)
{
    QSet<ObjectId> ids;
    ids.insert(itemId);
    return executeDeleteItems(ids);
}

bool CanvasContextMenuController::executeAddBlockAt(const QPointF& scenePos)
{
    if (!m_doc)
        return false;

    const double size = Constants::kGridStep * 6.0;
    QRectF bounds(scenePos.x() - size * 0.5, scenePos.y() - size * 0.5, size, size);
    const double step = m_doc->fabric().config().step;
    bounds = Support::snapBoundsToGrid(bounds, step > 0.0 ? step : Constants::kGridStep);

    auto block = std::make_unique<CanvasBlock>(bounds, true, QStringLiteral("BLOCK"));
    block->setId(m_doc->allocateId());
    const ObjectId id = block->id();

    const bool executed = m_doc->commands().execute(std::make_unique<CreateItemCommand>(std::move(block)));
    if (executed && m_selection)
        m_selection->selectItem(id);
    return executed;
}

bool CanvasContextMenuController::executeAddHubAt(const QPointF& scenePos, Support::LinkHubKind kind)
{
    if (!m_doc)
        return false;

    const double size = Constants::kLinkHubSize;
    QRectF bounds(scenePos.x() - size * 0.5, scenePos.y() - size * 0.5, size, size);
    const double step = m_doc->fabric().config().step;
    bounds = Support::snapBoundsToGrid(bounds, step > 0.0 ? step : Constants::kGridStep);

    auto hub = std::make_unique<CanvasBlock>(bounds, true, QString());
    hub->setShowPorts(false);
    hub->setAutoPortLayout(false);
    hub->setPortSnapStep(Constants::kGridStep);
    hub->setLinkHub(true);
    hub->setKeepoutMargin(0.0);
    hub->setContentPadding(QMarginsF(0.0, 0.0, 0.0, 0.0));
    hub->setId(m_doc->allocateId());
    const ObjectId id = hub->id();

    const auto style = Support::linkHubStyle(kind);
    hub->setCustomColors(style.outline, style.fill, style.text);
    SymbolContentStyle symbolStyle;
    symbolStyle.text = style.text;
    auto content = std::make_unique<BlockContentSymbol>(style.symbol, symbolStyle);
    hub->setContent(std::move(content));

    const bool executed = m_doc->commands().execute(std::make_unique<CreateItemCommand>(std::move(hub)));
    if (executed && m_selection)
        m_selection->selectItem(id);
    return executed;
}

bool CanvasContextMenuController::executeSetHubKind(ObjectId itemId, Support::LinkHubKind kind)
{
    auto* hub = findBlock(itemId);
    if (!hub || !hub->isLinkHub() || !m_doc)
        return false;

    const auto style = Support::linkHubStyle(kind);
    hub->setCustomColors(style.outline, style.fill, style.text);
    SymbolContentStyle symbolStyle;
    symbolStyle.text = style.text;
    hub->setContent(std::make_unique<BlockContentSymbol>(style.symbol, symbolStyle));
    m_doc->notifyChanged();
    return true;
}

bool CanvasContextMenuController::executeClearWireRoute(ObjectId wireId)
{
    if (!m_doc || wireId.isNull())
        return false;
    auto* wire = dynamic_cast<CanvasWire*>(m_doc->findItem(wireId));
    if (!wire || !wire->hasRouteOverride())
        return false;
    wire->clearRouteOverride();
    m_doc->notifyChanged();
    return true;
}

bool CanvasContextMenuController::executeFrameAll()
{
    if (!m_doc)
        return false;
    return executeFrameItems(allItemIds(*m_doc));
}

bool CanvasContextMenuController::executeFrameSelection()
{
    if (!m_selection)
        return false;
    return executeFrameItems(m_selection->selectedItems());
}

bool CanvasContextMenuController::executeFrameItems(const QSet<ObjectId>& ids)
{
    if (!m_doc || ids.isEmpty())
        return false;

    bool hasBounds = false;
    QRectF bounds;
    for (const auto& item : m_doc->items()) {
        if (!item || !ids.contains(item->id()))
            continue;
        if (!hasBounds) {
            bounds = item->boundsScene();
            hasBounds = true;
        } else {
            bounds = bounds.united(item->boundsScene());
        }
    }
    if (!hasBounds)
        return false;
    return executeFrameRect(bounds);
}

bool CanvasContextMenuController::executeFrameRect(const QRectF& bounds)
{
    if (!m_view)
        return false;

    const QRectF rect = bounds.normalized();
    if (!rect.isValid())
        return false;

    const QSize viewSize = m_view->size();
    if (viewSize.width() <= 1 || viewSize.height() <= 1)
        return false;

    const double paddingPx = 48.0;
    const double availW = std::max(16.0, static_cast<double>(viewSize.width()) - paddingPx * 2.0);
    const double availH = std::max(16.0, static_cast<double>(viewSize.height()) - paddingPx * 2.0);
    const double fitW = std::max(rect.width(), Constants::kGridStep * 2.0);
    const double fitH = std::max(rect.height(), Constants::kGridStep * 2.0);

    const double zoom = Tools::clampZoom(std::min(availW / fitW, availH / fitH));
    m_view->setZoom(zoom);

    const QPointF viewCenter(static_cast<double>(viewSize.width()) * 0.5,
                             static_cast<double>(viewSize.height()) * 0.5);
    m_view->setPan((viewCenter / zoom) - rect.center());
    return true;
}

void CanvasContextMenuController::handleMenuAction(const QString& actionId)
{
    if (!m_doc)
        return;

    const MenuTarget target = m_activeTarget.value_or(MenuTarget{});

    if (actionId == kActionUndo) {
        m_doc->commands().undo();
        return;
    }
    if (actionId == kActionRedo) {
        m_doc->commands().redo();
        return;
    }
    if (actionId == kActionResetView) {
        if (m_view) {
            m_view->setZoom(1.0);
            m_view->setPan(QPointF());
        }
        return;
    }
    if (actionId == kActionFrameAll) {
        executeFrameAll();
        return;
    }
    if (actionId == kActionFrameSelection) {
        executeFrameSelection();
        return;
    }
    if (actionId == kActionToggleWireAnnotations) {
        if (m_view)
            m_view->setShowAllWireAnnotations(!m_view->showAllWireAnnotations());
        return;
    }
    if (actionId == kActionClearSelection) {
        if (m_selection) {
            m_selection->clearSelection();
            m_selection->clearSelectedPort();
        }
        return;
    }
    if (actionId == kActionDeleteSelection) {
        if (m_selection)
            executeDeleteItems(m_selection->selectedItems());
        return;
    }
    if (actionId == kActionAddBlock) {
        executeAddBlockAt(target.scenePos);
        return;
    }
    if (actionId == kActionAddHubSplit) {
        executeAddHubAt(target.scenePos, Support::LinkHubKind::Split);
        return;
    }
    if (actionId == kActionAddHubJoin) {
        executeAddHubAt(target.scenePos, Support::LinkHubKind::Join);
        return;
    }
    if (actionId == kActionAddHubBroadcast) {
        executeAddHubAt(target.scenePos, Support::LinkHubKind::Broadcast);
        return;
    }
    if (actionId == kActionDeleteItem) {
        executeDeleteSingleItem(target.itemId);
        return;
    }
    if (actionId == kActionDeleteWire) {
        executeDeleteSingleItem(target.itemId);
        return;
    }
    if (actionId == kActionClearWireRoute) {
        executeClearWireRoute(target.itemId);
        return;
    }
    if (actionId == kActionDeletePort) {
        executeDeletePort(target.itemId, target.portId);
        return;
    }
    if (actionId == kActionCreateBoundProducer) {
        m_pendingBoundProducerPlacement = PendingBoundProducerPlacement{target.itemId, target.portId};
        return;
    }
    if (actionId == kActionAddPort) {
        if (auto* block = findBlock(target.itemId)) {
            if (block->addPortToward(target.scenePos, PortRole::Dynamic))
                m_doc->notifyChanged();
        }
        return;
    }
    if (actionId == kActionToggleMovable) {
        if (auto* block = findBlock(target.itemId)) {
            block->setMovable(!block->isMovable());
            m_doc->notifyChanged();
        }
        return;
    }
    if (actionId == kActionToggleShowPorts) {
        if (auto* block = findBlock(target.itemId)) {
            block->setShowPorts(!block->showPorts());
            m_doc->notifyChanged();
        }
        return;
    }
    if (actionId == kActionHubKindSplit) {
        executeSetHubKind(target.itemId, Support::LinkHubKind::Split);
        return;
    }
    if (actionId == kActionHubKindJoin) {
        executeSetHubKind(target.itemId, Support::LinkHubKind::Join);
        return;
    }
    if (actionId == kActionHubKindBroadcast) {
        executeSetHubKind(target.itemId, Support::LinkHubKind::Broadcast);
        return;
    }
}

} // namespace Canvas::Controllers
