// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "core/ui/IconLoader.hpp"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSet>
#include <QtCore/QVector>
#include <QtGui/QPainter>

#if defined(CORE_HAVE_QT_SVG) && (CORE_HAVE_QT_SVG == 1)
#include <QtSvg/QSvgRenderer>
#endif

namespace Core::Ui {

namespace {

QVector<QSize> iconRasterSizes(const QSize& preferredSize)
{
    QVector<QSize> sizes = {
        QSize(16, 16), QSize(18, 18), QSize(20, 20), QSize(22, 22),
        QSize(24, 24), QSize(28, 28), QSize(32, 32), QSize(40, 40)
    };

    if (preferredSize.isValid())
        sizes.push_front(preferredSize);

    QSet<QSize> seen;
    QVector<QSize> deduped;
    deduped.reserve(sizes.size());
    for (const QSize& size : sizes) {
        if (!size.isValid() || seen.contains(size))
            continue;
        seen.insert(size);
        deduped.push_back(size);
    }

    return deduped;
}

#if defined(CORE_HAVE_QT_SVG) && (CORE_HAVE_QT_SVG == 1)
QPixmap renderSvgPixmap(QSvgRenderer& renderer, const QSize& size, qreal opacity)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(opacity);
    renderer.render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(size)));
    painter.end();

    return pixmap;
}
#endif

} // namespace

bool IconLoader::isSvgResource(const QString& resourcePath)
{
    return QFileInfo(resourcePath).suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0;
}

QIcon IconLoader::load(const QString& resourcePath, const QSize& preferredSize)
{
    if (resourcePath.isEmpty())
        return {};

#if defined(CORE_HAVE_QT_SVG) && (CORE_HAVE_QT_SVG == 1)
    if (isSvgResource(resourcePath)) {
        QFile source(resourcePath);
        if (source.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QSvgRenderer renderer(source.readAll());
            if (renderer.isValid()) {
                QIcon icon;
                for (const QSize& size : iconRasterSizes(preferredSize)) {
                    icon.addPixmap(renderSvgPixmap(renderer, size, 1.0), QIcon::Normal, QIcon::Off);
                    icon.addPixmap(renderSvgPixmap(renderer, size, 0.42), QIcon::Disabled, QIcon::Off);
                    icon.addPixmap(renderSvgPixmap(renderer, size, 1.0), QIcon::Normal, QIcon::On);
                    icon.addPixmap(renderSvgPixmap(renderer, size, 0.42), QIcon::Disabled, QIcon::On);
                }

                if (!icon.isNull())
                    return icon;
            }
        }
    }
#endif

    return QIcon(resourcePath);
}

} // namespace Core::Ui
