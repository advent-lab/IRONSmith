// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasTypes.hpp"

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <qnamespace.h>

#include <vector>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace Canvas {

class CANVAS_EXPORT CanvasFabric final
{
public:
    struct Config final {
        double step;
        double pointRadius;
        double pointInnerRadius;
        const char* pointColor;
        const char* pointInnerColor;

        constexpr Config() noexcept
            : step(16.0)
            , pointRadius(1.25)
            , pointInnerRadius(0.0)
            , pointColor("#662A2F36")
            , pointInnerColor("#3A424C")
        {}
    };

    using IsBlockedFn = bool(*)(const FabricCoord& coord, void* user);

    explicit CanvasFabric(Config cfg = {}) : m_cfg(cfg) {}

    const Config& config() const { return m_cfg; }
    void setConfig(const Config& cfg) { m_cfg = cfg; }

    std::vector<FabricCoord> enumerate(const QRectF& sceneRect,
                                       IsBlockedFn isBlocked = nullptr,
                                       void* user = nullptr) const;

    void draw(QPainter& p,
              const QRectF& sceneRect,
              IsBlockedFn isBlocked = nullptr,
              void* user = nullptr) const;

private:
    Config m_cfg;
};

} // namespace Canvas
