// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasGlobal.hpp"

#include "utils/StrongId.hpp"

#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QRect>
#include <vector>

namespace Canvas {

struct BlockIdTag {};
struct PortIdTag {};
struct LinkIdTag {};
struct ObjectIdTag {};

using BlockId = Utils::StrongId<BlockIdTag>;
using PortId  = Utils::StrongId<PortIdTag>;
using LinkId  = Utils::StrongId<LinkIdTag>;
using ObjectId = Utils::StrongId<ObjectIdTag>;

struct CANVAS_EXPORT GridCoord final {
	int x = 0;
	int y = 0;
};

struct CANVAS_EXPORT GridSize final {
	int w = 1;
	int h = 1;
};

struct CANVAS_EXPORT FabricCoord final {
	int x = 0;
	int y = 0;
};

enum class WireArrowPolicy : uint8_t {
    None,
    Start,
    End
};

// struct Port final {
// 	PortId   id{};
// 	BlockId  owner{};
// 	PortCap  cap = PortCap::InOut;
// 	PortSide side = PortSide::East;
// 	uint16_t slot = 0;
// 	QString  tag;
// };
//
// struct Block final {
// 	BlockId   id{};
// 	QString   typeTag;
// 	QString   label;
// 	GridCoord anchor{};
// 	GridSize  footprint{1,1};
// 	std::vector<PortId> ports;
// };
//
// struct Link final {
// 	LinkId   id{};
// 	PortId   src{};
// 	PortId   dst{};
// 	std::vector<FabricCoord> steps;
// 	QString  tag;
// };
//

} // namespace Canvas

Q_DECLARE_METATYPE(Canvas::BlockId)
Q_DECLARE_METATYPE(Canvas::PortId)
Q_DECLARE_METATYPE(Canvas::LinkId)
Q_DECLARE_METATYPE(Canvas::ObjectId)
