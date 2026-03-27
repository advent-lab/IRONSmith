// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/symbol_table/SymbolTableTypes.hpp"

#include <QtWidgets/QWidget>

namespace Aie::Internal {

class AIEPLUGIN_EXPORT TapPreviewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TapPreviewWidget(QWidget* parent = nullptr);

    void setTapData(const TensorAccessPatternSymbolData& tapData);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize minimumSizeHint() const override;

private:
    TensorAccessPatternSymbolData m_tapData;
};

} // namespace Aie::Internal
