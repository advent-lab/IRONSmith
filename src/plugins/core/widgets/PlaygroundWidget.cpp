// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/widgets/PlaygroundWidget.hpp"

#include <algorithm>

#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStackedLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <QtWidgets/QSizePolicy>

#include "core/ui/UiStyle.hpp"
#include "core/widgets/InfoBarWidget.hpp"

namespace Core {

class ResizableSidebarContainer;

class SidebarPanelSlot final : public QWidget
{
public:
    explicit SidebarPanelSlot(const char* objectName, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(objectName);
        setAttribute(Qt::WA_StyledBackground, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setMinimumWidth(0);

        m_installHost = new QWidget(this);
        m_installHost->setObjectName("SidebarPanelInstallHost");
        m_installHost->setAttribute(Qt::WA_StyledBackground, false);
        m_installHost->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        auto* l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        l->addWidget(m_installHost, 1);

        auto* installLayout = new QVBoxLayout(m_installHost);
        installLayout->setContentsMargins(0, 0, 0, 0);
        installLayout->setSpacing(0);
    }

    QWidget* installHost() const { return m_installHost; }

private:
    QWidget* m_installHost = nullptr;
};

class SidebarResizeGrip final : public QWidget
{
public:
    enum class Side { Left, Right };

    SidebarResizeGrip(ResizableSidebarContainer* owner, Side side, QWidget* parent = nullptr)
        : QWidget(parent), m_owner(owner), m_side(side)
    {
        setObjectName("SidebarResizeGrip");
        setFixedWidth(kGripPx);
        setCursor(Qt::SplitHCursor);
        setMouseTracking(true);
        setAttribute(Qt::WA_StyledBackground, false);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    static constexpr int kGripPx = 6;

    ResizableSidebarContainer* m_owner = nullptr; // non-owning
    Side m_side;
    bool m_resizing = false;
    qreal m_pressGlobalX = 0.0;
    int m_pressContentW = 0;
};

class ResizableSidebarContainer final : public QWidget
{
public:
    enum class Side { Left, Right };

