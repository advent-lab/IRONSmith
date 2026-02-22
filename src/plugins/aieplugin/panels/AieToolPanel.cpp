// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/AieToolPanel.hpp"

#include "aieplugin/AieCanvasCoordinator.hpp"

#include <utils/ui/LabeledSlider.hpp>
#include <utils/ui/SidebarPanelFrame.hpp>

#include <QtCore/QSignalBlocker>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <cmath>

namespace Aie::Internal {

namespace {

struct ToolControls final {
    Utils::LabeledSlider* horizontalSpacing = nullptr;
    Utils::LabeledSlider* verticalSpacing = nullptr;
    Utils::LabeledSlider* outwardSpread = nullptr;
    Utils::LabeledSlider* nudgeStep = nullptr;

    QCheckBox* autoCell = nullptr;
    Utils::LabeledSlider* cellSize = nullptr;
    QWidget* cellSizeControl = nullptr;

    QCheckBox* showPorts = nullptr;
    QCheckBox* showLabels = nullptr;
    QCheckBox* showAnnotations = nullptr;
    QCheckBox* showStereotypes = nullptr;
    QCheckBox* showPortAnnotations = nullptr;
    Utils::LabeledSlider* keepout = nullptr;
    QComboBox* wireVisibility = nullptr;
    QComboBox* wireDetail = nullptr;
    QCheckBox* wireScaleWithZoom = nullptr;
};

Utils::LabeledSlider* makeSlider(int min, int max, int step)
{
    auto* slider = new Utils::LabeledSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setSingleStep(step);
    slider->setPageStep(std::max(step * 4, 1));
    return slider;
}

QWidget* makeSliderControl(Utils::LabeledSlider* slider, QWidget* parent)
{
    auto* host = new QWidget(parent);
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* decrease = new QToolButton(host);
    decrease->setText(QStringLiteral("-"));
    decrease->setAutoRepeat(true);
    decrease->setAutoRaise(true);
    decrease->setFixedWidth(22);

    auto* increase = new QToolButton(host);
    increase->setText(QStringLiteral("+"));
    increase->setAutoRepeat(true);
    increase->setAutoRaise(true);
    increase->setFixedWidth(22);

    const auto applyStep = [slider](int direction) {
        if (!slider)
            return;
        const int step = std::max(1, slider->singleStep());
        const int nextValue = slider->value() + direction * step;
        slider->setValue(std::clamp(nextValue, slider->minimum(), slider->maximum()));
    };

    QObject::connect(decrease, &QToolButton::clicked, host, [applyStep]() { applyStep(-1); });
    QObject::connect(increase, &QToolButton::clicked, host, [applyStep]() { applyStep(1); });

    layout->addWidget(decrease);
    layout->addWidget(slider, 1);
    layout->addWidget(increase);
    return host;
}

void syncControlsFromCoordinator(const ToolControls& controls, AieCanvasCoordinator* coordinator)
{
    if (!coordinator)
        return;

    QSignalBlocker blockHSpacing(controls.horizontalSpacing);
    QSignalBlocker blockVSpacing(controls.verticalSpacing);
    QSignalBlocker blockOutward(controls.outwardSpread);
    QSignalBlocker blockAuto(controls.autoCell);
    QSignalBlocker blockCell(controls.cellSize);
    QSignalBlocker blockPorts(controls.showPorts);
    QSignalBlocker blockLabels(controls.showLabels);
    QSignalBlocker blockAnnotations(controls.showAnnotations);
    QSignalBlocker blockStereotypes(controls.showStereotypes);
    QSignalBlocker blockPortAnnotations(controls.showPortAnnotations);
    QSignalBlocker blockKeepout(controls.keepout);
    QSignalBlocker blockWireVisibility(controls.wireVisibility);
    QSignalBlocker blockWireDetail(controls.wireDetail);
    QSignalBlocker blockWireScale(controls.wireScaleWithZoom);

    controls.horizontalSpacing->setValue(static_cast<int>(std::lround(coordinator->horizontalSpacing())));
    controls.verticalSpacing->setValue(static_cast<int>(std::lround(coordinator->verticalSpacing())));
    controls.outwardSpread->setValue(static_cast<int>(std::lround(coordinator->outwardSpread())));
    controls.autoCell->setChecked(coordinator->autoCellSize());
    controls.cellSize->setValue(static_cast<int>(std::lround(coordinator->cellSize())));
    controls.cellSizeControl->setEnabled(!coordinator->autoCellSize());

    controls.showPorts->setChecked(coordinator->showPorts());
    controls.showLabels->setChecked(coordinator->showLabels());
    controls.showAnnotations->setChecked(coordinator->showAnnotations());
    controls.showStereotypes->setChecked(coordinator->showStereotypes());
    controls.showPortAnnotations->setChecked(coordinator->showPortAnnotations());
    controls.keepout->setValue(static_cast<int>(std::lround(coordinator->keepoutMargin())));

    const int visibilityMode = static_cast<int>(coordinator->wireAnnotationVisibilityMode());
    const int detailMode = static_cast<int>(coordinator->wireAnnotationDetailMode());
    controls.wireVisibility->setCurrentIndex(std::max(0, controls.wireVisibility->findData(visibilityMode)));
    controls.wireDetail->setCurrentIndex(std::max(0, controls.wireDetail->findData(detailMode)));
    controls.wireScaleWithZoom->setChecked(coordinator->wireAnnotationsScaleWithZoom());
}

void connectSpacingSlider(Utils::LabeledSlider* slider,
                          AieCanvasCoordinator* coordinator,
                          AieCanvasCoordinator::SelectionSpacingAxis axis,
                          void (AieCanvasCoordinator::*setter)(double))
{
    QObject::connect(slider, &Utils::LabeledSlider::valueChanged, slider,
                     [coordinator, setter](int value) {
                         if (coordinator)
                             (coordinator->*setter)(static_cast<double>(value));
                     });

    QObject::connect(slider, &Utils::LabeledSlider::sliderPressed, slider,
                     [coordinator, axis]() {
                         if (coordinator)
                             coordinator->beginSelectionSpacing(axis);
                     });

    QObject::connect(slider, &Utils::LabeledSlider::valueChanged, slider,
                     [coordinator, axis](int value) {
                         if (coordinator)
                             coordinator->updateSelectionSpacing(axis, static_cast<double>(value));
                     });

    QObject::connect(slider, &Utils::LabeledSlider::sliderReleased, slider,
                     [coordinator, axis]() {
                         if (coordinator)
                             coordinator->endSelectionSpacing(axis);
                     });
}

void connectNudgeButton(QPushButton* button,
                        Utils::LabeledSlider* stepSlider,
                        AieCanvasCoordinator* coordinator,
                        double dx,
                        double dy)
{
    QObject::connect(button, &QPushButton::clicked, button,
                     [stepSlider, coordinator, dx, dy]() {
                         if (!coordinator)
                             return;
                         const double step = static_cast<double>(stepSlider->value());
                         coordinator->nudgeSelection(dx * step, dy * step);
                     });
}

void bindControlsToCoordinator(const ToolControls& controls, AieCanvasCoordinator* coordinator)
{
    connectSpacingSlider(controls.horizontalSpacing,
                         coordinator,
                         AieCanvasCoordinator::SelectionSpacingAxis::Horizontal,
                         &AieCanvasCoordinator::setHorizontalSpacing);
    connectSpacingSlider(controls.verticalSpacing,
                         coordinator,
                         AieCanvasCoordinator::SelectionSpacingAxis::Vertical,
                         &AieCanvasCoordinator::setVerticalSpacing);
    connectSpacingSlider(controls.outwardSpread,
                         coordinator,
                         AieCanvasCoordinator::SelectionSpacingAxis::Outward,
                         &AieCanvasCoordinator::setOutwardSpread);

    QObject::connect(controls.autoCell, &QCheckBox::toggled,
                     coordinator, &AieCanvasCoordinator::setAutoCellSize);

    QObject::connect(controls.cellSize, &Utils::LabeledSlider::valueChanged, controls.cellSize,
                     [coordinator](int value) {
                         if (coordinator)
                             coordinator->setCellSize(static_cast<double>(value));
                     });

    QObject::connect(controls.showPorts, &QCheckBox::toggled,
                     coordinator, &AieCanvasCoordinator::setShowPorts);
    QObject::connect(controls.showLabels, &QCheckBox::toggled,
                     coordinator, &AieCanvasCoordinator::setShowLabels);
    QObject::connect(controls.showAnnotations, &QCheckBox::toggled,
                     coordinator, &AieCanvasCoordinator::setShowAnnotations);
    QObject::connect(controls.showStereotypes, &QCheckBox::toggled,
                     coordinator, &AieCanvasCoordinator::setShowStereotypes);
    QObject::connect(controls.showPortAnnotations, &QCheckBox::toggled,
                     coordinator, &AieCanvasCoordinator::setShowPortAnnotations);
    QObject::connect(controls.wireVisibility,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     controls.wireVisibility,
                     [coordinator, combo = controls.wireVisibility](int index) {
                         if (!coordinator || index < 0)
                             return;
                         coordinator->setWireAnnotationVisibilityMode(
                             static_cast<AieCanvasCoordinator::WireAnnotationVisibilityMode>(
                                 combo->itemData(index).toInt()));
                     });
    QObject::connect(controls.wireDetail,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     controls.wireDetail,
                     [coordinator, combo = controls.wireDetail](int index) {
                         if (!coordinator || index < 0)
                             return;
                         coordinator->setWireAnnotationDetailMode(
                             static_cast<AieCanvasCoordinator::WireAnnotationDetailMode>(
                                 combo->itemData(index).toInt()));
                     });
    QObject::connect(controls.wireScaleWithZoom, &QCheckBox::toggled,
                     coordinator, &AieCanvasCoordinator::setWireAnnotationsScaleWithZoom);

    QObject::connect(controls.keepout, &Utils::LabeledSlider::valueChanged, controls.keepout,
                     [coordinator](int value) {
                         if (coordinator)
                             coordinator->setKeepoutMargin(static_cast<double>(value));
                     });
}

void bindCoordinatorToControls(const ToolControls& controls, AieCanvasCoordinator* coordinator)
{
    QObject::connect(coordinator, &AieCanvasCoordinator::autoCellSizeChanged, controls.cellSizeControl,
                     [cellSizeControl = controls.cellSizeControl](bool enabled) {
                         cellSizeControl->setEnabled(!enabled);
                     });

    QObject::connect(coordinator, &AieCanvasCoordinator::horizontalSpacingChanged, controls.horizontalSpacing,
                     [slider = controls.horizontalSpacing](double value) {
                         QSignalBlocker block(slider);
                         slider->setValue(static_cast<int>(std::lround(value)));
                     });

    QObject::connect(coordinator, &AieCanvasCoordinator::verticalSpacingChanged, controls.verticalSpacing,
                     [slider = controls.verticalSpacing](double value) {
                         QSignalBlocker block(slider);
                         slider->setValue(static_cast<int>(std::lround(value)));
                     });

    QObject::connect(coordinator, &AieCanvasCoordinator::outwardSpreadChanged, controls.outwardSpread,
                     [slider = controls.outwardSpread](double value) {
                         QSignalBlocker block(slider);
                         slider->setValue(static_cast<int>(std::lround(value)));
                     });

    QObject::connect(coordinator, &AieCanvasCoordinator::autoCellSizeChanged, controls.autoCell,
                     [check = controls.autoCell](bool enabled) {
                         QSignalBlocker block(check);
                         check->setChecked(enabled);
                     });

    QObject::connect(coordinator, &AieCanvasCoordinator::cellSizeChanged, controls.cellSize,
                     [slider = controls.cellSize](double value) {
                         QSignalBlocker block(slider);
                         slider->setValue(static_cast<int>(std::lround(value)));
                     });

    QObject::connect(coordinator, &AieCanvasCoordinator::showPortsChanged, controls.showPorts,
                     [check = controls.showPorts](bool enabled) {
                         QSignalBlocker block(check);
                         check->setChecked(enabled);
                     });
    QObject::connect(coordinator, &AieCanvasCoordinator::showLabelsChanged, controls.showLabels,
                     [check = controls.showLabels](bool enabled) {
                         QSignalBlocker block(check);
                         check->setChecked(enabled);
                     });
    QObject::connect(coordinator, &AieCanvasCoordinator::showAnnotationsChanged, controls.showAnnotations,
                     [coordinator, controls](bool enabled) {
                         QSignalBlocker blockAnnotations(controls.showAnnotations);
                         QSignalBlocker blockStereotypes(controls.showStereotypes);
                         QSignalBlocker blockPortLabels(controls.showPortAnnotations);
                         controls.showAnnotations->setChecked(enabled);
                         controls.showStereotypes->setChecked(coordinator->showStereotypes());
                         controls.showPortAnnotations->setChecked(coordinator->showPortAnnotations());
                         controls.wireVisibility->setCurrentIndex(
                             std::max(0, controls.wireVisibility->findData(
                                              static_cast<int>(coordinator->wireAnnotationVisibilityMode()))));
                         controls.wireDetail->setCurrentIndex(
                             std::max(0, controls.wireDetail->findData(
                                              static_cast<int>(coordinator->wireAnnotationDetailMode()))));
                         controls.wireScaleWithZoom->setChecked(coordinator->wireAnnotationsScaleWithZoom());
                     });

    QObject::connect(coordinator, &AieCanvasCoordinator::keepoutMarginChanged, controls.keepout,
                     [slider = controls.keepout](double value) {
                         QSignalBlocker block(slider);
                         slider->setValue(static_cast<int>(std::lround(value)));
                     });
}

} // namespace

AieToolPanel::AieToolPanel(AieCanvasCoordinator* coordinator, QWidget* parent)
    : QWidget(parent)
    , m_coordinator(coordinator)
{
    buildUi();
}

void AieToolPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* frame = new Utils::SidebarPanelFrame(this);
    frame->setTitle(QStringLiteral("AIE Grid"));
    frame->setSearchEnabled(false);
    frame->setHeaderDividerVisible(true);

