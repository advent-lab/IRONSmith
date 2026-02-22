// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/AieCanvasCoordinator.hpp"

#include "aieplugin/AieConstants.hpp"

#include "canvas/api/ICanvasGridHost.hpp"
#include "canvas/api/ICanvasHost.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasController.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasView.hpp"

#include <QtCore/QLoggingCategory>
#include <QtCore/QMarginsF>
#include <QtCore/QtGlobal>
#include <QtCore/QSet>

#include <algorithm>
#include <limits>

Q_LOGGING_CATEGORY(aiecanvaslog, "ironsmith.aie.canvas")

namespace Aie {

namespace {

constexpr int kApplyDebounceMs = 50;

Canvas::WireAnnotationVisibilityMode toCanvasVisibilityMode(AieCanvasCoordinator::WireAnnotationVisibilityMode mode)
{
    switch (mode) {
        case AieCanvasCoordinator::WireAnnotationVisibilityMode::Auto:
            return Canvas::WireAnnotationVisibilityMode::Auto;
        case AieCanvasCoordinator::WireAnnotationVisibilityMode::ShowAll:
            return Canvas::WireAnnotationVisibilityMode::ShowAll;
        case AieCanvasCoordinator::WireAnnotationVisibilityMode::Hidden:
            return Canvas::WireAnnotationVisibilityMode::Hidden;
    }
    return Canvas::WireAnnotationVisibilityMode::Auto;
}

Canvas::WireAnnotationDetailMode toCanvasDetailMode(AieCanvasCoordinator::WireAnnotationDetailMode mode)
{
    switch (mode) {
        case AieCanvasCoordinator::WireAnnotationDetailMode::Adaptive:
            return Canvas::WireAnnotationDetailMode::Adaptive;
        case AieCanvasCoordinator::WireAnnotationDetailMode::Compact:
            return Canvas::WireAnnotationDetailMode::Compact;
        case AieCanvasCoordinator::WireAnnotationDetailMode::Full:
            return Canvas::WireAnnotationDetailMode::Full;
    }
    return Canvas::WireAnnotationDetailMode::Adaptive;
}

struct SelectionBlockInfo final {
    Canvas::ObjectId id{};
    QString specId;
    QRectF bounds;
    QPointF baseOffset;

    QPointF topLeft() const { return bounds.topLeft(); }
    QSizeF size() const { return bounds.size(); }
    QPointF center() const { return bounds.center(); }
};

struct SelectionLayout final {
    QVector<SelectionBlockInfo> blocks;
    QRectF bounds;
    QPointF center;
};

} // namespace

struct AieCanvasCoordinator::SelectionSnapshot final {
    SelectionSpacingAxis axis = SelectionSpacingAxis::Horizontal;
    SelectionLayout layout;
};

namespace {

SelectionLayout buildSelectionLayout(Canvas::CanvasDocument* doc,
                                     const QSet<Canvas::ObjectId>& ids,
                                     const QHash<QString, QPointF>& offsets)
{
    SelectionLayout layout;
    if (!doc)
        return layout;

    for (const auto& id : ids) {
        auto* item = doc->findItem(id);
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item);
        if (!block)
            continue;
        const QString specId = block->specId();
        if (specId.isEmpty())
            continue;

        SelectionBlockInfo info;
        info.id = id;
        info.specId = specId;
        info.bounds = block->boundsScene();
        info.baseOffset = offsets.value(specId);
        layout.blocks.push_back(info);
        if (layout.bounds.isNull())
            layout.bounds = info.bounds;
        else
            layout.bounds = layout.bounds.united(info.bounds);
    }