    ResizableSidebarContainer(Side side, int defaultContentW, int minContentW, int maxContentW, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_side(side)
        , m_defaultContentW(defaultContentW)
        , m_minContentW(minContentW)
        , m_maxContentW(maxContentW)
        , m_savedContentW(defaultContentW)
    {
        setObjectName(m_side == Side::Left ? "LeftSidebarHost" : "RightSidebarHost");
        setAttribute(Qt::WA_StyledBackground, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        m_installHost = new QWidget(this);
        m_installHost->setObjectName("SidebarInstallHost");
        m_installHost->setAttribute(Qt::WA_StyledBackground, false);
        m_installHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        auto* installLayout = new QVBoxLayout(m_installHost);
        installLayout->setContentsMargins(0, 0, 0, 0);
        installLayout->setSpacing(0);

        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        if (m_side == Side::Left) {
            root->addWidget(m_installHost, 1);
            m_grip = new SidebarResizeGrip(this, SidebarResizeGrip::Side::Left, this);
            root->addWidget(m_grip, 0);
        } else {
            m_grip = new SidebarResizeGrip(this, SidebarResizeGrip::Side::Right, this);
            root->addWidget(m_grip, 0);
            root->addWidget(m_installHost, 1);
        }

        m_installHost->installEventFilter(this);

        setVisible(false);
        setFixedWidth(0);
    }

    QWidget* installHost() const { return m_installHost; }

    int contentWidth() const noexcept { return m_savedContentW; }

    void setContentWidthClamped(int w)
    {
        w = std::clamp(w, m_minContentW, m_maxContentW);
        m_savedContentW = w;
        applyVisibleWidthFromSaved();
    }

    bool hasInstalledRail() const
    {
        const auto kids = m_installHost->children();
        for (QObject* obj : kids) {
            if (qobject_cast<QWidget*>(obj))
                return true;
        }
        return false;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* e) override
    {
        if (watched == m_installHost) {
            if (e->type() == QEvent::ChildAdded || e->type() == QEvent::ChildRemoved) {
                syncCollapsedState();
            }
        }
        return QWidget::eventFilter(watched, e);
    }

private:
    friend class SidebarResizeGrip;

    void syncCollapsedState()
    {
        if (hasInstalledRail()) {
            setVisible(true);
            applyVisibleWidthFromSaved();
        } else {
            setVisible(false);
            setFixedWidth(0);
        }
    }

    void applyVisibleWidthFromSaved()
    {
        if (!hasInstalledRail())
            return;

        const int totalW = m_savedContentW + m_grip->width();
        setFixedWidth(totalW);
    }

private:
    Side m_side;
    int m_defaultContentW;
    int m_minContentW;
    int m_maxContentW;
    int m_savedContentW;

    QWidget* m_installHost = nullptr;
    SidebarResizeGrip* m_grip = nullptr;
};

// The chrome overlay uses a StackAll layout to draw tool panels/top/bottom bars on top
// of the center "base" content. That overlay must NOT consume pointer/wheel events in
// the center region, otherwise the canvas never sees input.
//
// We keep the chrome layout (so the base does not resize) but forward input that occurs
// in the center spacer region down into the base host.
class CenterPassthroughWidget final : public QWidget
{
public:
    explicit CenterPassthroughWidget(QWidget* baseHost, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_baseHost(baseHost)
    {
        setObjectName("PlaygroundCenterSpacer");
        setAttribute(Qt::WA_StyledBackground, false);
        setMouseTracking(true);
    }

protected:
    bool event(QEvent* e) override
    {
        switch (e->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
            forwardToBase(e);
            return true;
        case QEvent::Leave:
            unsetCursor();
            break;
        default:
            break;
        }
        return QWidget::event(e);
    }

private:
    void forwardToBase(QEvent* e)
    {
        if (!m_baseHost)
            return;

        // Map the event position in this widget to global, then into baseHost coordinates.
        auto mapToBase = [&](const QPointF& local) -> std::pair<QPointF, QPointF> {
            const QPoint globalPt = mapToGlobal(local.toPoint());
            const QPointF baseLocal = QPointF(m_baseHost->mapFromGlobal(globalPt));
            return {baseLocal, QPointF(globalPt)};
        };

        QWidget* target = nullptr;

        if (auto* me = dynamic_cast<QMouseEvent*>(e)) {
            const auto [basePos, globalPos] = mapToBase(me->position());
            target = m_baseHost->childAt(basePos.toPoint());
            if (!target)
                target = m_baseHost;

            // Ensure the canvas can receive key events (Space to pan, etc.) after a click.
            target->setFocus(Qt::MouseFocusReason);

            QMouseEvent forwarded(me->type(),
                                  target->mapFromGlobal(globalPos.toPoint()),
                                  globalPos,
                                  me->button(),
                                  me->buttons(),
                                  me->modifiers());
            QApplication::sendEvent(target, &forwarded);
            applyTargetCursor(target);
            return;
        }

        if (auto* we = dynamic_cast<QWheelEvent*>(e)) {
            const auto [basePos, globalPos] = mapToBase(we->position());
            target = m_baseHost->childAt(basePos.toPoint());
            if (!target)
                target = m_baseHost;

            QWheelEvent forwarded(basePos,
                                  globalPos,
                                  we->pixelDelta(),
                                  we->angleDelta(),
                                  we->buttons(),
                                  we->modifiers(),
                                  we->phase(),
                                  we->inverted(),
                                  we->source());
            QApplication::sendEvent(target, &forwarded);
            return;
        }
    }

    void applyTargetCursor(QWidget* target)
    {
        if (!target) {
            unsetCursor();
            return;
        }

        const QCursor targetCursor = target->cursor();
        if (targetCursor.shape() == Qt::ArrowCursor && !target->testAttribute(Qt::WA_SetCursor)) {
            unsetCursor();
            return;
        }

        setCursor(targetCursor);
    }

private:
    QWidget* m_baseHost = nullptr; // non-owning
};

void SidebarResizeGrip::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_owner || !m_owner->hasInstalledRail()) {
        QWidget::mousePressEvent(e);
        return;
    }

    m_resizing = true;
    m_pressGlobalX = e->globalPosition().x();
    m_pressContentW = m_owner->contentWidth();
    grabMouse();
    e->accept();
}

void SidebarResizeGrip::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_resizing || !m_owner) {
        QWidget::mouseMoveEvent(e);
        return;
    }

    const int dx = int(e->globalPosition().x() - m_pressGlobalX);

    int newW = m_pressContentW;
    if (m_side == Side::Left)
        newW += dx;
    else
        newW -= dx;

