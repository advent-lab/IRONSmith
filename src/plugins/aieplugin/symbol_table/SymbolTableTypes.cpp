// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/symbol_table/SymbolTableTypes.hpp"

#include <QtCore/QJsonArray>
#include <QtCore/QRegularExpression>
#include <QtCore/QtGlobal>

namespace Aie::Internal {

namespace {

using namespace Qt::StringLiterals;

const QString kSchemaKey = u"schema"_s;
const QString kSchemaVersionKey = u"schemaVersion"_s;
const QString kEntriesKey = u"entries"_s;
const QString kEntryIdKey = u"id"_s;
const QString kKindKey = u"kind"_s;
const QString kNameKey = u"name"_s;
const QString kValueKey = u"value"_s;
const QString kShapeKey = u"shape"_s;
const QString kDTypeKey = u"dtype"_s;

const QString kSchemaValue = u"aie.symbols/1"_s;
constexpr int kSchemaVersionValue = 1;
const QRegularExpression kIdentifierPattern(u"^[A-Za-z_][A-Za-z0-9_]*$"_s);

QString kindKey(SymbolKind kind)
{
    switch (kind) {
        case SymbolKind::Constant:
            return u"constant"_s;
        case SymbolKind::TypeAbstraction:
            return u"type"_s;
    }

    return u"constant"_s;
}

bool parseKind(const QString& text, SymbolKind& outKind)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == u"constant"_s) {
        outKind = SymbolKind::Constant;
        return true;
    }
    if (normalized == u"type"_s || normalized == u"typeabstraction"_s) {
        outKind = SymbolKind::TypeAbstraction;
        return true;
    }
    return false;
}

QStringList normalizeShapeTokens(const QStringList& tokens)
{
    QStringList normalized;
    normalized.reserve(tokens.size());
    for (const QString& token : tokens)
        normalized.push_back(token.trimmed());
    return normalized;
}

QString shapeTupleText(const QStringList& shapeTokens)
{
    const QStringList normalized = normalizeShapeTokens(shapeTokens);
    if (normalized.isEmpty())
        return QStringLiteral("()");
    if (normalized.size() == 1)
        return QStringLiteral("(%1,)").arg(normalized.front());
    return QStringLiteral("(%1)").arg(normalized.join(QStringLiteral(", ")));
}

} // namespace

QString symbolKindDisplayName(SymbolKind kind)
{
    switch (kind) {
        case SymbolKind::Constant:
            return QStringLiteral("Constant");
        case SymbolKind::TypeAbstraction:
            return QStringLiteral("Type");
    }

    return QStringLiteral("Constant");
}

QString typeAbstractionSummary(const TypeAbstractionSymbolData& typeData)
{
    return QStringLiteral("ndarray[%1, %2]")
        .arg(shapeTupleText(typeData.shapeTokens), typeData.dtype.trimmed());
}

QString symbolSummary(const SymbolRecord& symbol)
{
    if (symbol.kind == SymbolKind::Constant)
        return QString::number(symbol.constant.value);
    return typeAbstractionSummary(symbol.type);
}

QString typeAbstractionPreview(const QString& name, const TypeAbstractionSymbolData& typeData)
{
    return QStringLiteral("%1 = np.ndarray[%2, np.dtype[np.%3]]")
        .arg(name.trimmed(), shapeTupleText(typeData.shapeTokens), typeData.dtype.trimmed());
}

QString symbolPreview(const SymbolRecord& symbol)
{
    if (symbol.kind == SymbolKind::Constant)
        return QStringLiteral("%1 = %2").arg(symbol.name.trimmed()).arg(symbol.constant.value);
    return typeAbstractionPreview(symbol.name, symbol.type);
}

QStringList supportedSymbolDtypes()
{
    return {
        QStringLiteral("int8"),
        QStringLiteral("int16"),
        QStringLiteral("int32"),
        QStringLiteral("int64"),
        QStringLiteral("uint8"),
        QStringLiteral("uint16"),
        QStringLiteral("uint32"),
        QStringLiteral("uint64"),
        QStringLiteral("float16"),
        QStringLiteral("float32"),
        QStringLiteral("float64"),
        QStringLiteral("bfloat16")
    };
}

bool isValidSymbolIdentifier(const QString& name)
{
    return kIdentifierPattern.match(name.trimmed()).hasMatch();
}

