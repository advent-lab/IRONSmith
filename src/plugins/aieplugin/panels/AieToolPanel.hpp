// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtWidgets/QWidget>

namespace Aie {
class AieCanvasCoordinator;
}

namespace Aie::Internal {

class AieToolPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit AieToolPanel(AieCanvasCoordinator* coordinator, QWidget* parent = nullptr);

private:
    void buildUi();
    AieCanvasCoordinator* m_coordinator = nullptr;
};

} // namespace Aie::Internal