    if (!layout.blocks.isEmpty())
        layout.center = layout.bounds.center();
    return layout;
}

double minBlockExtent(const QVector<SelectionBlockInfo>& blocks, bool horizontal)
{
    double minExtent = std::numeric_limits<double>::max();
    for (const auto& block : blocks) {
        const double extent = horizontal ? block.bounds.width() : block.bounds.height();
        minExtent = std::min(minExtent, extent);
    }
    if (minExtent == std::numeric_limits<double>::max())
        return 0.0;
    return minExtent;
}

QVector<QVector<int>> clusterByAxis(const QVector<SelectionBlockInfo>& blocks, bool byY)
{
    QVector<int> order;
    order.reserve(blocks.size());
    for (int i = 0; i < blocks.size(); ++i)
        order.push_back(i);

    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const QPointF ca = blocks[a].center();
        const QPointF cb = blocks[b].center();
        return byY ? (ca.y() < cb.y()) : (ca.x() < cb.x());
    });

    const double minExtent = minBlockExtent(blocks, !byY);
    const double tolerance = std::max(1.0, minExtent * 0.5);

    QVector<QVector<int>> groups;
    double groupCenter = 0.0;
    int groupCount = 0;

    for (int idx : order) {
        const double pos = byY ? blocks[idx].center().y() : blocks[idx].center().x();
        if (groups.isEmpty() || std::abs(pos - groupCenter) > tolerance) {
            groups.push_back(QVector<int>{idx});
            groupCenter = pos;
            groupCount = 1;
        } else {
            groups.back().push_back(idx);
            groupCenter = (groupCenter * groupCount + pos) / static_cast<double>(groupCount + 1);
            ++groupCount;
        }
    }
    return groups;
}

QHash<Canvas::ObjectId, QPointF> computeHorizontalSpacing(const SelectionLayout& layout, double spacing)
{
    QHash<Canvas::ObjectId, QPointF> targets;
    if (layout.blocks.isEmpty())
        return targets;

    const auto rows = clusterByAxis(layout.blocks, true);
    for (const auto& row : rows) {
        if (row.isEmpty())
            continue;
        QVector<int> ordered = row;
        std::sort(ordered.begin(), ordered.end(), [&](int a, int b) {
            return layout.blocks[a].center().x() < layout.blocks[b].center().x();
        });

        double totalWidth = 0.0;
        for (int idx : ordered)
            totalWidth += layout.blocks[idx].bounds.width();
        const qsizetype gaps = std::max<qsizetype>(0, ordered.size() - 1);
        totalWidth += spacing * static_cast<double>(gaps);

        double centerX = 0.0;
        for (int idx : ordered)
            centerX += layout.blocks[idx].center().x();
        centerX /= static_cast<double>(ordered.size());

        double left = centerX - totalWidth * 0.5;
        for (int idx : ordered) {
            const auto& block = layout.blocks[idx];
            QPointF topLeft(left, block.bounds.top());
            targets.insert(block.id, topLeft);
            left += block.bounds.width() + spacing;
        }
    }
    return targets;
}

QHash<Canvas::ObjectId, QPointF> computeVerticalSpacing(const SelectionLayout& layout, double spacing)
{
    QHash<Canvas::ObjectId, QPointF> targets;
    if (layout.blocks.isEmpty())
        return targets;

    const auto cols = clusterByAxis(layout.blocks, false);
    for (const auto& col : cols) {
        if (col.isEmpty())
            continue;
        QVector<int> ordered = col;
        std::sort(ordered.begin(), ordered.end(), [&](int a, int b) {
            return layout.blocks[a].center().y() < layout.blocks[b].center().y();
        });

        double totalHeight = 0.0;
        for (int idx : ordered)
            totalHeight += layout.blocks[idx].bounds.height();
        const qsizetype gaps = std::max<qsizetype>(0, ordered.size() - 1);
        totalHeight += spacing * static_cast<double>(gaps);

        double centerY = 0.0;
        for (int idx : ordered)
            centerY += layout.blocks[idx].center().y();
        centerY /= static_cast<double>(ordered.size());

        double top = centerY - totalHeight * 0.5;
        for (int idx : ordered) {
            const auto& block = layout.blocks[idx];
            QPointF topLeft(block.bounds.left(), top);
            targets.insert(block.id, topLeft);
            top += block.bounds.height() + spacing;
        }
    }
    return targets;
}

