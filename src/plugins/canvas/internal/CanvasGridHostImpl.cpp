// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/internal/CanvasGridHostImpl.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/CanvasWire.hpp"
#include "canvas/internal/CanvasBlockHandleImpl.hpp"
#include "canvas/api/ICanvasStyleHost.hpp"
#include "canvas/api/CanvasStyleTypes.hpp"

#include "utils/ui/GridLayout.hpp"

#include <QtCore/QEvent>
#include <QtCore/QMarginsF>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QtGlobal>

#include <limits>

namespace Canvas::Internal {

namespace {

constexpr double kFallbackCellSize = Canvas::Constants::kGridStep * 10.0;
constexpr QMarginsF kDefaultContentPadding{Canvas::Constants::kContentPadding,
                                           Canvas::Constants::kContentPadding,
                                           Canvas::Constants::kContentPadding,
                                           Canvas::Constants::kContentPadding};
constexpr double kWorldScale = Canvas::Constants::kWorldScale;

Utils::GridSpec scaledSpec(const Utils::GridSpec& spec)
{
    Utils::GridSpec out = spec;
    out.cellSpacing = QSizeF(spec.cellSpacing.width() * kWorldScale,
                             spec.cellSpacing.height() * kWorldScale);
    out.outerMargin = QMarginsF(spec.outerMargin.left() * kWorldScale,
                                spec.outerMargin.top() * kWorldScale,
                                spec.outerMargin.right() * kWorldScale,
                                spec.outerMargin.bottom() * kWorldScale);
    return out;
}

struct ResolvedBlockStyle final {
    bool hasCustomColors = false;
    QColor fill;
    QColor outline;
    QColor label;
    double cornerRadius = -1.0;
};

ResolvedBlockStyle resolveStyle(const Canvas::Api::CanvasBlockSpec& spec,
                                Canvas::Api::ICanvasStyleHost* styleHost)
{
    ResolvedBlockStyle out;

    Canvas::Api::CanvasBlockStyle style;
    const bool hasStyle = styleHost && !spec.styleKey.trimmed().isEmpty()
                          && styleHost->hasBlockStyle(spec.styleKey);
    if (hasStyle)
        style = styleHost->blockStyle(spec.styleKey);

    if (spec.hasCustomColors) {
        out.hasCustomColors = true;
        out.outline = spec.outlineColor;
        out.fill = spec.fillColor;
        out.label = spec.labelColor;
    } else if (hasStyle && style.hasColors()) {
        out.hasCustomColors = true;
        out.outline = style.outlineColor.isValid() ? style.outlineColor : QColor(Canvas::Constants::kBlockOutlineColor);
        out.fill = style.fillColor.isValid() ? style.fillColor : QColor(Canvas::Constants::kBlockFillColor);
        out.label = style.labelColor.isValid() ? style.labelColor : QColor(Canvas::Constants::kBlockTextColor);
    }

    if (spec.cornerRadius >= 0.0)
        out.cornerRadius = spec.cornerRadius * kWorldScale;
    else if (hasStyle && style.cornerRadius >= 0.0)
        out.cornerRadius = style.cornerRadius * kWorldScale;

    return out;
}

bool applyBlockSpec(Canvas::CanvasBlock* block,
                    const Canvas::Api::CanvasBlockSpec& spec,
                    const QRectF& bounds,
                    const ResolvedBlockStyle& style,
                    bool& geometryChanged,
                    bool& keepoutChanged)
{
    if (!block)
        return false;

    bool changed = false;
    geometryChanged = false;
    keepoutChanged = false;

    if (block->boundsScene() != bounds) {
        block->setBoundsScene(bounds);
        geometryChanged = true;
        changed = true;
    }

    if (block->isMovable() != spec.movable) {
        block->setMovable(spec.movable);
        changed = true;
    }

    if (block->isDeletable() != spec.deletable) {
        block->setDeletable(spec.deletable);
        changed = true;
    }

    if (block->label() != spec.label) {
        block->setLabel(spec.label);
        changed = true;
    }

    if (block->stereotype() != spec.stereotype) {
        block->setStereotype(spec.stereotype);
        changed = true;
    }

    if (block->showPorts() != spec.showPorts) {
        block->setShowPorts(spec.showPorts);
        changed = true;
    }

    if (block->allowMultiplePorts() != spec.allowMultiplePorts) {
        block->setAllowMultiplePorts(spec.allowMultiplePorts);
        changed = true;
    }

    if (spec.hasAutoPortRole) {
        if (!block->hasAutoPortRole() || block->autoPortRole() != spec.autoPortRole) {
            block->setAutoPortRole(spec.autoPortRole);
            changed = true;
        }
    } else if (block->hasAutoPortRole()) {
        block->clearAutoPortRole();
        changed = true;
    }

    if (block->autoOppositeProducerPort() != spec.autoOppositeProducerPort) {
        block->setAutoOppositeProducerPort(spec.autoOppositeProducerPort);
        changed = true;
    }

    if (block->showPortLabels() != spec.showPortLabels) {
        block->setShowPortLabels(spec.showPortLabels);
        changed = true;
    }

    const double keepoutMargin = (spec.keepoutMargin >= 0.0)
                                     ? spec.keepoutMargin * kWorldScale
                                     : -1.0;
    if (!qFuzzyCompare(block->keepoutMargin(), keepoutMargin)) {
        block->setKeepoutMargin(keepoutMargin);
        keepoutChanged = true;
        changed = true;
    }

    if (spec.hasCustomPadding) {
        const QMarginsF scaled(spec.contentPadding.left() * kWorldScale,
                               spec.contentPadding.top() * kWorldScale,
                               spec.contentPadding.right() * kWorldScale,
                               spec.contentPadding.bottom() * kWorldScale);
        if (block->contentPadding() != scaled) {
            block->setContentPadding(scaled);
            changed = true;
        }
    } else if (block->contentPadding() != kDefaultContentPadding) {
        block->setContentPadding(kDefaultContentPadding);
        changed = true;
    }

    if (style.hasCustomColors) {
        if (!block->hasCustomColors() ||
            block->outlineColor() != style.outline ||
            block->fillColor() != style.fill ||
            block->labelColor() != style.label) {
            block->setCustomColors(style.outline, style.fill, style.label);
            changed = true;
        }
    } else if (block->hasCustomColors()) {
        block->clearCustomColors();
        changed = true;
    }

    if (block->cornerRadius() != style.cornerRadius) {
        block->setCornerRadius(style.cornerRadius);
        changed = true;
    }

    return changed;
}

} // namespace

CanvasGridHostImpl::CanvasGridHostImpl(CanvasDocument* document,
                                       CanvasView* view,
                                       Canvas::Api::ICanvasStyleHost* styleHost,
                                       QObject* parent)
    : Canvas::Api::ICanvasGridHost(parent)
    , m_document(document)
    , m_view(view)
    , m_styleHost(styleHost)
    , m_rebuildDebounce(this)
{
    if (m_view)
        m_view->installEventFilter(this);
    m_rebuildDebounce.setDelayMs(0);
    m_rebuildDebounce.setAction([this]() { rebuildBlocks(); });
}

void CanvasGridHostImpl::setGridSpec(const Utils::GridSpec& spec)
{
    if (m_gridSpec.columns == spec.columns &&
        m_gridSpec.rows == spec.rows &&
        m_gridSpec.origin == spec.origin &&
        m_gridSpec.cellSize == spec.cellSize &&
        m_gridSpec.autoCellSize == spec.autoCellSize &&
        m_gridSpec.cellSpacing == spec.cellSpacing &&
        m_gridSpec.outerMargin == spec.outerMargin)
        return;

    m_gridSpec = spec;
    emit gridSpecChanged(m_gridSpec);
    scheduleRebuild();
}

Utils::GridSpec CanvasGridHostImpl::gridSpec() const
{
    return m_gridSpec;
}

void CanvasGridHostImpl::setBlocks(const QVector<Canvas::Api::CanvasBlockSpec>& blocks)
{
    m_blockSpecs = blocks;
    scheduleRebuild();
}

void CanvasGridHostImpl::clearBlocks()
{
    m_blockSpecs.clear();
    scheduleRebuild();
}

Canvas::Api::ICanvasBlockHandle* CanvasGridHostImpl::blockHandle(const QString& id) const
{
    return m_handles.value(id, nullptr);
}

QVector<Canvas::Api::ICanvasBlockHandle*> CanvasGridHostImpl::blockHandles() const
{
    QVector<Canvas::Api::ICanvasBlockHandle*> result;
    result.reserve(m_handles.size());
    for (auto* handle : m_handles)
        result.push_back(handle);
    return result;
}

bool CanvasGridHostImpl::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event && event->type() == QEvent::Resize) {
        if (m_gridSpec.autoCellSize)
            scheduleRebuild();
    }

    return Canvas::Api::ICanvasGridHost::eventFilter(watched, event);
}

