// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/widgets/GlobalMenuBarWidget.hpp"

#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QToolButton>

#include "core/ui/UiObjectNames.hpp"
#include "core/ui/UiStyle.hpp"

namespace Core {

GlobalMenuBarWidget::GlobalMenuBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(UiObjectNames::MenuHost);
    setAttribute(Qt::WA_StyledBackground, true);
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(Ui::UiStyle::MenuBarHMargin, 0,
                                 Ui::UiStyle::MenuBarHMargin, 0);
    m_layout->setSpacing(Ui::UiStyle::MenuBarButtonSpacing);
}

void GlobalMenuBarWidget::setModel(GlobalMenuBar* model)
{
    if (m_model == model)
        return;

    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);

    m_model = model;

    if (m_model) {
        connect(m_model, &GlobalMenuBar::changed, this, &GlobalMenuBarWidget::rebuild);
        connect(m_model, &GlobalMenuBar::activeChanged, this, &GlobalMenuBarWidget::rebuild);
    }

    rebuild();
}

void GlobalMenuBarWidget::clearButtons()
{
    while (auto* item = m_layout->takeAt(0)) {
        if (auto* w = item->widget())
            delete w;
        delete item;
    }
}

void GlobalMenuBarWidget::rebuild()
{
    clearButtons();

    if (!m_model)
        return;

    const QString active = m_model->activeId();

    for (const auto& it : m_model->items()) {
        auto* b = new QToolButton(this);
        b->setObjectName(QStringLiteral("MenuTabButton"));
        b->setProperty("menuTabId", it.id());
        b->setText(it.title());
        b->setAutoRaise(false);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        b->setMinimumHeight(Ui::UiStyle::MenuBarHeightPx - 6);

        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setChecked(!active.isEmpty() && it.id() == active);

        const QString id = it.id();
        connect(b, &QToolButton::clicked, this, [this, id] {
            if (m_model)
                m_model->setActiveId(id);
            emit itemTriggered(id);
        });

        m_layout->addWidget(b);
    }

    m_layout->addStretch(1);
}

} // namespace Core
