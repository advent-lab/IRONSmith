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
    TensorAccessPattern,
    LayoutDims
};

enum class SymbolFilterKind : uint8_t {
    All,
    Constants,
    Types,
    TensorAccessPatterns,
    LayoutDims
};

struct ConstantSymbolData final {
    qint64 value = 1;
};

struct TypeAbstractionSymbolData final {
    QStringList shapeTokens{QStringLiteral("1")};
    QString dtype = QStringLiteral("int32");
};

// One level of a tiling layout: (count, stride) pair stored as expression strings.
struct LayoutDimsEntry final {
    QString count;   // first tuple element, e.g. "M_tile // micro_r"
    QString stride;  // second tuple element, e.g. "micro_r * K"
};

struct LayoutDimsSymbolData final {
    QVector<LayoutDimsEntry> entries;
};

struct TensorAccessPatternSymbolData final {
    int rows = 16;
    int cols = 16;
    int offset = 0;
    QVector<int> sizes{4, 4};
    QVector<int> strides{16, 1};
    bool showRepetitions = true;
    bool useTiler2D = true;  // If true, generate TensorTiler2D code; if false, use TensorAccessPattern
    // TensorTiler2D-specific fields (used when useTiler2D = true)
    QString tensorDims;   // total tensor size, e.g. "16 x 16"
    QString tileDims;
    QString tileCounts;
    QString patternRepeat;
};

struct SymbolRecord final {
    QString id;
    SymbolKind kind = SymbolKind::Constant;
    QString name;
    ConstantSymbolData constant;
    TypeAbstractionSymbolData type;
    TensorAccessPatternSymbolData tap;
    LayoutDimsSymbolData layoutDims;
};

QString symbolKindDisplayName(SymbolKind kind);
QString typeAbstractionSummary(const TypeAbstractionSymbolData& typeData);
QString tensorAccessPatternSummary(const TensorAccessPatternSymbolData& tapData);
QString layoutDimsSummary(const LayoutDimsSymbolData& dimsData);
QString symbolSummary(const SymbolRecord& symbol);
QString typeAbstractionPreview(const QString& name, const TypeAbstractionSymbolData& typeData);
QString tensorAccessPatternPreview(const QString& name, const TensorAccessPatternSymbolData& tapData);
QString layoutDimsPreview(const QString& name, const LayoutDimsSymbolData& dimsData);
QString layoutDimsPythonExpr(const LayoutDimsSymbolData& dimsData);
QString symbolPreview(const SymbolRecord& symbol);
QStringList supportedSymbolDtypes();
bool isValidSymbolIdentifier(const QString& name);
bool parseIntegralToken(const QString& token, qint64& outValue);
QJsonObject ensureSymbolsMetadataSchema(const QJsonObject& metadata);

QJsonObject serializeSymbolsMetadata(const QVector<SymbolRecord>& symbols);
Utils::Result parseSymbolsMetadata(const QJsonObject& metadata, QVector<SymbolRecord>& outSymbols);

} // namespace Aie::Internal
