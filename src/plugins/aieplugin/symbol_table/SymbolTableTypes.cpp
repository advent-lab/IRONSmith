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
const QString kRowsKey = u"rows"_s;
const QString kColsKey = u"cols"_s;
const QString kOffsetKey = u"offset"_s;
const QString kSizesKey = u"sizes"_s;
const QString kStridesKey = u"strides"_s;
const QString kShowRepetitionsKey = u"showRepetitions"_s;
const QString kUseTiler2DKey = u"useTiler2D"_s;
const QString kTensorDimsKey = u"tensorDims"_s;
const QString kTileDimsKey = u"tileDims"_s;
const QString kTileCountsKey = u"tileCounts"_s;
const QString kPatternRepeatKey = u"patternRepeat"_s;
const QString kDimsEntriesKey   = u"dims_entries"_s;
const QString kDimsCountKey     = u"count"_s;
const QString kDimsStrideKey    = u"stride"_s;

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
        case SymbolKind::TensorAccessPattern:
            return u"tap"_s;
        case SymbolKind::LayoutDims:
            return u"dims"_s;
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
    if (normalized == u"tap"_s || normalized == u"tensoraccesspattern"_s) {
        outKind = SymbolKind::TensorAccessPattern;
        return true;
    }
    if (normalized == u"dims"_s || normalized == u"layoutdims"_s) {
        outKind = SymbolKind::LayoutDims;
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

QString intVectorSummary(const QVector<int>& values)
{
    QStringList tokens;
    tokens.reserve(values.size());
    for (const int value : values)
        tokens.push_back(QString::number(value));
    return QStringLiteral("[%1]").arg(tokens.join(QStringLiteral(", ")));
}

} // namespace

QString symbolKindDisplayName(SymbolKind kind)
{
    switch (kind) {
        case SymbolKind::Constant:
            return QStringLiteral("Constant");
        case SymbolKind::TypeAbstraction:
            return QStringLiteral("Type");
        case SymbolKind::TensorAccessPattern:
            return QStringLiteral("TAP");
        case SymbolKind::LayoutDims:
            return QStringLiteral("Dims");
    }

    return QStringLiteral("Constant");
}

QString typeAbstractionSummary(const TypeAbstractionSymbolData& typeData)
{
    return QStringLiteral("ndarray[%1, %2]")
        .arg(shapeTupleText(typeData.shapeTokens), typeData.dtype.trimmed());
}

QString tensorAccessPatternSummary(const TensorAccessPatternSymbolData& tapData)
{
    if (tapData.useTiler2D) {
        const QString arraySize = tapData.tensorDims.isEmpty() ? QStringLiteral("?") : tapData.tensorDims;
        const QString tileSize  = tapData.tileDims.isEmpty()   ? QStringLiteral("?") : tapData.tileDims;
        const QString tileCounts = tapData.tileCounts.isEmpty() ? QStringLiteral("?") : tapData.tileCounts;
        QString summary = QStringLiteral("array=%1, tile=%2, counts=%3")
            .arg(arraySize, tileSize, tileCounts);
        if (!tapData.patternRepeat.isEmpty())
            summary += QStringLiteral(", repeat=%1").arg(tapData.patternRepeat);
        return summary;
    }
    return QStringLiteral("%1x%2, off=%3, sizes=%4, strides=%5")
        .arg(tapData.rows)
        .arg(tapData.cols)
        .arg(tapData.offset)
        .arg(intVectorSummary(tapData.sizes), intVectorSummary(tapData.strides));
}

QString layoutDimsPythonExpr(const LayoutDimsSymbolData& dimsData)
{
    if (dimsData.entries.isEmpty())
        return QStringLiteral("[]");
    QStringList items;
    items.reserve(dimsData.entries.size());
    for (const LayoutDimsEntry& e : dimsData.entries) {
        const QString c = e.count.trimmed().isEmpty()  ? QStringLiteral("0") : e.count.trimmed();
        const QString s = e.stride.trimmed().isEmpty() ? QStringLiteral("0") : e.stride.trimmed();
        items.push_back(QStringLiteral("(%1, %2)").arg(c, s));
    }
    return QStringLiteral("[%1]").arg(items.join(QStringLiteral(", ")));
}

QString layoutDimsSummary(const LayoutDimsSymbolData& dimsData)
{
    const int n = dimsData.entries.size();
    return QStringLiteral("%1 level%2").arg(n).arg(n == 1 ? QString() : QStringLiteral("s"));
}

