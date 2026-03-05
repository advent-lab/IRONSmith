// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/widgets/ToolRailWidget.hpp"

#include "core/SidebarModel.hpp"

#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QWidget>

namespace Core {

ToolRailWidget::ToolRailWidget(SidebarModel* model, SidebarSide side, SidebarFamily family, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
    , m_side(side)
    , m_family(family)
{
    Q_ASSERT(m_model);

    setObjectName("ToolRail");
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* topHost = new QWidget(this);
    topHost->setObjectName("ToolRailTopHost");
    topHost->setAttribute(Qt::WA_StyledBackground, true);
    topHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_top = new QVBoxLayout(topHost);
    m_top->setContentsMargins(6, 8, 6, 8);
    m_top->setSpacing(8);
    m_top->setAlignment(Qt::AlignTop);

    auto* bottomHost = new QWidget(this);
    bottomHost->setObjectName("ToolRailBottomHost");
    bottomHost->setAttribute(Qt::WA_StyledBackground, true);
    bottomHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_bottom = new QVBoxLayout(bottomHost);
    m_bottom->setContentsMargins(6, 8, 6, 8);
    m_bottom->setSpacing(8);
    m_bottom->setAlignment(Qt::AlignBottom);

    root->addWidget(topHost, 0);
    root->addStretch(1);

    auto* regionSep = new QFrame(this);
    regionSep->setObjectName("ToolRailRegionSeparator");
    regionSep->setFrameShape(QFrame::HLine);
    regionSep->setFixedHeight(1);
    regionSep->setAttribute(Qt::WA_StyledBackground, true);
    root->addWidget(regionSep, 0);

    root->addWidget(bottomHost, 0);

    connect(m_model, &SidebarModel::railToolsChanged, this, &ToolRailWidget::rebuild);
    connect(m_model, &SidebarModel::toolRegistered, this, &ToolRailWidget::rebuild);
    connect(m_model, &SidebarModel::toolUnregistered, this, &ToolRailWidget::rebuild);
    connect(m_model, &SidebarModel::toolOpenStateChanged, this, &ToolRailWidget::rebuild);
    connect(m_model, &SidebarModel::exclusiveActiveChanged, this, &ToolRailWidget::rebuild);

    rebuild();
}

QToolButton* ToolRailWidget::makeButton(const QString& id)
{
    const SidebarToolSpec* spec = m_model->toolSpec(id);
    if (!spec)
        return nullptr;

    auto* b = new QToolButton(this);
    b->setObjectName("RailToolButton"); // matches QSS selector
    b->setCheckable(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setAutoRaise(true);
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    b->setIconSize(QSize(22, 22));
    b->setFixedHeight(60);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    b->setMinimumWidth(32);

    b->setToolTip(spec->toolTip.isEmpty() ? spec->title : spec->toolTip);
    b->setText(spec->title);

    if (!spec->iconResource.isEmpty())
        b->setIcon(QIcon(spec->iconResource));

    connect(b, &QToolButton::clicked, this, &ToolRailWidget::onToolClicked);

    m_btnInfo.insert(b, ButtonInfo{spec->id, spec->region});

    b->setChecked(m_model->isOpen(spec->id));

    return b;
}

void ToolRailWidget::rebuild()
{
    auto clearLayout = [&](QVBoxLayout* lay) {
        while (lay->count() > 0) {
            QLayoutItem* it = lay->takeAt(0);
            if (QWidget* w = it->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete it;
        }
    };

    m_btnInfo.clear();
    clearLayout(m_top);
    clearLayout(m_bottom);

    auto addButtons = [&](QVBoxLayout* lay, SidebarRail rail) {
        const auto ids = m_model->toolIdsForRail(m_side, m_family, rail);
        for (const auto& id : ids) {
            const SidebarToolSpec* spec = m_model->toolSpec(id);
            if (!spec)
                continue;

            auto* b = makeButton(id);
            if (!b)
                continue;

            lay->addWidget(b, 0);
        }
    };

    addButtons(m_top, SidebarRail::Top);
    addButtons(m_bottom, SidebarRail::Bottom);

    const bool hasTop = (m_top->count() > 0);
    const bool hasBottom = (m_bottom->count() > 0);

    if (auto* topHost = findChild<QWidget*>("ToolRailTopHost"))
        topHost->setVisible(hasTop);
    if (auto* bottomHost = findChild<QWidget*>("ToolRailBottomHost"))
        bottomHost->setVisible(hasBottom);
    if (auto* sep = findChild<QFrame*>("ToolRailRegionSeparator"))
        sep->setVisible(hasTop && hasBottom);

    setVisible(hasTop || hasBottom);

}

void ToolRailWidget::onToolClicked()
{
    auto* b = qobject_cast<QToolButton*>(sender());
    if (!b)
        return;

    const auto info = m_btnInfo.value(b);
    QString err;

    m_model->requestToggleTool(info.id, &err);
    if (!err.isEmpty())
        qWarning() << "Tool toggle failed:" << err;
}

} // namespace Core
