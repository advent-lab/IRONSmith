// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"
#include "aieplugin/symbol_table/SymbolTableTypes.hpp"

#include <utils/Result.hpp>

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QVector>

namespace Canvas::Api {
class ICanvasDocumentService;
struct CanvasDocumentHandle;
enum class CanvasDocumentCloseReason : unsigned char;
} // namespace Canvas::Api

namespace Aie::Internal {

class AIEPLUGIN_EXPORT SymbolsController final : public QObject
{
    Q_OBJECT

public:
    explicit SymbolsController(QObject* parent = nullptr);

    void setCanvasDocumentService(Canvas::Api::ICanvasDocumentService* service);

    bool hasActiveDocument() const;
    QVector<SymbolRecord> symbols() const;
    QString selectedSymbolId() const;
    const SymbolRecord* symbolById(const QString& id) const;
    QStringList dimensionReferenceCandidates() const;
    QStringList referencesForSymbol(const QString& id) const;

    void setSelectedSymbolId(const QString& id);

    Utils::Result createConstant(QString* outId = nullptr);
    Utils::Result createTypeAbstraction(QString* outId = nullptr);
    Utils::Result createTensorAccessPattern(QString* outId = nullptr);
    Utils::Result updateSymbol(const SymbolRecord& updatedSymbol);
    Utils::Result removeSymbol(const QString& id);

signals:
    void symbolsChanged();
    void activeDocumentChanged(bool active);
    void selectedSymbolChanged(const QString& id);

private:
    void reloadFromDocument();
    void clearDocumentState();
    Utils::Result persistSymbols(const QVector<SymbolRecord>& symbols);
    Utils::Result validateSymbols(const QVector<SymbolRecord>& symbols) const;
    QString uniqueNameForBase(const QString& base) const;
    QStringList referencesForConstantName(const QString& name,
                                          const QVector<SymbolRecord>& symbols) const;
    void replaceSymbols(QVector<SymbolRecord> nextSymbols);
    void reconcileSelectedSymbol();

    QPointer<Canvas::Api::ICanvasDocumentService> m_canvasDocuments;
    QVector<SymbolRecord> m_symbols;
    QString m_selectedSymbolId;
};

} // namespace Aie::Internal