QString layoutDimsPreview(const QString& name, const LayoutDimsSymbolData& dimsData)
{
    return QStringLiteral("%1 = %2").arg(name.trimmed(), layoutDimsPythonExpr(dimsData));
}

QString symbolSummary(const SymbolRecord& symbol)
{
    if (symbol.kind == SymbolKind::Constant)
        return QString::number(symbol.constant.value);
    if (symbol.kind == SymbolKind::TypeAbstraction)
        return typeAbstractionSummary(symbol.type);
    if (symbol.kind == SymbolKind::LayoutDims)
        return layoutDimsSummary(symbol.layoutDims);
    return tensorAccessPatternSummary(symbol.tap);
}

QString typeAbstractionPreview(const QString& name, const TypeAbstractionSymbolData& typeData)
{
    return QStringLiteral("%1 = np.ndarray[%2, np.dtype[np.%3]]")
        .arg(name.trimmed(), shapeTupleText(typeData.shapeTokens), typeData.dtype.trimmed());
}

QString tensorAccessPatternPreview(const QString& name, const TensorAccessPatternSymbolData& tapData)
{
    if (tapData.useTiler2D) {
        const QString tensorDims  = tapData.tensorDims.isEmpty()  ? QStringLiteral("?") : tapData.tensorDims;
        const QString tileDims    = tapData.tileDims.isEmpty()    ? QStringLiteral("?") : tapData.tileDims;
        const QString tileCounts  = tapData.tileCounts.isEmpty()  ? QStringLiteral("?") : tapData.tileCounts;
        const QString repeat      = tapData.patternRepeat.isEmpty() ? QString() : tapData.patternRepeat;
        QString result = QStringLiteral("%1 = TensorTiler2D.group_tiler((%2), (%3), (%4)")
            .arg(name.trimmed(), tensorDims, tileDims, tileCounts);
        if (!repeat.isEmpty())
            result += QStringLiteral(", pattern_repeat=%1").arg(repeat);
        result += QStringLiteral(", prune_step=False)[0]");
        return result;
    }
    return QStringLiteral("%1 = TensorAccessPattern((%2, %3), offset=%4, sizes=%5, strides=%6)")
        .arg(name.trimmed())
        .arg(tapData.rows)
        .arg(tapData.cols)
        .arg(tapData.offset)
        .arg(intVectorSummary(tapData.sizes), intVectorSummary(tapData.strides));
}

