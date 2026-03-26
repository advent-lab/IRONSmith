// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/state/AieLayoutSettings.hpp"

#include "aieplugin/AieCanvasCoordinator.hpp"
#include "aieplugin/state/AiePanelState.hpp"

#include <utils/EnvironmentQtPolicy.hpp>

#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

#include <cmath>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kStateName = u"aie/panelState"_s;
const QString kTileSpacingKey = u"tileSpacing"_s;
const QString kOuterMarginKey = u"outerMargin"_s;
const QString kHorizontalSpacingKey = u"horizontalSpacing"_s;
const QString kVerticalSpacingKey = u"verticalSpacing"_s;
const QString kOutwardSpreadKey = u"outwardSpread"_s;
const QString kAutoCellSizeKey = u"autoCellSize"_s;
const QString kCellSizeKey = u"cellSize"_s;
const QString kKeepoutKey = u"keepoutMargin"_s;
const QString kPanelStateKey = u"panelState"_s;
const QString kOverrideDefaultsKey = u"overrideDefaults"_s;
const QString kLayoutKey = u"layout"_s;
const QString kDisplayKey = u"display"_s;
const QString kStyleKey = u"style"_s;
const QString kOffsetsKey = u"offsets"_s;
const QString kShowPortsKey = u"showPorts"_s;
const QString kShowLabelsKey = u"showLabels"_s;
const QString kShowAnnotationsKey = u"showAnnotations"_s;
const QString kShowStereotypesKey = u"showStereotypes"_s;
const QString kShowPortAnnotationsKey = u"showPortAnnotations"_s;
const QString kUseCustomColorsKey = u"useCustomColors"_s;
const QString kFillKey = u"fillColor"_s;
const QString kOutlineKey = u"outlineColor"_s;
const QString kLabelKey = u"labelColor"_s;

bool nearlyEqual(double a, double b)
{
    return std::abs(a - b) <= 1e-6;
}

QString colorToString(const QColor& color)
{
    return color.isValid() ? color.name(QColor::HexArgb) : QString();
}

QColor colorFromString(const QString& text, const QColor& fallback = {})
{
    const QColor parsed(text);
    return parsed.isValid() ? parsed : fallback;
}

QJsonObject pointToJson(const QPointF& point)
{
    QJsonObject obj;
    obj.insert(u"x"_s, point.x());
    obj.insert(u"y"_s, point.y());
    return obj;
}

QPointF pointFromJson(const QJsonValue& value, const QPointF& fallback = {})
{
    const QJsonObject obj = value.toObject();
    const QJsonValue x = obj.value(u"x"_s);
    const QJsonValue y = obj.value(u"y"_s);
    if (!x.isDouble() || !y.isDouble())
        return fallback;
    return QPointF(x.toDouble(), y.toDouble());
}

bool offsetsEqual(const QHash<QString, QPointF>& a, const QHash<QString, QPointF>& b)
{
    if (a.size() != b.size())
        return false;

    for (auto it = a.begin(); it != a.end(); ++it) {
        const auto other = b.find(it.key());
        if (other == b.end())
            return false;
        if (!nearlyEqual(it.value().x(), other.value().x()) ||
            !nearlyEqual(it.value().y(), other.value().y())) {
            return false;
        }
    }

    return true;
}

} // namespace

LayoutSettings layoutDefaults()
{
    return LayoutSettings{};
}

LayoutSettings layoutFromCoordinator(const AieCanvasCoordinator& coordinator)
{
    LayoutSettings settings;
    settings.horizontalSpacing = coordinator.horizontalSpacing();
    settings.verticalSpacing = coordinator.verticalSpacing();
    settings.outwardSpread = coordinator.outwardSpread();
    settings.autoCellSize = coordinator.autoCellSize();
    settings.cellSize = coordinator.cellSize();
    settings.keepoutMargin = coordinator.keepoutMargin();
    return settings;
}

LayoutSettings layoutFromJson(const QJsonObject& obj, const LayoutSettings& fallback)
{
    LayoutSettings settings = fallback;

    if (obj.contains(kHorizontalSpacingKey))
        settings.horizontalSpacing = obj.value(kHorizontalSpacingKey).toDouble(settings.horizontalSpacing);
    if (obj.contains(kVerticalSpacingKey))
        settings.verticalSpacing = obj.value(kVerticalSpacingKey).toDouble(settings.verticalSpacing);
    if (obj.contains(kOutwardSpreadKey))
        settings.outwardSpread = obj.value(kOutwardSpreadKey).toDouble(settings.outwardSpread);

    if (!obj.contains(kHorizontalSpacingKey) && !obj.contains(kVerticalSpacingKey) && obj.contains(kTileSpacingKey)) {
        const double spacing = obj.value(kTileSpacingKey).toDouble(settings.horizontalSpacing);
        settings.horizontalSpacing = spacing;
        settings.verticalSpacing = spacing;
    }
    if (!obj.contains(kOutwardSpreadKey) && obj.contains(kOuterMarginKey))
        settings.outwardSpread = obj.value(kOuterMarginKey).toDouble(settings.outwardSpread);

    if (obj.contains(kAutoCellSizeKey))
        settings.autoCellSize = obj.value(kAutoCellSizeKey).toBool(settings.autoCellSize);
    if (obj.contains(kCellSizeKey))
        settings.cellSize = obj.value(kCellSizeKey).toDouble(settings.cellSize);
    if (obj.contains(kKeepoutKey))
        settings.keepoutMargin = obj.value(kKeepoutKey).toDouble(settings.keepoutMargin);

    return settings;
}

