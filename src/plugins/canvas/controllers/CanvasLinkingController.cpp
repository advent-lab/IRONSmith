// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/controllers/CanvasLinkingController.hpp"

#include "canvas/controllers/CanvasDragController.hpp"
#include "canvas/controllers/CanvasInteractionHelpers.hpp"
#include "canvas/controllers/CanvasSelectionController.hpp"
#include "canvas/services/CanvasHitTestService.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasCommands.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasGeometry.hpp"
#include "canvas/utils/CanvasAutoPorts.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"
#include "canvas/utils/CanvasLinkingMode.hpp"
#include "canvas/utils/CanvasLinkWireStyle.hpp"
#include "canvas/utils/CanvasPortUsage.hpp"

#include <algorithm>
#include <cmath>

namespace Canvas::Controllers {

namespace {

PortRole oppositePortRole(Support::LinkWireRole role)
{
    return role == Support::LinkWireRole::Producer ? PortRole::Consumer
                                                   : PortRole::Producer;
}

std::optional<WireArrowPolicy> arrowPolicyFromPortRoles(const CanvasDocument* doc,
                                                        const PortRef& a,
                                                        const PortRef& b)
{
    if (!doc)
        return std::nullopt;

    CanvasPort aMeta;
    CanvasPort bMeta;
    if (!doc->getPort(a.itemId, a.portId, aMeta) || !doc->getPort(b.itemId, b.portId, bMeta))
        return std::nullopt;

    const bool aConsumer = aMeta.role == PortRole::Consumer;
    const bool bConsumer = bMeta.role == PortRole::Consumer;

    if (aConsumer && !bConsumer)
        return WireArrowPolicy::Start;
    if (bConsumer && !aConsumer)
        return WireArrowPolicy::End;
    return std::nullopt;
}

bool isDdrBlock(const CanvasDocument* doc, ObjectId itemId)
{
    if (!doc)
        return false;

    const auto* block = dynamic_cast<const CanvasBlock*>(doc->findItem(itemId));
    return block && block->specId().trimmed() == QStringLiteral("ddr");
}

struct DirectedLinkPorts final {
    PortRef producer;
    PortRef consumer;
};

std::optional<DirectedLinkPorts> directedLinkPorts(const CanvasDocument* doc,
                                                   const PortRef& a,
                                                   const PortRef& b)
{
    if (!doc)
        return std::nullopt;

    CanvasPort aMeta;
    CanvasPort bMeta;
    if (!doc->getPort(a.itemId, a.portId, aMeta) || !doc->getPort(b.itemId, b.portId, bMeta))
        return std::nullopt;

    if (aMeta.role == PortRole::Producer && bMeta.role == PortRole::Consumer)
        return DirectedLinkPorts{a, b};
    if (bMeta.role == PortRole::Producer && aMeta.role == PortRole::Consumer)
        return DirectedLinkPorts{b, a};

    return std::nullopt;
}

std::optional<CanvasWire::ObjectFifoOperation> inferredDdrOperation(const CanvasDocument* doc,
                                                                    const PortRef& start,
                                                                    const PortRef& end)
{
    const bool startDdr = isDdrBlock(doc, start.itemId);
    const bool endDdr = isDdrBlock(doc, end.itemId);
    if (startDdr == endDdr)
        return std::nullopt;

    const auto directed = directedLinkPorts(doc, start, end);
    if (!directed.has_value()) {
        const auto inferredFromHub = [&](const PortRef& endpoint, bool endpointIsStart)
            -> std::optional<CanvasWire::ObjectFifoOperation> {
            const auto* block = dynamic_cast<const CanvasBlock*>(doc->findItem(endpoint.itemId));
            if (!block || !block->isLinkHub())
                return std::nullopt;

            const auto* content = dynamic_cast<const BlockContentSymbol*>(block->content());
            if (!content)
                return std::nullopt;

            const QString symbol = content->symbol().trimmed();
            if (symbol == Support::linkHubStyle(Support::LinkHubKind::Distribute).symbol)
                return CanvasWire::ObjectFifoOperation::Fill;
            if (symbol == Support::linkHubStyle(Support::LinkHubKind::Collect).symbol)
                return CanvasWire::ObjectFifoOperation::Drain;

            const bool joinHub =
                symbol == Support::linkHubStyle(Support::LinkHubKind::Join).symbol ||
                symbol == Support::linkHubStyle(Support::LinkHubKind::Collect).symbol;
            const bool splitLikeHub =
                symbol == Support::linkHubStyle(Support::LinkHubKind::Split).symbol ||
                symbol == Support::linkHubStyle(Support::LinkHubKind::Broadcast).symbol ||
                symbol == Support::linkHubStyle(Support::LinkHubKind::Distribute).symbol;
            if (!joinHub && !splitLikeHub)
                return std::nullopt;

            const bool hubIsProducer = joinHub ? endpointIsStart : !endpointIsStart;
            return hubIsProducer ? CanvasWire::ObjectFifoOperation::Drain
                                 : CanvasWire::ObjectFifoOperation::Fill;
        };

        if (const auto op = inferredFromHub(start, true); op.has_value())
            return op;
        if (const auto op = inferredFromHub(end, false); op.has_value())
            return op;

        return startDdr ? CanvasWire::ObjectFifoOperation::Fill
                        : CanvasWire::ObjectFifoOperation::Drain;
    }

    const bool producerDdr = isDdrBlock(doc, directed->producer.itemId);
    return producerDdr ? CanvasWire::ObjectFifoOperation::Fill
                       : CanvasWire::ObjectFifoOperation::Drain;
}

QString defaultObjectFifoNameForPort(const CanvasDocument* doc, const PortRef& portRef)
{
    if (!doc)
        return QStringLiteral("of");

    CanvasPort meta;
    if (!doc->getPort(portRef.itemId, portRef.portId, meta))
        return QStringLiteral("of");

    const QString name = meta.name.trimmed();
    if (name.isEmpty())
        return QStringLiteral("of");
    return name.toLower();
}

std::optional<Support::LinkHubKind> hubKindFromBlock(CanvasBlock* block)
{
    if (!block)
        return std::nullopt;
    auto* content = dynamic_cast<BlockContentSymbol*>(block->content());
    if (!content)
        return std::nullopt;

    const QString symbol = content->symbol().trimmed();
    if (symbol == Support::linkHubStyle(Support::LinkHubKind::Split).symbol)
        return Support::LinkHubKind::Split;
    if (symbol == Support::linkHubStyle(Support::LinkHubKind::Join).symbol)
        return Support::LinkHubKind::Join;
    if (symbol == Support::linkHubStyle(Support::LinkHubKind::Broadcast).symbol)
        return Support::LinkHubKind::Broadcast;
    if (symbol == Support::linkHubStyle(Support::LinkHubKind::Distribute).symbol)
        return Support::LinkHubKind::Distribute;
    if (symbol == Support::linkHubStyle(Support::LinkHubKind::Collect).symbol)
        return Support::LinkHubKind::Collect;
    return std::nullopt;
}

std::optional<Support::LinkWireRole> wireRoleForHubConnection(CanvasBlock* hub, bool hubIsStart)
{
    const auto kind = hubKindFromBlock(hub);
    if (!kind)
        return std::nullopt;

    switch (*kind) {
        case Support::LinkHubKind::Join:
        case Support::LinkHubKind::Collect:
            return hubIsStart ? Support::LinkWireRole::Producer
                              : Support::LinkWireRole::Consumer;
        case Support::LinkHubKind::Split:
        case Support::LinkHubKind::Distribute:
            return hubIsStart ? Support::LinkWireRole::Consumer
                              : Support::LinkWireRole::Producer;
        case Support::LinkHubKind::Broadcast:
            return hubIsStart ? Support::LinkWireRole::Broadcast
                              : Support::LinkWireRole::BroadcastProducer;
    }
    return std::nullopt;
}

} // namespace

CanvasLinkingController::CanvasLinkingController(CanvasDocument* doc,
                                                 CanvasView* view,
                                                 CanvasSelectionController* selection,
                                                 CanvasDragController* drag)
    : m_doc(doc)
    , m_view(view)
    , m_selection(selection)
    , m_drag(drag)
{}

void CanvasLinkingController::setLinkingMode(CanvasController::LinkingMode mode)
{
    if (m_linkingMode == mode)
        return;
    m_linkingMode = mode;
    resetLinkingSession();
    if (m_view)
        m_view->update();
}

void CanvasLinkingController::setObjectFifoDefaults(CanvasController::ObjectFifoDefaults defaults)
{
    defaults.name = defaults.name.trimmed();
    if (defaults.name.isEmpty())
        defaults.name = QStringLiteral("of");
    defaults.depth = std::max(1, defaults.depth);
    defaults.type.typeId = defaults.type.typeId.trimmed();
    defaults.type.valueType = defaults.type.valueType.trimmed().toLower();
    if (defaults.type.valueType.isEmpty())
        defaults.type.valueType = QStringLiteral("i32");
    defaults.type.dimensions = defaults.type.dimensions.trimmed();
    m_objectFifoDefaults = std::move(defaults);
}

void CanvasLinkingController::resetLinkingSession()
{
    m_wiring = false;
    m_wireStartItem = ObjectId{};
    m_wireStartPort = PortId{};
    m_linkHubId = ObjectId{};
    if (m_view) {
        m_view->clearHoveredPort();
        m_view->clearHoveredEdge();
    }
    m_hoverEdge.reset();
}

CanvasLinkingController::LinkingPressResult
CanvasLinkingController::handleLinkingPress(const QPointF& scenePos,
                                            CanvasController::Mode mode)
{
    if (!m_view || !m_doc)
        return LinkingPressResult::NotHandled;
    if (mode != CanvasController::Mode::Linking)
        return LinkingPressResult::NotHandled;

    const double radiusScene = Constants::kPortHitRadiusPx / std::max(m_view->zoom(), 0.25);
    std::optional<PortRef> resolvedPort = m_doc->hitTestPort(scenePos, radiusScene);
    if (!resolvedPort) {
        if (auto edge = Detail::edgeCandidateAt(m_doc, m_view, scenePos))
            resolvedPort = Detail::ensureEdgePort(m_doc, *edge);
    }

    if (resolvedPort && !Support::isPortAvailable(*m_doc, resolvedPort->itemId, resolvedPort->portId)) {
        CanvasPort meta;
        auto* block = dynamic_cast<CanvasBlock*>(m_doc->findItem(resolvedPort->itemId));
        if (block && block->allowMultiplePorts() && m_doc->getPort(resolvedPort->itemId, resolvedPort->portId, meta)) {
            EdgeCandidate candidate;
            candidate.itemId = resolvedPort->itemId;
            candidate.side = meta.side;
            candidate.t = meta.t;
            candidate.anchorScene = block->portAnchorScene(meta.id);
            resolvedPort = Detail::ensureEdgePort(m_doc, candidate);
        } else {
            return LinkingPressResult::Handled;
        }
    }

    if (!resolvedPort) {
        if (Support::isHubLinkingMode(m_linkingMode))
            return LinkingPressResult::RequestLinkingModeReset;

        if (m_wiring) {
            resetLinkingSession();
            m_view->update();
            return LinkingPressResult::Handled;
        }

        CanvasItem* hit = nullptr;
        if (m_view) {
            const CanvasRenderContext ctx = Detail::buildRenderContext(m_doc, m_view);
            hit = Services::hitTestItem(*m_doc, scenePos, &ctx);
        } else {
            hit = Services::hitTestItem(*m_doc, scenePos, nullptr);
        }

        if (m_selection) {
            m_selection->clearSelectedPort();
            m_selection->selectItem(hit ? hit->id() : ObjectId{});
        }
        return LinkingPressResult::Handled;
    }

    if (Support::isHubLinkingMode(m_linkingMode))
        return handleLinkingHubPress(scenePos, *resolvedPort);

    if (!m_wiring) {
        m_wiring = true;
        m_wireStartItem = resolvedPort->itemId;
        m_wireStartPort = resolvedPort->portId;
        m_wirePreviewScene = scenePos;

        if (m_selection) {
            m_selection->clearSelectedPort();
            m_selection->selectItem(resolvedPort->itemId);
            m_selection->clearMarqueeSelection();
        }
        if (m_drag)
            m_drag->clearTransientState();

        m_view->update();
        return LinkingPressResult::Handled;
    }

    if (resolvedPort->itemId == m_wireStartItem && resolvedPort->portId == m_wireStartPort) {
        m_wirePreviewScene = scenePos;
        return LinkingPressResult::Handled;
    }

    CanvasPort startMeta;
    CanvasPort endMeta;
    Q_UNUSED(startMeta);
    Q_UNUSED(endMeta);
    if (m_doc->getPort(m_wireStartItem, m_wireStartPort, startMeta) &&
        m_doc->getPort(resolvedPort->itemId, resolvedPort->portId, endMeta))
    {
        const PortRef startRef{m_wireStartItem, m_wireStartPort};
        const PortRef endRef{resolvedPort->itemId, resolvedPort->portId};
        auto w = buildWire(startRef, endRef);
        w->setId(m_doc->allocateId());
        std::optional<Support::LinkWireRole> hubRole;
        if (auto* startBlock = dynamic_cast<CanvasBlock*>(m_doc->findItem(m_wireStartItem));
            startBlock && startBlock->isLinkHub()) {
            hubRole = wireRoleForHubConnection(startBlock, true);
        }
        if (!hubRole) {
            if (auto* endBlock = dynamic_cast<CanvasBlock*>(m_doc->findItem(resolvedPort->itemId));
                endBlock && endBlock->isLinkHub()) {
                hubRole = wireRoleForHubConnection(endBlock, false);
            }
        }
        if (hubRole) {
            const auto style = Support::linkWireStyle(*hubRole);
            w->setColorOverride(style.color);
        }
        applyLinkingModeDefaults(*w, startRef, endRef);

        m_doc->commands().execute(std::make_unique<CreateItemCommand>(std::move(w)));

        resetLinkingSession();
        m_view->clearHoveredPort();
        m_view->update();
        return LinkingPressResult::Handled;
    }

    return LinkingPressResult::Handled;
}

void CanvasLinkingController::updateLinkingHoverAndPreview(const QPointF& scenePos,
                                                           CanvasController::Mode mode,
                                                           bool panning,
                                                           bool dragEndpoint)
{
    if (!m_view || !m_doc)
        return;
    if (mode != CanvasController::Mode::Linking || panning || dragEndpoint)
        return;

    const double radiusScene = Constants::kPortHitRadiusPx / std::max(m_view->zoom(), 0.25);
    if (auto hitPort = m_doc->hitTestPort(scenePos, radiusScene)) {
        m_view->setHoveredPort(hitPort->itemId, hitPort->portId);
        m_view->clearHoveredEdge();
        m_hoverEdge.reset();
    } else if (auto edge = Detail::edgeCandidateAt(m_doc, m_view, scenePos)) {
        m_view->clearHoveredPort();
        m_view->setHoveredEdge(edge->itemId, edge->side, edge->anchorScene);
        m_hoverEdge = edge;
    } else {
        m_view->clearHoveredPort();
        m_view->clearHoveredEdge();
        m_hoverEdge.reset();
    }

    const QPointF preview = (m_hoverEdge.has_value() ? m_hoverEdge->anchorScene : scenePos);
    if (m_wiring && m_wirePreviewScene != preview) {
        m_wirePreviewScene = preview;
        m_view->update();
    }
}

CanvasLinkingController::LinkingPressResult
CanvasLinkingController::handleLinkingHubPress(const QPointF& scenePos, const PortRef& hitPort)
{
    if (!m_view || !m_doc)
        return LinkingPressResult::NotHandled;

    if (m_linkHubId.isNull() && !m_wiring) {
        beginLinkingFromPort(hitPort, scenePos);
        return LinkingPressResult::Handled;
    }

    if (!m_linkHubId.isNull()) {
        if (!connectToExistingHub(scenePos, hitPort))
            return LinkingPressResult::RequestLinkingModeReset;
        return LinkingPressResult::Handled;
    }

    if (hitPort.itemId == m_wireStartItem && hitPort.portId == m_wireStartPort) {
        m_wirePreviewScene = scenePos;
        return LinkingPressResult::Handled;
    }

    createHubAndWires(scenePos, hitPort);
    return LinkingPressResult::Handled;
}

bool CanvasLinkingController::beginLinkingFromPort(const PortRef& hitPort, const QPointF& scenePos)
{
    m_wiring = true;
    m_wireStartItem = hitPort.itemId;
    m_wireStartPort = hitPort.portId;
    m_wirePreviewScene = scenePos;

    if (m_selection) {
        m_selection->clearSelectedPort();
        m_selection->selectItem(hitPort.itemId);
        m_selection->clearMarqueeSelection();
    }
    if (m_drag)
        m_drag->clearTransientState();

    if (m_view)
        m_view->update();
    return true;
}

bool CanvasLinkingController::resolvePortTerminal(const PortRef& port,
                                                  QPointF& outAnchor,
                                                  QPointF& outBorder,
                                                  QPointF& outFabric) const
{
    return m_doc && m_doc->computePortTerminal(port.itemId, port.portId,
                                               outAnchor, outBorder, outFabric);
}

std::unique_ptr<CanvasWire> CanvasLinkingController::buildWire(const PortRef& a, const PortRef& b) const
{
    const CanvasWire::Endpoint start{a, QPointF()};
    const CanvasWire::Endpoint end{b, QPointF()};
    auto wire = std::make_unique<CanvasWire>(start, end);
    if (auto policy = arrowPolicyFromPortRoles(m_doc, a, b))
        wire->setArrowPolicy(*policy);
    return wire;
}

void CanvasLinkingController::applyLinkingModeDefaults(CanvasWire& wire,
                                                       const PortRef& start,
                                                       const PortRef& end) const
{
    const auto config = defaultObjectFifoConfig(start, end);
    if (!config.has_value()) {
        wire.clearObjectFifo();
        return;
    }

    wire.setObjectFifo(*config);
}

std::optional<CanvasWire::ObjectFifoConfig>
CanvasLinkingController::defaultObjectFifoConfig(const PortRef& start,
                                                 const PortRef& end) const
{
    const auto ddrOperation =
        (m_linkingMode == CanvasController::LinkingMode::Normal)
            ? inferredDdrOperation(m_doc, start, end)
            : std::nullopt;

    if (m_linkingMode != CanvasController::LinkingMode::Fifo &&
        m_linkingMode != CanvasController::LinkingMode::ForwardFifo &&
        !ddrOperation.has_value())
        return std::nullopt;

    CanvasWire::ObjectFifoConfig config;
    config.name = m_objectFifoDefaults.name;
    config.depth = m_objectFifoDefaults.depth;
    config.operation = CanvasWire::ObjectFifoOperation::Fifo;
    config.type = m_objectFifoDefaults.type;

    if (ddrOperation.has_value()) {
        config.operation = *ddrOperation;
    } else {
        config.operation = (m_linkingMode == CanvasController::LinkingMode::ForwardFifo)
            ? CanvasWire::ObjectFifoOperation::Forward
            : CanvasWire::ObjectFifoOperation::Fifo;
    }

    if (!m_doc)
        return config;

    if (const auto directed = directedLinkPorts(m_doc, start, end); directed.has_value()) {
        config.name = defaultObjectFifoNameForPort(m_doc, directed->consumer);
        return config;
    }

    if (config.operation == CanvasWire::ObjectFifoOperation::Fill) {
        config.name = defaultObjectFifoNameForPort(m_doc, end);
        return config;
    }
    if (config.operation == CanvasWire::ObjectFifoOperation::Drain) {
        config.name = defaultObjectFifoNameForPort(m_doc, start);
        return config;
    }

    CanvasPort startMeta;
    CanvasPort endMeta;
    if (!m_doc->getPort(start.itemId, start.portId, startMeta) ||
        !m_doc->getPort(end.itemId, end.portId, endMeta)) {
        return config;
    }

    if (startMeta.role == PortRole::Consumer && endMeta.role != PortRole::Consumer) {
        config.name = defaultObjectFifoNameForPort(m_doc, start);
        return config;
    }
    if (endMeta.role == PortRole::Consumer && startMeta.role != PortRole::Consumer) {
        config.name = defaultObjectFifoNameForPort(m_doc, end);
        return config;
    }

    config.name = defaultObjectFifoNameForPort(m_doc, end);
    return config;
}

CanvasBlock* CanvasLinkingController::findLinkHub() const
{
    if (!m_doc || m_linkHubId.isNull())
        return nullptr;
    return dynamic_cast<CanvasBlock*>(m_doc->findItem(m_linkHubId));
}

bool CanvasLinkingController::connectToExistingHub(const QPointF& scenePos, const PortRef& hitPort)
{
    if (hitPort.itemId == m_linkHubId) {
        m_wirePreviewScene = scenePos;
        return true;
    }

    CanvasBlock* hub = findLinkHub();
    if (!hub)
        return false;

    QPointF endAnchor, endBorder, endFabric;
    if (!resolvePortTerminal(hitPort, endAnchor, endBorder, endFabric))
        return true;

    const auto finishRole = Support::linkFinishWireRole(m_linkingMode);
    const PortId hubPort = hub->addPortToward(endAnchor, oppositePortRole(finishRole));
    auto w = buildWire(PortRef{hub->id(), hubPort}, hitPort);
    w->setId(m_doc->allocateId());
    const auto finishStyle = Support::linkWireStyle(Support::linkFinishWireRole(m_linkingMode));
    w->setColorOverride(finishStyle.color);
    w->setArrowPolicy(finishRole == Support::LinkWireRole::Consumer
                          ? WireArrowPolicy::End
                          : WireArrowPolicy::None);
    m_doc->commands().execute(std::make_unique<CreateItemCommand>(std::move(w)));

    m_wiring = true;
    m_wireStartItem = hub->id();
    m_wireStartPort = hubPort;
    m_wirePreviewScene = scenePos;
    m_view->clearHoveredPort();
    m_view->update();
    return true;
}

bool CanvasLinkingController::createHubAndWires(const QPointF& scenePos, const PortRef& hitPort)
{
    QPointF startAnchor, startBorder, startFabric;
    QPointF endAnchor, endBorder, endFabric;
    if (!resolvePortTerminal(PortRef{m_wireStartItem, m_wireStartPort},
                             startAnchor, startBorder, startFabric) ||
        !resolvePortTerminal(hitPort, endAnchor, endBorder, endFabric)) {
        resetLinkingSession();
        if (m_view)
            m_view->update();
        return true;
    }

    const double size = Constants::kLinkHubSize;
    QPointF hubCenter = (startFabric + endFabric) * 0.5;
    const double step = m_doc->fabric().config().step;
    if (step > 0.0)
        hubCenter = Support::snapPointToGrid(hubCenter, step);

    const QPointF topLeft(hubCenter.x() - size * 0.5, hubCenter.y() - size * 0.5);
    auto hub = std::make_unique<CanvasBlock>(QRectF(topLeft, QSizeF(size, size)), true, QString());
    hub->setShowPorts(false);
    hub->setAutoPortLayout(false);
    hub->setPortSnapStep(Constants::kGridStep);
    hub->setLinkHub(true);
    hub->setKeepoutMargin(0.0);
    hub->setContentPadding(QMarginsF(0.0, 0.0, 0.0, 0.0));
    hub->setId(m_doc->allocateId());

    const auto hubKind = Support::linkHubKindForMode(m_linkingMode);
    if (!hubKind) {
        resetLinkingSession();
        if (m_view)
            m_view->update();
        return false;
    }
    const auto style = Support::linkHubStyle(*hubKind);
    hub->setCustomColors(style.outline, style.fill, style.text);

    SymbolContentStyle symbolStyle;
    symbolStyle.text = style.text;
    auto content = std::make_unique<BlockContentSymbol>(style.symbol, symbolStyle);
    hub->setContent(std::move(content));

    const auto startRole = Support::linkStartWireRole(m_linkingMode);
    const auto finishRole = Support::linkFinishWireRole(m_linkingMode);
    const PortId hubPortA = hub->addPortToward(startAnchor, oppositePortRole(startRole));
    const PortId hubPortB = hub->addPortToward(endAnchor, oppositePortRole(finishRole));
    const ObjectId hubId = hub->id();

    m_doc->commands().execute(std::make_unique<CreateItemCommand>(std::move(hub)));

    auto w0 = buildWire(PortRef{m_wireStartItem, m_wireStartPort}, PortRef{hubId, hubPortA});
    w0->setId(m_doc->allocateId());
    const auto startStyle = Support::linkWireStyle(startRole);
    w0->setColorOverride(startStyle.color);
    w0->setArrowPolicy(startRole == Support::LinkWireRole::Consumer
                           ? WireArrowPolicy::Start
                           : WireArrowPolicy::None);
    m_doc->commands().execute(std::make_unique<CreateItemCommand>(std::move(w0)));

    auto w1 = buildWire(PortRef{hubId, hubPortB}, hitPort);
    w1->setId(m_doc->allocateId());
    const auto finishStyle = Support::linkWireStyle(finishRole);
    w1->setColorOverride(finishStyle.color);
    w1->setArrowPolicy(finishRole == Support::LinkWireRole::Consumer
                           ? WireArrowPolicy::End
                           : WireArrowPolicy::None);
    m_doc->commands().execute(std::make_unique<CreateItemCommand>(std::move(w1)));

    m_linkHubId = hubId;
    m_wiring = true;
    m_wireStartItem = hubId;
    m_wireStartPort = hubPortB;
    m_wirePreviewScene = scenePos;
    m_view->clearHoveredPort();
    m_view->update();
    return true;
}

} // namespace Canvas::Controllers