QString symbolPreview(const SymbolRecord& symbol)
{
    if (symbol.kind == SymbolKind::Constant)
        return QStringLiteral("%1 = %2").arg(symbol.name.trimmed()).arg(symbol.constant.value);
    if (symbol.kind == SymbolKind::TypeAbstraction)
        return typeAbstractionPreview(symbol.name, symbol.type);
    if (symbol.kind == SymbolKind::LayoutDims)
        return layoutDimsPreview(symbol.name, symbol.layoutDims);
    return tensorAccessPatternPreview(symbol.name, symbol.tap);
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
        } else if (symbol.kind == SymbolKind::LayoutDims) {
            QJsonArray dimsArray;
            for (const LayoutDimsEntry& e : symbol.layoutDims.entries) {
                QJsonObject entryObj;
                entryObj.insert(kDimsCountKey,  e.count.trimmed());
                entryObj.insert(kDimsStrideKey, e.stride.trimmed());
                dimsArray.push_back(entryObj);
            }
            entry.insert(kDimsEntriesKey, dimsArray);
        } else if (symbol.kind == SymbolKind::TypeAbstraction) {
            QJsonArray shape;
            for (const QString& token : symbol.type.shapeTokens)
                shape.push_back(token.trimmed());
            entry.insert(kShapeKey, shape);
            entry.insert(kDTypeKey, symbol.type.dtype.trimmed());
        } else {
            entry.insert(kRowsKey, symbol.tap.rows);
            entry.insert(kColsKey, symbol.tap.cols);
            entry.insert(kOffsetKey, symbol.tap.offset);

            QJsonArray sizes;
            for (const int size : symbol.tap.sizes)
                sizes.push_back(size);
            entry.insert(kSizesKey, sizes);

            QJsonArray strides;
            for (const int stride : symbol.tap.strides)
                strides.push_back(stride);
            entry.insert(kStridesKey, strides);
            entry.insert(kShowRepetitionsKey, symbol.tap.showRepetitions);
            entry.insert(kUseTiler2DKey, symbol.tap.useTiler2D);
            if (!symbol.tap.tensorDims.isEmpty())
                entry.insert(kTensorDimsKey, symbol.tap.tensorDims);
            if (!symbol.tap.tileDims.isEmpty())
                entry.insert(kTileDimsKey, symbol.tap.tileDims);
            if (!symbol.tap.tileCounts.isEmpty())
                entry.insert(kTileCountsKey, symbol.tap.tileCounts);
            if (!symbol.tap.patternRepeat.isEmpty())
                entry.insert(kPatternRepeatKey, symbol.tap.patternRepeat);
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
        } else if (symbol.kind == SymbolKind::LayoutDims) {
            const QJsonArray dimsArray = entry.value(kDimsEntriesKey).toArray();
            for (const QJsonValue& v : dimsArray) {
                if (!v.isObject())
                    continue;
                const QJsonObject obj = v.toObject();
                LayoutDimsEntry e;
                e.count  = obj.value(kDimsCountKey).toString().trimmed();
                e.stride = obj.value(kDimsStrideKey).toString().trimmed();
                symbol.layoutDims.entries.push_back(e);
            }
        } else if (symbol.kind == SymbolKind::TypeAbstraction) {
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
        } else {
            const QJsonValue rowsValue = entry.value(kRowsKey);
            const QJsonValue colsValue = entry.value(kColsKey);
            const QJsonValue offsetValue = entry.value(kOffsetKey);
            const QJsonArray sizes = entry.value(kSizesKey).toArray();
            const QJsonArray strides = entry.value(kStridesKey).toArray();
            if (!rowsValue.isDouble() || !colsValue.isDouble() || !offsetValue.isDouble()) {
                return Utils::Result::failure(QStringLiteral("TAP symbol '%1' is missing dimensions or offset.")
                                                  .arg(symbol.name));
            }

            symbol.tap.rows = rowsValue.toInt();
            symbol.tap.cols = colsValue.toInt();
            symbol.tap.offset = offsetValue.toInt();
            const bool hasUseTiler2DKey = entry.contains(kUseTiler2DKey);
            symbol.tap.useTiler2D = entry.value(kUseTiler2DKey).toBool(true);
            // Migration: old files without useTiler2D that have >2 sizes used TensorAccessPattern.
            if (!hasUseTiler2DKey && sizes.size() > 2)
                symbol.tap.useTiler2D = false;
            symbol.tap.sizes.clear();
            symbol.tap.strides.clear();
            if (!symbol.tap.useTiler2D) {
                // For TensorAccessPattern mode, sizes/strides are required.
                if (sizes.isEmpty() || strides.isEmpty() || sizes.size() != strides.size()) {
                    return Utils::Result::failure(QStringLiteral("TAP symbol '%1' must declare matching sizes and strides.")
                                                      .arg(symbol.name));
                }
                symbol.tap.sizes.reserve(sizes.size());
                symbol.tap.strides.reserve(strides.size());
                for (const QJsonValue& sizeValue : sizes) {
                    if (!sizeValue.isDouble())
                        return Utils::Result::failure(QStringLiteral("TAP symbol '%1' has an invalid size entry.")
                                                          .arg(symbol.name));
                    symbol.tap.sizes.push_back(sizeValue.toInt());
                }
                for (const QJsonValue& strideValue : strides) {
                    if (!strideValue.isDouble())
                        return Utils::Result::failure(QStringLiteral("TAP symbol '%1' has an invalid stride entry.")
                                                          .arg(symbol.name));
                    symbol.tap.strides.push_back(strideValue.toInt());
                }
            } else {
                // For TensorTiler2D mode, sizes/strides are not required but load if present.
                for (const QJsonValue& sizeValue : sizes)
                    if (sizeValue.isDouble()) symbol.tap.sizes.push_back(sizeValue.toInt());
                for (const QJsonValue& strideValue : strides)
                    if (strideValue.isDouble()) symbol.tap.strides.push_back(strideValue.toInt());
            }
            symbol.tap.showRepetitions = entry.value(kShowRepetitionsKey).toBool(true);
            symbol.tap.tensorDims = entry.value(kTensorDimsKey).toString();
            symbol.tap.tileDims = entry.value(kTileDimsKey).toString();
            symbol.tap.tileCounts = entry.value(kTileCountsKey).toString();
            symbol.tap.patternRepeat = entry.value(kPatternRepeatKey).toString();
        }

        outSymbols.push_back(symbol);
    }

    return Utils::Result::success();
}

} // namespace Aie::Internal
