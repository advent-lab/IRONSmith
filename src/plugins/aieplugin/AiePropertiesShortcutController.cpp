// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/AiePropertiesShortcutController.hpp"

#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/api/ICanvasHost.hpp"
#include "core/api/ISidebarRegistry.hpp"

#include <QtCore/QPointF>
#include <QtCore/QtGlobal>

namespace Aie::Internal {

namespace {

constexpr auto kPropertiesSidebarToolId = "IRONSmith.AieProperties";

bool isPropertiesShortcut(Qt::MouseButtons buttons, Qt::KeyboardModifiers mods)
{
    if (!buttons.testFlag(Qt::LeftButton))
        return false;

    const Qt::KeyboardModifiers relevantModifiers =
        mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier);
    return relevantModifiers == (Qt::ControlModifier | Qt::ShiftModifier);
}

} // namespace

AiePropertiesShortcutController::AiePropertiesShortcutController(QObject* parent)
    : QObject(parent)
{
}

void AiePropertiesShortcutController::setCanvasHost(Canvas::Api::ICanvasHost* host)
{
    if (m_canvasHost == host)
        return;

    if (m_canvasMousePressConnection)
        disconnect(m_canvasMousePressConnection);

    m_canvasHost = host;
    reconnectCanvasSignals();
}

void AiePropertiesShortcutController::setSidebarRegistry(Core::ISidebarRegistry* sidebarRegistry)
{
    m_sidebarRegistry = sidebarRegistry;
}

void AiePropertiesShortcutController::reconnectCanvasSignals()
{
    auto* view = m_canvasHost ? qobject_cast<Canvas::CanvasView*>(m_canvasHost->viewWidget()) : nullptr;
    m_canvasView = view;
    if (!view)
        return;

    m_canvasMousePressConnection = connect(view,
                                           &Canvas::CanvasView::canvasMousePressed,
                                           this,
                                           &AiePropertiesShortcutController::onCanvasMousePressed);
}

void AiePropertiesShortcutController::onCanvasMousePressed(const QPointF& scenePos,
                                                           Qt::MouseButtons buttons,
                                                           Qt::KeyboardModifiers mods)
{
    if (!isPropertiesShortcut(buttons, mods))
        return;

    auto* host = m_canvasHost.data();
    auto* document = host ? host->document() : nullptr;
    if (!host || !host->canvasActive() || !document || !m_sidebarRegistry)
        return;

    if (!document->hitTest(scenePos))
        return;

    m_sidebarRegistry->requestShowTool(QString::fromLatin1(kPropertiesSidebarToolId));
}

} // namespace Aie::Internal
