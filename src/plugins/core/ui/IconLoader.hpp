// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QString>
#include <QtCore/QSize>
#include <QtGui/QIcon>

namespace Core::Ui {

class IconLoader final
{
public:
    static QIcon load(const QString& resourcePath, const QSize& preferredSize = QSize());

private:
    static bool isSvgResource(const QString& resourcePath);
};

} // namespace Core::Ui