    auto* scrollArea = new QScrollArea(frame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setObjectName(QStringLiteral("AieToolPanelScrollArea"));

    auto* content = new QWidget(scrollArea);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(12);

    ToolControls controls;

    auto* layoutGroup = new QGroupBox(QStringLiteral("Layout"), content);
    auto* layoutForm = new QFormLayout(layoutGroup);
    layoutForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layoutForm->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);

    controls.horizontalSpacing = makeSlider(0, 512, 1);
    controls.verticalSpacing = makeSlider(0, 512, 1);
    controls.outwardSpread = makeSlider(0, 512, 1);
    controls.autoCell = new QCheckBox(QStringLiteral("Auto size"), layoutGroup);
    controls.cellSize = makeSlider(24, 200, 2);

    layoutForm->addRow(QStringLiteral("Horizontal spacing"), makeSliderControl(controls.horizontalSpacing, layoutGroup));
    layoutForm->addRow(QStringLiteral("Vertical spacing"), makeSliderControl(controls.verticalSpacing, layoutGroup));
    layoutForm->addRow(QStringLiteral("Outward spread"), makeSliderControl(controls.outwardSpread, layoutGroup));
    layoutForm->addRow(QString(), controls.autoCell);
    controls.cellSizeControl = makeSliderControl(controls.cellSize, layoutGroup);
    layoutForm->addRow(QStringLiteral("Cell size"), controls.cellSizeControl);

