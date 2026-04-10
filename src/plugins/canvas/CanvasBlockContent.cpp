// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/CanvasBlockContent.hpp"

#include "canvas/CanvasStyle.hpp"

#include <QtCore/Qt>
#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtGui/QPainter>

#include <algorithm>
#include <utility>

namespace Canvas {

namespace {

constexpr double kLabelPadX = Constants::kBlockLabelPadX;
constexpr double kLabelPadY = Constants::kBlockLabelPadY;

QFont blockLabelFont(const QPainter& p, double fontSize = Constants::kBlockLabelPointSize)
{
    QFont f = p.font();
    f.setPointSizeF(fontSize);
    f.setBold(true);
    return f;
}

QSizeF labelSize(const QString& text, double fontSize = Constants::kBlockLabelPointSize)
{
    QFont f;
    f.setPointSizeF(fontSize);
    f.setBold(true);
    QFontMetricsF metrics(f);
    const QSizeF textSize = metrics.size(Qt::TextSingleLine, text);
    return QSizeF(textSize.width() + kLabelPadX * 2.0,
                  textSize.height() + kLabelPadY * 2.0);
}

QRectF paddedRect(const QRectF& rect, const QMarginsF& padding)
{
    return rect.adjusted(padding.left(),
                         padding.top(),
                         -padding.right(),
                         -padding.bottom());
}

} // namespace

QSizeF BlockContent::measure(const CanvasRenderContext&) const
{
    return m_hasPreferredSize ? m_preferredSize : QSizeF();
}

void BlockContent::layout(const QRectF& bounds, const CanvasRenderContext&)
{
    m_bounds = bounds;
}

bool BlockContent::hitTest(const QPointF& scenePos) const
{
    return m_bounds.contains(scenePos);
}

void BlockContent::setPreferredSize(const QSizeF& size)
{
    m_preferredSize = size;
    m_hasPreferredSize = true;
}

void BlockContent::clearPreferredSize()
{
    m_preferredSize = QSizeF();
    m_hasPreferredSize = false;
}

BlockContentBlock::BlockContentBlock(QString label, BlockContentStyle style)
    : m_label(std::move(label))
    , m_style(std::move(style))
{}

QSizeF BlockContentBlock::measure(const CanvasRenderContext& ctx) const
{
    Q_UNUSED(ctx);
    if (m_hasPreferredSize)
        return m_preferredSize;
    if (m_label.isEmpty())
        return QSizeF();
    return labelSize(m_label, m_style.fontSize);
}

void BlockContentBlock::draw(QPainter& p, const CanvasRenderContext& ctx) const
{
    CanvasStyle::drawBlockFrame(p, m_bounds, ctx.zoom, m_style.outline, m_style.fill, m_style.cornerRadius);

    if (m_label.isEmpty())
        return;

    QFont f = blockLabelFont(p, m_style.fontSize);
    p.setFont(f);
    p.setPen(m_style.text);

    const QRectF r = m_bounds.adjusted(kLabelPadX, kLabelPadY, -kLabelPadX, -kLabelPadY);
    p.drawText(r, Qt::AlignCenter, m_label);
}

std::unique_ptr<BlockContent> BlockContentBlock::clone() const
{
    auto copy = std::make_unique<BlockContentBlock>(m_label, m_style);
    if (m_hasPreferredSize)
        copy->setPreferredSize(m_preferredSize);
    return copy;
}

BlockContentContainer::BlockContentContainer(Layout layout)
    : m_layout(layout)
{}

void BlockContentContainer::addChild(std::unique_ptr<BlockContent> child)
{
    if (child)
        m_children.push_back(std::move(child));
}

QSizeF BlockContentContainer::measure(const CanvasRenderContext& ctx) const
{
    if (m_hasPreferredSize)
        return m_preferredSize;

    if (m_children.empty())
        return QSizeF();

    const double gap = m_gap;
    const QMarginsF pad = m_padding;

    if (m_layout == Layout::Vertical) {
        double width = 0.0;
        double height = 0.0;
        for (const auto& child : m_children) {
            const QSizeF pref = child->measure(ctx);
            width = std::max(width, pref.width());
            height += pref.height();
        }
        height += gap * std::max(0.0, static_cast<double>(m_children.size() - 1));
        width += pad.left() + pad.right();
        height += pad.top() + pad.bottom();
        return QSizeF(width, height);
    }

    if (m_layout == Layout::Horizontal) {
        double width = 0.0;
        double height = 0.0;
        for (const auto& child : m_children) {
            const QSizeF pref = child->measure(ctx);
            width += pref.width();
            height = std::max(height, pref.height());
        }
        width += gap * std::max(0.0, static_cast<double>(m_children.size() - 1));
        width += pad.left() + pad.right();
        height += pad.top() + pad.bottom();
        return QSizeF(width, height);
    }

    const int cols = std::max(1, m_columns);
    const int rows = static_cast<int>((m_children.size() + cols - 1) / cols);
    double cellW = 0.0;
    double cellH = 0.0;
    for (const auto& child : m_children) {
        const QSizeF pref = child->measure(ctx);
        cellW = std::max(cellW, pref.width());
        cellH = std::max(cellH, pref.height());
    }
    const double width = cols * cellW + gap * std::max(0, cols - 1) + pad.left() + pad.right();
    const double height = rows * cellH + gap * std::max(0, rows - 1) + pad.top() + pad.bottom();
    return QSizeF(width, height);
}

void BlockContentContainer::layout(const QRectF& bounds, const CanvasRenderContext& ctx)
{
    m_bounds = bounds;
    if (m_children.empty())
        return;

    const QRectF inner = paddedRect(bounds, m_padding);
    if (inner.width() <= 0.0 || inner.height() <= 0.0)
        return;

    if (m_layout == Layout::Vertical) {
        const size_t count = m_children.size();
        const double totalGap = m_gap * std::max(0.0, static_cast<double>(count - 1));
        const double available = std::max(0.0, inner.height() - totalGap);

        double fixed = 0.0;
        size_t flexible = 0;
        std::vector<double> heights(count, 0.0);
        for (size_t i = 0; i < count; ++i) {
            const QSizeF pref = m_children[i]->measure(ctx);
            if (pref.height() > 0.0) {
                heights[i] = pref.height();
                fixed += pref.height();
            } else {
                ++flexible;
            }
        }

        double scale = 1.0;
        if (fixed > available && fixed > 0.0 && flexible == 0)
            scale = available / fixed;

        const double remaining = std::max(0.0, available - fixed * scale);
        const double flexHeight = (flexible > 0) ? (remaining / static_cast<double>(flexible)) : 0.0;

        double y = inner.top();
        for (size_t i = 0; i < count; ++i) {
            double h = heights[i] > 0.0 ? heights[i] * scale : flexHeight;
            QRectF childRect(inner.left(), y, inner.width(), h);
            m_children[i]->layout(childRect, ctx);
            y += h + m_gap;
        }
        return;
    }

    if (m_layout == Layout::Horizontal) {
        const size_t count = m_children.size();
        const double totalGap = m_gap * std::max(0.0, static_cast<double>(count - 1));
        const double available = std::max(0.0, inner.width() - totalGap);

        double fixed = 0.0;
        size_t flexible = 0;
        std::vector<double> widths(count, 0.0);
        for (size_t i = 0; i < count; ++i) {
            const QSizeF pref = m_children[i]->measure(ctx);
            if (pref.width() > 0.0) {
                widths[i] = pref.width();
                fixed += pref.width();
            } else {
                ++flexible;
            }
        }

        double scale = 1.0;
        if (fixed > available && fixed > 0.0 && flexible == 0)
            scale = available / fixed;

        const double remaining = std::max(0.0, available - fixed * scale);
        const double flexWidth = (flexible > 0) ? (remaining / static_cast<double>(flexible)) : 0.0;

        double x = inner.left();
        for (size_t i = 0; i < count; ++i) {
            double w = widths[i] > 0.0 ? widths[i] * scale : flexWidth;
            QRectF childRect(x, inner.top(), w, inner.height());
            m_children[i]->layout(childRect, ctx);
            x += w + m_gap;
        }
        return;
    }

    const int cols = std::max(1, m_columns);
    const int rows = std::max(1, static_cast<int>((m_children.size() + cols - 1) / cols));
    const double totalGapX = m_gap * std::max(0, cols - 1);
    const double totalGapY = m_gap * std::max(0, rows - 1);
    const double cellW = (inner.width() - totalGapX) / static_cast<double>(cols);
    const double cellH = (inner.height() - totalGapY) / static_cast<double>(rows);

    for (size_t idx = 0; idx < m_children.size(); ++idx) {
        const int row = static_cast<int>(idx / cols);
        const int col = static_cast<int>(idx % cols);
        const double x = inner.left() + col * (cellW + m_gap);
        const double y = inner.top() + row * (cellH + m_gap);
        QRectF cell(x, y, cellW, cellH);

        const QSizeF pref = m_children[idx]->measure(ctx);
        QSizeF size = pref;
        if (size.width() <= 0.0)
            size.setWidth(cell.width());
        if (size.height() <= 0.0)
            size.setHeight(cell.height());
        size.setWidth(std::min(size.width(), cell.width()));
        size.setHeight(std::min(size.height(), cell.height()));

        const QPointF center = cell.center();
        QRectF childRect(center.x() - size.width() * 0.5,
                         center.y() - size.height() * 0.5,
                         size.width(), size.height());
        m_children[idx]->layout(childRect, ctx);
    }
}

void BlockContentContainer::draw(QPainter& p, const CanvasRenderContext& ctx) const
{
    for (const auto& child : m_children) {
        if (child)
            child->draw(p, ctx);
    }
}

bool BlockContentContainer::hitTest(const QPointF& scenePos) const
{
    if (!m_bounds.contains(scenePos))
        return false;
    for (const auto& child : m_children) {
        if (child && child->hitTest(scenePos))
            return true;
    }
    return false;
}

std::unique_ptr<BlockContent> BlockContentContainer::clone() const
{
    auto copy = std::make_unique<BlockContentContainer>(m_layout);
    copy->m_padding = m_padding;
    copy->m_gap = m_gap;
    copy->m_columns = m_columns;
    for (const auto& child : m_children) {
        if (child)
            copy->addChild(child->clone());
    }
    if (m_hasPreferredSize)
        copy->setPreferredSize(m_preferredSize);
    return copy;
}

} // namespace Canvas
