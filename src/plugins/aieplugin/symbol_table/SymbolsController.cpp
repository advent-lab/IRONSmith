// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/symbol_table/SymbolsController.hpp"

#include "canvas/api/ICanvasDocumentService.hpp"

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QUuid>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kAieMetadataSchemaKey = u"schema"_s;
const QString kAieMetadataSchemaValue = u"aie.spec/1"_s;
const QString kSymbolsMetadataKey = u"symbols"_s;

QString cleanedName(const QString& name)
{
    return name.trimmed();
}

QStringList dimensionSuggestions()
{
    return {
        QStringLiteral("1"),
        QStringLiteral("2"),
        QStringLiteral("4"),
        QStringLiteral("8"),
        QStringLiteral("16"),
        QStringLiteral("32"),
        QStringLiteral("64"),
        QStringLiteral("128"),
        QStringLiteral("256"),
        QStringLiteral("512"),
        QStringLiteral("1024")
    };
}

} // namespace

SymbolsController::SymbolsController(QObject* parent)
    : QObject(parent)
{
}

void SymbolsController::setCanvasDocumentService(Canvas::Api::ICanvasDocumentService* service)
{
    if (m_canvasDocuments == service)
        return;

    if (m_canvasDocuments)
        disconnect(m_canvasDocuments, nullptr, this, nullptr);

    const bool wasActive = hasActiveDocument();
    m_canvasDocuments = service;

    if (m_canvasDocuments) {
        connect(m_canvasDocuments, &Canvas::Api::ICanvasDocumentService::documentOpened,
                this, [this](const Canvas::Api::CanvasDocumentHandle&) {
                    emit activeDocumentChanged(true);
                    reloadFromDocument();
                });
        connect(m_canvasDocuments, &Canvas::Api::ICanvasDocumentService::documentClosed,
                this, [this](const Canvas::Api::CanvasDocumentHandle&,
                             Canvas::Api::CanvasDocumentCloseReason) {
                    emit activeDocumentChanged(false);
                    clearDocumentState();
                });
    }

    if (hasActiveDocument())
        reloadFromDocument();
    else
        clearDocumentState();

    if (wasActive != hasActiveDocument())
        emit activeDocumentChanged(hasActiveDocument());
}

bool SymbolsController::hasActiveDocument() const
{
    return m_canvasDocuments && m_canvasDocuments->hasOpenDocument();
}

QVector<SymbolRecord> SymbolsController::symbols() const
{
    return m_symbols;
}

QString SymbolsController::selectedSymbolId() const
{
    return m_selectedSymbolId;
}

const SymbolRecord* SymbolsController::symbolById(const QString& id) const
{
    const QString cleaned = id.trimmed();
    if (cleaned.isEmpty())
        return nullptr;

    for (const SymbolRecord& symbol : m_symbols) {
        if (symbol.id == cleaned)
            return &symbol;
    }

    return nullptr;
}

QStringList SymbolsController::dimensionReferenceCandidates() const
{
    QStringList values = dimensionSuggestions();
    for (const SymbolRecord& symbol : m_symbols) {
        if (symbol.kind != SymbolKind::Constant)
            continue;
        values.push_back(symbol.name);
    }

    values.removeDuplicates();
    values.sort(Qt::CaseInsensitive);
    return values;
}

QStringList SymbolsController::referencesForSymbol(const QString& id) const
{
    const SymbolRecord* symbol = symbolById(id);
    if (!symbol)
        return {};

    if (symbol->kind != SymbolKind::Constant)
        return {};

    return referencesForConstantName(symbol->name, m_symbols);
}

void SymbolsController::setSelectedSymbolId(const QString& id)
{
    const QString cleaned = id.trimmed();
    QString nextId = cleaned;
    if (!nextId.isEmpty() && !symbolById(nextId))
        nextId.clear();

    if (m_selectedSymbolId == nextId)
        return;

    m_selectedSymbolId = nextId;
    emit selectedSymbolChanged(m_selectedSymbolId);
}

