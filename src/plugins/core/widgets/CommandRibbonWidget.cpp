// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/widgets/CommandRibbonWidget.hpp"

#include <QtCore/QtGlobal>
#include <QtCore/QMetaObject>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QStyle>

#include "core/CommandRibbon.hpp"
#include "core/ui/UiStyle.hpp"

namespace Core {

namespace {
class RibbonCommandTileWidget final : public QWidget
{
public:
    explicit RibbonCommandTileWidget(QAction* action, RibbonControlType control, QWidget* parent = nullptr)
        : QWidget(parent), m_action(action), m_control(control)
    {
        setObjectName("RibbonCommandTile");
        setAttribute(Qt::WA_StyledBackground, true);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        m_icon = new QLabel(this);
        m_icon->setObjectName("RibbonCommandIcon");
        m_icon->setAlignment(Qt::AlignCenter);
        m_icon->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        m_caption = new QLabel(this);
        m_caption->setObjectName("RibbonCommandCaption");
        m_caption->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        m_caption->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        v->addWidget(m_icon, 1);
        v->addWidget(m_caption, 0);

        if (m_action) {
            QObject::connect(m_action, &QAction::changed, this, [this] { syncFromAction(); });
            syncFromAction();
        }
    }

    void setIconSize(QSize sz) { m_iconSize = sz; syncFromAction(); }

protected:
    void enterEvent(QEnterEvent*) override { setProperty("ribbonHover", true); repolish(); }
    void leaveEvent(QEvent*) override { setProperty("ribbonHover", false); setProperty("ribbonPressed", false); repolish(); }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && isEnabled()) {
            setProperty("ribbonPressed", true);
            repolish();
        }
        QWidget::mousePressEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        const bool inside = rect().contains(e->position().toPoint());

        if (e->button() == Qt::LeftButton) {
            setProperty("ribbonPressed", false);
            repolish();

            if (inside && isEnabled() && m_action) {
                if (m_control == RibbonControlType::DropDownButton) {
                    if (QMenu* menu = m_action->menu()) {
                        menu->popup(mapToGlobal(QPoint(0, height())));
                        return;
                    }
                }

                m_action->trigger();
            }
        } else if (e->button() == Qt::RightButton) {
            if (inside && isEnabled() && m_action && m_control == RibbonControlType::SplitButton) {
                if (QMenu* menu = m_action->menu())
                    menu->popup(mapToGlobal(QPoint(0, height())));
            }
        }

        QWidget::mouseReleaseEvent(e);
    }

private:
    void syncFromAction()
    {
        if (!m_action)
            return;

        setEnabled(m_action->isEnabled());
        setProperty("ribbonChecked", m_action->isCheckable() && m_action->isChecked());

        QString text = m_action->text();
        if (m_action->menu() && m_control == RibbonControlType::DropDownButton)
            text += " \u25BE";
        m_caption->setText(text);

        const QString tip = m_action->toolTip().trimmed().isEmpty()
            ? m_action->text()
            : m_action->toolTip();
        setToolTip(tip);
        m_icon->setToolTip(tip);
        m_caption->setToolTip(tip);

        if (!m_action->icon().isNull()) {
            const QSize logical = m_iconSize.isValid() ? m_iconSize : QSize(24, 24);
            const qreal dpr = m_icon->devicePixelRatioF();
            const QIcon::Mode mode = isEnabled() ? QIcon::Normal : QIcon::Disabled;
            const QIcon::State state = (m_action->isCheckable() && m_action->isChecked()) ? QIcon::On : QIcon::Off;
            const QPixmap pm = m_action->icon().pixmap(logical, dpr, mode, state);
            m_icon->setPixmap(pm);
        } else {
            m_icon->setPixmap(QPixmap());
        }

        repolish();
    }

    void repolish()
    {
        style()->unpolish(this);
        style()->polish(this);
        update();
    }

    QAction* m_action = nullptr;
    RibbonControlType m_control = RibbonControlType::Button;
    QLabel* m_icon = nullptr;
    QLabel* m_caption = nullptr;
    QSize m_iconSize;
};

QSize tileMinSizeFor(const RibbonPresentation& pres)
{
    switch (pres.size) {
        case RibbonVisualSize::Large:
            return QSize(Ui::UiStyle::RibbonCommandLargeMinWidthPx,
                         Ui::UiStyle::RibbonCommandLargeMinHeightPx);
        case RibbonVisualSize::Medium:
            return QSize(Ui::UiStyle::RibbonCommandMediumMinWidthPx,
                         Ui::UiStyle::RibbonCommandMediumMinHeightPx);
        case RibbonVisualSize::Small:
        default:
            return QSize(Ui::UiStyle::RibbonCommandSmallMinWidthPx,
                         Ui::UiStyle::RibbonCommandSmallMinHeightPx);
    }
}
} // namespace

