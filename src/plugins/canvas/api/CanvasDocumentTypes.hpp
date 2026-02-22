// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"

#include <QtCore/QMetaType>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace Canvas::Api {

struct CANVAS_EXPORT CanvasDocumentHandle final {
    QString id;
    QString bundlePath;
    QString persistencePath;

    bool isValid() const
    {
        return !id.trimmed().isEmpty()
               && !bundlePath.trimmed().isEmpty()
               && !persistencePath.trimmed().isEmpty();
    }
};

struct CANVAS_EXPORT CanvasDocumentCreateRequest final {
    QString bundlePath;
    QString persistenceRelativePath = QStringLiteral("canvas/document.json");
    QJsonObject specification;
    QJsonObject metadata;
    bool activate = true;
    bool initializeFromCurrentCanvas = false;
};

struct CANVAS_EXPORT CanvasDocumentOpenRequest final {
    QString bundlePath;
    QString persistencePath;
    bool activate = true;
};

enum class CANVAS_EXPORT CanvasDocumentCloseReason : unsigned char {
    UserClosed,
    BundleDeleted,
    WorkspaceChanged,
    OpenReplaced,
    Shutdown,
    Error
};

} // namespace Canvas::Api

Q_DECLARE_METATYPE(Canvas::Api::CanvasDocumentHandle)
Q_DECLARE_METATYPE(Canvas::Api::CanvasDocumentCloseReason)