Utils::Result SymbolsController::createConstant(QString* outId)
{
    if (!hasActiveDocument())
        return Utils::Result::failure(QStringLiteral("Open a design before creating symbols."));

    SymbolRecord symbol;
    symbol.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    symbol.kind = SymbolKind::Constant;
    symbol.name = uniqueNameForBase(QStringLiteral("const"));
    symbol.constant.value = 1;

    QVector<SymbolRecord> nextSymbols = m_symbols;
    nextSymbols.push_back(symbol);

    const Utils::Result validateResult = validateSymbols(nextSymbols);
    if (!validateResult)
        return validateResult;

    const Utils::Result persistResult = persistSymbols(nextSymbols);
    if (!persistResult)
        return persistResult;

    replaceSymbols(std::move(nextSymbols));
    setSelectedSymbolId(symbol.id);
    if (outId)
        *outId = symbol.id;
    return Utils::Result::success();
}

Utils::Result SymbolsController::createTypeAbstraction(QString* outId)
{
    if (!hasActiveDocument())
        return Utils::Result::failure(QStringLiteral("Open a design before creating symbols."));

    SymbolRecord symbol;
    symbol.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    symbol.kind = SymbolKind::TypeAbstraction;
    symbol.name = uniqueNameForBase(QStringLiteral("tensor_ty"));
    symbol.type.shapeTokens = {QStringLiteral("1")};
    symbol.type.dtype = QStringLiteral("int32");

    QVector<SymbolRecord> nextSymbols = m_symbols;
    nextSymbols.push_back(symbol);

    const Utils::Result validateResult = validateSymbols(nextSymbols);
    if (!validateResult)
        return validateResult;

    const Utils::Result persistResult = persistSymbols(nextSymbols);
    if (!persistResult)
        return persistResult;

    replaceSymbols(std::move(nextSymbols));
    setSelectedSymbolId(symbol.id);
    if (outId)
        *outId = symbol.id;
    return Utils::Result::success();
}

Utils::Result SymbolsController::createTensorAccessPattern(QString* outId)
{
    if (!hasActiveDocument())
        return Utils::Result::failure(QStringLiteral("Open a design before creating symbols."));

    SymbolRecord symbol;
    symbol.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    symbol.kind = SymbolKind::TensorAccessPattern;
    symbol.name = uniqueNameForBase(QStringLiteral("tap"));
    symbol.tap = {};

    QVector<SymbolRecord> nextSymbols = m_symbols;
    nextSymbols.push_back(symbol);

    const Utils::Result validateResult = validateSymbols(nextSymbols);
    if (!validateResult)
        return validateResult;

    const Utils::Result persistResult = persistSymbols(nextSymbols);
    if (!persistResult)
        return persistResult;

    replaceSymbols(std::move(nextSymbols));
    setSelectedSymbolId(symbol.id);
    if (outId)
        *outId = symbol.id;
    return Utils::Result::success();
}

Utils::Result SymbolsController::createLayoutDims(QString* outId)
{
    if (!hasActiveDocument())
        return Utils::Result::failure(QStringLiteral("Open a design before creating symbols."));

    SymbolRecord symbol;
    symbol.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    symbol.kind = SymbolKind::LayoutDims;
    symbol.name = uniqueNameForBase(QStringLiteral("layout_dims"));
    symbol.layoutDims.entries = {LayoutDimsEntry{QStringLiteral("1"), QStringLiteral("1")}};

    QVector<SymbolRecord> nextSymbols = m_symbols;
    nextSymbols.push_back(symbol);

    const Utils::Result validateResult = validateSymbols(nextSymbols);
    if (!validateResult)
        return validateResult;

    const Utils::Result persistResult = persistSymbols(nextSymbols);
    if (!persistResult)
        return persistResult;

    replaceSymbols(std::move(nextSymbols));
    setSelectedSymbolId(symbol.id);
    if (outId)
        *outId = symbol.id;
    return Utils::Result::success();
}