QJsonObject layoutToJson(const LayoutSettings& settings)
{
    QJsonObject obj;
    obj.insert(kHorizontalSpacingKey, settings.horizontalSpacing);
    obj.insert(kVerticalSpacingKey, settings.verticalSpacing);
    obj.insert(kOutwardSpreadKey, settings.outwardSpread);
    obj.insert(kAutoCellSizeKey, settings.autoCellSize);
    obj.insert(kCellSizeKey, settings.cellSize);
    obj.insert(kKeepoutKey, settings.keepoutMargin);
    return obj;
}

void applyLayout(AieCanvasCoordinator& coordinator, const LayoutSettings& settings)
{
    coordinator.setHorizontalSpacing(settings.horizontalSpacing);
    coordinator.setVerticalSpacing(settings.verticalSpacing);
    coordinator.setOutwardSpread(settings.outwardSpread);
    coordinator.setAutoCellSize(settings.autoCellSize);
    coordinator.setCellSize(settings.cellSize);
    coordinator.setKeepoutMargin(settings.keepoutMargin);
}

bool layoutEquals(const LayoutSettings& a, const LayoutSettings& b)
{
    return nearlyEqual(a.horizontalSpacing, b.horizontalSpacing)
        && nearlyEqual(a.verticalSpacing, b.verticalSpacing)
        && nearlyEqual(a.outwardSpread, b.outwardSpread)
        && nearlyEqual(a.cellSize, b.cellSize)
        && nearlyEqual(a.keepoutMargin, b.keepoutMargin)
        && (a.autoCellSize == b.autoCellSize);
}

LayoutSettings loadDefaultLayout()
{
    Utils::Environment env = AiePanelState::makeEnvironment();
    const auto loaded = env.loadState(Utils::EnvironmentScope::Global, kStateName);
    const LayoutSettings fallback = layoutDefaults();
    if (loaded.status != Utils::DocumentLoadResult::Status::Ok)
        return fallback;
    return layoutFromJson(loaded.object, fallback);
}

PanelSettings panelDefaults()
{
    PanelSettings settings;
    settings.layout = layoutDefaults();
    settings.display.showPorts = true;
    settings.display.showLabels = true;
    settings.display.showAnnotations = false;
    settings.display.showStereotypes = true;
    settings.display.showPortAnnotations = true;
    settings.style.useCustomColors = false;
    return settings;
}

PanelSettings panelFromCoordinator(const AieCanvasCoordinator& coordinator)
{
    PanelSettings settings;
    settings.layout = layoutFromCoordinator(coordinator);
    settings.display.showPorts = coordinator.showPorts();
    settings.display.showLabels = coordinator.showLabels();
    settings.display.showAnnotations = coordinator.showAnnotations();
    settings.display.showStereotypes = coordinator.showStereotypes();
    settings.display.showPortAnnotations = coordinator.showPortAnnotations();
    settings.style.useCustomColors = coordinator.useCustomColors();
    settings.style.fillColor = coordinator.fillColor();
    settings.style.outlineColor = coordinator.outlineColor();
    settings.style.labelColor = coordinator.labelColor();
    settings.offsets = coordinator.blockOffsets();
    return settings;
}

PanelSettings panelFromSettingsObject(const QJsonObject& obj, const PanelSettings& fallback)
{
    PanelSettings settings = fallback;

    settings.layout = layoutFromJson(obj.value(kLayoutKey).toObject(), settings.layout);
    if (!obj.contains(kLayoutKey))
        settings.layout = layoutFromJson(obj, settings.layout);

    const QJsonObject display = obj.value(kDisplayKey).toObject();
    const QJsonObject displaySource = display.isEmpty() ? obj : display;
    if (displaySource.contains(kShowPortsKey))
        settings.display.showPorts = displaySource.value(kShowPortsKey).toBool(settings.display.showPorts);
    if (displaySource.contains(kShowLabelsKey))
        settings.display.showLabels = displaySource.value(kShowLabelsKey).toBool(settings.display.showLabels);
    if (displaySource.contains(kShowAnnotationsKey))
        settings.display.showAnnotations = displaySource.value(kShowAnnotationsKey).toBool(settings.display.showAnnotations);
    if (displaySource.contains(kShowStereotypesKey))
        settings.display.showStereotypes =
            displaySource.value(kShowStereotypesKey).toBool(settings.display.showStereotypes);
    if (displaySource.contains(kShowPortAnnotationsKey)) {
        settings.display.showPortAnnotations =
            displaySource.value(kShowPortAnnotationsKey).toBool(settings.display.showPortAnnotations);
    }

    const QJsonObject style = obj.value(kStyleKey).toObject();
    const QJsonObject styleSource = style.isEmpty() ? obj : style;
    if (styleSource.contains(kUseCustomColorsKey))
        settings.style.useCustomColors = styleSource.value(kUseCustomColorsKey).toBool(settings.style.useCustomColors);
    if (styleSource.contains(kFillKey))
        settings.style.fillColor = colorFromString(styleSource.value(kFillKey).toString(), settings.style.fillColor);
    if (styleSource.contains(kOutlineKey))
        settings.style.outlineColor =
            colorFromString(styleSource.value(kOutlineKey).toString(), settings.style.outlineColor);
    if (styleSource.contains(kLabelKey))
        settings.style.labelColor = colorFromString(styleSource.value(kLabelKey).toString(), settings.style.labelColor);

    const QJsonObject offsets = obj.value(kOffsetsKey).toObject();
    if (!offsets.isEmpty()) {
        settings.offsets.clear();
        for (auto it = offsets.begin(); it != offsets.end(); ++it)
            settings.offsets.insert(it.key(), pointFromJson(it.value()));
    }

    return settings;
}

