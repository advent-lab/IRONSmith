// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/CanvasCommands.hpp"

#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasItem.hpp"
#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasWire.hpp"

#include <algorithm>
#include <utility>

#include "canvas/utils/CanvasPortUsage.hpp"

namespace Canvas {

namespace {

void cleanupOrphanDynamicPorts(CanvasDocument& doc,
                               const CanvasWire& wire,
                               std::vector<SavedPort>& savedPorts,
                               std::optional<PortRef> ignore = std::nullopt)
{
    auto tryRemove = [&](const CanvasWire::Endpoint& endpoint) {
        if (!endpoint.attached)
            return;
        const auto ref = *endpoint.attached;
        if (ignore && ignore->itemId == ref.itemId && ignore->portId == ref.portId)
            return;
        CanvasPort meta;
        if (!doc.getPort(ref.itemId, ref.portId, meta))
            return;
        if (meta.role != PortRole::Dynamic)
            return;
        if (Support::countPortAttachments(doc, ref.itemId, ref.portId) != 0)
            return;
        auto* block = dynamic_cast<CanvasBlock*>(doc.findItem(ref.itemId));
        if (!block)
            return;
        size_t idx = 0;
        auto removed = block->removePort(ref.portId, &idx);
        if (removed)
            savedPorts.push_back(SavedPort{ref.itemId, idx, *removed});
    };

    tryRemove(wire.a());
    tryRemove(wire.b());
}

} // namespace

MoveItemCommand::MoveItemCommand(ObjectId itemId, QPointF fromTopLeftScene, QPointF toTopLeftScene)
    : m_itemId(itemId)
    , m_from(fromTopLeftScene)
    , m_to(toTopLeftScene)
{}

QString MoveItemCommand::name() const
{
    return QStringLiteral("Move Item");
}

bool MoveItemCommand::apply(CanvasDocument& doc)
{
    return doc.setItemTopLeft(m_itemId, m_to);
}

bool MoveItemCommand::revert(CanvasDocument& doc)
{
    return doc.setItemTopLeft(m_itemId, m_from);
}

DeleteItemCommand::DeleteItemCommand(ObjectId itemId)
    : m_itemId(itemId)
{}

QString DeleteItemCommand::name() const
{
    return QStringLiteral("Delete Item");
}

bool DeleteItemCommand::apply(CanvasDocument& doc)
{
    if (!m_itemId)
        return false;

    if (!m_initialized) {
        CanvasItem* item = doc.findItem(m_itemId);
        if (!item)
            return false;

        if (auto* block = dynamic_cast<CanvasBlock*>(item)) {
            if (!block->isDeletable())
                return false;

            if (block->isLinkHub()) {
                std::vector<ObjectId> wireIds;
                for (const auto& it : doc.items()) {
                    auto* wire = dynamic_cast<CanvasWire*>(it.get());
                    if (wire && wire->attachesTo(m_itemId))
                        wireIds.push_back(wire->id());
                }

                for (const auto& id : wireIds) {
                    auto removed = doc.removeItem(id);
                    if (removed) {
                        if (auto* wire = dynamic_cast<CanvasWire*>(removed->item.get())) {
                            cleanupOrphanDynamicPorts(doc, *wire, m_savedPorts);
                        }
                        m_savedItems.push_back(SavedItem{id, removed->index, std::move(removed->item)});
                    }
                }
            }
        }

        auto removed = doc.removeItem(m_itemId);
        if (!removed)
            return false;
        if (auto* wire = dynamic_cast<CanvasWire*>(removed->item.get())) {
            cleanupOrphanDynamicPorts(doc, *wire, m_savedPorts);
        }
        m_savedItems.push_back(SavedItem{m_itemId, removed->index, std::move(removed->item)});
        m_initialized = true;
        if (!m_savedPorts.empty())
            doc.notifyChanged();
        return true;
    }

    for (const auto& saved : m_savedItems) {
        auto removed = doc.removeItem(saved.id);
        if (!removed)
            return false;
    }
    for (const auto& saved : m_savedPorts) {
        if (auto* block = dynamic_cast<CanvasBlock*>(doc.findItem(saved.itemId)))
            block->removePort(saved.port.id);
    }
    if (!m_savedPorts.empty())
        doc.notifyChanged();
    return true;
}

bool DeleteItemCommand::revert(CanvasDocument& doc)
{
    if (m_savedItems.empty())
        return false;

    std::vector<const SavedItem*> order;
    order.reserve(m_savedItems.size());
    for (const auto& saved : m_savedItems)
        order.push_back(&saved);

    std::sort(order.begin(), order.end(),
              [](const SavedItem* a, const SavedItem* b) { return a->index < b->index; });

    bool ok = true;
    for (const auto* saved : order) {
        auto copy = saved->item->clone();
        ok = doc.insertItem(saved->index, std::move(copy)) && ok;
    }
    for (const auto& saved : m_savedPorts) {
        if (auto* block = dynamic_cast<CanvasBlock*>(doc.findItem(saved.itemId))) {
            block->insertPort(saved.index, saved.port);
        } else {
            ok = false;
        }
    }
    return ok;
}

CreateItemCommand::CreateItemCommand(std::unique_ptr<CanvasItem> item)
    : m_item(std::move(item))
{
    if (m_item)
        m_itemId = m_item->id();
}

QString CreateItemCommand::name() const
{
    return QStringLiteral("Create Item");
}

bool CreateItemCommand::apply(CanvasDocument& doc)
{
    if (!m_item)
        return false;

    if (!m_hasIndex) {
        m_index = doc.items().size();
        m_hasIndex = true;
    }

    auto tmp = std::move(m_item);
    if (!doc.insertItem(m_index, std::move(tmp))) {
        m_item = std::move(tmp);
        return false;
    }
    return true;
}

bool CreateItemCommand::revert(CanvasDocument& doc)
{
    auto removed = doc.removeItem(m_itemId);
    if (!removed)
        return false;
    m_index = removed->index;
    m_item = std::move(removed->item);
    return true;
}

DeletePortCommand::DeletePortCommand(ObjectId itemId, PortId portId)
    : m_itemId(itemId)
    , m_portId(portId)
{}

QString DeletePortCommand::name() const
{
    return QStringLiteral("Delete Port");
}

bool DeletePortCommand::apply(CanvasDocument& doc)
{
    if (!m_itemId || !m_portId)
        return false;

    auto* item = doc.findItem(m_itemId);
    auto* block = dynamic_cast<CanvasBlock*>(item);
    if (!block || !block->hasPorts())
        return false;

    if (!m_initialized) {
        std::vector<ObjectId> wireIds;
        for (const auto& it : doc.items()) {
            auto* wire = dynamic_cast<CanvasWire*>(it.get());
            if (!wire)
                continue;
            const auto& a = wire->a();
            const auto& b = wire->b();
            const bool hitsA = a.attached.has_value() &&
                               a.attached->itemId == m_itemId &&
                               a.attached->portId == m_portId;
            const bool hitsB = b.attached.has_value() &&
                               b.attached->itemId == m_itemId &&
                               b.attached->portId == m_portId;
            if (hitsA || hitsB)
                wireIds.push_back(wire->id());
        }

        for (const auto& id : wireIds) {
            auto removed = doc.removeItem(id);
            if (removed) {
                if (auto* wire = dynamic_cast<CanvasWire*>(removed->item.get())) {
                    cleanupOrphanDynamicPorts(doc, *wire, m_savedOrphanPorts, PortRef{m_itemId, m_portId});
                }
                m_savedWires.push_back(SavedWire{id, removed->index, std::move(removed->item)});
            }
        }

        m_savedPort = block->removePort(m_portId, &m_portIndex);
        if (!m_savedPort.has_value())
            return false;

        m_initialized = true;
        doc.notifyChanged();
        return true;
    }

    for (const auto& saved : m_savedWires) {
        auto removed = doc.removeItem(saved.id);
        if (!removed)
            return false;
    }

    if (!block->removePort(m_portId))
        return false;

    for (const auto& saved : m_savedOrphanPorts) {
        if (auto* orphanBlock = dynamic_cast<CanvasBlock*>(doc.findItem(saved.itemId))) {
            orphanBlock->removePort(saved.port.id);
        }
    }

    doc.notifyChanged();
    return true;
}

bool DeletePortCommand::revert(CanvasDocument& doc)
{
    if (!m_savedPort.has_value())
        return false;

    auto* item = doc.findItem(m_itemId);
    auto* block = dynamic_cast<CanvasBlock*>(item);
    if (!block)
        return false;

    block->insertPort(m_portIndex, *m_savedPort);

    for (const auto& saved : m_savedOrphanPorts) {
        if (auto* orphanBlock = dynamic_cast<CanvasBlock*>(doc.findItem(saved.itemId))) {
            orphanBlock->insertPort(saved.index, saved.port);
        } else {
            return false;
        }
    }

    std::vector<const SavedWire*> order;
    order.reserve(m_savedWires.size());
    for (const auto& saved : m_savedWires)
        order.push_back(&saved);

    std::sort(order.begin(), order.end(),
              [](const SavedWire* a, const SavedWire* b) { return a->index < b->index; });

    bool ok = true;
    for (const auto* saved : order) {
        auto copy = saved->item->clone();
        ok = doc.insertItem(saved->index, std::move(copy)) && ok;
    }

    doc.notifyChanged();
    return ok;
}

CompositeCommand::CompositeCommand(QString name)
    : m_name(std::move(name))
{}

void CompositeCommand::add(std::unique_ptr<CanvasCommand> cmd)
{
    if (cmd)
        m_commands.push_back(std::move(cmd));
}

QString CompositeCommand::name() const
{
    return m_name;
}

bool CompositeCommand::apply(CanvasDocument& doc)
{
    if (m_commands.empty())
        return false;

    size_t applied = 0;
    for (auto& cmd : m_commands) {
        if (!cmd || !cmd->apply(doc)) {
            for (size_t i = applied; i > 0; --i) {
                auto& revertCmd = m_commands[i - 1];
                if (revertCmd)
                    revertCmd->revert(doc);
            }
            return false;
        }
        ++applied;
    }
    return true;
}

bool CompositeCommand::revert(CanvasDocument& doc)
{
    if (m_commands.empty())
        return false;

    bool ok = true;
    for (size_t i = m_commands.size(); i > 0; --i) {
        auto& cmd = m_commands[i - 1];
        if (cmd)
            ok = cmd->revert(doc) && ok;
    }
    return ok;
}

} // namespace Canvas