Utils::Result SymbolsController::updateSymbol(const SymbolRecord& updatedSymbol)
{
    if (!hasActiveDocument())
        return Utils::Result::failure(QStringLiteral("Open a design before editing symbols."));

    const QString targetId = updatedSymbol.id.trimmed();
    if (targetId.isEmpty())
        return Utils::Result::failure(QStringLiteral("Symbol id is missing."));

    QVector<SymbolRecord> nextSymbols = m_symbols;
    int index = -1;
    SymbolRecord previousSymbol;
    for (int i = 0; i < nextSymbols.size(); ++i) {
        if (nextSymbols[i].id == targetId) {
            index = i;
            previousSymbol = nextSymbols[i];
            break;
        }
    }

    if (index < 0)
        return Utils::Result::failure(QStringLiteral("Symbol could not be found."));

    SymbolRecord normalized = updatedSymbol;
    normalized.name = cleanedName(normalized.name);
    if (normalized.id.isEmpty())
        normalized.id = previousSymbol.id;

    if (normalized.kind == SymbolKind::TypeAbstraction) {
        for (QString& token : normalized.type.shapeTokens)
            token = token.trimmed();
        normalized.type.dtype = normalized.type.dtype.trimmed();
    } else if (normalized.kind == SymbolKind::TensorAccessPattern) {
        normalized.tap.rows = qMax(1, normalized.tap.rows);
        normalized.tap.cols = qMax(1, normalized.tap.cols);
        normalized.tap.offset = qMax(0, normalized.tap.offset);
    }

    nextSymbols[index] = normalized;

    if (previousSymbol.kind == SymbolKind::Constant
        && normalized.kind == SymbolKind::Constant
        && previousSymbol.name != normalized.name) {
        for (SymbolRecord& symbol : nextSymbols) {
            if (symbol.id == normalized.id || symbol.kind != SymbolKind::TypeAbstraction)
                continue;
            for (QString& token : symbol.type.shapeTokens) {
                if (token == previousSymbol.name)
                    token = normalized.name;
            }
        }
    }

    const Utils::Result validateResult = validateSymbols(nextSymbols);
    if (!validateResult)
        return validateResult;

    const Utils::Result persistResult = persistSymbols(nextSymbols);
    if (!persistResult)
        return persistResult;

    replaceSymbols(std::move(nextSymbols));
    return Utils::Result::success();
}

Utils::Result SymbolsController::removeSymbol(const QString& id)
{
    if (!hasActiveDocument())
        return Utils::Result::failure(QStringLiteral("Open a design before editing symbols."));

    const QString targetId = id.trimmed();
    if (targetId.isEmpty())
        return Utils::Result::failure(QStringLiteral("Symbol id is missing."));

    QVector<SymbolRecord> nextSymbols;
    nextSymbols.reserve(m_symbols.size());

    SymbolRecord removed;
    bool found = false;

    for (const SymbolRecord& symbol : m_symbols) {
        if (symbol.id == targetId) {
            removed = symbol;
            found = true;
            continue;
        }
        nextSymbols.push_back(symbol);
    }

    if (!found)
        return Utils::Result::failure(QStringLiteral("Symbol could not be found."));

    if (removed.kind == SymbolKind::Constant) {
        const QStringList references = referencesForConstantName(removed.name, nextSymbols);
        if (!references.isEmpty()) {
            return Utils::Result::failure(QStringLiteral("Cannot remove '%1' because it is referenced by %2.")
                                              .arg(removed.name, references.join(QStringLiteral(", "))));
        }
    }

    const Utils::Result validateResult = validateSymbols(nextSymbols);
    if (!validateResult)
        return validateResult;

    const Utils::Result persistResult = persistSymbols(nextSymbols);
    if (!persistResult)
        return persistResult;

    replaceSymbols(std::move(nextSymbols));
    if (m_selectedSymbolId == targetId)
        reconcileSelectedSymbol();
    return Utils::Result::success();
}

void SymbolsController::reloadFromDocument()
{
    if (!hasActiveDocument()) {
        clearDocumentState();
        return;
    }

    QVector<SymbolRecord> parsedSymbols;
    const QJsonObject metadata = m_canvasDocuments->activeMetadata();
    const Utils::Result parseResult =
        parseSymbolsMetadata(metadata.value(kSymbolsMetadataKey).toObject(), parsedSymbols);
    if (!parseResult) {
        clearDocumentState();
        return;
    }

    const Utils::Result validateResult = validateSymbols(parsedSymbols);
    if (!validateResult) {
        clearDocumentState();
        return;
    }

    replaceSymbols(std::move(parsedSymbols));
    reconcileSelectedSymbol();
}

