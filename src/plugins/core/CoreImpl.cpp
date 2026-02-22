// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/CoreImpl.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QGuiApplication>
#include <qnamespace.h> // Qt enums (e.g., Qt::Corner, Qt::DockWidgetArea)

#include "core/CoreGlobal.hpp"
#include "core/CoreConstants.hpp"
#include "core/state/CoreUiState.hpp"
#include "core/widgets/FrameWidget.hpp"
#include "core/ui/UiHostImpl.hpp"
#include "core/ui/UiStyle.hpp"

Q_LOGGING_CATEGORY(corelog, "ironsmith.core")

namespace Core {
namespace Internal {

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QGuiApplication::applicationDisplayName());

        // Allowing recursive splitter layouts within widgets
        setDockNestingEnabled(true);

        // Bottom widget area will own both bottom corners (conflict-resolution rule override)
        setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
        setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    }

private:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
};
} // namespace Internal

namespace Internal {
void MainWindow::closeEvent(QCloseEvent* event) {

}

void MainWindow::keyPressEvent(QKeyEvent* event) {

}

void MainWindow::mousePressEvent(QMouseEvent* event) {

}
} // namespace Internal

namespace {

constexpr int kMainWindowStateSaveDelayMs = 200;

} // namespace

CoreImpl::CoreImpl(QObject* parent)
    : ICore(parent)
    , m_uiState(std::make_unique<Internal::CoreUiState>())
{
    m_windowStateSaveTimer.setSingleShot(true);
    m_windowStateSaveTimer.setInterval(kMainWindowStateSaveDelayMs);
    connect(&m_windowStateSaveTimer, &QTimer::timeout,
            this, &CoreImpl::flushMainWindowStateSave);

    ensureWindowCreated();
}

CoreImpl::~CoreImpl()
{
    m_shuttingDown = true;
    if (m_mainWindow)
        m_mainWindow->removeEventFilter(this);
    if (m_windowStateSaveTimer.isActive())
        m_windowStateSaveTimer.stop();
}

void CoreImpl::ensureWindowCreated()
{
    if (m_mainWindow)
        return;

    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
        Ui::UiStyle::applyAppStyle(*app);

    qCInfo(corelog) << "Creating main application window";
    m_mainWindow = new Internal::MainWindow();
    m_mainWindow->setObjectName(Constants::MAIN_WINDOW_OBJECT_NAME);
    m_mainWindow->installEventFilter(this);

    m_frame = new FrameWidget(m_mainWindow);
    m_mainWindow->setCentralWidget(m_frame);

    m_uiHost = new UiHostImpl(m_frame, this);

    bool restoredGeometry = false;
    if (m_uiState) {
        const QByteArray geometry = m_uiState->mainWindowGeometry();
        if (!geometry.isEmpty())
            restoredGeometry = m_mainWindow->restoreGeometry(geometry);
    }

    if (!restoredGeometry)
        m_mainWindow->resize(Constants::DEFAULT_MAIN_WINDOW_WIDTH, Constants::DEFAULT_MAIN_WINDOW_HEIGHT);

    qCInfo(corelog) << "Created main window";
}


void CoreImpl::setCentralWidget(QWidget* widget)
{
    ensureWindowCreated();

    if (!widget || !m_uiHost) {
        qCWarning(corelog) << "setCentralWidget (func): null widget";
        return;
    }

    m_uiHost->setPlaygroundCenterBase(widget);
    qCInfo(corelog) << "setCentralWidget (func): playground center has been set";
}

IUiHost* CoreImpl::uiHost() const
{
    return m_uiHost;
}

void CoreImpl::open()
{
    ensureWindowCreated();

    if (m_openCalled)
        return;
    m_openCalled = true;

    emit coreAboutToOpen();

    qCInfo(corelog) << "Application about to open...";
    QTimer::singleShot(0, this, [this] {
        if (!m_mainWindow)
            return;

        m_mainWindow->show();
        QTimer::singleShot(0, this, [this] { emit coreOpened(); });
    });
}

bool CoreImpl::eventFilter(QObject* watched, QEvent* event)
{
    if (m_shuttingDown)
        return ICore::eventFilter(watched, event);

    if (watched == m_mainWindow && event) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::WindowStateChange:
            scheduleMainWindowStateSave();
            break;
        case QEvent::Close:
            flushMainWindowStateSave();
            break;
        default:
            break;
        }
    }

    return ICore::eventFilter(watched, event);
}

void CoreImpl::scheduleMainWindowStateSave()
{
    if (m_shuttingDown)
        return;
    if (!m_mainWindow || !m_uiState)
        return;

    if (m_mainWindow->windowState().testFlag(Qt::WindowMinimized))
        return;

    m_windowStateSaveTimer.start();
}

void CoreImpl::flushMainWindowStateSave()
{
    if (m_shuttingDown && !m_uiState)
        return;
    if (!m_mainWindow || !m_uiState)
        return;

    if (m_windowStateSaveTimer.isActive())
        m_windowStateSaveTimer.stop();

    if (m_mainWindow->windowState().testFlag(Qt::WindowMinimized))
        return;

    m_uiState->setMainWindowGeometry(m_mainWindow->saveGeometry());
}

} // namespace Core
