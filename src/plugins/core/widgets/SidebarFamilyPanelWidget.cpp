// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/widgets/SidebarFamilyPanelWidget.hpp"

#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>

namespace Core {

namespace {
constexpr int kDefaultDockedExtent = 240;
constexpr int kMinDockedExtent = 96;
constexpr int kHandlePx = 6;

bool updateDockedExtent(int& storage, int nextValue)
{
    if (storage == nextValue)
        return false;

    storage = nextValue;
    return true;
}
}

SidebarFamilyPanelWidget::SidebarFamilyPanelWidget(SidebarSide side,
                                                   SidebarFamily family,
                                                   QWidget* parent)
    : QFrame(parent)
    , m_side(side)
    , m_family(family)
{
    setObjectName("SidebarFamilyPanel");
    setAttribute(Qt::WA_StyledBackground, true);

    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(0, 0, 0, 0);
    m_root->setSpacing(0);

    m_splitter = new QSplitter(this);
    m_splitter->setObjectName("SidebarFamilySplitter");
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(0);

    m_splitter->setOrientation(m_family == SidebarFamily::Horizontal ? Qt::Horizontal : Qt::Vertical);

    m_exclusiveHost = new QWidget(m_splitter);
    m_exclusiveHost->setObjectName("SidebarFamilyExclusiveHost");
    m_exclusiveHost->setAttribute(Qt::WA_StyledBackground, false);
    m_exclusiveLayout = new QVBoxLayout(m_exclusiveHost);
    m_exclusiveLayout->setContentsMargins(0, 0, 0, 0);
    m_exclusiveLayout->setSpacing(0);

    m_additiveHost = new QWidget(m_splitter);
    m_additiveHost->setObjectName("SidebarFamilyAdditiveHost");
    m_additiveHost->setAttribute(Qt::WA_StyledBackground, false);
    m_additiveLayout = new QVBoxLayout(m_additiveHost);
    m_additiveLayout->setContentsMargins(0, 0, 0, 0);
    m_additiveLayout->setSpacing(0);

    m_splitter->addWidget(m_exclusiveHost);
    m_splitter->addWidget(m_additiveHost);

    m_exclusiveHost->setVisible(false);
    m_additiveHost->setVisible(false);

    m_root->addWidget(m_splitter, 1);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (!(m_hasExclusive && m_hasAdditive))
            return;

        const QList<int> sizes = m_splitter->sizes();
        if (sizes.size() != 2)
            return;

        const int extent = clampDockedExtent(sizes[1]);
        if (updateDockedExtent(m_lastDockedAdditivePx, extent))
            emit additiveDockedExtentChanged(extent);
    });
}

void SidebarFamilyPanelWidget::setHasExclusive(bool hasExclusive)
{
    if (m_hasExclusive == hasExclusive)
        return;
    m_hasExclusive = hasExclusive;
    syncRegionVisibilityAndSizes();
}

void SidebarFamilyPanelWidget::setHasAdditive(bool hasAdditive)
{
    if (m_hasAdditive == hasAdditive)
        return;
    m_hasAdditive = hasAdditive;
    syncRegionVisibilityAndSizes();
}

void SidebarFamilyPanelWidget::setAdditiveFillMode(bool fill)
{
    if (m_additiveFillMode == fill)
        return;
    m_additiveFillMode = fill;
    syncRegionVisibilityAndSizes();
}

int SidebarFamilyPanelWidget::clampDockedExtent(int px) const
{
    if (px <= 0)
        px = kDefaultDockedExtent;

    return qMax(px, kMinDockedExtent);
}

void SidebarFamilyPanelWidget::setAdditiveDockedHeight(int targetHeight, bool animate)
{
    Q_UNUSED(animate);

    const int docked = clampDockedExtent(targetHeight);
    if (updateDockedExtent(m_lastDockedAdditivePx, docked))
        emit additiveDockedExtentChanged(docked);

    if (!(m_hasExclusive && m_hasAdditive))
        return;

    m_additiveFillMode = false;

    const QList<int> sizes = m_splitter->sizes();
    int total = 0;
    for (int s : sizes)
        total += s;

    if (total <= 0) {
        m_splitter->setSizes({1, docked});
        return;
    }

    const int add = qMin(docked, total - 1);
    const int ex = qMax(1, total - add);
    m_splitter->setSizes({ex, add});
}

void SidebarFamilyPanelWidget::syncRegionVisibilityAndSizes()
{
    const bool haveEx = m_hasExclusive;
    const bool haveAdd = m_hasAdditive;

    m_exclusiveHost->setVisible(haveEx);
    m_additiveHost->setVisible(haveAdd);

    const bool both = haveEx && haveAdd;
    m_splitter->setHandleWidth(both ? kHandlePx : 0);

    if (haveEx && !haveAdd) {
        m_splitter->setSizes({1, 0});
        return;
    }

    if (!haveEx && haveAdd) {
        m_splitter->setSizes({0, 1});
        return;
    }

    if (both) {
        const QList<int> sizes = m_splitter->sizes();
        int total = 0;
        for (int s : sizes)
            total += s;

        if (total <= 0)
            total = 1;

        const int docked = clampDockedExtent(m_lastDockedAdditivePx);
        const int add = qMin(docked, total - 1);
        const int ex = qMax(1, total - add);
        m_splitter->setSizes({ex, add});
        return;
    }

    m_splitter->setSizes({0, 0});
}

} // namespace Core
