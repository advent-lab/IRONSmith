// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/document/CanvasDocumentJsonSerializer.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasSymbolContent.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/utils/CanvasAutoPorts.hpp"

#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>
#include <QtGui/QColor>

namespace Canvas::Internal {

namespace {

using namespace Qt::StringLiterals;

constexpr int kCurrentSchemaVersion = 2;
constexpr int kLegacySchemaVersion = 1;
constexpr int kPlaceholderManagedBlockExtent = 1;
constexpr auto kAieSchemaKey = "schema";
constexpr auto kAieSchemaValue = "aie.spec/1";

bool isSupportedSchemaVersion(int version)
{
    return version == kLegacySchemaVersion || version == kCurrentSchemaVersion;
}

bool isAieDocument(const QJsonObject& metadata)
{
    return metadata.value(QLatin1StringView(kAieSchemaKey)).toString().trimmed()
        == QLatin1StringView(kAieSchemaValue);
}

bool isAieManagedBlock(const QJsonObject& metadata, const QString& specId)
{
    return isAieDocument(metadata) && !specId.trimmed().isEmpty();
}

QRectF placeholderManagedBlockBounds()
{
    return QRectF(0.0,
                  0.0,
                  static_cast<double>(kPlaceholderManagedBlockExtent),
                  static_cast<double>(kPlaceholderManagedBlockExtent));
}

QJsonObject pointObject(const QPointF& point)
{
    QJsonObject obj;
    obj.insert(u"x"_s, point.x());
    obj.insert(u"y"_s, point.y());
    return obj;
}

QJsonObject rectObject(const QRectF& rect)
{
    QJsonObject obj;
    obj.insert(u"x"_s, rect.x());
    obj.insert(u"y"_s, rect.y());
    obj.insert(u"w"_s, rect.width());
    obj.insert(u"h"_s, rect.height());
    return obj;
}

QJsonObject marginsObject(const QMarginsF& margins)
{
    QJsonObject obj;
    obj.insert(u"l"_s, margins.left());
    obj.insert(u"t"_s, margins.top());
    obj.insert(u"r"_s, margins.right());
    obj.insert(u"b"_s, margins.bottom());
    return obj;
}

QJsonObject fabricCoordObject(const FabricCoord& coord)
{
    QJsonObject obj;
    obj.insert(u"x"_s, coord.x);
    obj.insert(u"y"_s, coord.y);
    return obj;
}

QString colorToString(const QColor& color)
{
    if (!color.isValid())
        return {};
    return color.name(QColor::HexArgb);
}

QColor colorFromString(const QString& text)
{
    if (text.trimmed().isEmpty())
        return {};
    const QColor color(text);
    return color.isValid() ? color : QColor{};
}

QString portSideToString(PortSide side)
{
    switch (side) {
        case PortSide::Left: return u"left"_s;
        case PortSide::Right: return u"right"_s;
        case PortSide::Top: return u"top"_s;
        case PortSide::Bottom: return u"bottom"_s;
    }
    return u"left"_s;
}

bool portSideFromString(const QString& text, PortSide& out)
{
    const QString key = text.trimmed().toLower();
    if (key == u"left"_s) {
        out = PortSide::Left;
        return true;
    }
    if (key == u"right"_s) {
        out = PortSide::Right;
        return true;
    }
    if (key == u"top"_s) {
        out = PortSide::Top;
        return true;
    }
    if (key == u"bottom"_s) {
        out = PortSide::Bottom;
        return true;
    }
    return false;
}

QString portRoleToString(PortRole role)
{
    switch (role) {
        case PortRole::Producer: return u"producer"_s;
        case PortRole::Consumer: return u"consumer"_s;
        case PortRole::Dynamic: return u"dynamic"_s;
    }
    return u"dynamic"_s;
}

bool portRoleFromString(const QString& text, PortRole& out)
{
    const QString key = text.trimmed().toLower();
    if (key == u"producer"_s) {
        out = PortRole::Producer;
        return true;
    }
    if (key == u"consumer"_s) {
        out = PortRole::Consumer;
        return true;
    }
    if (key == u"dynamic"_s) {
        out = PortRole::Dynamic;
        return true;
    }
    return false;
}

QString arrowPolicyToString(WireArrowPolicy policy)
{
    switch (policy) {
        case WireArrowPolicy::None: return u"none"_s;
        case WireArrowPolicy::Start: return u"start"_s;
        case WireArrowPolicy::End: return u"end"_s;
    }
    return u"end"_s;
}

bool arrowPolicyFromString(const QString& text, WireArrowPolicy& out)
{
    const QString key = text.trimmed().toLower();
    if (key == u"none"_s) {
        out = WireArrowPolicy::None;
        return true;
    }
    if (key == u"start"_s) {
        out = WireArrowPolicy::Start;
        return true;
    }
    if (key == u"end"_s) {
        out = WireArrowPolicy::End;
        return true;
    }
    return false;
}

QString objectFifoOperationToString(CanvasWire::ObjectFifoOperation operation)
{
    switch (operation) {
        case CanvasWire::ObjectFifoOperation::Forward: return u"forward"_s;
        case CanvasWire::ObjectFifoOperation::Fill: return u"fill"_s;
        case CanvasWire::ObjectFifoOperation::Drain: return u"drain"_s;
        case CanvasWire::ObjectFifoOperation::Split: return u"split"_s;
        case CanvasWire::ObjectFifoOperation::Join: return u"join"_s;
        case CanvasWire::ObjectFifoOperation::Fifo: return u"fifo"_s;
    }
    return u"fifo"_s;
}

CanvasWire::ObjectFifoOperation objectFifoOperationFromString(const QString& text)
{
    const QString key = text.trimmed().toLower();
    if (key == u"forward"_s || key == u"fwd"_s || key == u"forward_fifo"_s || key == u"forward-fifo"_s)
        return CanvasWire::ObjectFifoOperation::Forward;
    if (key == u"fill"_s)
        return CanvasWire::ObjectFifoOperation::Fill;
    if (key == u"drain"_s)
        return CanvasWire::ObjectFifoOperation::Drain;
    if (key == u"split"_s)
        return CanvasWire::ObjectFifoOperation::Split;
    if (key == u"join"_s)
        return CanvasWire::ObjectFifoOperation::Join;
    return CanvasWire::ObjectFifoOperation::Fifo;
}

bool parsePoint(const QJsonObject& object, QPointF& out)
{
    const QJsonValue x = object.value(u"x"_s);
    const QJsonValue y = object.value(u"y"_s);
    if (!x.isDouble() || !y.isDouble())
        return false;
    out = QPointF(x.toDouble(), y.toDouble());
    return true;
}

bool parseRect(const QJsonObject& object, QRectF& out)
{
    const QJsonValue x = object.value(u"x"_s);
    const QJsonValue y = object.value(u"y"_s);
    const QJsonValue w = object.value(u"w"_s);
    const QJsonValue h = object.value(u"h"_s);
    if (!x.isDouble() || !y.isDouble() || !w.isDouble() || !h.isDouble())
        return false;
    out = QRectF(x.toDouble(), y.toDouble(), w.toDouble(), h.toDouble());
    return true;
}

bool parseMargins(const QJsonObject& object, QMarginsF& out)
{
    const QJsonValue l = object.value(u"l"_s);
    const QJsonValue t = object.value(u"t"_s);
    const QJsonValue r = object.value(u"r"_s);
    const QJsonValue b = object.value(u"b"_s);
    if (!l.isDouble() || !t.isDouble() || !r.isDouble() || !b.isDouble())
        return false;
    out = QMarginsF(l.toDouble(), t.toDouble(), r.toDouble(), b.toDouble());
    return true;
}

bool parseFabricCoord(const QJsonObject& object, FabricCoord& out)
{
    const QJsonValue x = object.value(u"x"_s);
    const QJsonValue y = object.value(u"y"_s);
    if (!x.isDouble() || !y.isDouble())
        return false;
    out = FabricCoord{x.toInt(), y.toInt()};
    return true;
}

void clearDocument(CanvasDocument& document)
{
    QVector<ObjectId> ids;
    ids.reserve(static_cast<int>(document.items().size()));
    for (const auto& item : document.items()) {
        if (item)
            ids.push_back(item->id());
    }
    for (const auto& id : ids)
        document.removeItem(id);
}

QString makePortKey(const QString& itemId, const QString& portId)
{
    return itemId + u"|"_s + portId;
}

void normalizeAutoOppositePortNames(std::vector<CanvasPort>& ports, bool autoOppositeProducerPort)
{
    if (!autoOppositeProducerPort || ports.empty())
        return;

    auto isConsumerLike = [](PortRole role) {
        return role == PortRole::Consumer || role == PortRole::Dynamic;
    };

    QHash<QString, int> producerIndexByKey;
    QHash<QString, QVector<int>> consumerIndicesByKey;
    QVector<int> consumerWithoutKey;

    for (int i = 0; i < static_cast<int>(ports.size()); ++i) {
        const CanvasPort& port = ports[static_cast<size_t>(i)];
        const auto key = Support::pairedPortKeyFromName(port.name);
        if (key && !key->isEmpty()) {
            if (port.role == PortRole::Producer) {
                if (!producerIndexByKey.contains(*key))
                    producerIndexByKey.insert(*key, i);
            } else if (isConsumerLike(port.role)) {
                consumerIndicesByKey[*key].push_back(i);
            }
            continue;
        }

        if (isConsumerLike(port.role))
            consumerWithoutKey.push_back(i);
    }

    for (auto it = producerIndexByKey.begin(); it != producerIndexByKey.end(); ++it) {
        const auto consumersIt = consumerIndicesByKey.constFind(it.key());
        if (consumersIt == consumerIndicesByKey.constEnd() || consumersIt->isEmpty())
            continue;

        const QString canonicalName = Support::pairedPortName(it.key());
        CanvasPort& producer = ports[static_cast<size_t>(it.value())];
        if (producer.name != canonicalName)
            producer.name = canonicalName;

        for (const int consumerIndex : *consumersIt) {
            CanvasPort& consumer = ports[static_cast<size_t>(consumerIndex)];
            if (consumer.name != canonicalName)
                consumer.name = canonicalName;
        }
    }

    for (const int consumerIndex : consumerWithoutKey) {
        CanvasPort& consumer = ports[static_cast<size_t>(consumerIndex)];
        const QString consumerIdKey = consumer.id.toString();
        const auto producerIt = producerIndexByKey.constFind(consumerIdKey);
        if (producerIt == producerIndexByKey.constEnd())
            continue;

        const QString canonicalName = Support::pairedPortName(consumerIdKey);
        CanvasPort& producer = ports[static_cast<size_t>(producerIt.value())];
        producer.name = canonicalName;
        consumer.name = canonicalName;
    }
}

} // namespace

QJsonObject CanvasDocumentJsonSerializer::serialize(const CanvasDocument& document,
                                                    const CanvasView* view,
                                                    const QJsonObject& metadata)
{
    QJsonObject root;
    root.insert(u"schemaVersion"_s, kCurrentSchemaVersion);

    if (view) {
        QJsonObject viewObject;
        viewObject.insert(u"zoom"_s, view->zoom());
        viewObject.insert(u"pan"_s, pointObject(view->pan()));
        root.insert(u"view"_s, viewObject);
    }

    if (!metadata.isEmpty())
        root.insert(u"metadata"_s, metadata);

    QJsonArray items;
    for (const auto& item : document.items()) {
        if (!item)
            continue;

        if (const auto* block = dynamic_cast<const CanvasBlock*>(item.get())) {
            QJsonObject obj;
            const QString specId = block->specId().trimmed();
            obj.insert(u"type"_s, u"block"_s);
            obj.insert(u"id"_s, block->id().toString());
            if (!isAieManagedBlock(metadata, specId))
                obj.insert(u"bounds"_s, rectObject(block->boundsScene()));
            obj.insert(u"movable"_s, block->isMovable());
            obj.insert(u"deletable"_s, block->isDeletable());
            obj.insert(u"label"_s, block->label());
            if (!block->stereotype().trimmed().isEmpty())
                obj.insert(u"stereotype"_s, block->stereotype());
            obj.insert(u"specId"_s, specId);
            obj.insert(u"showPorts"_s, block->showPorts());
            obj.insert(u"allowMultiplePorts"_s, block->allowMultiplePorts());
            obj.insert(u"autoOppositeProducerPort"_s, block->autoOppositeProducerPort());
            obj.insert(u"showPortLabels"_s, block->showPortLabels());
            obj.insert(u"autoPortLayout"_s, block->autoPortLayout());
            obj.insert(u"portSnapStep"_s, block->portSnapStep());
            obj.insert(u"isLinkHub"_s, block->isLinkHub());
            obj.insert(u"keepoutMargin"_s, block->keepoutMargin());
            obj.insert(u"contentPadding"_s, marginsObject(block->contentPadding()));
            obj.insert(u"cornerRadius"_s, block->cornerRadius());
            if (block->hasAutoPortRole())
                obj.insert(u"autoPortRole"_s, portRoleToString(block->autoPortRole()));

            if (block->hasCustomColors()) {
                QJsonObject style;
                style.insert(u"outline"_s, colorToString(block->outlineColor()));
                style.insert(u"fill"_s, colorToString(block->fillColor()));
                style.insert(u"label"_s, colorToString(block->labelColor()));
                obj.insert(u"style"_s, style);
            }

            if (const auto* symbol = dynamic_cast<const BlockContentSymbol*>(block->content())) {
                QJsonObject contentObject;
                contentObject.insert(u"type"_s, u"symbol"_s);
                contentObject.insert(u"symbol"_s, symbol->symbol());

                QJsonObject symbolStyle;
                symbolStyle.insert(u"textColor"_s, colorToString(symbol->style().text));
                symbolStyle.insert(u"pointSize"_s, symbol->style().pointSize);
                symbolStyle.insert(u"bold"_s, symbol->style().bold);
                contentObject.insert(u"style"_s, symbolStyle);
                obj.insert(u"content"_s, contentObject);
            }

            QJsonArray ports;
            for (const auto& port : block->ports()) {
                QJsonObject portObject;
                portObject.insert(u"id"_s, port.id.toString());
                portObject.insert(u"side"_s, portSideToString(port.side));
                portObject.insert(u"role"_s, portRoleToString(port.role));
                portObject.insert(u"t"_s, port.t);
                portObject.insert(u"name"_s, port.name);
                if (port.hasBinding) {
                    QJsonObject bindingObject;
                    bindingObject.insert(u"itemId"_s, port.bindingItemId.toString());
                    bindingObject.insert(u"portId"_s, port.bindingPortId.toString());
                    portObject.insert(u"binding"_s, bindingObject);
                }
                ports.append(portObject);
            }
            obj.insert(u"ports"_s, ports);
            items.append(obj);
            continue;
        }

        if (const auto* wire = dynamic_cast<const CanvasWire*>(item.get())) {
            auto endpointObject = [](const CanvasWire::Endpoint& endpoint) {
                QJsonObject object;
                object.insert(u"free"_s, pointObject(endpoint.freeScene));
                if (endpoint.attached.has_value()) {
                    QJsonObject attached;
                    attached.insert(u"itemId"_s, endpoint.attached->itemId.toString());
                    attached.insert(u"portId"_s, endpoint.attached->portId.toString());
                    object.insert(u"attached"_s, attached);
                }
                return object;
            };

            QJsonObject obj;
            obj.insert(u"type"_s, u"wire"_s);
            obj.insert(u"id"_s, wire->id().toString());
            obj.insert(u"a"_s, endpointObject(wire->a()));
            obj.insert(u"b"_s, endpointObject(wire->b()));
            obj.insert(u"arrowPolicy"_s, arrowPolicyToString(wire->arrowPolicy()));
            if (wire->hasColorOverride())
                obj.insert(u"colorOverride"_s, colorToString(wire->colorOverride()));
            if (wire->hasObjectFifo()) {
                const auto& fifo = wire->objectFifo().value();
                QJsonObject fifoObject;
                fifoObject.insert(u"name"_s, fifo.name);
                fifoObject.insert(u"depth"_s, fifo.depth);
                fifoObject.insert(u"operation"_s, objectFifoOperationToString(fifo.operation));
                fifoObject.insert(u"dimensions"_s, fifo.type.dimensions);
                fifoObject.insert(u"valueType"_s, fifo.type.valueType);
                if (!fifo.type.typeId.trimmed().isEmpty())
                    fifoObject.insert(u"typeId"_s, fifo.type.typeId);
                if (!fifo.type.tapSymbolId.trimmed().isEmpty())
                    fifoObject.insert(u"tapSymbolId"_s, fifo.type.tapSymbolId);
                fifoObject.insert(u"dimensionMode"_s,
                    fifo.type.mode == CanvasWire::DimensionMode::Matrix
                        ? u"matrix"_s : u"vector"_s);
                if (fifo.type.tap.has_value()) {
                    const auto& t = *fifo.type.tap;
                    QJsonObject tapObj;
                    tapObj.insert(u"tileDims"_s,      t.tileDims);
                    tapObj.insert(u"tileCounts"_s,    t.tileCounts);
                    tapObj.insert(u"pruneStep"_s,     t.pruneStep);
                    tapObj.insert(u"index"_s,         t.index);
                    tapObj.insert(u"patternRepeat"_s, t.patternRepeat);
                    fifoObject.insert(u"tap"_s, tapObj);
                }
                obj.insert(u"objectFifo"_s, fifoObject);
            }
            if (wire->hasRouteOverride()) {
                QJsonArray route;
                for (const auto& coord : wire->routeOverride())
                    route.append(fabricCoordObject(coord));
                obj.insert(u"routeOverride"_s, route);
            }
            items.append(obj);
        }
    }

    root.insert(u"items"_s, items);
    return root;
}

Utils::Result CanvasDocumentJsonSerializer::deserialize(const QJsonObject& json,
                                                        CanvasDocument& document,
                                                        CanvasView* view,
                                                        QJsonObject* outMetadata)
{
    QStringList errors;

    const QJsonValue schemaValue = json.value(u"schemaVersion"_s);
    if (!schemaValue.isUndefined()) {
        if (!schemaValue.isDouble()) {
            errors.push_back(QStringLiteral("schemaVersion must be a number."));
        } else if (!isSupportedSchemaVersion(schemaValue.toInt())) {
            errors.push_back(QStringLiteral("Unsupported schemaVersion: %1").arg(schemaValue.toInt()));
        }
    }

    const QJsonObject metadata = json.value(u"metadata"_s).toObject();
    if (outMetadata)
        *outMetadata = metadata;

    double zoom = 1.0;
    QPointF pan(0.0, 0.0);
    const QJsonObject viewObject = json.value(u"view"_s).toObject();
    if (!viewObject.isEmpty()) {
        const QJsonValue zoomValue = viewObject.value(u"zoom"_s);
        if (!zoomValue.isUndefined()) {
            if (zoomValue.isDouble())
                zoom = zoomValue.toDouble();
            else
                errors.push_back(QStringLiteral("view.zoom must be numeric."));
        }

        const QJsonObject panObject = viewObject.value(u"pan"_s).toObject();
        if (!panObject.isEmpty() && !parsePoint(panObject, pan))
            errors.push_back(QStringLiteral("view.pan must include numeric x/y."));
    }

    const QJsonValue itemsValue = json.value(u"items"_s);
    if (!itemsValue.isUndefined() && !itemsValue.isArray())
        errors.push_back(QStringLiteral("items must be an array."));

    if (!errors.isEmpty())
        return Utils::Result::failure(errors.join("\n"));

    struct ParsedWireEndpoint final {
        bool hasAttached = false;
        QString itemId;
        QString portId;
        QPointF freeScene;
    };

    struct ParsedWire final {
        QString id;
        ParsedWireEndpoint a;
        ParsedWireEndpoint b;
        WireArrowPolicy arrowPolicy = WireArrowPolicy::End;
        QColor colorOverride;
        bool hasColorOverride = false;
        std::optional<CanvasWire::ObjectFifoConfig> objectFifo;
        QVector<FabricCoord> routeOverride;
    };

    QVector<ParsedWire> pendingWires;
    QHash<QString, ObjectId> blockIdMap;
    QHash<QString, PortId> portIdMap;

    clearDocument(document);
    if (view) {
        view->clearSelectedItems();
        view->clearSelectedPort();
        view->clearHoveredPort();
        view->clearHoveredEdge();
        view->clearMarqueeRect();
    }

    const QJsonArray items = itemsValue.toArray();
    pendingWires.reserve(items.size());

    for (int index = 0; index < items.size(); ++index) {
        const QJsonObject item = items.at(index).toObject();
        const QString type = item.value(u"type"_s).toString().trimmed().toLower();
        const QString idText = item.value(u"id"_s).toString().trimmed();

        if (type == u"block"_s) {
            if (idText.isEmpty()) {
                errors.push_back(QStringLiteral("items[%1]: block id is missing.").arg(index));
                continue;
            }

            const auto parsedId = ObjectId::fromString(idText);
            if (!parsedId) {
                errors.push_back(QStringLiteral("items[%1]: block id is invalid.").arg(index));
                continue;
            }

            if (blockIdMap.contains(idText)) {
                errors.push_back(QStringLiteral("items[%1]: duplicate block id '%2'.")
                                     .arg(index)
                                     .arg(idText));
                continue;
            }

            const QString specId = item.value(u"specId"_s).toString();
            const bool managedAieBlock = isAieManagedBlock(metadata, specId);

            QRectF bounds = managedAieBlock ? placeholderManagedBlockBounds() : QRectF{};
            if (!managedAieBlock && !parseRect(item.value(u"bounds"_s).toObject(), bounds)) {
                errors.push_back(QStringLiteral("items[%1]: block bounds are invalid.").arg(index));
                continue;
            }

            auto block = std::make_unique<CanvasBlock>(bounds,
                                                       item.value(u"movable"_s).toBool(true),
                                                       item.value(u"label"_s).toString());
            block->setId(*parsedId);
            block->setDeletable(item.value(u"deletable"_s).toBool(true));
            block->setStereotype(item.value(u"stereotype"_s).toString());
            block->setSpecId(specId);
            block->setShowPorts(item.value(u"showPorts"_s).toBool(true));
            block->setAllowMultiplePorts(item.value(u"allowMultiplePorts"_s).toBool(false));
            block->setAutoOppositeProducerPort(item.value(u"autoOppositeProducerPort"_s).toBool(false));
            block->setShowPortLabels(item.value(u"showPortLabels"_s).toBool(false));
            block->setAutoPortLayout(item.value(u"autoPortLayout"_s).toBool(false));
            block->setPortSnapStep(item.value(u"portSnapStep"_s).toDouble(Constants::kGridStep));
            block->setLinkHub(item.value(u"isLinkHub"_s).toBool(false));
            block->setKeepoutMargin(item.value(u"keepoutMargin"_s).toDouble(-1.0));
            block->setCornerRadius(item.value(u"cornerRadius"_s).toDouble(-1.0));

            const QString autoPortRoleText = item.value(u"autoPortRole"_s).toString();
            if (!autoPortRoleText.isEmpty()) {
                PortRole role = PortRole::Dynamic;
                if (portRoleFromString(autoPortRoleText, role))
                    block->setAutoPortRole(role);
                else
                    errors.push_back(QStringLiteral("items[%1]: autoPortRole is invalid.").arg(index));
            }

            const QJsonObject paddingObject = item.value(u"contentPadding"_s).toObject();
            if (!paddingObject.isEmpty()) {
                QMarginsF padding;
                if (parseMargins(paddingObject, padding))
                    block->setContentPadding(padding);
                else
                    errors.push_back(QStringLiteral("items[%1]: contentPadding is invalid.").arg(index));
            }

            const QJsonObject styleObject = item.value(u"style"_s).toObject();
            if (!styleObject.isEmpty()) {
                const QColor outline = colorFromString(styleObject.value(u"outline"_s).toString());
                const QColor fill = colorFromString(styleObject.value(u"fill"_s).toString());
                const QColor label = colorFromString(styleObject.value(u"label"_s).toString());
                if (outline.isValid() || fill.isValid() || label.isValid())
                    block->setCustomColors(outline, fill, label);
            }

            const QJsonObject contentObject = item.value(u"content"_s).toObject();
            if (!contentObject.isEmpty()) {
                const QString contentType = contentObject.value(u"type"_s).toString().trimmed().toLower();
                if (contentType == u"symbol"_s) {
                    SymbolContentStyle style;
                    const QJsonObject styleObject = contentObject.value(u"style"_s).toObject();
                    if (!styleObject.isEmpty()) {
                        const QColor textColor = colorFromString(styleObject.value(u"textColor"_s).toString());
                        if (textColor.isValid())
                            style.text = textColor;
                        style.pointSize = styleObject.value(u"pointSize"_s).toDouble(style.pointSize);
                        style.bold = styleObject.value(u"bold"_s).toBool(style.bold);
                    }

                    auto symbol = std::make_unique<BlockContentSymbol>(contentObject.value(u"symbol"_s).toString(),
                                                                       style);
                    block->setContent(std::move(symbol));
                }
            }

            std::vector<CanvasPort> ports;
            const QJsonArray portsArray = item.value(u"ports"_s).toArray();
            ports.reserve(static_cast<size_t>(portsArray.size()));
            for (int portIndex = 0; portIndex < portsArray.size(); ++portIndex) {
                const QJsonObject portObject = portsArray.at(portIndex).toObject();

                const QString portIdText = portObject.value(u"id"_s).toString().trimmed();
                const auto parsedPortId = PortId::fromString(portIdText);
                if (!parsedPortId) {
                    errors.push_back(QStringLiteral("items[%1].ports[%2]: invalid port id.")
                                         .arg(index).arg(portIndex));
                    continue;
                }

                PortSide side = PortSide::Left;
                if (!portSideFromString(portObject.value(u"side"_s).toString(), side)) {
                    errors.push_back(QStringLiteral("items[%1].ports[%2]: invalid side.")
                                         .arg(index).arg(portIndex));
                    continue;
                }

                PortRole role = PortRole::Dynamic;
                if (!portRoleFromString(portObject.value(u"role"_s).toString(u"dynamic"_s), role)) {
                    errors.push_back(QStringLiteral("items[%1].ports[%2]: invalid role.")
                                         .arg(index).arg(portIndex));
                    continue;
                }

                CanvasPort port;
                port.id = *parsedPortId;
                port.side = side;
                port.role = role;
                port.t = portObject.value(u"t"_s).toDouble(0.5);
                port.name = portObject.value(u"name"_s).toString();

                const QJsonObject bindingObject = portObject.value(u"binding"_s).toObject();
                if (!bindingObject.isEmpty()) {
                    const QString bindingItemIdText =
                        bindingObject.value(u"itemId"_s).toString().trimmed();
                    const QString bindingPortIdText =
                        bindingObject.value(u"portId"_s).toString().trimmed();
                    const auto bindingItemId = ObjectId::fromString(bindingItemIdText);
                    const auto bindingPortId = PortId::fromString(bindingPortIdText);
                    if (bindingItemId && bindingPortId) {
                        port.hasBinding = true;
                        port.bindingItemId = *bindingItemId;
                        port.bindingPortId = *bindingPortId;
                    } else {
                        errors.push_back(QStringLiteral("items[%1].ports[%2]: invalid binding endpoint.")
                                             .arg(index).arg(portIndex));
                    }
                }
                ports.push_back(port);
                portIdMap.insert(makePortKey(idText, port.id.toString()), port.id);
            }
            normalizeAutoOppositePortNames(ports, block->autoOppositeProducerPort());
            block->setPorts(std::move(ports));

            const ObjectId blockId = block->id();
            if (!document.insertItem(document.items().size(), std::move(block))) {
                errors.push_back(QStringLiteral("items[%1]: failed to insert block into document.").arg(index));
                continue;
            }

            blockIdMap.insert(idText, blockId);
            continue;
        }

        if (type == u"wire"_s) {
            ParsedWire wire;
            wire.id = idText;

            auto parseEndpoint = [&](const QJsonObject& endpointObject,
                                     ParsedWireEndpoint& endpoint,
                                     const QString& label) {
                endpoint = ParsedWireEndpoint{};
                if (!parsePoint(endpointObject.value(u"free"_s).toObject(), endpoint.freeScene)) {
                    errors.push_back(QStringLiteral("items[%1].%2.free is invalid.")
                                         .arg(index)
                                         .arg(label));
                }

                const QJsonObject attachedObject = endpointObject.value(u"attached"_s).toObject();
                if (attachedObject.isEmpty())
                    return;

                endpoint.hasAttached = true;
                endpoint.itemId = attachedObject.value(u"itemId"_s).toString().trimmed();
                endpoint.portId = attachedObject.value(u"portId"_s).toString().trimmed();
                if (endpoint.itemId.isEmpty() || endpoint.portId.isEmpty()) {
                    errors.push_back(QStringLiteral("items[%1].%2.attached is incomplete.")
                                         .arg(index)
                                         .arg(label));
                }
            };

            parseEndpoint(item.value(u"a"_s).toObject(), wire.a, QStringLiteral("a"));
            parseEndpoint(item.value(u"b"_s).toObject(), wire.b, QStringLiteral("b"));

            const QString arrowPolicy = item.value(u"arrowPolicy"_s).toString();
            if (!arrowPolicy.isEmpty() && !arrowPolicyFromString(arrowPolicy, wire.arrowPolicy)) {
                errors.push_back(QStringLiteral("items[%1]: invalid arrowPolicy.").arg(index));
            }

            const QColor overrideColor = colorFromString(item.value(u"colorOverride"_s).toString());
            if (overrideColor.isValid()) {
                wire.colorOverride = overrideColor;
                wire.hasColorOverride = true;
            }

            const QJsonObject objectFifoObject = item.value(u"objectFifo"_s).toObject();
            if (!objectFifoObject.isEmpty()) {
                CanvasWire::ObjectFifoConfig objectFifo;
                objectFifo.name = objectFifoObject.value(u"name"_s).toString();
                objectFifo.depth = objectFifoObject.value(u"depth"_s).toInt(2);
                objectFifo.operation = objectFifoOperationFromString(objectFifoObject.value(u"operation"_s).toString());
                objectFifo.type.typeId = objectFifoObject.value(u"typeId"_s).toString();
                objectFifo.type.tapSymbolId = objectFifoObject.value(u"tapSymbolId"_s).toString();
                objectFifo.type.dimensions = objectFifoObject.value(u"dimensions"_s).toString();
                objectFifo.type.valueType  = objectFifoObject.value(u"valueType"_s).toString();
                objectFifo.type.mode = objectFifoObject.value(u"dimensionMode"_s).toString() == u"matrix"_s
                    ? CanvasWire::DimensionMode::Matrix
                    : CanvasWire::DimensionMode::Vector;
                const QJsonObject tapObj = objectFifoObject.value(u"tap"_s).toObject();
                if (!tapObj.isEmpty()) {
                    CanvasWire::TensorTilerConfig tap;
                    tap.tileDims      = tapObj.value(u"tileDims"_s).toString();
                    tap.tileCounts    = tapObj.value(u"tileCounts"_s).toString();
                    tap.pruneStep     = tapObj.value(u"pruneStep"_s).toBool(false);
                    tap.index         = tapObj.value(u"index"_s).toInt(0);
                    tap.patternRepeat = tapObj.value(u"patternRepeat"_s).toString();
                    objectFifo.type.tap = tap;
                }
                wire.objectFifo = objectFifo;
            }

            const QJsonArray routeArray = item.value(u"routeOverride"_s).toArray();
            wire.routeOverride.reserve(routeArray.size());
            for (int routeIndex = 0; routeIndex < routeArray.size(); ++routeIndex) {
                FabricCoord coord{};
                if (!parseFabricCoord(routeArray.at(routeIndex).toObject(), coord)) {
                    errors.push_back(QStringLiteral("items[%1].routeOverride[%2] is invalid.")
                                         .arg(index).arg(routeIndex));
                    continue;
                }
                wire.routeOverride.push_back(coord);
            }

            pendingWires.push_back(std::move(wire));
            continue;
        }

        errors.push_back(QStringLiteral("items[%1]: unknown type '%2'.")
                             .arg(index)
                             .arg(type));
    }

    for (const ParsedWire& parsed : pendingWires) {
        const auto parsedWireId = ObjectId::fromString(parsed.id);
        if (!parsedWireId) {
            errors.push_back(QStringLiteral("Wire id is invalid."));
            continue;
        }

        auto resolveEndpoint = [&](const ParsedWireEndpoint& in, CanvasWire::Endpoint& out) -> bool {
            out.freeScene = in.freeScene;
            if (!in.hasAttached) {
                out.attached = std::nullopt;
                return true;
            }

            const auto blockIt = blockIdMap.find(in.itemId);
            if (blockIt == blockIdMap.end())
                return false;

            const PortId portId = portIdMap.value(makePortKey(in.itemId, in.portId));
            if (portId.isNull())
                return false;

            PortRef ref;
            ref.itemId = blockIt.value();
            ref.portId = portId;
            out.attached = ref;
            return true;
        };

        CanvasWire::Endpoint a;
        CanvasWire::Endpoint b;
        if (!resolveEndpoint(parsed.a, a) || !resolveEndpoint(parsed.b, b)) {
            errors.push_back(QStringLiteral("Wire endpoint references a missing block or port."));
            continue;
        }

        auto wire = std::make_unique<CanvasWire>(a, b);
        wire->setId(*parsedWireId);
        wire->setArrowPolicy(parsed.arrowPolicy);
        if (parsed.hasColorOverride)
            wire->setColorOverride(parsed.colorOverride);
        if (parsed.objectFifo.has_value())
            wire->setObjectFifo(*parsed.objectFifo);
        if (!parsed.routeOverride.isEmpty()) {
            std::vector<FabricCoord> route;
            route.reserve(static_cast<size_t>(parsed.routeOverride.size()));
            for (const FabricCoord& coord : parsed.routeOverride)
                route.push_back(coord);
            wire->setRouteOverride(std::move(route));
        }

        if (!document.insertItem(document.items().size(), std::move(wire)))
            errors.push_back(QStringLiteral("Failed to insert wire into document."));
    }

    if (!errors.isEmpty())
        return Utils::Result::failure(errors.join("\n"));

    if (view) {
        view->setZoom(zoom);
        view->setPan(pan);
    }
    return Utils::Result::success();
}

} // namespace Canvas::Internal