QHash<Canvas::ObjectId, QPointF> computeOutwardSpread(const SelectionLayout& layout, double spread)
{
    QHash<Canvas::ObjectId, QPointF> targets;
    if (layout.blocks.isEmpty())
        return targets;

    const QRectF bounds = layout.bounds;
    const QPointF center = layout.center;
    const double width = bounds.width();
    const double height = bounds.height();

    double scaleX = 1.0;
    double scaleY = 1.0;
    if (width > 1e-3)
        scaleX = (width + 2.0 * spread) / width;
    if (height > 1e-3)
        scaleY = (height + 2.0 * spread) / height;

    for (const auto& block : layout.blocks) {
        const QPointF delta = block.center() - center;
        const QPointF newCenter(center.x() + delta.x() * scaleX,
                                center.y() + delta.y() * scaleY);
        const QPointF topLeft = newCenter - QPointF(block.bounds.width() * 0.5,
                                                    block.bounds.height() * 0.5);
        targets.insert(block.id, topLeft);
    }
    return targets;
}

const SelectionBlockInfo* findBlockInfo(const SelectionLayout& layout, Canvas::ObjectId id)
{
    for (const auto& block : layout.blocks) {
        if (block.id == id)
            return &block;
    }
    return nullptr;
}

} // namespace

AieCanvasCoordinator::AieCanvasCoordinator(QObject* parent)
    : QObject(parent)
    , m_applyDebounce(this)
{
    m_layout.horizontalSpacing = Aie::kDefaultTileSpacing;
    m_layout.verticalSpacing = Aie::kDefaultTileSpacing;
    m_layout.outwardSpread = Aie::kDefaultOuterMargin;
    m_layout.cellSize = Aie::kDefaultCellSize;
    m_layout.keepoutMargin = Aie::kDefaultKeepoutMargin;

    m_colors.fill = QColor(Canvas::Constants::kBlockFillColor);
    m_colors.outline = QColor(Canvas::Constants::kBlockOutlineColor);
    m_colors.label = QColor(Canvas::Constants::kBlockTextColor);

    m_applyDebounce.setDelayMs(kApplyDebounceMs);
    m_applyDebounce.setAction([this]() { applyNow(); });
}

AieCanvasCoordinator::~AieCanvasCoordinator() = default;

bool AieCanvasCoordinator::hasFlag(FlagBit flag) const
{
    const quint8 bit = static_cast<quint8>(flag);
    return (m_flags & bit) != 0u;
}

bool AieCanvasCoordinator::setFlag(FlagBit flag, bool enabled)
{
    const quint8 bit = static_cast<quint8>(flag);
    const bool wasEnabled = (m_flags & bit) != 0u;
    if (wasEnabled == enabled)
        return false;

    if (enabled)
        m_flags |= bit;
    else
        m_flags &= static_cast<quint8>(~bit);
    return true;
}

void AieCanvasCoordinator::setCanvasHost(Canvas::Api::ICanvasHost* host)
{
    if (m_canvasHost == host)
        return;
    m_canvasHost = host;
    applyWireAnnotationSettings();
}

void AieCanvasCoordinator::setGridHost(Canvas::Api::ICanvasGridHost* host)
{
    if (m_gridHost == host)
        return;
    m_gridHost = host;
    requestApply();
}

void AieCanvasCoordinator::setStyleHost(Canvas::Api::ICanvasStyleHost* host)
{
    if (m_styleHost == host)
        return;
    m_styleHost = host;
    requestApply();
}

void AieCanvasCoordinator::setBaseModel(const CanvasGridModel& model)
{
    m_baseModel = model;
    m_blockOffsets.clear();
    requestApply();
}

