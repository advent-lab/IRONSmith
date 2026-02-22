// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/NpuProfileCanvasMapper.hpp"
#include "aieplugin/AieConstants.hpp"

#include <QtCore/QMarginsF>

namespace Aie {

namespace {

QString tileKindId(TileKind kind)
{
    switch (kind) {
        case TileKind::Shim: return QStringLiteral("shim");
        case TileKind::Mem: return QStringLiteral("mem");
        case TileKind::Aie: return QStringLiteral("aie");
        default: break;
    }
    return QStringLiteral("tile");
}

QString tileKindLabel(TileKind kind)
{
    return tileKindId(kind).toUpper();
}

TileKind tileKindFor(const NpuProfile& profile, int col, int row)
{
    if (profile.tiles.shim.contains(col, row, true))
        return TileKind::Shim;
    if (profile.tiles.mem.contains(col, row, true))
        return TileKind::Mem;
    if (profile.tiles.aie.contains(col, row, true))
        return TileKind::Aie;
    return TileKind::Unknown;
}

bool validateGridBounds(const NpuProfile& profile, Utils::Result& result)
{
    const int totalRows = profile.grid.rows.total();
    if (profile.grid.columns <= 0 || totalRows <= 0) {
        result.addError(QStringLiteral("Grid dimensions must be positive."));
        return false;
    }

    auto validateGroup = [&](const TileGroup& group, const QString& name) {
        for (int row : group.rows) {
            if (row < 0 || row >= totalRows)
                result.addError(QStringLiteral("Row %1 out of bounds for %2 tiles.").arg(row).arg(name));
        }
        for (int col : group.cols) {
            if (col < 0 || col >= profile.grid.columns)
                result.addError(QStringLiteral("Column %1 out of bounds for %2 tiles.").arg(col).arg(name));
        }
        for (int col : group.virtualCols) {
            if (col < 0 || col >= profile.grid.columns)
                result.addError(QStringLiteral("Virtual column %1 out of bounds for %2 tiles.").arg(col).arg(name));
        }
    };

    validateGroup(profile.tiles.shim, QStringLiteral("shim"));
    validateGroup(profile.tiles.mem, QStringLiteral("mem"));
    validateGroup(profile.tiles.aie, QStringLiteral("aie"));

    return result.ok;
}

} // namespace

Utils::Result buildCanvasGridModel(const NpuProfile& profile, CanvasGridModel& out)
{
    Utils::Result result = Utils::Result::success();

    if (!validateGridBounds(profile, result))
        return result;

    if (!profile.tiles.coordinateSystem.isEmpty() && profile.tiles.coordinateSystem != QLatin1String("col_row")) {
        result.addError(QStringLiteral("Unsupported coordinate system: %1").arg(profile.tiles.coordinateSystem));
        return result;
    }

    const int totalRows = profile.grid.rows.total();
    const int gridRows = totalRows + 1;

    Utils::GridSpec gridSpec;
    gridSpec.columns = profile.grid.columns;
    gridSpec.rows = gridRows;
    gridSpec.origin = Utils::GridOrigin::BottomLeft;
    gridSpec.autoCellSize = true;
    gridSpec.cellSpacing = QSizeF(Aie::kDefaultTileSpacing, Aie::kDefaultTileSpacing);
    gridSpec.outerMargin = QMarginsF(Aie::kDefaultOuterMargin, Aie::kDefaultOuterMargin,
                                     Aie::kDefaultOuterMargin, Aie::kDefaultOuterMargin);

    QVector<Canvas::Api::CanvasBlockSpec> blocks;
    blocks.reserve(gridSpec.columns * gridSpec.rows);

    if (gridSpec.columns > 0) {
        Canvas::Api::CanvasBlockSpec ddr;
        ddr.id = QStringLiteral("ddr");
        ddr.label = QStringLiteral("DDR");
        ddr.gridRect = {0, 0, gridSpec.columns, 1};
        ddr.movable = false;
        ddr.showPorts = false;
        ddr.deletable = false;
        ddr.allowMultiplePorts = true;
        ddr.styleKey = QStringLiteral("ddr");
        blocks.push_back(std::move(ddr));
    }

    for (int row = 0; row < totalRows; ++row) {
        for (int col = 0; col < gridSpec.columns; ++col) {
            const TileKind kind = tileKindFor(profile, col, row);
            if (kind == TileKind::Unknown)
                continue;

            Canvas::Api::CanvasBlockSpec spec;
            const QString kindId = tileKindId(kind);
            spec.id = QStringLiteral("%1%2_%3").arg(kindId).arg(col).arg(row);
            spec.label = tileKindLabel(kind);
            spec.gridRect = { col, row + 1, 1, 1 };
            spec.movable = false;
            spec.showPorts = true;
            spec.deletable = false;
            spec.styleKey = kindId;

            blocks.push_back(std::move(spec));
        }
    }

    out.gridSpec = gridSpec;
    out.blocks = std::move(blocks);

    return result;
}

} // namespace Aie
