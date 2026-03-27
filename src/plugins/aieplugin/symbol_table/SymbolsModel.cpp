// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/symbol_table/SymbolsModel.hpp"

#include "aieplugin/symbol_table/SymbolsController.hpp"

#include <QtGui/QFontDatabase>

namespace Aie::Internal {

namespace {

enum Column : int {
    NameColumn = 0,
    KindColumn,
    SummaryColumn,
    ColumnCount
};

} // namespace

SymbolsModel::SymbolsModel(SymbolsController* controller, QObject* parent)
    : QAbstractTableModel(parent)
    , m_controller(controller)
{
    if (m_controller) {
        connect(m_controller, &SymbolsController::symbolsChanged,
                this, &SymbolsModel::reload);
    }
    reload();
}

int SymbolsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_symbols.size();
}

int SymbolsModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return ColumnCount;
}

QVariant SymbolsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_symbols.size())
        return {};

    const SymbolRecord& symbol = m_symbols.at(index.row());

    if (role == SymbolIdRole)
        return symbol.id;
    if (role == SymbolKindRole)
        return static_cast<int>(symbol.kind);
    if (role == Qt::ToolTipRole)
        return symbolPreview(symbol);
    if (role == Qt::TextAlignmentRole && index.column() == KindColumn)
        return Qt::AlignCenter;
    if (role == Qt::FontRole && index.column() == SummaryColumn)
        return QFontDatabase::systemFont(QFontDatabase::FixedFont);

    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
        case NameColumn:
            return symbol.name;
        case KindColumn:
            return symbolKindDisplayName(symbol.kind);
        case SummaryColumn:
            return symbolSummary(symbol);
        default:
            return {};
    }
}

QVariant SymbolsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
        case NameColumn:
            return QStringLiteral("Name");
        case KindColumn:
            return QStringLiteral("Kind");
        case SummaryColumn:
            return QStringLiteral("Summary");
        default:
            return {};
    }
}

void SymbolsModel::reload()
{
    beginResetModel();
    m_symbols = m_controller ? m_controller->symbols() : QVector<SymbolRecord>{};
    endResetModel();
}

SymbolsFilterModel::SymbolsFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(false);
}

QString SymbolsFilterModel::searchText() const
{
    return m_searchText;
}

void SymbolsFilterModel::setSearchText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (m_searchText == normalized)
        return;
    m_searchText = normalized;
    invalidateFilter();
}

SymbolFilterKind SymbolsFilterModel::filterKind() const
{
    return m_filterKind;
}

void SymbolsFilterModel::setFilterKind(SymbolFilterKind kind)
{
    if (m_filterKind == kind)
        return;
    m_filterKind = kind;
    invalidateFilter();
}

bool SymbolsFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex nameIndex = sourceModel()->index(sourceRow, NameColumn, sourceParent);
    const QModelIndex kindIndex = sourceModel()->index(sourceRow, KindColumn, sourceParent);
    const QModelIndex summaryIndex = sourceModel()->index(sourceRow, SummaryColumn, sourceParent);

    const SymbolKind kind =
        static_cast<SymbolKind>(kindIndex.data(SymbolsModel::SymbolKindRole).toInt());
    if (m_filterKind == SymbolFilterKind::Constants && kind != SymbolKind::Constant)
        return false;
    if (m_filterKind == SymbolFilterKind::Types && kind != SymbolKind::TypeAbstraction)
        return false;
    if (m_filterKind == SymbolFilterKind::TensorAccessPatterns
        && kind != SymbolKind::TensorAccessPattern) {
        return false;
    }

    if (m_searchText.isEmpty())
        return true;

    const QString haystack = QStringLiteral("%1 %2 %3")
        .arg(nameIndex.data().toString(),
             kindIndex.data().toString(),
             summaryIndex.data().toString())
        .toLower();
    return haystack.contains(m_searchText);
}

} // namespace Aie::Internal