void AieCanvasCoordinator::setBaseStyles(const QHash<QString, Canvas::Api::CanvasBlockStyle>& styles)
{
    m_baseStyles = styles;
    requestApply();
}

void AieCanvasCoordinator::setBlockLabelOverrides(const QHash<QString, QString>& overrides)
{
    if (m_blockLabelOverrides == overrides)
        return;

    m_blockLabelOverrides = overrides;
    requestApply();
}

void AieCanvasCoordinator::setBlockLabelOverride(const QString& blockId, const QString& label)
{
    const QString cleanedId = blockId.trimmed();
    if (cleanedId.isEmpty())
        return;

    const QString cleanedLabel = label.trimmed();
    if (cleanedLabel.isEmpty()) {
        clearBlockLabelOverride(cleanedId);
        return;
    }

    if (m_blockLabelOverrides.value(cleanedId) == cleanedLabel)
        return;

    m_blockLabelOverrides.insert(cleanedId, cleanedLabel);
    requestApply();
}

void AieCanvasCoordinator::clearBlockLabelOverride(const QString& blockId)
{
    const QString cleanedId = blockId.trimmed();
    if (cleanedId.isEmpty())
        return;

    if (m_blockLabelOverrides.remove(cleanedId) <= 0)
        return;

    requestApply();
}

void AieCanvasCoordinator::clearBlockLabelOverrides()
{
    if (m_blockLabelOverrides.isEmpty())
        return;

    m_blockLabelOverrides.clear();
    requestApply();
}

void AieCanvasCoordinator::setBlockStereotypeOverrides(const QHash<QString, QString>& overrides)
{
    if (m_blockStereotypeOverrides == overrides)
        return;

    m_blockStereotypeOverrides = overrides;
    requestApply();
}

void AieCanvasCoordinator::setBlockStereotypeOverride(const QString& blockId, const QString& stereotype)
{
    const QString cleanedId = blockId.trimmed();
    if (cleanedId.isEmpty())
        return;

    const QString cleanedStereotype = stereotype.trimmed();
    if (cleanedStereotype.isEmpty()) {
        clearBlockStereotypeOverride(cleanedId);
        return;
    }

    if (m_blockStereotypeOverrides.value(cleanedId) == cleanedStereotype)
        return;

    m_blockStereotypeOverrides.insert(cleanedId, cleanedStereotype);
    requestApply();
}

void AieCanvasCoordinator::clearBlockStereotypeOverride(const QString& blockId)
{
    const QString cleanedId = blockId.trimmed();
    if (cleanedId.isEmpty())
        return;

    if (m_blockStereotypeOverrides.remove(cleanedId) <= 0)
        return;

    requestApply();
}

void AieCanvasCoordinator::clearBlockStereotypeOverrides()
{
    if (m_blockStereotypeOverrides.isEmpty())
        return;

    m_blockStereotypeOverrides.clear();
    requestApply();
}

double AieCanvasCoordinator::tileSpacing() const
{
    if (qFuzzyCompare(m_layout.horizontalSpacing, m_layout.verticalSpacing))
        return m_layout.horizontalSpacing;
    return (m_layout.horizontalSpacing + m_layout.verticalSpacing) * 0.5;
}

void AieCanvasCoordinator::setTileSpacing(double spacing)
{
    setHorizontalSpacing(spacing);
    setVerticalSpacing(spacing);
}

void AieCanvasCoordinator::setHorizontalSpacing(double spacing)
{
    spacing = std::max(0.0, spacing);
    if (qFuzzyCompare(m_layout.horizontalSpacing, spacing))
        return;
    m_layout.horizontalSpacing = spacing;
    emit horizontalSpacingChanged(m_layout.horizontalSpacing);
    emit tileSpacingChanged(tileSpacing());
    requestApply();
}

void AieCanvasCoordinator::setVerticalSpacing(double spacing)
{
    spacing = std::max(0.0, spacing);
    if (qFuzzyCompare(m_layout.verticalSpacing, spacing))
        return;
    m_layout.verticalSpacing = spacing;
    emit verticalSpacingChanged(m_layout.verticalSpacing);
    emit tileSpacingChanged(tileSpacing());
    requestApply();
}