    auto* selectionGroup = new QGroupBox(QStringLiteral("Selection"), content);
    auto* selectionLayout = new QVBoxLayout(selectionGroup);
    selectionLayout->setSpacing(6);
    auto* selectionForm = new QFormLayout();
    selectionForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    selectionForm->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);

    controls.nudgeStep = makeSlider(1, 64, 1);
    controls.nudgeStep->setValue(8);
    selectionForm->addRow(QStringLiteral("Nudge step"), makeSliderControl(controls.nudgeStep, selectionGroup));

    auto* nudgeGrid = new QGridLayout();
    nudgeGrid->setHorizontalSpacing(6);
    nudgeGrid->setVerticalSpacing(6);
    auto* nudgeUpButton = new QPushButton(QStringLiteral("Up"), selectionGroup);
    auto* nudgeDownButton = new QPushButton(QStringLiteral("Down"), selectionGroup);
    auto* nudgeLeftButton = new QPushButton(QStringLiteral("Left"), selectionGroup);
    auto* nudgeRightButton = new QPushButton(QStringLiteral("Right"), selectionGroup);
    nudgeGrid->addWidget(nudgeUpButton, 0, 1);
    nudgeGrid->addWidget(nudgeLeftButton, 1, 0);
    nudgeGrid->addWidget(nudgeRightButton, 1, 2);
    nudgeGrid->addWidget(nudgeDownButton, 2, 1);

    selectionLayout->addLayout(selectionForm);
    selectionLayout->addLayout(nudgeGrid);

    auto* displayGroup = new QGroupBox(QStringLiteral("Display"), content);
    auto* displayForm = new QFormLayout(displayGroup);
    displayForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    displayForm->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);

    controls.showPorts = new QCheckBox(QStringLiteral("Show ports"), displayGroup);
    displayForm->addRow(QString(), controls.showPorts);

    auto* annotationsGroup = new QGroupBox(QStringLiteral("Annotations"), content);
    auto* annotationsForm = new QFormLayout(annotationsGroup);
    annotationsForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    annotationsForm->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);

    controls.showAnnotations = new QCheckBox(QStringLiteral("Enable annotations"), annotationsGroup);
    controls.showLabels = new QCheckBox(QStringLiteral("Show block labels"), annotationsGroup);
    controls.showStereotypes = new QCheckBox(QStringLiteral("Show stereotypes"), annotationsGroup);
    controls.showPortAnnotations = new QCheckBox(QStringLiteral("Show port labels"), annotationsGroup);
    controls.keepout = makeSlider(-1, 40, 1);
    controls.keepout->setSpecialValue(-1, QStringLiteral("Auto"));
    controls.keepout->setValue(-1);

    annotationsForm->addRow(QString(), controls.showAnnotations);
    annotationsForm->addRow(QString(), controls.showLabels);
    annotationsForm->addRow(QString(), controls.showStereotypes);
    annotationsForm->addRow(QString(), controls.showPortAnnotations);
    annotationsForm->addRow(QStringLiteral("Keepout"), makeSliderControl(controls.keepout, annotationsGroup));

    auto* wireAnnotationsGroup = new QGroupBox(QStringLiteral("Wire Annotations"), content);
    auto* wireAnnotationsForm = new QFormLayout(wireAnnotationsGroup);
    wireAnnotationsForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    wireAnnotationsForm->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);

    controls.wireVisibility = new QComboBox(wireAnnotationsGroup);
    controls.wireVisibility->addItem(QStringLiteral("Auto (selection/hover)"),
                                     static_cast<int>(AieCanvasCoordinator::WireAnnotationVisibilityMode::Auto));
    controls.wireVisibility->addItem(QStringLiteral("Show all"),
                                     static_cast<int>(AieCanvasCoordinator::WireAnnotationVisibilityMode::ShowAll));
    controls.wireVisibility->addItem(QStringLiteral("Hide all"),
                                     static_cast<int>(AieCanvasCoordinator::WireAnnotationVisibilityMode::Hidden));

    controls.wireDetail = new QComboBox(wireAnnotationsGroup);
    controls.wireDetail->addItem(QStringLiteral("Adaptive"),
                                 static_cast<int>(AieCanvasCoordinator::WireAnnotationDetailMode::Adaptive));
    controls.wireDetail->addItem(QStringLiteral("Compact"),
                                 static_cast<int>(AieCanvasCoordinator::WireAnnotationDetailMode::Compact));
    controls.wireDetail->addItem(QStringLiteral("Expanded"),
                                 static_cast<int>(AieCanvasCoordinator::WireAnnotationDetailMode::Full));

    controls.wireScaleWithZoom = new QCheckBox(QStringLiteral("Scale with zoom"), wireAnnotationsGroup);

    wireAnnotationsForm->addRow(QStringLiteral("Visibility"), controls.wireVisibility);
    wireAnnotationsForm->addRow(QStringLiteral("Detail"), controls.wireDetail);
    wireAnnotationsForm->addRow(QString(), controls.wireScaleWithZoom);

    layout->addWidget(layoutGroup);
    layout->addWidget(selectionGroup);
    layout->addWidget(displayGroup);
    layout->addWidget(annotationsGroup);
    layout->addWidget(wireAnnotationsGroup);
    layout->addStretch(1);

    scrollArea->setWidget(content);
    frame->setContentWidget(scrollArea);
    root->addWidget(frame);

    if (!m_coordinator)
        return;

    bindControlsToCoordinator(controls, m_coordinator);
    connectNudgeButton(nudgeUpButton, controls.nudgeStep, m_coordinator, 0.0, -1.0);
    connectNudgeButton(nudgeDownButton, controls.nudgeStep, m_coordinator, 0.0, 1.0);
    connectNudgeButton(nudgeLeftButton, controls.nudgeStep, m_coordinator, -1.0, 0.0);
    connectNudgeButton(nudgeRightButton, controls.nudgeStep, m_coordinator, 1.0, 0.0);
    bindCoordinatorToControls(controls, m_coordinator);
    syncControlsFromCoordinator(controls, m_coordinator);
}

} // namespace Aie::Internal
