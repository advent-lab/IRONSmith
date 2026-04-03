// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/symbol_table/TapPreviewWidget.hpp"

#include <QtCore/QPointF>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QLinearGradient>
#include <QtGui/QColor>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainterPath>
#include <QtGui/QPainter>
#include <QtGui/QPen>

namespace Aie::Internal {

namespace {

QVector<int> expandTapIndices(const TensorAccessPatternSymbolData& tapData)
{
    QVector<int> indices;
    if (tapData.rows <= 0 || tapData.cols <= 0 || tapData.sizes.size() != tapData.strides.size())
        return indices;

    const int rank = tapData.sizes.size();
    if (rank <= 0)
        return indices;

    QVector<int> iter(rank, 0);
    bool done = false;
    while (!done) {
        int linear = tapData.offset;
        for (int axis = 0; axis < rank; ++axis)
            linear += iter[axis] * tapData.strides[axis];
        indices.push_back(linear);

        for (int axis = rank - 1; axis >= 0; --axis) {
            ++iter[axis];
            if (iter[axis] < tapData.sizes[axis])
                break;
            iter[axis] = 0;
            if (axis == 0)
                done = true;
        }
    }

    return indices;
}

// Parse "A x B" or "A B" into a list of positive integers. Returns empty on failure.
static QVector<int> parseSimpleDims(const QString& text)
{
    QVector<int> out;
    QString cleaned = text;
    cleaned.replace(QLatin1Char('x'), QLatin1Char(' '));
    cleaned.replace(QLatin1Char(','), QLatin1Char(' '));
    const QStringList parts = cleaned.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const int val = part.trimmed().toInt(&ok);
        if (!ok || val <= 0)
            return {};
        out.push_back(val);
    }
    return out;
}

// Derive an equivalent TensorAccessPatternSymbolData for a TensorTiler2D config.
// Returns an empty (rows=0) struct when the fields are symbolic / not parseable.
static TensorAccessPatternSymbolData tiler2DToTapData(const TensorAccessPatternSymbolData& src)
{
    TensorAccessPatternSymbolData viz;

    const QVector<int> tensorDims = parseSimpleDims(src.tensorDims);
    const QVector<int> tileDims   = parseSimpleDims(src.tileDims);
    const QVector<int> tileCounts = parseSimpleDims(src.tileCounts);

    if (tensorDims.size() != 2 || tileDims.size() != 2 || tileCounts.size() != 2)
        return viz; // non-numeric/symbolic — cannot visualize

    const int R  = tensorDims[0], C  = tensorDims[1];
    const int th = tileDims[0],   tw = tileDims[1];
    const int ntR = tileCounts[0], ntC = tileCounts[1];

    if (R <= 0 || C <= 0 || th <= 0 || tw <= 0 || ntR <= 0 || ntC <= 0)
        return viz;

    viz.rows   = R;
    viz.cols   = C;
    viz.offset = 0;
    viz.showRepetitions = false;

    // Parse optional pattern_repeat. When provided, each repeat covers a (ntR*th) x (ntC*tw)
    // block and the repeats tile the remaining array dimensions, giving a 6D structure:
    //   sizes   = [nR_blocks, nC_blocks, ntR, ntC, th, tw]
    //   strides = [ntR*th*C,  ntC*tw,    th*C, tw,  C,  1]
    bool repeatOk = false;
    const int P = src.patternRepeat.trimmed().toInt(&repeatOk);
    if (repeatOk && P > 1) {
        const int blockH = ntR * th;
        const int blockW = ntC * tw;
        // Require the block to divide the array evenly and the block count to match P.
        if (blockH > 0 && blockW > 0 && R % blockH == 0 && C % blockW == 0) {
            const int nRb = R / blockH;
            const int nCb = C / blockW;
            if (nRb * nCb == P) {
                viz.sizes   = {nRb, nCb, ntR, ntC, th, tw};
                viz.strides = {ntR * th * C, ntC * tw, th * C, tw, C, 1};
                return viz;
            }
        }
        return viz; // inconsistent — cannot visualize
    }

    // No pattern_repeat: plain 4D tiling.
    viz.sizes   = {ntR, ntC, th, tw};
    viz.strides = {th * C, tw, C, 1};
    return viz;
}

QColor wireColor(double t)
{
    const QColor start(86, 140, 232);
    const QColor end(123, 224, 163);
    return QColor::fromRgbF(start.redF() + (end.redF() - start.redF()) * t,
                            start.greenF() + (end.greenF() - start.greenF()) * t,
                            start.blueF() + (end.blueF() - start.blueF()) * t);
}

} // namespace

TapPreviewWidget::TapPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("AieTapPreviewWidget"));
    setAttribute(Qt::WA_StyledBackground, true);
}

void TapPreviewWidget::setTapData(const TensorAccessPatternSymbolData& tapData)
{
    m_tapData = tapData;
    update();
}

void TapPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(QStringLiteral("#161A1F")));

    // For TensorTiler2D, derive a visualization TAP. If fields are symbolic, show a message.
    const TensorAccessPatternSymbolData& vizData = m_tapData.useTiler2D
        ? tiler2DToTapData(m_tapData)
        : m_tapData;

    const QRectF content = rect().adjusted(10, 10, -10, -10);

    if (m_tapData.useTiler2D && vizData.rows == 0) {
        painter.setPen(QColor(QStringLiteral("#5A6472")));
        painter.drawText(content, Qt::AlignCenter | Qt::TextWordWrap,
                         QStringLiteral("Enter integer Array Dimensions, Tile Dimensions,\nand Tile Counts to see the tiling pattern."));
        return;
    }

    if (content.width() <= 0 || content.height() <= 0 || vizData.rows <= 0 || vizData.cols <= 0)
        return;

    const qreal cellWidth = content.width() / qMax(1, vizData.cols);
    const qreal cellHeight = content.height() / qMax(1, vizData.rows);
    const qreal cellSize = qMin(cellWidth, cellHeight);
    const qreal gridWidth = cellSize * vizData.cols;
    const qreal gridHeight = cellSize * vizData.rows;
    const QRectF gridRect(content.x() + (content.width() - gridWidth) / 2.0,
                          content.y() + (content.height() - gridHeight) / 2.0,
                          gridWidth,
                          gridHeight);

    const QVector<int> rawIndices = expandTapIndices(vizData);
    QVector<int> visibleIndices;
    visibleIndices.reserve(rawIndices.size());
    QVector<int> repetitionCounts(vizData.rows * vizData.cols, 0);
    for (const int linear : rawIndices) {
        if (linear < 0 || linear >= repetitionCounts.size())
            continue;
        ++repetitionCounts[linear];
        if (!vizData.showRepetitions && visibleIndices.contains(linear))
            continue;
        visibleIndices.push_back(linear);
    }

    painter.setPen(QPen(QColor(QStringLiteral("#2B333D")), 1));
    for (int row = 0; row < vizData.rows; ++row) {
        for (int col = 0; col < vizData.cols; ++col) {
            const QRectF cell(gridRect.x() + col * cellSize,
                              gridRect.y() + row * cellSize,
                              cellSize,
                              cellSize);
            painter.fillRect(cell.adjusted(1, 1, -1, -1), QColor(QStringLiteral("#1E242B")));
            painter.drawRect(cell);
        }
    }

    QVector<QPointF> centers;
    centers.reserve(visibleIndices.size());
    for (const int linear : visibleIndices) {
        const int row = linear / vizData.cols;
        const int col = linear % vizData.cols;
        if (row < 0 || row >= vizData.rows || col < 0 || col >= vizData.cols)
            continue;
        centers.push_back(QPointF(gridRect.x() + (col + 0.5) * cellSize,
                                  gridRect.y() + (row + 0.5) * cellSize));
    }

    if (centers.size() >= 2) {
        QPainterPath path(centers.front());
        for (int index = 1; index < centers.size(); ++index) {
            const QPointF& previous = centers.at(index - 1);
            const QPointF& current = centers.at(index);
            const QPointF mid((previous.x() + current.x()) * 0.5, (previous.y() + current.y()) * 0.5);
            path.quadTo(mid, current);
        }

        QLinearGradient gradient(path.boundingRect().topLeft(), path.boundingRect().bottomRight());
        gradient.setColorAt(0.0, wireColor(0.0));
        gradient.setColorAt(1.0, wireColor(1.0));
        QPen wirePen(QBrush(gradient), qMax(1.1, cellSize * 0.12), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(wirePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

    for (int index = 0; index < centers.size(); ++index) {
        const QPointF& center = centers.at(index);
        const double t = centers.size() <= 1 ? 0.0 : static_cast<double>(index) / (centers.size() - 1);
        painter.setPen(Qt::NoPen);
        painter.setBrush(wireColor(t));
        painter.drawEllipse(center, qMax(1.4, cellSize * 0.11), qMax(1.4, cellSize * 0.11));

        const int linear = visibleIndices.at(index);
        if (linear >= 0 && linear < repetitionCounts.size() && repetitionCounts.at(linear) > 1) {
            painter.setPen(QColor(QStringLiteral("#C6D5E4")));
            painter.drawText(QRectF(center.x() + 2.0,
                                    center.y() - cellSize * 0.3,
                                    cellSize * 0.7,
                                    cellSize * 0.5),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("x%1").arg(repetitionCounts.at(linear)));
        }
    }

    if (!centers.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#F4F8FC")));
        painter.drawEllipse(centers.front(), qMax(1.8, cellSize * 0.14), qMax(1.8, cellSize * 0.14));
    }
}

QSize TapPreviewWidget::minimumSizeHint() const
{
    return {320, 260};
}

} // namespace Aie::Internal