void AieCanvasCoordinator::setOutwardSpread(double spread)
{
    spread = std::max(0.0, spread);
    if (qFuzzyCompare(m_layout.outwardSpread, spread))
        return;
    m_layout.outwardSpread = spread;
    emit outwardSpreadChanged(m_layout.outwardSpread);
    emit outerMarginChanged(m_layout.outwardSpread);
    requestApply();
}

void AieCanvasCoordinator::setOuterMargin(double margin)
{
    setOutwardSpread(margin);
}

void AieCanvasCoordinator::setAutoCellSize(bool enabled)
{
    if (!setFlag(FlagBit::AutoCellSize, enabled))
        return;
    emit autoCellSizeChanged(enabled);
    requestApply();
}

void AieCanvasCoordinator::setCellSize(double size)
{
    size = std::max(1.0, size);
    if (qFuzzyCompare(m_layout.cellSize, size))
        return;
    m_layout.cellSize = size;
    emit cellSizeChanged(m_layout.cellSize);
    if (!autoCellSize())
        requestApply();
}

void AieCanvasCoordinator::setShowPorts(bool enabled)
{
    if (!setFlag(FlagBit::ShowPorts, enabled))
        return;
    emit showPortsChanged(enabled);
    requestApply();
}

void AieCanvasCoordinator::setShowLabels(bool enabled)
{
    if (!setFlag(FlagBit::ShowLabels, enabled))
        return;
    emit showLabelsChanged(enabled);
    requestApply();
}

void AieCanvasCoordinator::setShowAnnotations(bool enabled)
{
    if (!setFlag(FlagBit::ShowAnnotations, enabled))
        return;
    emit showAnnotationsChanged(enabled);
    requestApply();
}

void AieCanvasCoordinator::setShowStereotypes(bool enabled)
{
    if (!setFlag(FlagBit::ShowStereotypes, enabled))
        return;
    emit showAnnotationsChanged(showAnnotations());
    requestApply();
}

void AieCanvasCoordinator::setShowPortAnnotations(bool enabled)
{
    if (!setFlag(FlagBit::ShowPortAnnotations, enabled))
        return;
    emit showAnnotationsChanged(showAnnotations());
    requestApply();
}

void AieCanvasCoordinator::setWireAnnotationVisibilityMode(WireAnnotationVisibilityMode mode)
{
    if (m_wireAnnotationVisibilityMode == mode)
        return;
    m_wireAnnotationVisibilityMode = mode;
    applyWireAnnotationSettings();
    emit showAnnotationsChanged(showAnnotations());
}

void AieCanvasCoordinator::setWireAnnotationDetailMode(WireAnnotationDetailMode mode)
{
    if (m_wireAnnotationDetailMode == mode)
        return;
    m_wireAnnotationDetailMode = mode;
    applyWireAnnotationSettings();
    emit showAnnotationsChanged(showAnnotations());
}

void AieCanvasCoordinator::setWireAnnotationsScaleWithZoom(bool enabled)
{
    if (m_wireAnnotationsScaleWithZoom == enabled)
        return;
    m_wireAnnotationsScaleWithZoom = enabled;
    applyWireAnnotationSettings();
    emit showAnnotationsChanged(showAnnotations());
}

void AieCanvasCoordinator::setKeepoutMargin(double margin)
{
    if (qFuzzyCompare(m_layout.keepoutMargin, margin))
        return;
    m_layout.keepoutMargin = margin;
    emit keepoutMarginChanged(m_layout.keepoutMargin);
    requestApply();
}

void AieCanvasCoordinator::setUseCustomColors(bool enabled)
{
    if (!setFlag(FlagBit::UseCustomColors, enabled))
        return;
    emit useCustomColorsChanged(enabled);
    requestApply();
}

