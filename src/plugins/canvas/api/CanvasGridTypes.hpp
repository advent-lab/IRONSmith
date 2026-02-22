// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasPorts.hpp"
#include "utils/ui/GridSpec.hpp"

#include <QtCore/QMarginsF>
#include <QtGui/QColor>
#include <QtCore/QPointF>
#include <QtCore/QMetaType>
#include <QtCore/QSizeF>
#include <QtCore/QString>

namespace Canvas::Api {

struct CANVAS_EXPORT CanvasBlockSpec final {
    QString id;
    Utils::GridRect gridRect;
    QString label;
    QString stereotype;

    QSizeF preferredSize{};
    bool useGridSize = true;

    bool movable = false;
    bool showPorts = true;
    bool deletable = true;
    bool allowMultiplePorts = false;
    bool hasAutoPortRole = false;
    Canvas::PortRole autoPortRole = Canvas::PortRole::Dynamic;
    bool autoOppositeProducerPort = false;
    bool showPortLabels = false;

    QPointF positionOffset{};

    double keepoutMargin = -1.0;
    bool hasCustomPadding = false;
    QMarginsF contentPadding{};

    QString styleKey;

    bool hasCustomColors = false;
    QColor fillColor;
    QColor outlineColor;
    QColor labelColor;
    double cornerRadius = -1.0;

    bool hasPreferredSize() const {
        return !useGridSize && preferredSize.width() > 0.0 && preferredSize.height() > 0.0;
    }
};

} // namespace Canvas::Api

Q_DECLARE_METATYPE(Canvas::Api::CanvasBlockSpec)
