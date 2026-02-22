// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/AieStyleCatalog.hpp"

#include <QtGui/QColor>

namespace Aie::Internal {

QHash<QString, Canvas::Api::CanvasBlockStyle> createDefaultBlockStyles()
{
    QHash<QString, Canvas::Api::CanvasBlockStyle> styles;

    Canvas::Api::CanvasBlockStyle aieStyle;
    aieStyle.fillColor = QColor(QStringLiteral("#0B1B16"));
    aieStyle.outlineColor = QColor(QStringLiteral("#16E08C"));
    aieStyle.labelColor = QColor(QStringLiteral("#B7F3DA"));
    aieStyle.cornerRadius = 0.0;
    styles.insert(QStringLiteral("aie"), aieStyle);

    Canvas::Api::CanvasBlockStyle memStyle;
    memStyle.fillColor = QColor(QStringLiteral("#1F1710"));
    memStyle.outlineColor = QColor(QStringLiteral("#F2A14C"));
    memStyle.labelColor = QColor(QStringLiteral("#FFD7A8"));
    memStyle.cornerRadius = 0.0;
    styles.insert(QStringLiteral("mem"), memStyle);

    Canvas::Api::CanvasBlockStyle shimStyle;
    shimStyle.fillColor = QColor(QStringLiteral("#0E1722"));
    shimStyle.outlineColor = QColor(QStringLiteral("#58B5FF"));
    shimStyle.labelColor = QColor(QStringLiteral("#CFE9FF"));
    shimStyle.cornerRadius = 0.0;
    styles.insert(QStringLiteral("shim"), shimStyle);

    Canvas::Api::CanvasBlockStyle ddrStyle;
    ddrStyle.fillColor = QColor(QStringLiteral("#0F1116"));
    ddrStyle.outlineColor = QColor(QStringLiteral("#E6E9EF"));
    ddrStyle.labelColor = QColor(QStringLiteral("#E6E9EF"));
    ddrStyle.cornerRadius = 0.0;
    styles.insert(QStringLiteral("ddr"), ddrStyle);

    return styles;
}

} // namespace Aie::Internal