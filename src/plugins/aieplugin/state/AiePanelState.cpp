// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/state/AiePanelState.hpp"

#include "aieplugin/AieCanvasCoordinator.hpp"

#include <QtCore/QJsonObject>

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
const QString kShowPortsKey = u"showPorts"_s;
const QString kShowLabelsKey = u"showLabels"_s;
const QString kShowAnnotationsKey = u"showAnnotations"_s;
const QString kShowStereotypesKey = u"showStereotypes"_s;
const QString kShowPortAnnotationsKey = u"showPortAnnotations"_s;
const QString kWireAnnotationsVisibilityKey = u"wireAnnotationsVisibility"_s;
const QString kWireAnnotationsDetailKey = u"wireAnnotationsDetail"_s;
const QString kWireAnnotationsScaleWithZoomKey = u"wireAnnotationsScaleWithZoom"_s;
const QString kKeepoutKey = u"keepoutMargin"_s;
const QString kUseCustomColorsKey = u"useCustomColors"_s;
const QString kFillKey = u"fillColor"_s;
const QString kOutlineKey = u"outlineColor"_s;
const QString kLabelKey = u"labelColor"_s;

QString colorToString(const QColor& color)
{
    return color.name(QColor::HexArgb);
}

QColor colorFromString(const QString& text, const QColor& fallback)
{
    QColor c(text);
    return c.isValid() ? c : fallback;
}

} // namespace

AiePanelState::AiePanelState(AieCanvasCoordinator* coordinator, QObject* parent)
    : QObject(parent)
    , m_env(makeEnvironment())
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(250);
    connect(&m_saveTimer, &QTimer::timeout, this, &AiePanelState::saveState);
    setCoordinator(coordinator);
}

Utils::Environment AiePanelState::makeEnvironment()
{
    Utils::EnvironmentConfig cfg;
    cfg.organizationName = QStringLiteral("IRONSmith");
    cfg.applicationName = QStringLiteral("IRONSmith");
    return Utils::Environment(cfg);
}

void AiePanelState::setCoordinator(AieCanvasCoordinator* coordinator)
{
    if (m_coordinator == coordinator)
        return;

    if (m_coordinator)
        disconnect(m_coordinator, nullptr, this, nullptr);

    m_coordinator = coordinator;
    if (!m_coordinator)
        return;

    connect(m_coordinator, &AieCanvasCoordinator::horizontalSpacingChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::verticalSpacingChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::outwardSpreadChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::autoCellSizeChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::cellSizeChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::showPortsChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::showLabelsChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::showAnnotationsChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::keepoutMarginChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::useCustomColorsChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::fillColorChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::outlineColorChanged, this, &AiePanelState::scheduleSave);
    connect(m_coordinator, &AieCanvasCoordinator::labelColorChanged, this, &AiePanelState::scheduleSave);

    loadState();
}

void AiePanelState::setDefaultsPersistenceEnabled(bool enabled)
{
    m_persistDefaults = enabled;
}

void AiePanelState::loadState()
{
    const auto loaded = m_env.loadState(Utils::EnvironmentScope::Global, kStateName);
    if (loaded.status != Utils::DocumentLoadResult::Status::Ok)
        return;

    apply(loaded.object);
}

