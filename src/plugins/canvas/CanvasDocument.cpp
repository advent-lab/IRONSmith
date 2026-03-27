// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/CanvasDocument.hpp"

#include "canvas/CanvasConstants.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasGeometry.hpp"
#include "canvas/utils/CanvasPortHitTest.hpp"
#include "canvas/services/CanvasHitTestService.hpp"
#include "canvas/services/CanvasGeometryService.hpp"
#include "canvas/services/CanvasLayoutEngine.hpp"

#include <QtCore/QPointF>
#include <QtCore/QtGlobal>

#include <vector>
#include <utility>

namespace Canvas {

namespace {

QString normalizedHandleValueType(QString valueType)
{
    valueType = valueType.trimmed().toLower();
    if (valueType == QStringLiteral("i8") ||
        valueType == QStringLiteral("i16") ||
        valueType == QStringLiteral("i32") ||
        valueType == QStringLiteral("i64") ||
        valueType == QStringLiteral("ui8") ||
        valueType == QStringLiteral("ui16") ||
        valueType == QStringLiteral("ui32") ||
        valueType == QStringLiteral("ui64") ||
        valueType == QStringLiteral("f16") ||
        valueType == QStringLiteral("f32") ||
        valueType == QStringLiteral("f64") ||
        valueType == QStringLiteral("bf16")) {
        return valueType;
    }
    return QStringLiteral("i32");
}

int normalizedHandleDepth(int depth)
{
    return depth > 0 ? depth : 2;
}

} // namespace

CanvasBlock* CanvasDocument::createBlock(const QRectF& boundsScene, bool movable)
{
    const double step = m_fabric.config().step;
    const QRectF snapped = Support::snapBoundsToGrid(boundsScene, step);

    auto blk = std::make_unique<CanvasBlock>(snapped, movable);
    blk->setId(nextId());

    CanvasBlock* raw = blk.get();
    m_items.push_back(std::move(blk));
    notifyChanged();
    return raw;
}


CanvasItem* CanvasDocument::hitTest(const QPointF& scenePos) const
{
    return Services::hitTestItem(*this, scenePos);
}

std::optional<PortRef> CanvasDocument::hitTestPort(const QPointF& scenePos, double radiusScene) const
{
    ensureAutoPortLayout();
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
        const CanvasItem* item = it->get();
        if (!item || !item->hasPorts())
            continue;
        for (const auto& port : item->ports()) {
            const QPointF a = item->portAnchorScene(port.id);
            if (Support::hitTestPortGeometry(a, port.side, scenePos, radiusScene))
                return PortRef{item->id(), port.id};
        }
    }
    return std::nullopt;
}

bool CanvasDocument::getPort(ObjectId itemId, PortId portId, CanvasPort& out) const
{
    const CanvasItem* item = findItem(itemId);
    if (!item || !item->hasPorts())
        return false;
    for (const auto& p : item->ports()) {
        if (p.id == portId) { out = p; return true; }
    }
    return false;
}

std::optional<CanvasDocument::RemovedItem> CanvasDocument::removeItem(ObjectId itemId)
{
    for (size_t i = 0; i < m_items.size(); ++i) {
        auto& ptr = m_items[i];
        if (ptr && ptr->id() == itemId) {
            RemovedItem out;
            out.index = i;
            out.item = std::move(ptr);
            m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(i));
            notifyChanged();
            return out;
        }
    }
    return std::nullopt;
}