void SymbolsController::clearDocumentState()
{
    const bool hadSymbols = !m_symbols.isEmpty();
    m_symbols.clear();

    const bool hadSelection = !m_selectedSymbolId.isEmpty();
    m_selectedSymbolId.clear();

    if (hadSymbols)
        emit symbolsChanged();
    if (hadSelection)
        emit selectedSymbolChanged(QString());
}

Utils::Result SymbolsController::persistSymbols(const QVector<SymbolRecord>& symbols)
{
    if (!m_canvasDocuments)
        return Utils::Result::failure(QStringLiteral("Canvas document service is not available."));
    if (!m_canvasDocuments->hasOpenDocument())
        return Utils::Result::failure(QStringLiteral("No active design is available."));

    QJsonObject metadata = m_canvasDocuments->activeMetadata();
    if (metadata.value(kAieMetadataSchemaKey).toString().trimmed().isEmpty())
        metadata.insert(kAieMetadataSchemaKey, kAieMetadataSchemaValue);

    if (symbols.isEmpty())
        metadata.remove(kSymbolsMetadataKey);
    else
        metadata.insert(kSymbolsMetadataKey,
                        ensureSymbolsMetadataSchema(serializeSymbolsMetadata(symbols)));
    return m_canvasDocuments->updateActiveMetadata(metadata);
}