void AiePanelState::apply(const QJsonObject& state)
{
    if (!m_coordinator)
        return;

    m_applying = true;

    if (state.contains(kHorizontalSpacingKey))
        m_coordinator->setHorizontalSpacing(state.value(kHorizontalSpacingKey).toDouble(m_coordinator->horizontalSpacing()));
    if (state.contains(kVerticalSpacingKey))
        m_coordinator->setVerticalSpacing(state.value(kVerticalSpacingKey).toDouble(m_coordinator->verticalSpacing()));
    if (state.contains(kOutwardSpreadKey))
        m_coordinator->setOutwardSpread(state.value(kOutwardSpreadKey).toDouble(m_coordinator->outwardSpread()));

    if (!state.contains(kHorizontalSpacingKey) && !state.contains(kVerticalSpacingKey) && state.contains(kTileSpacingKey)) {
        m_coordinator->setTileSpacing(state.value(kTileSpacingKey).toDouble(m_coordinator->tileSpacing()));
    }
    if (!state.contains(kOutwardSpreadKey) && state.contains(kOuterMarginKey)) {
        m_coordinator->setOutwardSpread(state.value(kOuterMarginKey).toDouble(m_coordinator->outwardSpread()));
    }
    if (state.contains(kAutoCellSizeKey))
        m_coordinator->setAutoCellSize(state.value(kAutoCellSizeKey).toBool(m_coordinator->autoCellSize()));
    if (state.contains(kCellSizeKey))
        m_coordinator->setCellSize(state.value(kCellSizeKey).toDouble(m_coordinator->cellSize()));
    if (state.contains(kShowPortsKey))
        m_coordinator->setShowPorts(state.value(kShowPortsKey).toBool(m_coordinator->showPorts()));
    if (state.contains(kShowLabelsKey))
        m_coordinator->setShowLabels(state.value(kShowLabelsKey).toBool(m_coordinator->showLabels()));
    if (state.contains(kShowAnnotationsKey))
        m_coordinator->setShowAnnotations(state.value(kShowAnnotationsKey).toBool(m_coordinator->showAnnotations()));
    if (state.contains(kShowStereotypesKey))
        m_coordinator->setShowStereotypes(state.value(kShowStereotypesKey).toBool(m_coordinator->showStereotypes()));
    if (state.contains(kShowPortAnnotationsKey))
        m_coordinator->setShowPortAnnotations(state.value(kShowPortAnnotationsKey).toBool(m_coordinator->showPortAnnotations()));
    if (state.contains(kWireAnnotationsVisibilityKey)) {
        const int rawMode = state.value(kWireAnnotationsVisibilityKey).toInt(static_cast<int>(m_coordinator->wireAnnotationVisibilityMode()));
        m_coordinator->setWireAnnotationVisibilityMode(static_cast<AieCanvasCoordinator::WireAnnotationVisibilityMode>(rawMode));
    }
    if (state.contains(kWireAnnotationsDetailKey)) {
        const int rawMode = state.value(kWireAnnotationsDetailKey).toInt(static_cast<int>(m_coordinator->wireAnnotationDetailMode()));
        m_coordinator->setWireAnnotationDetailMode(static_cast<AieCanvasCoordinator::WireAnnotationDetailMode>(rawMode));
    }
    if (state.contains(kWireAnnotationsScaleWithZoomKey))
        m_coordinator->setWireAnnotationsScaleWithZoom(state.value(kWireAnnotationsScaleWithZoomKey).toBool(m_coordinator->wireAnnotationsScaleWithZoom()));
    if (state.contains(kKeepoutKey))
        m_coordinator->setKeepoutMargin(state.value(kKeepoutKey).toDouble(m_coordinator->keepoutMargin()));
    if (state.contains(kUseCustomColorsKey))
        m_coordinator->setUseCustomColors(state.value(kUseCustomColorsKey).toBool(m_coordinator->useCustomColors()));

    if (state.contains(kFillKey))
        m_coordinator->setFillColor(colorFromString(state.value(kFillKey).toString(), m_coordinator->fillColor()));
    if (state.contains(kOutlineKey))
        m_coordinator->setOutlineColor(colorFromString(state.value(kOutlineKey).toString(), m_coordinator->outlineColor()));
    if (state.contains(kLabelKey))
        m_coordinator->setLabelColor(colorFromString(state.value(kLabelKey).toString(), m_coordinator->labelColor()));

    m_applying = false;
}

QJsonObject AiePanelState::snapshot() const
{
    QJsonObject obj;
    if (!m_coordinator)
        return obj;

    obj.insert(kHorizontalSpacingKey, m_coordinator->horizontalSpacing());
    obj.insert(kVerticalSpacingKey, m_coordinator->verticalSpacing());
    obj.insert(kOutwardSpreadKey, m_coordinator->outwardSpread());
    obj.insert(kAutoCellSizeKey, m_coordinator->autoCellSize());
    obj.insert(kCellSizeKey, m_coordinator->cellSize());
    obj.insert(kShowPortsKey, m_coordinator->showPorts());
    obj.insert(kShowLabelsKey, m_coordinator->showLabels());
    obj.insert(kShowAnnotationsKey, m_coordinator->showAnnotations());
    obj.insert(kShowStereotypesKey, m_coordinator->showStereotypes());
    obj.insert(kShowPortAnnotationsKey, m_coordinator->showPortAnnotations());
    obj.insert(kWireAnnotationsVisibilityKey, static_cast<int>(m_coordinator->wireAnnotationVisibilityMode()));
    obj.insert(kWireAnnotationsDetailKey, static_cast<int>(m_coordinator->wireAnnotationDetailMode()));
    obj.insert(kWireAnnotationsScaleWithZoomKey, m_coordinator->wireAnnotationsScaleWithZoom());
    obj.insert(kKeepoutKey, m_coordinator->keepoutMargin());
    obj.insert(kUseCustomColorsKey, m_coordinator->useCustomColors());
    obj.insert(kFillKey, colorToString(m_coordinator->fillColor()));
    obj.insert(kOutlineKey, colorToString(m_coordinator->outlineColor()));
    obj.insert(kLabelKey, colorToString(m_coordinator->labelColor()));
    return obj;
}

void AiePanelState::scheduleSave()
{
    if (m_applying)
        return;
    if (!m_persistDefaults)
        return;
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void AiePanelState::saveState()
{
    m_env.saveState(Utils::EnvironmentScope::Global, kStateName, snapshot());
}

} // namespace Aie::Internal