PanelSettings panelFromJson(const QJsonObject& obj,
                            const PanelSettings& fallback,
                            bool* overrideOut)
{
    bool overrideDefaults = false;
    PanelSettings settings = fallback;

    const QJsonValue panelValue = obj.value(kPanelStateKey);
    if (panelValue.isObject()) {
        const QJsonObject panelObject = panelValue.toObject();
        overrideDefaults = panelObject.value(kOverrideDefaultsKey).toBool(true);
        settings = panelFromSettingsObject(panelObject, fallback);
    } else {
        overrideDefaults = obj.value(kOverrideDefaultsKey).toBool(false);
        settings = panelFromSettingsObject(obj, fallback);
    }

    if (overrideOut)
        *overrideOut = overrideDefaults;
    return settings;
}

QJsonObject panelToJson(const PanelSettings& settings, bool overrideFlag)
{
    QJsonObject panel;
    panel.insert(kOverrideDefaultsKey, overrideFlag);
    panel.insert(kLayoutKey, layoutToJson(settings.layout));

    QJsonObject display;
    display.insert(kShowPortsKey, settings.display.showPorts);
    display.insert(kShowLabelsKey, settings.display.showLabels);
    display.insert(kShowAnnotationsKey, settings.display.showAnnotations);
    display.insert(kShowStereotypesKey, settings.display.showStereotypes);
    display.insert(kShowPortAnnotationsKey, settings.display.showPortAnnotations);
    panel.insert(kDisplayKey, display);

    QJsonObject style;
    style.insert(kUseCustomColorsKey, settings.style.useCustomColors);
    style.insert(kFillKey, colorToString(settings.style.fillColor));
    style.insert(kOutlineKey, colorToString(settings.style.outlineColor));
    style.insert(kLabelKey, colorToString(settings.style.labelColor));
    panel.insert(kStyleKey, style);

    QJsonObject offsets;
    for (auto it = settings.offsets.begin(); it != settings.offsets.end(); ++it)
        offsets.insert(it.key(), pointToJson(it.value()));
    panel.insert(kOffsetsKey, offsets);

    QJsonObject root;
    root.insert(kPanelStateKey, panel);
    return root;
}

void applyPanel(AieCanvasCoordinator& coordinator, const PanelSettings& settings)
{
    applyLayout(coordinator, settings.layout);
    coordinator.setShowPorts(settings.display.showPorts);
    coordinator.setShowLabels(settings.display.showLabels);
    coordinator.setShowAnnotations(settings.display.showAnnotations);
    coordinator.setShowStereotypes(settings.display.showStereotypes);
    coordinator.setShowPortAnnotations(settings.display.showPortAnnotations);
    coordinator.setUseCustomColors(settings.style.useCustomColors);
    coordinator.setFillColor(settings.style.fillColor);
    coordinator.setOutlineColor(settings.style.outlineColor);
    coordinator.setLabelColor(settings.style.labelColor);
    coordinator.setBlockOffsets(settings.offsets);
}

bool panelEquals(const PanelSettings& a, const PanelSettings& b)
{
    return layoutEquals(a.layout, b.layout)
        && a.display.showPorts == b.display.showPorts
        && a.display.showLabels == b.display.showLabels
        && a.display.showAnnotations == b.display.showAnnotations
        && a.display.showStereotypes == b.display.showStereotypes
        && a.display.showPortAnnotations == b.display.showPortAnnotations
        && a.style.useCustomColors == b.style.useCustomColors
        && a.style.fillColor == b.style.fillColor
        && a.style.outlineColor == b.style.outlineColor
        && a.style.labelColor == b.style.labelColor
        && offsetsEqual(a.offsets, b.offsets);
}

PanelSettings loadDefaultPanel()
{
    Utils::Environment env = AiePanelState::makeEnvironment();
    const auto loaded = env.loadState(Utils::EnvironmentScope::Global, kStateName);
    const PanelSettings fallback = panelDefaults();
    if (loaded.status != Utils::DocumentLoadResult::Status::Ok)
        return fallback;
    return panelFromSettingsObject(loaded.object, fallback);
}

} // namespace Aie::Internal
