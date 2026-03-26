// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtWidgets/QFrame>

#include "core/api/SidebarToolSpec.hpp"

class QVBoxLayout;
class QSplitter;

namespace Core {

class SidebarFamilyPanelWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit SidebarFamilyPanelWidget(SidebarSide side,
                                      SidebarFamily family,
                                      QWidget* parent = nullptr);

    QWidget* exclusiveInstallHost() const noexcept { return m_exclusiveHost; }
    QWidget* additiveInstallHost() const noexcept { return m_additiveHost; }

    void setHasExclusive(bool hasExclusive);
    void setHasAdditive(bool hasAdditive);

    int additiveDockedExtent() const noexcept { return clampDockedExtent(m_lastDockedAdditivePx); }
    void setAdditiveDockedHeight(int targetHeight, bool animate);
    void setAdditiveFillMode(bool fill);

signals:
    void additiveDockedExtentChanged(int extent);

private:
    void syncRegionVisibilityAndSizes();
    int clampDockedExtent(int px) const;

private:
    SidebarSide m_side;
    SidebarFamily m_family;

    QVBoxLayout* m_root = nullptr;
    QSplitter* m_splitter = nullptr;

    QWidget* m_exclusiveHost = nullptr;
    QVBoxLayout* m_exclusiveLayout = nullptr;

    QWidget* m_additiveHost = nullptr;
    QVBoxLayout* m_additiveLayout = nullptr;

    bool m_hasExclusive = false;
    bool m_hasAdditive = false;
    bool m_additiveFillMode = false;

    int m_lastDockedAdditivePx = 0;
};

} // namespace Core
