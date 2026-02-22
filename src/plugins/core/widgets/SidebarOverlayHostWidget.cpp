// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/widgets/SidebarOverlayHostWidget.hpp"

#include <algorithm>

#include <QStyle>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QSizePolicy>
#include <QtCore/QPropertyAnimation>
#include <QtCore/QVariant>

#include "core/SidebarModel.hpp"
#include "core/widgets/SidebarFamilyPanelWidget.hpp"
#include "core/widgets/SidebarOverlayResizeGrip.hpp"

namespace Core {

static constexpr int kSidebarOverlayPanelWidth = 320;
static constexpr int kSidebarOverlayMinWidth = 220;
static constexpr int kSidebarOverlayMaxWidth = 1720;

SidebarOverlayHostWidget::SidebarOverlayHostWidget(SidebarModel* model,
                                                   SidebarSide side,
                                                   SidebarFamily family,
                                                   QWidget* parent)
    : QWidget(parent)
    , m_model(model)
    , m_side(side)
    , m_family(family)
{
    Q_ASSERT(m_model);

    setObjectName(m_side == SidebarSide::Left ? "LeftSidebarOverlayHost" : "RightSidebarOverlayHost");
    setAttribute(Qt::WA_StyledBackground, true);

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setMinimumWidth(0);
    setMaximumWidth(0);
    setVisible(false);
    setPanelWidth(kSidebarOverlayPanelWidth);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_familyPanel = new SidebarFamilyPanelWidget(m_side, m_family, this);
    m_resizeGrip = new SidebarOverlayResizeGrip(this, m_side, this);
    m_resizeGrip->setVisible(false);

    if (m_side == SidebarSide::Left) {
        root->addWidget(m_familyPanel, 1);
        root->addWidget(m_resizeGrip, 0);
    } else {
        root->addWidget(m_resizeGrip, 0);
        root->addWidget(m_familyPanel, 1);
    }

    auto matchesThisHost = [this](const QString& id) -> bool {
        const SidebarToolSpec* s = m_model->toolSpec(id);
        return s && s->side == m_side && s->family == m_family;
    };

    connect(m_model, &SidebarModel::toolRegistered, this, [this, matchesThisHost](const QString& id) {
        if (matchesThisHost(id))
            syncFromModel();
    });

    connect(m_model, &SidebarModel::toolUnregistered, this, [this, matchesThisHost](const QString& id) {
        Q_UNUSED(id);
        syncFromModel();
    });

    connect(m_model, &SidebarModel::railToolsChanged, this,
            [this](SidebarSide side, SidebarFamily fam, SidebarRail) {
                if (side == m_side && fam == m_family)
                    syncFromModel();
            });

    connect(m_model, &SidebarModel::toolOpenStateChanged, this, [this, matchesThisHost](const QString& id, bool) {
        if (matchesThisHost(id))
            syncFromModel();
    });

    connect(m_model, &SidebarModel::exclusiveActiveChanged, this,
            [this](SidebarSide side, SidebarFamily fam, SidebarRegion region, const QString&) {
                if (side == m_side && fam == m_family && region == SidebarRegion::Exclusive)
                    syncFromModel();
            });

    syncFromModel();
}

QString SidebarOverlayHostWidget::desiredExclusiveId() const
{
    return m_model->activeToolId(m_side, m_family, SidebarRegion::Exclusive);
}

QStringList SidebarOverlayHostWidget::desiredAdditiveIds() const
{
    const QString id = m_model->activeToolId(m_side, m_family, SidebarRegion::Additive);
    return id.isEmpty() ? QStringList{} : QStringList{id};
}

void SidebarOverlayHostWidget::clearLayout(QVBoxLayout* lay)
{
    while (lay->count() > 0) {
        QLayoutItem* it = lay->takeAt(0);
        if (!it)
            continue;

        if (QWidget* w = it->widget()) {
            w->hide();
        }

        delete it;
    }
}

SidebarOverlayHostWidget::PanelInstance SidebarOverlayHostWidget::ensurePanel(const QString& id)
{
    if (auto it = m_panels.find(id); it != m_panels.end())
        return it.value();

    const auto factory = m_model->panelFactory(id);
    if (!factory)
        return {};

    auto* chrome = new QFrame(this);
    chrome->setObjectName("SidebarPanelChrome");
    chrome->setAttribute(Qt::WA_StyledBackground, true);

    auto* lay = new QVBoxLayout(chrome);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    QWidget* content = factory(chrome);
    if (!content) {
        chrome->deleteLater();
        return {};
    }

    if (content->parentWidget() != chrome)
        content->setParent(chrome);

    content->setObjectName("SidebarPanelContent");
    lay->addWidget(content);

    PanelInstance inst{chrome, content};
    m_panels.insert(id, inst);
    return inst;
}

void SidebarOverlayHostWidget::destroyPanel(const QString& id)
{
    auto it = m_panels.find(id);
    if (it == m_panels.end())
        return;

    PanelInstance inst = it.value();
    m_panels.erase(it);

    if (inst.chrome) {
        inst.chrome->hide();
        inst.chrome->deleteLater();
    }
}

void SidebarOverlayHostWidget::setOverlayWidth(int width)
{
    const int w = qMax(0, width);

    setMinimumWidth(w);
    setMaximumWidth(w);

    m_overlayWidth = w;

    updateGeometry();
}

void SidebarOverlayHostWidget::setPanelWidth(int width)
{
    const int w = qMax(0, width);
    if (m_panelWidth == w)
        return;

    m_panelWidth = w;

    if (m_hasPanels)
        setOverlayWidth(w);

    emit panelWidthChanged(m_panelWidth);
}

void SidebarOverlayHostWidget::setPanelWidthClamped(int w)
{
    w = std::clamp(w, kSidebarOverlayMinWidth, kSidebarOverlayMaxWidth);
    setPanelWidth(w);
}

void SidebarOverlayHostWidget::updateRailExpandedProperty(bool expanded)
{
    if (QWidget* w = window()) {
        bool effective = expanded;

        if (!expanded) {
            const auto all = w->findChildren<SidebarOverlayHostWidget*>();
            for (SidebarOverlayHostWidget* h : all) {
                if (!h || h == this)
                    continue;
                if (h->m_side != m_side)
                    continue;
                if (h->overlayWidth() > 0) {
                    effective = true;
                    break;
                }
            }
        }

        const char* railName = (m_side == SidebarSide::Left) ? "LeftSidebarHost" : "RightSidebarHost";
        if (QWidget* rail = w->findChild<QWidget*>(railName)) {
            rail->setProperty("dockExpanded", effective);
            rail->style()->unpolish(rail);
            rail->style()->polish(rail);
            rail->update();
        }
    }
}

void SidebarOverlayHostWidget::applyVisibleState(bool visible)
{
    const int effectivePanelWidth = (m_panelWidth > 0) ? m_panelWidth : kSidebarOverlayPanelWidth;
    const int targetW = visible ? effectivePanelWidth : 0;

    if (m_hasPanels == visible && overlayWidth() == targetW) {
        if (!visible && targetW == 0) {
            if (overlayWidth() != 0)
                setOverlayWidth(0);
            setVisible(false);
            if (m_resizeGrip)
                m_resizeGrip->setVisible(false);
            updateRailExpandedProperty(false);
        }
        return;
    }

    m_hasPanels = visible;

    if (visible) {
        setVisible(true);
        if (m_resizeGrip)
            m_resizeGrip->setVisible(true);
        updateRailExpandedProperty(true);
    }

    auto* anim = findChild<QPropertyAnimation*>("SidebarOverlayWidthAnim");
    if (!anim) {
        anim = new QPropertyAnimation(this, "overlayWidth", this);
        anim->setObjectName("SidebarOverlayWidthAnim");
        anim->setDuration(140);
        anim->setEasingCurve(QEasingCurve::OutCubic);

        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            if (overlayWidth() <= 1) {
                setOverlayWidth(0);
                setVisible(false);
                if (m_resizeGrip)
                    m_resizeGrip->setVisible(false);
                updateRailExpandedProperty(false);
            }
        });
    }

    anim->stop();
    anim->setStartValue(overlayWidth());
    anim->setEndValue(targetW);
    anim->start();

    emit hasPanelsChanged(visible);
}

