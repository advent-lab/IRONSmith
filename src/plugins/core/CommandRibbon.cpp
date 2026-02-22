// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "CommandRibbon.hpp"

#include <QtCore/QDebug>

namespace Core {
RibbonNode::RibbonNode(Kind k, QString id)
    : m_kind(k), m_id(std::move(id))
{
}

std::unique_ptr<RibbonNode> RibbonNode::makeRow(QString id)
{
    return std::unique_ptr<RibbonNode>(new RibbonNode(Kind::Row, std::move(id)));
}

std::unique_ptr<RibbonNode> RibbonNode::makeColumn(QString id)
{
    return std::unique_ptr<RibbonNode>(new RibbonNode(Kind::Column, std::move(id)));
}

RibbonNode& RibbonNode::addRow(QString id)
{
    if (!isLayout())
        return *this;

    m_children.push_back(makeRow(std::move(id)));
    return *m_children.back();
}

RibbonNode& RibbonNode::addColumn(QString id)
{
    if (!isLayout())
        return *this;

    m_children.push_back(makeColumn(std::move(id)));
    return *m_children.back();
}

RibbonNode& RibbonNode::addCommand(QString itemId,
                                   QAction* action,
                                   RibbonControlType type,
                                   RibbonPresentation pres)
{
    if (!isLayout())
        return *this;

    auto n = std::unique_ptr<RibbonNode>(new RibbonNode(Kind::LeafCommand, std::move(itemId)));
    n->m_action = action;
    n->m_controlType = type;
    n->m_presentation = std::move(pres);

    m_children.push_back(std::move(n));
    return *m_children.back();
}

RibbonNode& RibbonNode::addWidget(QString itemId,
                                  std::function<QWidget*(QWidget*)> factory)
{
    if (!isLayout())
        return *this;

    auto n = std::unique_ptr<RibbonNode>(new RibbonNode(Kind::LeafWidget, std::move(itemId)));
    n->m_widgetFactory = std::move(factory);

    m_children.push_back(std::move(n));
    return *m_children.back();
}

RibbonNode& RibbonNode::addSeparator(QString itemId)
{
    if (!isLayout())
        return *this;

    m_children.push_back(std::unique_ptr<RibbonNode>(new RibbonNode(Kind::Separator, std::move(itemId))));
    return *m_children.back();
}

RibbonNode& RibbonNode::addStretch(QString itemId)
{
    if (!isLayout())
        return *this;

    m_children.push_back(std::unique_ptr<RibbonNode>(new RibbonNode(Kind::Stretch, std::move(itemId))));
    return *m_children.back();
}

QAction* RibbonNode::itemIdRecursive(const QString& itemId) const {
    if (itemId.isEmpty())
        return nullptr;

    const bool isLeaf = (m_kind == Kind::LeafCommand
                      || m_kind == Kind::LeafWidget
                      || m_kind == Kind::Separator
                      || m_kind == Kind::Stretch);

    if (isLeaf && m_id == itemId)
        return action();

    if (m_kind == Kind::Row || m_kind == Kind::Column) {
        for (const auto& c : m_children) {
            if (QAction* a = c->itemIdRecursive(itemId))
                return a;
        }
    }

    return nullptr;
}

bool RibbonNode::containsItemIdRecursive(const QString& itemId) const
{
    return itemIdRecursive(itemId) != nullptr;
}

bool RibbonNode::removeItemIdRecursive(const QString& itemId)
{
    if (itemId.isEmpty())
        return false;

    if (m_kind == Kind::Row || m_kind == Kind::Column) {
        for (auto it = m_children.begin(); it != m_children.end(); ++it) {
            RibbonNode& child = *(*it);

            const bool isLeaf = (child.kind() == Kind::LeafCommand
                              || child.kind() == Kind::LeafWidget
                              || child.kind() == Kind::Separator
                              || child.kind() == Kind::Stretch);

            if (isLeaf && child.id() == itemId) {
                m_children.erase(it);
                return true;
            }

            if (child.removeItemIdRecursive(itemId))
                return true;
        }
    }

    return false;
}

CommandRibbonGroup::CommandRibbonGroup(QString id, QString title, QObject* parent)
    : QObject(parent)
    , m_id(std::move(id))
    , m_title(std::move(title))
    , m_root(RibbonNode::makeRow("root"))
{
}

void CommandRibbonGroup::setTitle(QString title)
{
    if (m_title == title)
        return;
    m_title = std::move(title);
    emit changed();
}

RibbonNode& CommandRibbonGroup::layoutRoot()
{
    return *m_root;
}

const RibbonNode& CommandRibbonGroup::layoutRoot() const
{
    return *m_root;
}

RibbonResult CommandRibbonGroup::setLayout(std::unique_ptr<RibbonNode> root)
{
    if (!root)
        return RibbonResult::failure(QString("Ribbon group '%1': layout root is null.").arg(m_id));

    if (root->kind() != RibbonNode::Kind::Row && root->kind() != RibbonNode::Kind::Column)
        return RibbonResult::failure(QString("Ribbon group '%1': layout root must be Row or Column.").arg(m_id));

    m_root = std::move(root);
    emit changed();
    return RibbonResult::success();
}

static void flattenNode(const RibbonNode& n, QVector<RibbonItem>& out)
{
    switch (n.kind()) {
        case RibbonNode::Kind::Row:
        case RibbonNode::Kind::Column:
            for (const auto& c : n.children())
                flattenNode(*c, out);
            break;

        case RibbonNode::Kind::LeafCommand:
            if (n.action())
                out.push_back(RibbonItem::makeAction(n.id(), n.action()));
            break;

        case RibbonNode::Kind::LeafWidget:
            if (n.widgetFactory())
                out.push_back(RibbonItem::makeWidget(n.id(), n.widgetFactory()));
            break;

        case RibbonNode::Kind::Separator:
            out.push_back(RibbonItem::makeSeparator(n.id()));
            break;

        case RibbonNode::Kind::Stretch:
            out.push_back(RibbonItem::makeStretch(n.id()));
            break;
    }
}

QVector<RibbonItem> CommandRibbonGroup::items() const
{
    QVector<RibbonItem> out;
    flattenNode(*m_root, out);
    return out;
}

bool CommandRibbonGroup::itemIdTaken(const QString& itemId) const
{
    return m_root ? m_root->containsItemIdRecursive(itemId) : false;
}

QAction* CommandRibbonGroup::actionById(const QString& itemId) const {
    if (!itemId.isEmpty() && !itemIdTaken(itemId)) {
        qCWarning(corelog) << "Ribbon group '" << m_id << "' has no action '" << itemId;
        return nullptr;
    }

    return m_root->itemIdRecursive(itemId);
}

RibbonResult CommandRibbonGroup::addAction(const QString& itemId,
                                          QAction* action,
                                          RibbonControlType type,
                                          RibbonPresentation pres)
{
    if (!action)
        return RibbonResult::failure("Ribbon group: action is null.");
    if (!itemId.isEmpty() && itemIdTaken(itemId))
        return RibbonResult::failure(QString("Ribbon group '%1': duplicate item id '%2'.").arg(m_id, itemId));

    m_root->addCommand(itemId, action, type, std::move(pres));
    emit changed();
    return RibbonResult::success();
}

RibbonResult CommandRibbonGroup::addWidget(const QString& itemId, std::function<QWidget*(QWidget*)> factory)
{
    if (!factory)
        return RibbonResult::failure("Ribbon group: widget factory is empty.");
    if (!itemId.isEmpty() && itemIdTaken(itemId))
        return RibbonResult::failure(QString("Ribbon group '%1': duplicate item id '%2'.").arg(m_id, itemId));

    m_root->addWidget(itemId, std::move(factory));
    emit changed();
    return RibbonResult::success();
}

RibbonResult CommandRibbonGroup::addSeparator(const QString& itemId)
{
    if (!itemId.isEmpty() && itemIdTaken(itemId))
        return RibbonResult::failure(QString("Ribbon group '%1': duplicate item id '%2'.").arg(m_id, itemId));

    m_root->addSeparator(itemId);
    emit changed();
    return RibbonResult::success();
}

RibbonResult CommandRibbonGroup::addStretch(const QString& itemId)
{
    if (!itemId.isEmpty() && itemIdTaken(itemId))
        return RibbonResult::failure(QString("Ribbon group '%1': duplicate item id '%2'.").arg(m_id, itemId));

    m_root->addStretch(itemId);
    emit changed();
    return RibbonResult::success();
}

bool CommandRibbonGroup::removeItem(const QString& itemId)
{
    if (!m_root || itemId.isEmpty())
        return false;

    const bool removed = m_root->removeItemIdRecursive(itemId);
    if (removed)
        emit changed();
    return removed;
}

void CommandRibbonGroup::clearItems()
{
    m_root = RibbonNode::makeRow("root");
    emit changed();
}

CommandRibbonPage::CommandRibbonPage(QString id, QString title, QObject* parent)
    : QObject(parent), m_id(std::move(id)), m_title(std::move(title))
{
}

void CommandRibbonPage::setTitle(QString title)
{
    if (m_title == title)
        return;
    m_title = std::move(title);
    emit changed();
}

bool CommandRibbonPage::groupIdTaken(const QString& groupId) const
{
    return m_groupsById.contains(groupId);
}

CommandRibbonGroup* CommandRibbonPage::groupById(const QString& groupId) const
{
    return m_groupsById.value(groupId, nullptr);
}

RibbonResult CommandRibbonPage::addGroup(const QString& groupId, const QString& title, CommandRibbonGroup** outGroup)
{
    if (groupId.isEmpty())
        return RibbonResult::failure(QString("Ribbon page '%1': group id is empty.").arg(m_id));
    if (groupIdTaken(groupId))
        return RibbonResult::failure(QString("Ribbon page '%1': duplicate group id '%2'.").arg(m_id, groupId));

    auto* g = new CommandRibbonGroup(groupId, title, this);
    connect(g, &CommandRibbonGroup::changed, this, &CommandRibbonPage::changed, Qt::UniqueConnection);
    m_groups.push_back(g);
    m_groupsById.insert(groupId, g);

    if (outGroup)
        *outGroup = g;

    emit changed();
    return RibbonResult::success();
}

CommandRibbonGroup* CommandRibbonPage::ensureGroup(const QString& groupId, const QString& title)
{
    if (groupId.isEmpty())
        return nullptr;

    if (auto* g = groupById(groupId)) {
        if (!title.isEmpty())
            g->setTitle(title);
        return g;
    }

    CommandRibbonGroup* g = nullptr;
    const auto res = addGroup(groupId, title, &g);
    if (!res)
        return nullptr;
    return g;
}

bool CommandRibbonPage::removeGroup(const QString& groupId)
{
    auto* g = m_groupsById.value(groupId, nullptr);
    if (!g)
        return false;

    m_groupsById.remove(groupId);
    m_groups.removeOne(g);
    g->deleteLater();
    emit changed();
    return true;
}

void CommandRibbonPage::clearGroups()
{
    if (m_groups.isEmpty())
        return;

    for (auto* g : m_groups)
        g->deleteLater();

    m_groups.clear();
    m_groupsById.clear();
    emit changed();
}

CommandRibbon::CommandRibbon(QObject* parent)
    : QObject(parent)
{
}

void CommandRibbon::beginUpdateBatch()
{
    ++m_updateBatchDepth;
}

void CommandRibbon::endUpdateBatch()
{
    if (m_updateBatchDepth <= 0) {
        qCWarning(corelog) << "Ribbon: endUpdateBatch called without matching beginUpdateBatch.";
        m_updateBatchDepth = 0;
        return;
    }

    --m_updateBatchDepth;
    if (m_updateBatchDepth > 0)
        return;

    if (m_structureChangePending) {
        m_structureChangePending = false;
        emit structureChanged();
    }

    if (m_activePageChangePending) {
        m_activePageChangePending = false;
        emit activePageChanged(m_pendingActivePageId);
    }
}

bool CommandRibbon::isValidId(const QString& id)
{
    if (id.isEmpty())
        return false;
    for (QChar c : id) {
        if (!(c.isLetterOrNumber() || c == '_' || c == '-' || c == '.'))
            return false;
    }
    return true;
}

bool CommandRibbon::pageIdTaken(const QString& pageId) const
{
    return m_pagesById.contains(pageId);
}

void CommandRibbon::notifyStructureChanged()
{
    if (isInUpdateBatch()) {
        m_structureChangePending = true;
        return;
    }

    emit structureChanged();
}

void CommandRibbon::notifyActivePageChanged(const QString& activePageId)
{
    if (isInUpdateBatch()) {
        m_activePageChangePending = true;
        m_pendingActivePageId = activePageId;
        return;
    }

    emit activePageChanged(activePageId);
}

CommandRibbonPage* CommandRibbon::pageById(const QString& pageId) const
{
    return m_pagesById.value(pageId, nullptr);
}

RibbonResult CommandRibbon::addPage(const QString& pageId, const QString& title, CommandRibbonPage** outPage)
{
    if (!isValidId(pageId))
        return RibbonResult::failure(QString("Ribbon: invalid page id '%1'.").arg(pageId));
    if (pageIdTaken(pageId))
        return RibbonResult::failure(QString("Ribbon: duplicate page id '%1'.").arg(pageId));

    auto* p = new CommandRibbonPage(pageId, title, this);
    connect(p, &CommandRibbonPage::changed, this, &CommandRibbon::notifyStructureChanged, Qt::UniqueConnection);
    m_pages.push_back(p);
    m_pagesById.insert(pageId, p);

    if (outPage)
        *outPage = p;

    notifyStructureChanged();

    if (m_activePageId.isEmpty()) {
        m_activePageId = pageId;
        notifyActivePageChanged(m_activePageId);
    }

    return RibbonResult::success();
}

CommandRibbonPage* CommandRibbon::ensurePage(const QString& pageId, const QString& title)
{
    if (!isValidId(pageId))
        return nullptr;

    if (auto* p = pageById(pageId)) {
        if (!title.isEmpty())
            p->setTitle(title);
        return p;
    }

    CommandRibbonPage* p = nullptr;
    const auto res = addPage(pageId, title, &p);
    if (!res)
        return nullptr;
    return p;
}

bool CommandRibbon::removePage(const QString& pageId)
{
    auto* p = m_pagesById.value(pageId, nullptr);
    if (!p)
        return false;

    const bool removingActive = (m_activePageId == pageId);

    m_pagesById.remove(pageId);
    m_pages.removeOne(p);
    p->deleteLater();

    notifyStructureChanged();

    if (removingActive) {
        if (!m_pages.isEmpty()) {
            m_activePageId = m_pages.front()->id();
            notifyActivePageChanged(m_activePageId);
        } else {
            m_activePageId.clear();
            notifyActivePageChanged(QString());
        }
    }

    return true;
}

void CommandRibbon::clearPages()
{
    if (m_pages.isEmpty())
        return;

    for (auto* p : m_pages)
        p->deleteLater();

    m_pages.clear();
    m_pagesById.clear();

    notifyStructureChanged();

    if (!m_activePageId.isEmpty()) {
        m_activePageId.clear();
        notifyActivePageChanged(QString());
    }
}

RibbonResult CommandRibbon::setActivePageId(const QString& pageId)
{
    if (pageId.isEmpty())
        return RibbonResult::failure("Ribbon: active page id is empty.");
    if (!m_pagesById.contains(pageId))
        return RibbonResult::failure(QString("Ribbon: unknown page id '%1'.").arg(pageId));
    if (m_activePageId == pageId)
        return RibbonResult::success();

    m_activePageId = pageId;
    notifyActivePageChanged(m_activePageId);
    return RibbonResult::success();
}

} // namespace Core
