// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

#include <optional>

namespace Aie {

enum class TileKind : unsigned char {
    Shim,
    Mem,
    Aie,
    Unknown
};

struct TileGroup final {
    QVector<int> rows;
    QVector<int> cols;
    QVector<int> virtualCols;

    bool containsRow(int row) const { return rows.contains(row); }
    bool containsCol(int col, bool includeVirtual = true) const {
        return cols.contains(col) || (includeVirtual && virtualCols.contains(col));
    }
    bool isVirtualCol(int col) const { return virtualCols.contains(col); }
    bool contains(int col, int row, bool includeVirtual = true) const {
        return containsRow(row) && containsCol(col, includeVirtual);
    }
};

struct GridRows final {
    int shim = 0;
    int mem = 0;
    int aie = 0;

    int total() const { return shim + mem + aie; }
};

struct GridDefinition final {
    int columns = 0;
    GridRows rows;
    QStringList rowOrderBottomToTop;
};

struct TileLayout final {
    QString coordinateSystem;
    TileGroup shim;
    TileGroup mem;
    TileGroup aie;
};

struct LinuxDeviceMatch final {
    QString driver;
    QStringList pciIds;
};

struct DeviceMatch final {
    std::optional<LinuxDeviceMatch> linux;
};

struct VirtualShimResolvePolicy final {
    QString strategy;
    QVector<int> fallbackOrder;
};

struct VirtualShimPolicy final {
    bool enabled = false;
    QVector<int> virtualShimColumns;
    VirtualShimResolvePolicy resolveVirtualShimToRealShimColumn;
};

struct NonShimRoutePreference final {
    QVector<int> viaColumnsPreference;
};

struct HostInterface final {
    QVector<int> shimCapableColumns;
    QMap<int, NonShimRoutePreference> nonShimColumnsRouteVia;
    VirtualShimPolicy virtualShimPolicy;
};

struct ColumnSliceHint final {
    int rows = 0;
    QStringList rowKindsByIndex;
};

struct IronModelHints final {
    ColumnSliceHint columnSlice;
};

struct NpuProfile final {
    QString id;
    QString name;
    QString vendor;
    QString family;
    QString aieArch;
    DeviceMatch match;
    GridDefinition grid;
    TileLayout tiles;
    HostInterface hostInterface;
    IronModelHints ironModelHints;
};

struct UnknownDevicePolicy final {
    GridDefinition grid;
    HostInterface hostInterface;
};

struct NpuProfileCatalog final {
    int schemaVersion = 1;
    QVector<NpuProfile> devices;
    UnknownDevicePolicy defaults;
};

} // namespace Aie