Utils::Result SymbolsController::validateSymbols(const QVector<SymbolRecord>& symbols) const
{
    QHash<QString, QString> symbolIdByName;
    QHash<QString, qint64> constantValuesByName;
    QSet<QString> validDtypes;
    for (const QString& dtype : supportedSymbolDtypes())
        validDtypes.insert(dtype);

    for (const SymbolRecord& symbol : symbols) {
        const QString name = cleanedName(symbol.name);
        if (!isValidSymbolIdentifier(name)) {
            return Utils::Result::failure(QStringLiteral("'%1' is not a valid Python identifier.")
                                              .arg(symbol.name));
        }

        if (symbolIdByName.contains(name)) {
            return Utils::Result::failure(QStringLiteral("A symbol named '%1' already exists.")
                                              .arg(name));
        }

        symbolIdByName.insert(name, symbol.id);
        if (symbol.kind == SymbolKind::Constant)
            constantValuesByName.insert(name, symbol.constant.value);
    }

    for (const SymbolRecord& symbol : symbols) {
        if (symbol.kind != SymbolKind::TypeAbstraction)
            continue;

        if (symbol.type.shapeTokens.isEmpty()) {
            return Utils::Result::failure(QStringLiteral("Type '%1' must declare at least one dimension.")
                                              .arg(symbol.name));
        }

        if (!validDtypes.contains(symbol.type.dtype.trimmed())) {
            return Utils::Result::failure(QStringLiteral("'%1' is not a supported NumPy dtype.")
                                              .arg(symbol.type.dtype));
        }

        for (const QString& rawToken : symbol.type.shapeTokens) {
            const QString token = rawToken.trimmed();
            if (token.isEmpty()) {
                return Utils::Result::failure(QStringLiteral("Type '%1' has an empty dimension.")
                                                  .arg(symbol.name));
            }

            qint64 literalValue = 0;
            if (parseIntegralToken(token, literalValue)) {
                if (literalValue <= 0) {
                    return Utils::Result::failure(QStringLiteral("Type '%1' dimensions must be positive integers.")
                                                      .arg(symbol.name));
                }
                continue;
            }

            // Simple identifier: direct constant lookup with positivity check.
            if (isValidSymbolIdentifier(token)) {
                if (!constantValuesByName.contains(token)) {
                    return Utils::Result::failure(QStringLiteral("Type '%1' references unknown constant '%2'.")
                                                      .arg(symbol.name, token));
                }
                if (constantValuesByName.value(token) <= 0) {
                    return Utils::Result::failure(QStringLiteral("Constant '%1' must be positive when used as a dimension.")
                                                      .arg(token));
                }
                continue;
            }

            // Arithmetic expression (e.g. "M * K"): validate every identifier present.
            static const QRegularExpression kIdentRe(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
            static const QRegularExpression kValidExprChars(QStringLiteral("^[A-Za-z0-9_\\s\\+\\-\\*\\/\\(\\)]+$"));
            if (!kValidExprChars.match(token).hasMatch()) {
                return Utils::Result::failure(QStringLiteral("Type '%1' has an invalid dimension expression '%2'.")
                                                  .arg(symbol.name, token));
            }
            auto it = kIdentRe.globalMatch(token);
            while (it.hasNext()) {
                const QString ident = it.next().captured();
                if (!constantValuesByName.contains(ident)) {
                    return Utils::Result::failure(QStringLiteral("Type '%1' references unknown constant '%2'.")
                                                      .arg(symbol.name, ident));
                }
            }
        }
    }

    for (const SymbolRecord& symbol : symbols) {
        if (symbol.kind != SymbolKind::TensorAccessPattern)
            continue;

        if (symbol.tap.rows <= 0 || symbol.tap.cols <= 0) {
            return Utils::Result::failure(QStringLiteral("TAP '%1' dimensions must be positive.")
                                              .arg(symbol.name));
        }
        if (symbol.tap.offset < 0) {
            return Utils::Result::failure(QStringLiteral("TAP '%1' offset must be zero or greater.")
                                              .arg(symbol.name));
        }
        if (!symbol.tap.useTiler2D) {
            if (symbol.tap.sizes.isEmpty() || symbol.tap.strides.isEmpty()
                || symbol.tap.sizes.size() != symbol.tap.strides.size()) {
                return Utils::Result::failure(QStringLiteral("TAP '%1' must declare matching sizes and strides.")
                                                  .arg(symbol.name));
            }
            for (const int size : symbol.tap.sizes) {
                if (size <= 0) {
                    return Utils::Result::failure(QStringLiteral("TAP '%1' sizes must be positive integers.")
                                                      .arg(symbol.name));
                }
            }
            for (const int stride : symbol.tap.strides) {
                if (stride <= 0) {
                    return Utils::Result::failure(QStringLiteral("TAP '%1' strides must be positive integers.")
                                                      .arg(symbol.name));
                }
            }
        }
    }

    return Utils::Result::success();
}

QString SymbolsController::uniqueNameForBase(const QString& base) const
{
    QSet<QString> existing;
    for (const SymbolRecord& symbol : m_symbols)
        existing.insert(symbol.name);

    QString candidate = base.trimmed();
    if (candidate.isEmpty())
        candidate = QStringLiteral("symbol");

    if (!existing.contains(candidate))
        return candidate;

    int suffix = 1;
    while (existing.contains(QStringLiteral("%1_%2").arg(candidate).arg(suffix)))
        ++suffix;
    return QStringLiteral("%1_%2").arg(candidate).arg(suffix);
}

QStringList SymbolsController::referencesForConstantName(const QString& name,
                                                         const QVector<SymbolRecord>& symbols) const
{
    QStringList references;
    const QString cleaned = cleanedName(name);
    if (cleaned.isEmpty())
        return references;

    for (const SymbolRecord& symbol : symbols) {
        if (symbol.kind != SymbolKind::TypeAbstraction)
            continue;
        if (symbol.type.shapeTokens.contains(cleaned))
            references.push_back(symbol.name);
    }

    references.removeDuplicates();
    references.sort(Qt::CaseInsensitive);
    return references;
}

void SymbolsController::replaceSymbols(QVector<SymbolRecord> nextSymbols)
{
    m_symbols = std::move(nextSymbols);
    emit symbolsChanged();
}

void SymbolsController::reconcileSelectedSymbol()
{
    if (!m_selectedSymbolId.isEmpty() && symbolById(m_selectedSymbolId))
        return;

    const QString nextId = m_symbols.isEmpty() ? QString() : m_symbols.front().id;
    setSelectedSymbolId(nextId);
}

} // namespace Aie::Internal
