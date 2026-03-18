// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "utils/ui/SidebarPanelFrame.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QFrame>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

namespace Utils {

SidebarPanelFrame::SidebarPanelFrame(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SidebarPanelFrame"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    m_headerWidget = new QWidget(this);
    m_headerWidget->setObjectName(QStringLiteral("SidebarPanelHeader"));
    m_headerWidget->setAttribute(Qt::WA_StyledBackground, true);
    m_headerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    auto* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    m_viewButton = new QToolButton(m_headerWidget);
    QFont titleFont = m_viewButton->font();
    titleFont.setWeight(QFont::DemiBold);
    m_viewButton->setFont(titleFont);
    m_viewButton->setAutoRaise(true);
    m_viewButton->setCursor(Qt::PointingHandCursor);
    m_viewButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_viewButton->setPopupMode(QToolButton::InstantPopup);
    m_viewButton->setObjectName(QStringLiteral("SidebarPanelViewButton"));
    m_viewButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    m_viewMenu = new QMenu(m_viewButton);
    m_viewMenu->setObjectName(QStringLiteral("SidebarPanelViewMenu"));
    m_viewMenu->setAttribute(Qt::WA_StyledBackground, true);
    m_viewButton->setMenu(m_viewMenu);

    m_subtitleLabel = new QLabel(m_headerWidget);
    QFont subtitleFont = m_subtitleLabel->font();
    subtitleFont.setPointSize(std::max(8, subtitleFont.pointSize() - 1));
    m_subtitleLabel->setFont(subtitleFont);
    m_subtitleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_subtitleLabel->setObjectName(QStringLiteral("SidebarPanelSubtitle"));

    textLayout->addWidget(m_viewButton);
    textLayout->addWidget(m_subtitleLabel);

    m_actionLayout = new QHBoxLayout();
    m_actionLayout->setContentsMargins(0, 0, 0, 0);
    m_actionLayout->setSpacing(4);

    headerLayout->addLayout(textLayout, 0);
    headerLayout->setAlignment(textLayout, Qt::AlignLeft | Qt::AlignVCenter);
    headerLayout->addStretch(1);
    headerLayout->addLayout(m_actionLayout);

    rootLayout->addWidget(m_headerWidget);

    m_headerDivider = new QFrame(this);
    m_headerDivider->setFrameShape(QFrame::HLine);
    m_headerDivider->setFrameShadow(QFrame::Sunken);
    m_headerDivider->setObjectName(QStringLiteral("SidebarPanelHeaderDivider"));
    rootLayout->addWidget(m_headerDivider);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->setObjectName(QStringLiteral("SidebarPanelSearch"));
    rootLayout->addWidget(m_search);

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("SidebarPanelContent"));
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    rootLayout->addWidget(m_content, 1);

    connect(m_search, &QLineEdit::textChanged, this, &SidebarPanelFrame::handleSearchTextChanged);
    connect(m_viewMenu, &QMenu::triggered, this, &SidebarPanelFrame::handleViewTriggered);

    updateHeader();
    updateSearchVisibility();
}

QString SidebarPanelFrame::title() const
{
    return m_title;
}

void SidebarPanelFrame::setTitle(const QString& title)
{
    const QString cleaned = title.trimmed();
    if (cleaned == m_title)
        return;
    m_title = cleaned;
    updateHeader();
    emit titleChanged(m_title);
}

QStringList SidebarPanelFrame::viewOptions() const
{
    return m_viewOptions;
}

void SidebarPanelFrame::setViewOptions(const QStringList& options)
{
    if (m_viewOptions == options)
        return;

    m_viewOptions = options;
    m_viewMenu->clear();

    for (const QString& entry : m_viewOptions) {
        if (entry.trimmed().isEmpty())
            continue;
        auto* action = m_viewMenu->addAction(entry);
        action->setCheckable(true);
        action->setData(entry);
        action->setChecked(entry == m_title);
    }

    m_viewButton->setEnabled(!m_viewOptions.isEmpty());
    emit viewOptionsChanged(m_viewOptions);
}

QString SidebarPanelFrame::subtitle() const
{
    return m_subtitle;
}

void SidebarPanelFrame::setSubtitle(const QString& subtitle)
{
    const QString cleaned = subtitle.trimmed();
    if (cleaned == m_subtitle)
        return;
    m_subtitle = cleaned;
    updateHeader();
    emit subtitleChanged(m_subtitle);
}

bool SidebarPanelFrame::searchEnabled() const
{
    return m_searchEnabled;
}

void SidebarPanelFrame::setSearchEnabled(bool enabled)
{
    if (m_searchEnabled == enabled)
        return;
    m_searchEnabled = enabled;
    updateSearchVisibility();
    emit searchEnabledChanged(m_searchEnabled);
}