void SidebarOverlayHostWidget::syncFromModel()
{
    const QString exId = desiredExclusiveId();
    const QStringList addIds = desiredAdditiveIds();

    QSet<QString> keep;
    bool addedExclusive = false;
    bool addedAdditive = false;

    auto* exLayout = qobject_cast<QVBoxLayout*>(m_familyPanel->exclusiveInstallHost()->layout());
    auto* addLayout = qobject_cast<QVBoxLayout*>(m_familyPanel->additiveInstallHost()->layout());
    Q_ASSERT(exLayout && addLayout);

    clearLayout(exLayout);
    clearLayout(addLayout);

    if (!exId.isEmpty()) {
        const PanelInstance ex = ensurePanel(exId);
        if (ex.chrome) {
            ex.chrome->show();
            exLayout->addWidget(ex.chrome, 0);
            keep.insert(exId);
            addedExclusive = true;
        } else {
            m_model->requestHideTool(exId, nullptr);
        }
    }

    for (const QString& id : addIds) {
        const PanelInstance inst = ensurePanel(id);
        if (inst.chrome) {
            inst.chrome->show();
            addLayout->addWidget(inst.chrome, 0);
            keep.insert(id);
            addedAdditive = true;
        } else {
            m_model->requestHideTool(id, nullptr);
        }
    }

    for (auto it = m_panels.begin(); it != m_panels.end(); ) {
        const QString id = it.key();
        if (!keep.contains(id)) {
            PanelInstance inst = it.value();
            it = m_panels.erase(it);
            if (inst.chrome) {
                inst.chrome->hide();
                inst.chrome->deleteLater();
            }
        } else {
            ++it;
        }
    }

    const bool hasExclusive = addedExclusive;
    const bool hasAdditive = addedAdditive;

    m_familyPanel->setHasExclusive(hasExclusive);
    m_familyPanel->setHasAdditive(hasAdditive);

    if (hasAdditive && !hasExclusive) {
        m_familyPanel->setAdditiveFillMode(true);
    } else {
        m_familyPanel->setAdditiveFillMode(false);
        if (hasAdditive && hasExclusive) {
            if (m_familyPanel->additiveInstallHost()->layout())
                m_familyPanel->additiveInstallHost()->layout()->activate();
            const QSize hint = m_familyPanel->additiveInstallHost()->sizeHint();
            const int targetPx = (m_family == SidebarFamily::Horizontal) ? hint.width() : hint.height();
            m_familyPanel->setAdditiveDockedHeight(targetPx, true);
        }
    }

    applyVisibleState(!keep.isEmpty());
}

} // namespace Core