    m_owner->setContentWidthClamped(newW);
    e->accept();
}

void SidebarResizeGrip::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_resizing && e->button() == Qt::LeftButton) {
        m_resizing = false;
        releaseMouse();
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

PlaygroundWidget::PlaygroundWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("PlaygroundRoot");
    setAttribute(Qt::WA_StyledBackground, true);

    // Layout model:
    //   [Top bar]
    //   [Left rail] [ Center stack (base + overlay panels) ] [Right rail]
    //   [Bottom bar]
    //
    // The canvas (base) is constrained to the center content rect.
    // Tool panels are rendered as an overlay *within that same rect* so opening
    // them does not resize the canvas and they cannot steal input outside the center.
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_topBar = new InfoBarWidget(this);
    m_topBar->setObjectName("PlaygroundTopBar");
    m_topBar->setFixedHeight(Ui::UiStyle::TopBarHeight);
    m_topBar->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    root->addWidget(m_topBar, 0);

    auto* middle = new QWidget(this);
    middle->setObjectName("PlaygroundMiddle");
    middle->setAttribute(Qt::WA_StyledBackground, false);

    auto* midLayout = new QHBoxLayout(middle);
    midLayout->setContentsMargins(0, 0, 0, 0);
    midLayout->setSpacing(0);

    constexpr int kRailDefault = Ui::UiStyle::SidebarWidth; // e.g. 44
    constexpr int kRailMin = 36;
    constexpr int kRailMax = 96;

    auto* left = new ResizableSidebarContainer(ResizableSidebarContainer::Side::Left,
                                               kRailDefault, kRailMin, kRailMax, middle);
    left->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_leftSidebarContainer = left;
    m_leftSidebarInstallHost = left->installHost();
    midLayout->addWidget(left, 0);

    // Center content rect: base editor surface + overlay panels in a StackAll layout.
    m_centerContainer = new QWidget(middle);
    m_centerContainer->setObjectName("PlaygroundCenterContainer");
    m_centerContainer->setAttribute(Qt::WA_StyledBackground, false);
    m_centerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_centerStack = new QStackedLayout(m_centerContainer);
    m_centerStack->setContentsMargins(0, 0, 0, 0);
    m_centerStack->setStackingMode(QStackedLayout::StackAll);

    m_baseHost = new QWidget(m_centerContainer);
    m_baseHost->setObjectName("BaseHost");
    m_baseHost->setAttribute(Qt::WA_StyledBackground, true);
    m_centerStack->addWidget(m_baseHost);

    m_centerOverlay = new QWidget(m_centerContainer);
    m_centerOverlay->setObjectName("PlaygroundCenterOverlay");
    m_centerOverlay->setAttribute(Qt::WA_StyledBackground, false);
    m_centerStack->addWidget(m_centerOverlay);
    m_centerStack->setCurrentWidget(m_centerOverlay);

    auto* overlayLayout = new QHBoxLayout(m_centerOverlay);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setSpacing(0);

    auto* leftPanelSlot = new SidebarPanelSlot("LeftSidebarPanelSlot", m_centerOverlay);
    leftPanelSlot->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_leftSidebarPanelInstallHost = leftPanelSlot->installHost();
    overlayLayout->addWidget(leftPanelSlot, 0);

    // Spacer that covers the remaining center area on the overlay layer and forwards
    // input to the base editor surface.
    auto* centerSpacer = new CenterPassthroughWidget(m_baseHost, m_centerOverlay);
    centerSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    overlayLayout->addWidget(centerSpacer, 1);

    auto* rightPanelSlot = new SidebarPanelSlot("RightSidebarPanelSlot", m_centerOverlay);
    rightPanelSlot->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_rightSidebarPanelInstallHost = rightPanelSlot->installHost();
    overlayLayout->addWidget(rightPanelSlot, 0);

    midLayout->addWidget(m_centerContainer, 1);

    auto* right = new ResizableSidebarContainer(ResizableSidebarContainer::Side::Right,
                                                kRailDefault, kRailMin, kRailMax, middle);
    right->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_rightSidebarContainer = right;
    m_rightSidebarInstallHost = right->installHost();
    midLayout->addWidget(right, 0);

    root->addWidget(middle, 1);

    m_bottomBar = new InfoBarWidget(this);
    m_bottomBar->setObjectName("PlaygroundBottomBar");
    m_bottomBar->setFixedHeight(Ui::UiStyle::BottomBarHeight);
    m_bottomBar->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    root->addWidget(m_bottomBar, 0);

}

InfoBarWidget* PlaygroundWidget::topBar() const { return m_topBar; }
InfoBarWidget* PlaygroundWidget::bottomBar() const { return m_bottomBar; }

QWidget* PlaygroundWidget::leftSidebarHost() const { return m_leftSidebarInstallHost; }
QWidget* PlaygroundWidget::rightSidebarHost() const { return m_rightSidebarInstallHost; }

QWidget* PlaygroundWidget::leftSidebarPanelHost() const { return m_leftSidebarPanelInstallHost; }
QWidget* PlaygroundWidget::rightSidebarPanelHost() const { return m_rightSidebarPanelInstallHost; }

QWidget* PlaygroundWidget::centerBaseHost() const { return m_baseHost; }
QWidget* PlaygroundWidget::overlayHost() const { return m_centerOverlay; }

} // namespace Core