bool parseIntegralToken(const QString& token, qint64& outValue)
{
    bool ok = false;
    const qint64 parsed = token.trimmed().toLongLong(&ok);
    if (!ok)
        return false;
    outValue = parsed;
    return true;
}

QJsonObject ensureSymbolsMetadataSchema(const QJsonObject& metadata)
{
    QJsonObject normalized = metadata;
    if (normalized.value(kSchemaKey).toString().trimmed().isEmpty())
        normalized.insert(kSchemaKey, kSchemaValue);
    if (!normalized.contains(kSchemaVersionKey))
        normalized.insert(kSchemaVersionKey, kSchemaVersionValue);
    return normalized;
}

QJsonObject serializeSymbolsMetadata(const QVector<SymbolRecord>& symbols)
{
    QJsonObject root = ensureSymbolsMetadataSchema(QJsonObject{});

    QJsonArray entries;
    for (const SymbolRecord& symbol : symbols) {
        if (symbol.name.trimmed().isEmpty())
            continue;

        QJsonObject entry;
        entry.insert(kEntryIdKey, symbol.id);
        entry.insert(kKindKey, kindKey(symbol.kind));
        entry.insert(kNameKey, symbol.name.trimmed());

        if (symbol.kind == SymbolKind::Constant) {
            entry.insert(kValueKey, static_cast<qint64>(symbol.constant.value));
        } else {
            QJsonArray shape;
            for (const QString& token : symbol.type.shapeTokens)
                shape.push_back(token.trimmed());
            entry.insert(kShapeKey, shape);
            entry.insert(kDTypeKey, symbol.type.dtype.trimmed());
        }

        entries.push_back(entry);
    }

    root.insert(kEntriesKey, entries);
    return root;
}

Utils::Result parseSymbolsMetadata(const QJsonObject& metadata, QVector<SymbolRecord>& outSymbols)
{
    outSymbols.clear();

    if (metadata.isEmpty())
        return Utils::Result::success();

    const QJsonValue schemaValue = metadata.value(kSchemaKey);
    if (schemaValue.isString() && schemaValue.toString() != kSchemaValue) {
        return Utils::Result::failure(QStringLiteral("Unsupported symbols metadata schema: %1")
                                          .arg(schemaValue.toString()));
    }

    const QJsonValue schemaVersionValue = metadata.value(kSchemaVersionKey);
    if (schemaVersionValue.isDouble() && schemaVersionValue.toInt() != kSchemaVersionValue) {
        return Utils::Result::failure(QStringLiteral("Unsupported symbols metadata schemaVersion: %1")
                                          .arg(schemaVersionValue.toInt()));
    }

    const QJsonArray entries = metadata.value(kEntriesKey).toArray();
    outSymbols.reserve(entries.size());

    for (const QJsonValue& value : entries) {
        if (!value.isObject())
            return Utils::Result::failure(QStringLiteral("Symbols metadata entry must be an object."));

        const QJsonObject entry = value.toObject();
        SymbolRecord symbol;
        symbol.id = entry.value(kEntryIdKey).toString().trimmed();
        symbol.name = entry.value(kNameKey).toString().trimmed();

        SymbolKind kind = SymbolKind::Constant;
        if (!parseKind(entry.value(kKindKey).toString(), kind)) {
            return Utils::Result::failure(QStringLiteral("Unsupported symbol kind: %1")
                                              .arg(entry.value(kKindKey).toString()));
        }
        symbol.kind = kind;

        if (symbol.kind == SymbolKind::Constant) {
            const QJsonValue valueToken = entry.value(kValueKey);
            if (!valueToken.isDouble())
                return Utils::Result::failure(QStringLiteral("Constant symbol '%1' is missing an integral value.")
                                                  .arg(symbol.name));

            symbol.constant.value = static_cast<qint64>(valueToken.toDouble());
        } else {
            const QJsonArray shape = entry.value(kShapeKey).toArray();
            if (shape.isEmpty()) {
                return Utils::Result::failure(QStringLiteral("Type symbol '%1' must declare at least one dimension.")
                                                  .arg(symbol.name));
            }

            QStringList tokens;
            tokens.reserve(shape.size());
            for (const QJsonValue& dimValue : shape)
                tokens.push_back(dimValue.toString().trimmed());

            symbol.type.shapeTokens = std::move(tokens);
            symbol.type.dtype = entry.value(kDTypeKey).toString().trimmed();
        }

        outSymbols.push_back(symbol);
    }

    return Utils::Result::success();
}

} // namespace Aie::Internal