void CanvasGridHostImpl::scheduleRebuild()
{
    m_rebuildDebounce.trigger();
}

void CanvasGridHostImpl::rebuildBlocks()
{
    if (!m_document) {
        return;
    }

    if (!m_gridSpec.isValid()) {
        removeAllBlocks();
        emit blocksChanged();
        return;
    }

    const QSizeF cellSize = resolveCellSize();

    QHash<QString, CanvasBlockHandleImpl*> nextHandles;
    nextHandles.reserve(m_blockSpecs.size());

    QSet<ObjectId> geometryTouched;
    QSet<ObjectId> claimedBlockIds;
    QSet<QString> managedSpecIds;
    bool hasInPlaceChanges = false;
    bool handlesChanged = false;

    QHash<QString, QVector<CanvasBlock*>> existingBlocksBySpec;
    existingBlocksBySpec.reserve(static_cast<int>(m_blockSpecs.size()));
    QHash<ObjectId, int> attachmentCountByBlockId;
    for (const auto& item : m_document->items()) {
        auto* block = dynamic_cast<CanvasBlock*>(item.get());
        if (block && !block->specId().trimmed().isEmpty()) {
            existingBlocksBySpec[block->specId()].push_back(block);
            continue;
        }

        const auto* wire = dynamic_cast<const CanvasWire*>(item.get());
        if (!wire)
            continue;
        if (wire->a().attached)
            attachmentCountByBlockId[wire->a().attached->itemId] += 1;
        if (wire->b().attached)
            attachmentCountByBlockId[wire->b().attached->itemId] += 1;
    }

    auto scoreBlockReuse = [&](const CanvasBlock& block) -> int {
        const int attachments = attachmentCountByBlockId.value(block.id());
        const int ports = static_cast<int>(block.ports().size());
        return attachments * 100 + ports * 10;
    };

    auto takeBestExistingBlock = [&](const QString& specId) -> CanvasBlock* {
        const auto matchesIt = existingBlocksBySpec.constFind(specId);
        if (matchesIt == existingBlocksBySpec.constEnd())
            return nullptr;

        CanvasBlock* best = nullptr;
        int bestScore = std::numeric_limits<int>::min();
        for (auto* candidate : matchesIt.value()) {
            if (!candidate)
                continue;
            if (claimedBlockIds.contains(candidate->id()))
                continue;
            const int score = scoreBlockReuse(*candidate);
            if (!best || score > bestScore) {
                best = candidate;
                bestScore = score;
            }
        }
        return best;
    };

    for (const auto& spec : m_blockSpecs) {
        if (spec.id.trimmed().isEmpty())
            continue;
        if (!spec.gridRect.isValid())
            continue;
        managedSpecIds.insert(spec.id);

        const QRectF gridRect = rectForBlock(spec, cellSize);
        const QSizeF size = spec.hasPreferredSize()
                                ? QSizeF(spec.preferredSize.width() * kWorldScale,
                                         spec.preferredSize.height() * kWorldScale)
                                : gridRect.size();
        const QPointF topLeft = gridRect.topLeft() + spec.positionOffset;
        const QRectF bounds(topLeft, size);

        CanvasBlockHandleImpl* handle = m_handles.value(spec.id, nullptr);
        CanvasBlock* block = handle ? handle->block() : nullptr;
        if (!block)
            block = takeBestExistingBlock(spec.id);

        const bool isNewBlock = (block == nullptr);

        if (!block) {
            block = m_document->createBlock(bounds, spec.movable);
            handlesChanged = true;
        }
        if (!block)
            continue;
        claimedBlockIds.insert(block->id());

        if (block->specId() != spec.id)
            block->setSpecId(spec.id);

        const ResolvedBlockStyle style = resolveStyle(spec, m_styleHost);
        bool geometryChanged = false;
        bool keepoutChanged = false;
        const bool blockChanged = applyBlockSpec(block, spec, bounds, style,
                                                 geometryChanged, keepoutChanged);

        if (blockChanged && !isNewBlock)
            hasInPlaceChanges = true;
        if (geometryChanged || keepoutChanged)
            geometryTouched.insert(block->id());

        if (!handle)
            handle = new CanvasBlockHandleImpl(spec.id, m_document, block, this);
        else
            handle->setBlock(block);

        nextHandles.insert(spec.id, handle);
    }

    for (auto it = m_handles.begin(); it != m_handles.end(); ++it) {
        if (!nextHandles.contains(it.key())) {
            handlesChanged = true;
            if (it.value()) {
                if (auto* block = it.value()->block())
                    m_document->removeItem(block->id());
                it.value()->setBlock(nullptr);
                it.value()->deleteLater();
            }
        }
    }

    m_handles = std::move(nextHandles);

    // Keep exactly one managed block per spec id: remove stale duplicates left from prior rebuild races.
    QVector<ObjectId> orphanManagedBlocks;
    for (const auto& item : m_document->items()) {
        auto* block = dynamic_cast<CanvasBlock*>(item.get());
        if (!block)
            continue;
        const QString specId = block->specId();
        if (specId.isEmpty() || !managedSpecIds.contains(specId))
            continue;
        if (claimedBlockIds.contains(block->id()))
            continue;
        orphanManagedBlocks.push_back(block->id());
    }
    for (const auto& id : orphanManagedBlocks)
        m_document->removeItem(id);

    if (!geometryTouched.isEmpty()) {
        for (const auto& item : m_document->items()) {
            if (!item)
                continue;
            auto* wire = dynamic_cast<CanvasWire*>(item.get());
            if (!wire || !wire->hasRouteOverride())
                continue;
            for (const auto& id : geometryTouched) {
                if (wire->attachesTo(id)) {
                    wire->clearRouteOverride();
                    break;
                }
            }
        }
    }

    if (hasInPlaceChanges)
        m_document->notifyChanged();

    if (handlesChanged)
        emit blocksChanged();
}

