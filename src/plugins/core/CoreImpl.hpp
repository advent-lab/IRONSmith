// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <memory>

#include <QtCore/QPointer>
#include <QtCore/QTimer>

#include "core/ICore.hpp"
#include "core/ui/IUiHost.hpp"

class QMainWindow;
class QWidget;
class QEvent;

namespace Core {
namespace Internal { class MainWindow; class CoreUiState; }
class FrameWidget;
class UiHostImpl;

class CoreImpl final : public ICore
{
	Q_OBJECT

public:
	explicit CoreImpl(QObject* parent = nullptr);
	~CoreImpl() override;

	void setCentralWidget(QWidget* widget) override;
	void open() override;
	IUiHost* uiHost() const;

private:
	void ensureWindowCreated();
    void scheduleMainWindowStateSave();
    void flushMainWindowStateSave();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
	QPointer<Internal::MainWindow> m_mainWindow;
	QPointer<FrameWidget> m_frame;
	QPointer<IUiHost>     m_uiHost;
    std::unique_ptr<Internal::CoreUiState> m_uiState;
    QTimer m_windowStateSaveTimer;
    bool m_shuttingDown = false;

	bool m_openCalled = false;
};

} // namespace Core