bool CanvasDocument::insertItem(size_t index, std::unique_ptr<CanvasItem> item)
{
    if (!item)
        return false;
    if (index > m_items.size())
        index = m_items.size();
    m_items.insert(m_items.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
    notifyChanged();
    return true;
}

ObjectId CanvasDocument::nextId()
{
    return ObjectId::create();
}

CanvasItem* CanvasDocument::findItem(ObjectId id) const
{
    if (id.isNull())
        return nullptr;

    for (const auto& it : m_items) {
        if (it && it->id() == id)
            return it.get();
    }
    return nullptr;
}

void CanvasDocument::notifyChanged()
{
    if (!m_inAutoPortLayout)
        scheduleAutoPortLayout();
    emit changed();
}

void CanvasDocument::scheduleAutoPortLayout()
{
    if (m_autoPortLayoutPending)
        return;
    m_autoPortLayoutPending = true;
    m_autoPortLayoutTimer.start();
}

void CanvasDocument::applyAutoPortLayout()
{
    if (!m_autoPortLayoutPending)
        return;

    m_autoPortLayoutPending = false;
    m_autoPortLayoutTimer.stop();

    Services::CanvasLayoutEngine layout;
    bool changed_ = false;
    m_inAutoPortLayout = true;
    for (const auto& it : m_items) {
        auto* block = dynamic_cast<CanvasBlock*>(it.get());
        if (!block || !block->autoPortLayout() || !block->hasPorts())
            continue;
        changed_ = layout.arrangeAutoPorts(*this, *block) || changed_;
    }
    m_inAutoPortLayout = false;

    if (changed_)
        emit changed();
}

void CanvasDocument::ensureAutoPortLayout() const
{
    if (!m_autoPortLayoutPending)
        return;
    auto* self = const_cast<CanvasDocument*>(this);
    self->applyAutoPortLayout();
}

bool CanvasDocument::setItemTopLeftImpl(CanvasItem* item, const QPointF& newTopLeftScene, bool emitChanged)
{
    auto* block = dynamic_cast<CanvasBlock*>(item);
    if (!block)
        return false;
    if (!block->isMovable())
        return false;

    const double step = m_fabric.config().step;
    const QPointF snappedTopLeft(Support::snapCoord(newTopLeftScene.x(), step),
                                 Support::snapCoord(newTopLeftScene.y(), step));

    QRectF r = block->boundsScene();
    r.moveTopLeft(snappedTopLeft);
    if (r == block->boundsScene())
        return true;

    block->setBoundsScene(r);
    for (const auto& it : m_items) {
        auto* wire = dynamic_cast<CanvasWire*>(it.get());
        if (wire && wire->hasRouteOverride() && wire->attachesTo(item->id()))
            wire->clearRouteOverride();
    }
    if (emitChanged)
        notifyChanged();
    return true;
}

bool CanvasDocument::setItemTopLeft(ObjectId itemId, const QPointF& newTopLeftScene)
{
    return setItemTopLeftImpl(findItem(itemId), newTopLeftScene, true);
}

bool CanvasDocument::previewSetItemTopLeft(ObjectId itemId, const QPointF& newTopLeftScene)
{
    return setItemTopLeftImpl(findItem(itemId), newTopLeftScene, true);
}

CanvasDocument::CanvasDocument(QObject* parent)
    : QObject(parent)
    , m_commands(this)
{
    m_autoPortLayoutTimer.setSingleShot(true);
    m_autoPortLayoutTimer.setInterval(0);
    connect(&m_autoPortLayoutTimer, &QTimer::timeout, this, &CanvasDocument::applyAutoPortLayout);

	CanvasFabric::Config cfg;
	cfg.step = Constants::kGridStep;
	m_fabric.setConfig(cfg);
}

QString CanvasDocument::statusText() const
{
	return m_statusText;
}

void CanvasDocument::setStatusText(QString text)
{
	if (m_statusText == text)
		return;

	m_statusText = std::move(text);
	notifyChanged();
}

bool CanvasDocument::isFabricPointBlocked(const FabricCoord& coord) const
{
    return Services::CanvasGeometryService::isFabricPointBlocked(*this, coord);
}

bool CanvasDocument::computePortTerminal(ObjectId itemId, PortId portId,
                                        QPointF& outAnchorScene,
                                        QPointF& outBorderScene,
                                        QPointF& outFabricScene) const
{
    ensureAutoPortLayout();
    return Services::CanvasGeometryService::computePortTerminal(*this,
                                                                itemId,
                                                                portId,
                                                                outAnchorScene,
                                                                outBorderScene,
                                                                outFabricScene);
}

bool CanvasDocument::computePortTerminalThunk(void* user, ObjectId itemId, PortId portId,
                                             QPointF& outAnchorScene,
                                             QPointF& outBorderScene,
                                             QPointF& outFabricScene)
{
    auto* doc = static_cast<const CanvasDocument*>(user);
    return doc ? doc->computePortTerminal(itemId, portId, outAnchorScene, outBorderScene, outFabricScene) : false;
}

bool CanvasDocument::resolveObjectFifoNameForEndpoint(ObjectId itemId, PortId portId, QString& outName) const
{
    if (itemId.isNull() || portId.isNull())
        return false;

    for (const auto& it : m_items) {
        const auto* wire = dynamic_cast<const CanvasWire*>(it.get());
        if (!wire || !wire->hasObjectFifo())
            continue;

        const auto endpointMatches = [&](const CanvasWire::Endpoint& endpoint) {
            return endpoint.attached.has_value() &&
                   endpoint.attached->itemId == itemId &&
                   endpoint.attached->portId == portId;
        };
        if (!endpointMatches(wire->a()) && !endpointMatches(wire->b()))
            continue;

        const QString fifoName = wire->objectFifo()->name.trimmed();
        if (fifoName.isEmpty())
            continue;
        outName = fifoName;
        return true;
    }

    return false;
}

bool CanvasDocument::resolveObjectFifoNameForEndpointThunk(void* user,
                                                           ObjectId itemId,
                                                           PortId portId,
                                                           QString& outName)
{
    auto* doc = static_cast<const CanvasDocument*>(user);
    return doc ? doc->resolveObjectFifoNameForEndpoint(itemId, portId, outName) : false;
}

bool CanvasDocument::resolveConsumerHandleLabelForEndpoint(ObjectId itemId,
                                                           PortId portId,
                                                           QString& outLabel) const
{
    if (itemId.isNull() || portId.isNull())
        return false;

    CanvasPort producerMeta;
    if (!getPort(itemId, portId, producerMeta))
        return false;
    if (producerMeta.role != PortRole::Producer || !producerMeta.hasBinding)
        return false;

    CanvasPort consumerMeta;
    if (!getPort(producerMeta.bindingItemId, producerMeta.bindingPortId, consumerMeta))
        return false;
    if (consumerMeta.role == PortRole::Producer)
        return false;

    QString valueType = QStringLiteral("i32");
    int depth = 2;

    for (const auto& it : m_items) {
        const auto* wire = dynamic_cast<const CanvasWire*>(it.get());
        if (!wire || !wire->hasObjectFifo())
            continue;

        const auto endpointMatches = [&](const CanvasWire::Endpoint& endpoint) {
            return endpoint.attached.has_value() &&
                   endpoint.attached->itemId == producerMeta.bindingItemId &&
                   endpoint.attached->portId == producerMeta.bindingPortId;
        };
        if (!endpointMatches(wire->a()) && !endpointMatches(wire->b()))
            continue;

        const auto& objectFifo = wire->objectFifo().value();
        valueType = normalizedHandleValueType(objectFifo.type.valueType);
        depth = normalizedHandleDepth(objectFifo.depth);
        break;
    }

    outLabel = QStringLiteral("C: HANDLE<%1,%2>").arg(valueType).arg(depth);
    return true;
}

bool CanvasDocument::resolveConsumerHandleLabelForEndpointThunk(void* user,
                                                                ObjectId itemId,
                                                                PortId portId,
                                                                QString& outLabel)
{
    auto* doc = static_cast<const CanvasDocument*>(user);
    return doc ? doc->resolveConsumerHandleLabelForEndpoint(itemId, portId, outLabel) : false;
}

bool CanvasDocument::resolveHubArmLabelForEndpoint(ObjectId itemId,
                                                   PortId portId,
                                                   QString& outLabel) const
{
    if (itemId.isNull() || portId.isNull())
        return false;

    const auto* block = dynamic_cast<const CanvasBlock*>(findItem(itemId));
    if (!block || !block->isLinkHub())
        return false;

    // Find portId's role and count how many ports share that role.
    // The pivot port is the lone port of its role (1 input for split/distribute,
    // 1 output for join/collect). Arm ports are the many ports of the other role.
    // Only label ports where count > 1 — those are the actual arm ports.
    PortRole role = PortRole::Dynamic;
    for (const auto& port : block->ports()) {
        if (port.id == portId) {
            role = port.role;
            break;
        }
    }
    if (role == PortRole::Dynamic)
        return false;

    int count = 0;
    for (const auto& port : block->ports()) {
        if (port.role == role)
            ++count;
    }
    if (count <= 1)
        return false; // pivot port — not an arm

    int index = 0;
    for (const auto& port : block->ports()) {
        if (port.role != role)
            continue;
        if (port.id == portId) {
            outLabel = QString::number(index);
            return true;
        }
        ++index;
    }
    return false;
}

bool CanvasDocument::resolveHubArmLabelForEndpointThunk(void* user,
                                                        ObjectId itemId,
                                                        PortId portId,
                                                        QString& outLabel)
{
    auto* doc = static_cast<const CanvasDocument*>(user);
    return doc ? doc->resolveHubArmLabelForEndpoint(itemId, portId, outLabel) : false;
}

bool CanvasDocument::resolveItemSpecId(ObjectId itemId, QString& outSpecId) const
{
    const auto* block = dynamic_cast<const CanvasBlock*>(findItem(itemId));
    if (!block)
        return false;

    outSpecId = block->specId();
    return true;
}

bool CanvasDocument::resolveItemSpecIdThunk(void* user,
                                            ObjectId itemId,
                                            QString& outSpecId)
{
    auto* doc = static_cast<const CanvasDocument*>(user);
    return doc ? doc->resolveItemSpecId(itemId, outSpecId) : false;
}

bool CanvasDocument::resolveItemSymbol(ObjectId itemId, QString& outSymbol) const
{
    const auto* block = dynamic_cast<const CanvasBlock*>(findItem(itemId));
    if (!block)
        return false;

    const auto* symbol = dynamic_cast<const BlockContentSymbol*>(block->content());
    if (!symbol)
        return false;

    outSymbol = symbol->symbol();
    return true;
}

bool CanvasDocument::resolveItemSymbolThunk(void* user,
                                            ObjectId itemId,
                                            QString& outSymbol)
{
    auto* doc = static_cast<const CanvasDocument*>(user);
    return doc ? doc->resolveItemSymbol(itemId, outSymbol) : false;
}

bool CanvasDocument::isFabricPointBlockedThunk(const FabricCoord& coord, void* user)
{
	auto* doc = static_cast<const CanvasDocument*>(user);
	return doc ? doc->isFabricPointBlocked(coord) : false;
}

} // namespace Canvas
