// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utils/Result.hpp>

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

namespace Aie::Internal {

enum class SymbolKind : uint8_t {
    Constant,
    TypeAbstraction,
    TensorAccessPattern
};

enum class SymbolFilterKind : uint8_t {
    All,
    Constants,
    Types,
    TensorAccessPatterns
};

struct ConstantSymbolData final {
    qint64 value = 1;
};

struct TypeAbstractionSymbolData final {
    QStringList shapeTokens{QStringLiteral("1")};
    QString dtype = QStringLiteral("int32");
};

struct TensorAccessPatternSymbolData final {
    int rows = 16;
    int cols = 16;
    int offset = 0;
    QVector<int> sizes{4, 4, 4};
    QVector<int> strides{16, 64, 1};
    bool showRepetitions = true;
};

struct SymbolRecord final {
    QString id;
    SymbolKind kind = SymbolKind::Constant;
    QString name;
    ConstantSymbolData constant;
    TypeAbstractionSymbolData type;
    TensorAccessPatternSymbolData tap;
};

QString symbolKindDisplayName(SymbolKind kind);
QString typeAbstractionSummary(const TypeAbstractionSymbolData& typeData);
QString tensorAccessPatternSummary(const TensorAccessPatternSymbolData& tapData);
QString symbolSummary(const SymbolRecord& symbol);
QString typeAbstractionPreview(const QString& name, const TypeAbstractionSymbolData& typeData);
QString tensorAccessPatternPreview(const QString& name, const TensorAccessPatternSymbolData& tapData);
QString symbolPreview(const SymbolRecord& symbol);
QStringList supportedSymbolDtypes();
bool isValidSymbolIdentifier(const QString& name);
bool parseIntegralToken(const QString& token, qint64& outValue);
QJsonObject ensureSymbolsMetadataSchema(const QJsonObject& metadata);

QJsonObject serializeSymbolsMetadata(const QVector<SymbolRecord>& symbols);
Utils::Result parseSymbolsMetadata(const QJsonObject& metadata, QVector<SymbolRecord>& outSymbols);

} // namespace Aie::Internal
