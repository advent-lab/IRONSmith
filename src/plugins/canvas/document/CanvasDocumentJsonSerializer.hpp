// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utils/Result.hpp>

#include <QtCore/QJsonObject>

namespace Canvas {
class CanvasDocument;
class CanvasView;
} // namespace Canvas

namespace Canvas::Internal {

class CanvasDocumentJsonSerializer final
{
public:
    static QJsonObject serialize(const CanvasDocument& document,
                                 const CanvasView* view,
                                 const QJsonObject& metadata = {});

    static Utils::Result deserialize(const QJsonObject& json,
                                     CanvasDocument& document,
                                     CanvasView* view,
                                     QJsonObject* outMetadata = nullptr);
};

} // namespace Canvas::Internal