void AieCanvasCoordinator::setFillColor(const QColor& color)
{
    if (m_colors.fill == color)
        return;
    m_colors.fill = color;
    emit fillColorChanged(m_colors.fill);
    if (useCustomColors())
        requestApply();
}

void AieCanvasCoordinator::setOutlineColor(const QColor& color)
{
    if (m_colors.outline == color)
        return;
    m_colors.outline = color;
    emit outlineColorChanged(m_colors.outline);
    if (useCustomColors())
        requestApply();
}

void AieCanvasCoordinator::setLabelColor(const QColor& color)
{
    if (m_colors.label == color)
        return;
    m_colors.label = color;
    emit labelColorChanged(m_colors.label);
    if (useCustomColors())
        requestApply();
}

void AieCanvasCoordinator::apply()
{
    if (!m_gridHost || !m_baseModel.gridSpec.isValid())
        return;

    qCDebug(aiecanvaslog) << "AIE apply(): blocks=" << m_baseModel.blocks.size()
                          << "gridValid=" << m_baseModel.gridSpec.isValid();

    Utils::GridSpec spec = m_baseModel.gridSpec;
    const double spread = m_layout.outwardSpread;
    spec.cellSpacing = QSizeF(m_layout.horizontalSpacing + spread, m_layout.verticalSpacing + spread);
    spec.autoCellSize = autoCellSize();
    spec.cellSize = autoCellSize() ? QSizeF() : QSizeF(m_layout.cellSize, m_layout.cellSize);

    QVector<Canvas::Api::CanvasBlockSpec> blocks = m_baseModel.blocks;
    for (auto& block : blocks) {
        const bool basePortLabels = block.showPortLabels;
        block.showPorts = showPorts();
        QString effectiveLabel = block.label;
        const QString overrideLabel = m_blockLabelOverrides.value(block.id).trimmed();
        if (!overrideLabel.isEmpty())
            effectiveLabel = overrideLabel;

        QString effectiveStereotype = block.stereotype;
        const QString overrideStereotype = m_blockStereotypeOverrides.value(block.id).trimmed();
        if (!overrideStereotype.isEmpty())
            effectiveStereotype = overrideStereotype;

        block.label = showLabels() ? effectiveLabel : QString();
        block.stereotype = (showAnnotations() && showStereotypes()) ? effectiveStereotype : QString();
        block.showPortLabels = showAnnotations() && showPortAnnotations() && basePortLabels;
        block.positionOffset = m_blockOffsets.value(block.id);
        if (m_layout.keepoutMargin >= 0.0)
            block.keepoutMargin = m_layout.keepoutMargin;
        else
            block.keepoutMargin = -1.0;

        if (!m_styleHost) {
            if (useCustomColors()) {
                block.hasCustomColors = true;
                block.fillColor = m_colors.fill;
                block.outlineColor = m_colors.outline;
                block.labelColor = m_colors.label;
            } else {
                block.hasCustomColors = false;
            }
        }
    }

    m_gridHost->setGridSpec(spec);
    m_gridHost->setBlocks(blocks);

    if (m_styleHost && !m_baseStyles.isEmpty()) {
        for (auto it = m_baseStyles.begin(); it != m_baseStyles.end(); ++it) {
            Canvas::Api::CanvasBlockStyle style = it.value();
            if (useCustomColors()) {
                style.fillColor = m_colors.fill;
                style.outlineColor = m_colors.outline;
                style.labelColor = m_colors.label;
            }
            m_styleHost->setBlockStyle(it.key(), style);
        }
    }
}

void AieCanvasCoordinator::flushApply()
{
    applyNow();
}