static int defaultIconPxFor(const RibbonPresentation& pres)
{
    if (pres.iconPx > 0)
        return pres.iconPx;

    switch (pres.size) {
        case RibbonVisualSize::Large: return Ui::UiStyle::RibbonIconLargePx;
        case RibbonVisualSize::Medium: return Ui::UiStyle::RibbonIconMediumPx;
        case RibbonVisualSize::Small: return Ui::UiStyle::RibbonIconSmallPx;
        default:                     return Ui::UiStyle::RibbonIconDefaultPx;
    }
}

CommandRibbonWidget::CommandRibbonWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("CommandRibbon");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);
}

void CommandRibbonWidget::setModel(CommandRibbon* model)
{
    if (m_model == model)
        return;

    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);

    m_model = model;

    if (m_model) {
        connect(m_model, &CommandRibbon::structureChanged, this, &CommandRibbonWidget::scheduleRebuild);
        connect(m_model, &CommandRibbon::activePageChanged, this, &CommandRibbonWidget::syncActivePage);
    }

    rebuildAll();
}

void CommandRibbonWidget::scheduleRebuild()
{
    if (m_rebuildScheduled)
        return;

    m_rebuildScheduled = true;
    QMetaObject::invokeMethod(this,
                              [this]() {
                                  m_rebuildScheduled = false;
                                  rebuildAll();
                              },
                              Qt::QueuedConnection);
}

void CommandRibbonWidget::rebuildAll()
{
    m_pageIndex.clear();

    while (m_stack->count() > 0) {
        QWidget* w = m_stack->widget(0);
        m_stack->removeWidget(w);
        delete w;
    }

    if (!m_model)
        return;

    const auto pages = m_model->pages();
    for (auto* page : pages) {
        QWidget* pw = buildPageWidget(page);
        const int idx = m_stack->addWidget(pw);
        m_pageIndex.insert(page->id(), idx);
    }

    syncActivePage();
}

void CommandRibbonWidget::syncActivePage()
{
    if (!m_model)
        return;

    const QString active = m_model->activePageId();
    const auto it = m_pageIndex.find(active);
    if (it == m_pageIndex.end())
        return;

    m_stack->setCurrentIndex(it.value());
}

QWidget* CommandRibbonWidget::buildPageWidget(CommandRibbonPage* page)
{
    auto* pageRoot = new QWidget(m_stack);
    pageRoot->setObjectName("RibbonPage");
    pageRoot->setProperty("ribbonPageId", page->id());
    pageRoot->setAttribute(Qt::WA_StyledBackground, true);

    auto* row = new QHBoxLayout(pageRoot);
    row->setContentsMargins(Ui::UiStyle::RibbonPageHPaddingPx,
                            Ui::UiStyle::RibbonPageVPaddingPx,
                            Ui::UiStyle::RibbonPageHPaddingPx,
                            0);
    row->setSpacing(0);

    const auto groups = page->groups();
    for (int index = 0; index < groups.size(); ++index) {
        auto* group = groups.at(index);
        QWidget* gw = buildGroupWidget(group);
        row->addWidget(gw);

        if (index + 1 < groups.size())
            row->addWidget(buildGroupDividerWidget(pageRoot));
    }

    row->addStretch(1);
    return pageRoot;
}

QWidget* CommandRibbonWidget::buildGroupWidget(CommandRibbonGroup* group)
{
    auto* box = new QFrame(m_stack);
    box->setObjectName("RibbonGroup");
    box->setProperty("ribbonGroupId", group->id());
    box->setFrameShape(QFrame::NoFrame);
    box->setAttribute(Qt::WA_StyledBackground, true);
    box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto* content = new QWidget(box);
    content->setObjectName("RibbonGroupContent");

    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(Ui::UiStyle::RibbonGroupContentHPaddingPx,
                                      Ui::UiStyle::RibbonGroupContentVPaddingPx,
                                      Ui::UiStyle::RibbonGroupContentHPaddingPx,
                                      Ui::UiStyle::RibbonGroupContentVPaddingPx);
    contentLayout->setSpacing(Ui::UiStyle::RibbonLayoutSpacingPx);

    QWidget* tree = buildNodeWidget(group->layoutRoot(), content);
    if (tree)
        contentLayout->addWidget(tree, 1);

    col->addWidget(content, 1);

    auto* title = new QLabel(group->title(), box);
    title->setObjectName("RibbonGroupTitle");
    title->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    title->setMinimumHeight(Ui::UiStyle::RibbonGroupTitleHeightPx);
    title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    col->addWidget(title, 0);

    return box;
}

