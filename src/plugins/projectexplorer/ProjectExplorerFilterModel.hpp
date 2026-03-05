// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"

#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QString>

namespace ProjectExplorer::Internal {

class PROJECTEXPLORER_EXPORT ProjectExplorerFilterModel final : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit ProjectExplorerFilterModel(QObject* parent = nullptr);

    void setFilterText(QString text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    QString m_filter;
};

} // namespace ProjectExplorer::Internal
