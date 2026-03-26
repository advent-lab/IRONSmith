// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

namespace Aie {
class AieCanvasCoordinator;
}

namespace Canvas::Api {
class ICanvasDocumentService;
struct CanvasDocumentHandle;
enum class CanvasDocumentCloseReason : unsigned char;
} // namespace Canvas::Api

namespace Aie::Internal {

class AiePanelState final : public QObject
{
    Q_OBJECT

public:
    explicit AiePanelState(AieCanvasCoordinator* coordinator = nullptr, QObject* parent = nullptr);

    void setCoordinator(AieCanvasCoordinator* coordinator);
    void setCanvasDocumentService(Canvas::Api::ICanvasDocumentService* canvasDocuments);
    void setDefaultsPersistenceEnabled(bool enabled);
    bool defaultsPersistenceEnabled() const { return m_persistDefaults; }

    static Utils::Environment makeEnvironment();

private:
    void loadState();
    void loadDocumentState();
    void saveState();
    void saveDocumentState();
    void scheduleSave();

    void apply(const QJsonObject& state);
    QJsonObject snapshot() const;

    QPointer<AieCanvasCoordinator> m_coordinator;
    QPointer<Canvas::Api::ICanvasDocumentService> m_canvasDocuments;
    Utils::Environment m_env;
    QTimer m_saveTimer;
    bool m_applying = false;
    bool m_persistDefaults = true;
};

} // namespace Aie::Internal
