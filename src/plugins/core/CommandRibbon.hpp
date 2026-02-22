// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtCore/QHash>
#include <qnamespace.h>

#include <QAction>

#include <functional>
#include <memory>
#include <vector>

#include "core/CoreGlobal.hpp"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace Core {

enum class CORE_EXPORT RibbonItemKind {
    Action,
    WidgetFactory,
    Separator,
    Stretch
};

struct CORE_EXPORT RibbonItem final
{
    RibbonItemKind kind = RibbonItemKind::Separator;
    QString id;

    QPointer<QAction> action;
    std::function<QWidget*(QWidget* parent)> widgetFactory;

    static RibbonItem makeAction(const QString& id, QAction* a)
    {
        RibbonItem it;
        it.kind = RibbonItemKind::Action;
        it.id = id;
        it.action = a;
        return it;
    }

    static RibbonItem makeWidget(const QString& id, std::function<QWidget*(QWidget*)> f)
    {
        RibbonItem it;
        it.kind = RibbonItemKind::WidgetFactory;
        it.id = id;
        it.widgetFactory = std::move(f);
        return it;
    }

    static RibbonItem makeSeparator(const QString& id = {})
    {
        RibbonItem it;
        it.kind = RibbonItemKind::Separator;
        it.id = id;
        return it;
    }

    static RibbonItem makeStretch(const QString& id = {})
    {
        RibbonItem it;
        it.kind = RibbonItemKind::Stretch;
        it.id = id;
        return it;
    }
};

struct CORE_EXPORT RibbonResult final
{
    bool ok = true;
    QString error;

    static RibbonResult success() { return {}; }
    static RibbonResult failure(QString msg)
    {
        RibbonResult r;
        r.ok = false;
        r.error = std::move(msg);
        return r;
    }

    explicit operator bool() const { return ok; }
};

enum class CORE_EXPORT RibbonControlType {
    Button,
    SplitButton,
    DropDownButton,
    ToggleButton
};

enum class CORE_EXPORT RibbonVisualSize {
    Small,
    Medium,
    Large
};

enum class CORE_EXPORT RibbonIconPlacement {
    AboveText,
    LeftOfText,
    IconOnly,
    TextOnly
};

struct CORE_EXPORT RibbonPresentation final
{
    RibbonVisualSize size = RibbonVisualSize::Small;
    RibbonIconPlacement iconPlacement = RibbonIconPlacement::LeftOfText;
    int iconPx = 0;
    bool showText = true;
};

class CORE_EXPORT RibbonNode final
{
public:
    enum class Kind {
        Row,
        Column,
        LeafCommand,
        LeafWidget,
        Separator,
        Stretch
    };

    static std::unique_ptr<RibbonNode> makeRow(QString id = {});
    static std::unique_ptr<RibbonNode> makeColumn(QString id = {});

    Kind kind() const { return m_kind; }
    const QString& id() const { return m_id; }

    const std::vector<std::unique_ptr<RibbonNode>>& children() const { return m_children; }

    RibbonNode& addRow(QString id = {});
    RibbonNode& addColumn(QString id = {});

    RibbonNode& addCommand(QString itemId,
                           QAction* action,
                           RibbonControlType type = RibbonControlType::Button,
                           RibbonPresentation pres = {});

    RibbonNode& addWidget(QString itemId,
                          std::function<QWidget*(QWidget* parent)> factory);

    RibbonNode& addSeparator(QString itemId = {});
    RibbonNode& addStretch(QString itemId = {});

    QAction* action() const { return m_action; }
    QAction* itemIdRecursive(const QString& itemId) const;
    RibbonControlType controlType() const { return m_controlType; }
    const RibbonPresentation& presentation() const { return m_presentation; }
    const std::function<QWidget*(QWidget*)>& widgetFactory() const { return m_widgetFactory; }

    void setPresentation(RibbonPresentation p) { m_presentation = std::move(p); }

    bool containsItemIdRecursive(const QString& itemId) const;
    bool removeItemIdRecursive(const QString& itemId);

private:
    RibbonNode(Kind k, QString id);