void CanvasGridHostImpl::removeAllBlocks()
{
    if (!m_document)
        return;

    for (auto it = m_handles.begin(); it != m_handles.end(); ++it) {
        CanvasBlockHandleImpl* handle = it.value();
        if (!handle)
            continue;
        if (auto* block = handle->block())
            m_document->removeItem(block->id());
        handle->setBlock(nullptr);
    }
}

QSizeF CanvasGridHostImpl::resolveCellSize() const
{
    if (m_view) {
        const QSizeF viewportSize = m_view->size();
        const QSizeF base = Utils::GridLayout::resolveCellSize(m_gridSpec, viewportSize, kFallbackCellSize);
        return QSizeF(base.width() * kWorldScale, base.height() * kWorldScale);
    }
    const QSizeF base = Utils::GridLayout::resolveCellSize(m_gridSpec, QSizeF(), kFallbackCellSize);
    return QSizeF(base.width() * kWorldScale, base.height() * kWorldScale);
}

QRectF CanvasGridHostImpl::rectForBlock(const Canvas::Api::CanvasBlockSpec& spec,
                                        const QSizeF& cellSize) const
{
    const Utils::GridSpec specScaled = scaledSpec(m_gridSpec);
    return Utils::GridLayout::rectForGrid(specScaled, spec.gridRect, cellSize);
}

} // namespace Canvas::Internal
