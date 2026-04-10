// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasRenderContext.hpp"

#include <QtCore/QMarginsF>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>
#include <QtCore/QString>
#include <QtGui/QColor>

#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace Canvas {

struct CANVAS_EXPORT BlockContentStyle final {
    QColor fill = QColor(Constants::kBlockFillColor);
    QColor outline = QColor(Constants::kBlockOutlineColor);
    QColor text = QColor(Constants::kBlockTextColor);
    double cornerRadius = Constants::kBlockCornerRadius;
    double fontSize = Constants::kBlockLabelPointSize; // scene-units point size
};

class CANVAS_EXPORT BlockContent
{
public:
    virtual ~BlockContent() = default;

    virtual std::unique_ptr<BlockContent> clone() const = 0;

    virtual QSizeF measure(const CanvasRenderContext& ctx) const;
    virtual void layout(const QRectF& bounds, const CanvasRenderContext& ctx);
    virtual void draw(QPainter& p, const CanvasRenderContext& ctx) const = 0;
    virtual bool hitTest(const QPointF& scenePos) const;

    void setPreferredSize(const QSizeF& size);
    void clearPreferredSize();
    QSizeF preferredSize() const { return m_preferredSize; }
    bool hasPreferredSize() const { return m_hasPreferredSize; }

    const QRectF& bounds() const { return m_bounds; }

protected:
    QRectF m_bounds;
    QSizeF m_preferredSize;
    bool m_hasPreferredSize = false;
};

class CANVAS_EXPORT BlockContentBlock final : public BlockContent
{
public:
    explicit BlockContentBlock(QString label = {}, BlockContentStyle style = {});

    const QString& label() const { return m_label; }
    void setLabel(QString label) { m_label = std::move(label); }

    const BlockContentStyle& style() const { return m_style; }
    void setStyle(BlockContentStyle style) { m_style = std::move(style); }

    QSizeF measure(const CanvasRenderContext& ctx) const override;
    void draw(QPainter& p, const CanvasRenderContext& ctx) const override;
    std::unique_ptr<BlockContent> clone() const override;

private:
    QString m_label;
    BlockContentStyle m_style;
};

class CANVAS_EXPORT BlockContentContainer final : public BlockContent
{
public:
    enum class Layout {
        Vertical,
        Horizontal,
        Grid
    };

    explicit BlockContentContainer(Layout layout = Layout::Vertical);

    Layout layoutMode() const { return m_layout; }
    void setLayoutMode(Layout layout) { m_layout = layout; }

    const QMarginsF& padding() const { return m_padding; }
    void setPadding(const QMarginsF& padding) { m_padding = padding; }

    double gap() const { return m_gap; }
    void setGap(double gap) { m_gap = gap; }

    int columns() const { return m_columns; }
    void setColumns(int columns) { m_columns = columns; }

    void addChild(std::unique_ptr<BlockContent> child);
    const std::vector<std::unique_ptr<BlockContent>>& children() const { return m_children; }

    QSizeF measure(const CanvasRenderContext& ctx) const override;
    void layout(const QRectF& bounds, const CanvasRenderContext& ctx) override;
    void draw(QPainter& p, const CanvasRenderContext& ctx) const override;
    bool hitTest(const QPointF& scenePos) const override;

    std::unique_ptr<BlockContent> clone() const override;

private:
    Layout m_layout = Layout::Vertical;
    QMarginsF m_padding{Constants::kContentPadding,
                        Constants::kContentPadding,
                        Constants::kContentPadding,
                        Constants::kContentPadding};
    double m_gap = Constants::kContentGap;
    int m_columns = 2;
    std::vector<std::unique_ptr<BlockContent>> m_children;
};

} // namespace Canvas