    bool isLayout() const { return m_kind == Kind::Row || m_kind == Kind::Column; }

    Kind m_kind;
    QString m_id;

    std::vector<std::unique_ptr<RibbonNode>> m_children;

    QPointer<QAction> m_action;
    RibbonControlType m_controlType = RibbonControlType::Button;
    RibbonPresentation m_presentation;

    std::function<QWidget*(QWidget*)> m_widgetFactory;
};

class CORE_EXPORT CommandRibbonGroup final : public QObject
{
    Q_OBJECT

public:
    explicit CommandRibbonGroup(QString id, QString title, QObject* parent = nullptr);

    const QString& id() const { return m_id; }
    const QString& title() const { return m_title; }
    void setTitle(QString title);

    RibbonNode& layoutRoot();
    const RibbonNode& layoutRoot() const;

    RibbonResult setLayout(std::unique_ptr<RibbonNode> root);

    QVector<RibbonItem> items() const;

    QAction* actionById(const QString& id) const;
    RibbonResult addAction(const QString& itemId, QAction* action,
                           RibbonControlType type = RibbonControlType::Button,
                           RibbonPresentation pres = {});

    RibbonResult addWidget(const QString& itemId, std::function<QWidget*(QWidget*)> factory);

    RibbonResult addSeparator(const QString& itemId = {});
    RibbonResult addStretch(const QString& itemId = {});

    bool removeItem(const QString& itemId);
    void clearItems();

signals:
    void changed();

private:
    bool itemIdTaken(const QString& itemId) const;

private:
    QString m_id;
    QString m_title;
    std::unique_ptr<RibbonNode> m_root;
};

class CORE_EXPORT CommandRibbonPage final : public QObject
{
    Q_OBJECT

public:
    explicit CommandRibbonPage(QString id, QString title, QObject* parent = nullptr);

    const QString& id() const { return m_id; }
    const QString& title() const { return m_title; }
    void setTitle(QString title);

    QVector<CommandRibbonGroup*> groups() const { return m_groups; }

    CommandRibbonGroup* groupById(const QString& groupId) const;

    RibbonResult addGroup(const QString& groupId, const QString& title, CommandRibbonGroup** outGroup = nullptr);
    CommandRibbonGroup* ensureGroup(const QString& groupId, const QString& title);

    bool removeGroup(const QString& groupId);
    void clearGroups();

signals:
    void changed();

private:
    bool groupIdTaken(const QString& groupId) const;

private:
    QString m_id;
    QString m_title;

    QVector<CommandRibbonGroup*> m_groups;
    QHash<QString, CommandRibbonGroup*> m_groupsById;
};

class CORE_EXPORT CommandRibbon final : public QObject
{
    Q_OBJECT

public:
    explicit CommandRibbon(QObject* parent = nullptr);

    QVector<CommandRibbonPage*> pages() const { return m_pages; }
    CommandRibbonPage* pageById(const QString& pageId) const;

    RibbonResult addPage(const QString& pageId, const QString& title, CommandRibbonPage** outPage = nullptr);
    CommandRibbonPage* ensurePage(const QString& pageId, const QString& title);

    bool removePage(const QString& pageId);
    void clearPages();

    QString activePageId() const { return m_activePageId; }
    RibbonResult setActivePageId(const QString& pageId);
    void beginUpdateBatch();
    void endUpdateBatch();
    bool isInUpdateBatch() const { return m_updateBatchDepth > 0; }

signals:
    void structureChanged();
    void activePageChanged(const QString&);

private:
    static bool isValidId(const QString& id);
    bool pageIdTaken(const QString& pageId) const;
    void notifyStructureChanged();
    void notifyActivePageChanged(const QString& activePageId);

private:
    QVector<CommandRibbonPage*> m_pages;
    QHash<QString, CommandRibbonPage*> m_pagesById;
    QString m_activePageId;
    int m_updateBatchDepth = 0;
    bool m_structureChangePending = false;
    bool m_activePageChangePending = false;
    QString m_pendingActivePageId;
};

} // namespace Core
