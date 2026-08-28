// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace Aie {

constexpr const char kDefaultProfileId[] = "amd-xdna1-phoenix";
constexpr const char kDefaultDeviceId[] = "amd-xdna1-phoenix";
constexpr const char kDeviceTopologiesResource[] = ":/aie/AieDeviceTopologies.json";
constexpr double kDefaultTileSpacing = 20.0;
constexpr double kDefaultOuterMargin = 24.0;
constexpr double kDefaultCellSize = 96.0;
constexpr double kDefaultKeepoutMargin = -1.0;
// Extra pixels pushed between the DDR block and the shim row (on top of the
// normal cellSpacing already between every row), so Distribute/Collect hubs
// dropped on DDR<->shim wires have room to render without crowding either
// endpoint. Matches the y-offset (168px) found by hand-nudging the DDR block
// in Example_Designs/gemm.ironsmith/canvas/document.json's blockOffsets.
constexpr double kDdrHubGapPixels = 168.0;

} // namespace Aie