// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/api/CanvasDocumentTypes.hpp"

#include <utils/Result.hpp>

#include <QtCore/QObject>

namespace Canvas::Api {

class CANVAS_EXPORT ICanvasDocumentService : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~ICanvasDocumentService() override = default;

    virtual Utils::Result createDocument(const CanvasDocumentCreateRequest& request,
                                         CanvasDocumentHandle& outHandle) = 0;
    virtual Utils::Result openDocument(const CanvasDocumentOpenRequest& request,
                                       CanvasDocumentHandle& outHandle) = 0;
    virtual Utils::Result saveDocument(const CanvasDocumentHandle& handle) = 0;
    virtual Utils::Result closeDocument(const CanvasDocumentHandle& handle,
                                        CanvasDocumentCloseReason reason) = 0;

    virtual CanvasDocumentHandle activeDocument() const = 0;
    virtual bool hasOpenDocument() const = 0;
    virtual bool isDirty() const = 0;

signals:
    void documentOpened(const Canvas::Api::CanvasDocumentHandle& handle);
    void documentClosed(const Canvas::Api::CanvasDocumentHandle& handle,
                        Canvas::Api::CanvasDocumentCloseReason reason);
    void documentSaved(const Canvas::Api::CanvasDocumentHandle& handle, const QString& persistencePath);
    void documentDirtyChanged(const Canvas::Api::CanvasDocumentHandle& handle, bool dirty);
};

} // namespace Canvas::Api

