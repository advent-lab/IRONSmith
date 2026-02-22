// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieConstants.hpp"

#include <QtCore/QJsonObject>
#include <QColor>

namespace Aie {
class AieCanvasCoordinator;
}

namespace Aie::Internal {

struct LayoutSettings final {
    double horizontalSpacing = Aie::kDefaultTileSpacing;
    double verticalSpacing = Aie::kDefaultTileSpacing;
    double outwardSpread = Aie::kDefaultOuterMargin;
    bool autoCellSize = true;
    double cellSize = Aie::kDefaultCellSize;
    double keepoutMargin = Aie::kDefaultKeepoutMargin;
};

struct DisplaySettings final {
    bool showPorts = true;
    bool showLabels = true;
    bool showAnnotations = false;
    bool showStereotypes = true;
    bool showPortAnnotations = true;
};

struct StyleSettings final {
    bool useCustomColors = false;
    QColor fillColor;
    QColor outlineColor;
    QColor labelColor;
};

struct PanelSettings final {
    LayoutSettings layout;
    DisplaySettings display;
    StyleSettings style;
    QHash<QString, QPointF> offsets;
};

LayoutSettings layoutDefaults();
LayoutSettings layoutFromCoordinator(const AieCanvasCoordinator& coordinator);
LayoutSettings layoutFromJson(const QJsonObject& obj, const LayoutSettings& fallback);
QJsonObject layoutToJson(const LayoutSettings& settings);
void applyLayout(AieCanvasCoordinator& coordinator, const LayoutSettings& settings);
bool layoutEquals(const LayoutSettings& a, const LayoutSettings& b);

LayoutSettings loadDefaultLayout();

PanelSettings panelDefaults();
PanelSettings panelFromCoordinator(const AieCanvasCoordinator& coordinator);
PanelSettings panelFromJson(const QJsonObject& obj,
                            const PanelSettings& fallback,
                            bool* overrideOut = nullptr);
PanelSettings panelFromSettingsObject(const QJsonObject& obj, const PanelSettings& fallback);
QJsonObject panelToJson(const PanelSettings& settings, bool overrideFlag);
void applyPanel(AieCanvasCoordinator& coordinator, const PanelSettings& settings);
bool panelEquals(const PanelSettings& a, const PanelSettings& b);

PanelSettings loadDefaultPanel();

} // namespace Aie::Internal
