// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/CoreGlobal.hpp"

#include <QtCore/QObject>
#include <QtCore/QString>

namespace Core {

class CORE_EXPORT IHeaderInfo : public QObject
{
    Q_OBJECT

public:
    explicit IHeaderInfo(QObject* parent = nullptr) : QObject(parent) {}
    ~IHeaderInfo() override = default;

    virtual QString deviceLabel() const = 0;
    virtual QString designLabel() const = 0;

public slots:
    virtual void setDeviceLabel(QString label) = 0;
    virtual void setDesignLabel(QString label) = 0;

signals:
    void deviceLabelChanged(const QString& label);
    void designLabelChanged(const QString& label);
};

} // namespace Core