QString SidebarPanelFrame::searchText() const
{
    return m_search->text();
}

void SidebarPanelFrame::setSearchText(const QString& text)
{
    if (m_search->text() == text)
        return;
    m_blockSearchSignal = true;
    m_search->setText(text);
    m_blockSearchSignal = false;
    emit searchTextChanged(text);
}

QString SidebarPanelFrame::searchPlaceholder() const
{
    return m_search->placeholderText();
}

void SidebarPanelFrame::setSearchPlaceholder(const QString& text)
{
    if (m_search->placeholderText() == text)
        return;
    m_search->setPlaceholderText(text);
    emit searchPlaceholderChanged(text);
}

bool SidebarPanelFrame::headerDividerVisible() const
{
    return m_headerDividerVisible;
}

void SidebarPanelFrame::setHeaderDividerVisible(bool visible)
{
    if (m_headerDividerVisible == visible)
        return;
    m_headerDividerVisible = visible;
    if (m_headerDivider)
        m_headerDivider->setVisible(visible);
    emit headerDividerVisibleChanged(m_headerDividerVisible);
}

void SidebarPanelFrame::setContentWidget(QWidget* widget)
{
    if (widget == m_content)
        return;

    if (m_content) {
        if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout()))
            rootLayout->removeWidget(m_content);
        m_content->setParent(nullptr);
    }

    m_content = widget ? widget : new QWidget(this);
    m_content->setParent(this);

    if (widget) {
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout()))
        rootLayout->addWidget(m_content, 1);
}

QWidget* SidebarPanelFrame::contentWidget() const
{
    return m_content;
}

QLineEdit* SidebarPanelFrame::searchField() const
{
    return m_search;
}

void SidebarPanelFrame::addAction(const QString& id, const QIcon& icon, const QString& tooltip)
{
    if (id.trimmed().isEmpty())
        return;
    if (m_actions.contains(id))
        return;

    auto* button = new QToolButton(m_headerWidget);
    button->setIcon(icon);
    button->setAutoRaise(true);
    button->setToolTip(tooltip);
    button->setProperty("actionId", id);
    button->setObjectName(QStringLiteral("SidebarPanelActionButton"));

    connect(button, &QToolButton::clicked, this, &SidebarPanelFrame::handleAction);

    m_actionLayout->addWidget(button);
    m_actions.insert(id, button);
}

QToolButton* SidebarPanelFrame::actionButton(const QString& id) const
{
    return m_actions.value(id, nullptr);
}

void SidebarPanelFrame::setActionVisible(const QString& id, bool visible)
{
    auto it = m_actions.find(id);
    if (it == m_actions.end())
        return;
    it.value()->setVisible(visible);
}

void SidebarPanelFrame::clearActions()
{
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it) {
        if (auto* button = it.value()) {
            m_actionLayout->removeWidget(button);
            button->deleteLater();
        }
    }
    m_actions.clear();
}

void SidebarPanelFrame::handleAction()
{
    auto* button = qobject_cast<QToolButton*>(sender());
    if (!button)
        return;
    const QString id = button->property("actionId").toString();
    if (!id.isEmpty())
        emit actionTriggered(id);
}

void SidebarPanelFrame::handleSearchTextChanged(const QString& text)
{
    if (m_blockSearchSignal)
        return;
    emit searchTextChanged(text);
}

void SidebarPanelFrame::handleViewTriggered(QAction* action)
{
    if (!action)
        return;
    const QString name = action->data().toString();
    if (name.isEmpty())
        return;

    if (name != m_title)
        setTitle(name);
    emit viewSelected(name);
}

void SidebarPanelFrame::updateHeader()
{
    const QString titleText = m_title.isEmpty() ? QStringLiteral("Panel") : m_title;
    if (m_viewButton)
        m_viewButton->setText(titleText);

    if (m_subtitle.isEmpty()) {
        m_subtitleLabel->clear();
        m_subtitleLabel->setVisible(false);
    } else {
        const QFontMetrics fm(m_subtitleLabel->font());
        const QString elided = fm.elidedText(m_subtitle, Qt::ElideMiddle, 260);
        m_subtitleLabel->setText(elided);
        m_subtitleLabel->setToolTip(m_subtitle);
        m_subtitleLabel->setVisible(true);
    }

    if (m_viewMenu) {
        for (auto* action : m_viewMenu->actions())
            action->setChecked(action->data().toString() == m_title);
    }
}

void SidebarPanelFrame::updateSearchVisibility()
{
    m_search->setVisible(m_searchEnabled);
}

} // namespace Utils
