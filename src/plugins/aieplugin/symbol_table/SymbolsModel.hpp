// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/symbol_table/SymbolTableTypes.hpp"

#include <QtCore/QAbstractTableModel>
#include <QtCore/QPointer>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QString>
#include <QtCore/QVector>

namespace Aie::Internal {

class SymbolsController;

class AIEPLUGIN_EXPORT SymbolsModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Roles : int {
        SymbolIdRole = Qt::UserRole + 1,
        SymbolKindRole
    };

    explicit SymbolsModel(SymbolsController* controller, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void reload();

    QPointer<SymbolsController> m_controller;
    QVector<SymbolRecord> m_symbols;
};

class AIEPLUGIN_EXPORT SymbolsFilterModel final : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit SymbolsFilterModel(QObject* parent = nullptr);

    QString searchText() const;
    void setSearchText(const QString& text);

    SymbolFilterKind filterKind() const;
    void setFilterKind(SymbolFilterKind kind);

private:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

    QString m_searchText;
    SymbolFilterKind m_filterKind = SymbolFilterKind::All;
};

} // namespace Aie::Internal