void AieCanvasCoordinator::beginSelectionSpacing(SelectionSpacingAxis axis)
{
    auto* host = m_canvasHost.data();
    auto* doc = host ? host->document() : nullptr;
    auto* controller = host ? host->controller() : nullptr;
    if (!doc || !controller)
        return;

    SelectionLayout layout = buildSelectionLayout(doc, controller->selectedItems(), m_blockOffsets);
    if (layout.blocks.isEmpty())
        return;

    if ((axis == SelectionSpacingAxis::Horizontal || axis == SelectionSpacingAxis::Vertical) &&
        layout.blocks.size() < 2)
        return;

    m_selectionSnapshot = std::make_unique<SelectionSnapshot>();
    m_selectionSnapshot->axis = axis;
    m_selectionSnapshot->layout = std::move(layout);
}

void AieCanvasCoordinator::updateSelectionSpacing(SelectionSpacingAxis axis, double value)
{
    if (!m_selectionSnapshot || m_selectionSnapshot->axis != axis)
        return;

    const double spacing = std::max(0.0, value) * Canvas::Constants::kWorldScale;
    QHash<Canvas::ObjectId, QPointF> targets;
    switch (axis) {
        case SelectionSpacingAxis::Horizontal:
            targets = computeHorizontalSpacing(m_selectionSnapshot->layout, spacing);
            break;
        case SelectionSpacingAxis::Vertical:
            targets = computeVerticalSpacing(m_selectionSnapshot->layout, spacing);
            break;
        case SelectionSpacingAxis::Outward:
            targets = computeOutwardSpread(m_selectionSnapshot->layout, spacing);
            break;
    }

    bool updated = false;
    for (auto it = targets.constBegin(); it != targets.constEnd(); ++it) {
        const SelectionBlockInfo* info = findBlockInfo(m_selectionSnapshot->layout, it.key());
        if (!info)
            continue;
        const QPointF delta = it.value() - info->topLeft();
        m_blockOffsets[info->specId] = info->baseOffset + delta;
        updated = true;
    }
    if (updated)
        requestApply();
}

void AieCanvasCoordinator::endSelectionSpacing(SelectionSpacingAxis axis)
{
    if (!m_selectionSnapshot || m_selectionSnapshot->axis != axis)
        return;

    m_selectionSnapshot.reset();
    requestApply();
}

void AieCanvasCoordinator::nudgeSelection(double dx, double dy)
{
    if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy))
        return;

    auto* host = m_canvasHost.data();
    auto* doc = host ? host->document() : nullptr;
    auto* controller = host ? host->controller() : nullptr;
    if (!doc || !controller)
        return;

    const QPointF delta(dx * Canvas::Constants::kWorldScale,
                        dy * Canvas::Constants::kWorldScale);
    if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()))
        return;

    bool updated = false;
    const auto selected = controller->selectedItems();
    for (const auto& id : selected) {
        auto* item = doc->findItem(id);
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item);
        if (!block)
            continue;
        const QString specId = block->specId();
        if (specId.isEmpty())
            continue;
        m_blockOffsets[specId] += delta;
        updated = true;
    }
    if (updated)
        requestApply();
}

void AieCanvasCoordinator::requestApply()
{
    setFlag(FlagBit::Dirty, true);
    m_applyDebounce.trigger();
}

void AieCanvasCoordinator::applyWireAnnotationSettings()
{
    auto* host = m_canvasHost.data();
    auto* view = host ? qobject_cast<Canvas::CanvasView*>(host->viewWidget()) : nullptr;
    if (!view)
        return;

    view->setWireAnnotationVisibilityMode(toCanvasVisibilityMode(m_wireAnnotationVisibilityMode));
    view->setWireAnnotationDetailMode(toCanvasDetailMode(m_wireAnnotationDetailMode));
    view->setWireAnnotationsScaleWithZoom(m_wireAnnotationsScaleWithZoom);
}

void AieCanvasCoordinator::applyNow()
{
    if (!hasFlag(FlagBit::Dirty))
        return;
    if (!m_gridHost || !m_baseModel.gridSpec.isValid())
        return;
    setFlag(FlagBit::Dirty, false);
    apply();
}

} // namespace Aie
