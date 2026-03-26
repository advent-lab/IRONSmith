// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>

namespace Core {
class ISidebarRegistry;
}

namespace Canvas {
class CanvasView;
}

namespace Canvas::Api {
class ICanvasHost;
}

namespace Aie::Internal {

class AiePropertiesShortcutController final : public QObject
{
    Q_OBJECT

public:
    explicit AiePropertiesShortcutController(QObject* parent = nullptr);

    void setCanvasHost(Canvas::Api::ICanvasHost* host);
    void setSidebarRegistry(Core::ISidebarRegistry* sidebarRegistry);

private:
    void reconnectCanvasSignals();
    void onCanvasMousePressed(const QPointF& scenePos,
                              Qt::MouseButtons buttons,
                              Qt::KeyboardModifiers mods);

    QPointer<Canvas::Api::ICanvasHost> m_canvasHost;
    QPointer<Core::ISidebarRegistry> m_sidebarRegistry;
    QPointer<Canvas::CanvasView> m_canvasView;
    QMetaObject::Connection m_canvasMousePressConnection;
};

} // namespace Aie::Internal
