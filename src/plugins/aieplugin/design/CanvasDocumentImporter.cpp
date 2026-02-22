// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/design/CanvasDocumentImporter.hpp"

#include "aieplugin/AieService.hpp"
#include "aieplugin/design/DesignStateCanvas.hpp"
#include "aieplugin/design/DesignStateJson.hpp"

#include "canvas/api/ICanvasHost.hpp"
#include "canvas/CanvasView.hpp"

#include <QtCore/QLoggingCategory>

Q_LOGGING_CATEGORY(aieimportlog, "ironsmith.aie.import")

namespace Aie::Internal {

CanvasDocumentImporter::CanvasDocumentImporter(AieService* service)
    : m_service(service)
{
}

Utils::Result CanvasDocumentImporter::applyProfile(const QString& deviceId) const
{
    if (!m_service)
        return Utils::Result::failure(QStringLiteral("AIE service is not available."));

    qCDebug(aieimportlog) << "applyProfile:" << deviceId;
    return m_service->setProfileId(deviceId);
}

Utils::Result CanvasDocumentImporter::importLegacyDesignState(const QJsonObject& designState) const
{
    const Utils::Result designResult = applyDesignState(designState);
    if (!designResult)
        return designResult;

    if (auto* host = m_service ? m_service->canvasHost() : nullptr)
        host->setCanvasActive(true);
    return Utils::Result::success();
}

Utils::Result CanvasDocumentImporter::applyDesignState(const QJsonObject& designState) const
{
    if (!m_service)
        return Utils::Result::failure(QStringLiteral("AIE service is not available."));
    auto* host = m_service->canvasHost();
    if (!host || !host->document())
        return Utils::Result::failure(QStringLiteral("Canvas host is not available."));

    qCDebug(aieimportlog) << "applyDesignState: parsing design state";
    DesignState state;
    Utils::Result parseResult = parseDesignState(designState, state);
    if (!parseResult)
        return parseResult;

    auto* view = qobject_cast<Canvas::CanvasView*>(host->viewWidget());
    qCDebug(aieimportlog) << "applyDesignState: applying to canvas";
    return applyDesignStateToCanvas(state, *host->document(), view);
}

} // namespace Aie::Internal