QWidget* CommandRibbonWidget::buildGroupDividerWidget(QWidget* parent)
{
    auto* host = new QWidget(parent);
    host->setObjectName("RibbonGroupDivider");
    host->setAttribute(Qt::WA_StyledBackground, true);
    host->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    host->setFixedWidth(12);

    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(5, 10, 5, 10);
    layout->setSpacing(0);

    auto* line = new QWidget(host);
    line->setObjectName("RibbonGroupDividerLine");
    line->setAttribute(Qt::WA_StyledBackground, true);
    line->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    line->setFixedWidth(1);

    layout->addWidget(line, 1, Qt::AlignHCenter);
    return host;
}

QWidget* CommandRibbonWidget::buildNodeWidget(const RibbonNode& node, QWidget* parent)
{
    using K = RibbonNode::Kind;

    if (node.kind() == K::LeafCommand)
        return buildLeafCommandWidget(node, parent);

    if (node.kind() == K::LeafWidget)
        return buildLeafWidgetFactoryWidget(node, parent);

    if (node.kind() == K::Separator) {
        return buildSeparatorWidget(Qt::Horizontal, parent);
    }

    if (node.kind() == K::Stretch) {
        return nullptr;
    }

    if (node.kind() != K::Row && node.kind() != K::Column)
        return nullptr;

    auto* container = new QWidget(parent);
    container->setObjectName(QString("RibbonNode_%1").arg(node.id()));

    const bool isRow = (node.kind() == K::Row);
    const Qt::Orientation orientation = isRow ? Qt::Horizontal : Qt::Vertical;

    QBoxLayout* layout = isRow ? static_cast<QBoxLayout*>(new QHBoxLayout(container))
                               : static_cast<QBoxLayout*>(new QVBoxLayout(container));

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Ui::UiStyle::RibbonLayoutSpacingPx);

    for (const auto& childPtr : node.children()) {
        const RibbonNode& child = *childPtr;

        if (child.kind() == K::Stretch) {
            layout->addStretch(1);
            continue;
        }

        if (child.kind() == K::Separator) {
            layout->addWidget(buildSeparatorWidget(orientation, container));
            continue;
        }

        QWidget* w = buildNodeWidget(child, container);
        if (!w)
            continue;

        layout->addWidget(w);
    }

    return container;
}

QWidget* CommandRibbonWidget::buildSeparatorWidget(Qt::Orientation parentLayoutOrientation, QWidget* parent)
{
    auto* sep = new QFrame(parent);
    sep->setObjectName("RibbonSeparator");
    if (parentLayoutOrientation == Qt::Horizontal) {
        sep->setFrameShape(QFrame::VLine);
        sep->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        sep->setFixedWidth(1);
    } else {
        sep->setFrameShape(QFrame::HLine);
        sep->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sep->setFixedHeight(1);
    }
    sep->setFrameShadow(QFrame::Plain);
    return sep;
}

QWidget* CommandRibbonWidget::buildLeafCommandWidget(const RibbonNode& node, QWidget* parent)
{
    QAction* act = node.action();
    if (!act)
        return nullptr;

    if (node.controlType() == RibbonControlType::ToggleButton && !act->isCheckable())
        act->setCheckable(true);

    auto* tile = new RibbonCommandTileWidget(act, node.controlType(), parent);

    tile->setProperty("ribbonCommandId", node.id());
    tile->setProperty("ribbonVisualSize",
                      (node.presentation().size == RibbonVisualSize::Large) ? "large" : "small");

    const RibbonPresentation& pres = node.presentation();
    const int iconPx = defaultIconPxFor(pres);
    tile->setIconSize(QSize(iconPx, iconPx));

    const QSize minSize = tileMinSizeFor(node.presentation());
    tile->setMinimumSize(minSize);

    return tile;
}

QWidget* CommandRibbonWidget::buildLeafWidgetFactoryWidget(const RibbonNode& node, QWidget* parent)
{
    const auto& factory = node.widgetFactory();
    if (!factory)
        return nullptr;

    QWidget* w = factory(parent);
    if (!w)
        return nullptr;

    w->setParent(parent);
    if (w->objectName().isEmpty())
        w->setObjectName(QString("RibbonWidget_%1").arg(node.id()));

    return w;
}

} // namespace Core
