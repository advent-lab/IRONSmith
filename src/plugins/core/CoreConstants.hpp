// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtGlobal>

namespace Core {
namespace Constants {

// NOTE: These must be treated as stable IDs. They will end up in settings/layout persistence and action bindings.

inline constexpr char CORE_ID[] = "IRONSmith.Core";
inline constexpr char CORE_VERSION[] = "1.0.0";

inline constexpr char MAIN_WINDOW_OBJECT_NAME[] = "IRONSmithMainWindow";
inline constexpr int DEFAULT_MAIN_WINDOW_WIDTH = 1100;
inline constexpr int DEFAULT_MAIN_WINDOW_HEIGHT = 700;

// Frame regions (MainWindow central widget)
inline constexpr char FRAME_WIDGET[] = "IRONSmith.Frame";
inline constexpr char FRAME_MENU[] = "IRONSmith.Menu";
inline constexpr char FRAME_RIBBON[] = "IRONSmith.Frame.Ribbon";
inline constexpr char FRAME_PLAYGROUND[] = "IRONSmith.Frame.Playground";

// Ribbon tabs/pages
inline constexpr char RIBBON_TAB_HOME[] = "IRONSmith.Ribbon.Home";
inline constexpr char RIBBON_TAB_VIEW[] = "IRONSmith.Ribbon.View";
inline constexpr char RIBBON_TAB_OUTPUT[] = "IRONSmith.Ribbon.Output";

inline constexpr char RIBBON_TAB_HOME_PROJECT_GROUP[] = "IRONSmith.Ribbon.Home.ProjectGroup";
inline constexpr char RIBBON_TAB_HOME_CANVAS_GROUP[] = "IRONSmith.Ribbon.Home.CanvasGroup";
inline constexpr char RIBBON_TAB_HOME_WIRES_GROUP[] = "IRONSmith.Ribbon.Home.WiresGroup";
inline constexpr char RIBBON_TAB_HOME_VIEW_GROUP[] = "IRONSmith.Ribbon.Home.ViewGroup";

// Playground regions
inline constexpr char PLAYGROUND_WIDGET[] = "IRONSmith.Playground";
inline constexpr char PLAYGROUND_HEADER[] = "IRONSmith.Playground.HeaderBar";
inline constexpr char PLAYGROUND_STATUS[] = "IRONSmith.Playground.StatusBar";
inline constexpr char PLAYGROUND_CENTER[] = "IRONSmith.Playground.CenterHost";

// Sidebars (rails + overlay panels)
inline constexpr char SIDEBAR_LEFT_RAIL[] = "IRONSmith.Sidebar.Left.Rail";
inline constexpr char SIDEBAR_RIGHT_RAIL[] = "IRONSmith.Sidebar.Right.Rail";
inline constexpr char SIDEBAR_LEFT_PANEL[] = "IRONSmith.Sidebar.Left.Panel";
inline constexpr char SIDEBAR_RIGHT_PANEL[] = "IRONSmith.Sidebar.Right.Panel";

// Project Item Ids
inline constexpr char PROJECT_OPEN_ITEMID[] = "project.open";
inline constexpr char PROJECT_NEW_ITEMID[] = "project.new";
inline constexpr char PROJECT_SAVE_ITEMID[] = "project.save";
inline constexpr char PROJECT_SAVE_AS_ITEMID[] = "project.save.as";
inline constexpr char PROJECT_RECENT_ITEMID[] = "project.recent";

// Canvas Item Ids
inline constexpr char CANVAS_SELECT_ITEMID[] = "canvas.select";
inline constexpr char CANVAS_PAN_ITEMID[] = "canvas.pan";
inline constexpr char CANVAS_LINK_ITEMID[] = "canvas.link";
inline constexpr char CANVAS_LINK_SPLIT_ITEMID[] = "canvas.link.split";
inline constexpr char CANVAS_LINK_JOIN_ITEMID[] = "canvas.link.join";
inline constexpr char CANVAS_LINK_BROADCAST_ITEMID[] = "canvas.link.broadcast";
inline constexpr char CANVAS_LINK_FIFO_ITEMID[] = "canvas.link.fifo";
inline constexpr char CANVAS_LINK_FORWARD_FIFO_ITEMID[] = "canvas.link.forward_fifo";

inline constexpr char CANVAS_WIRE_AUTO_ROUTE_ITEMID[] = "canvas.wire.auto.route";
inline constexpr char CANVAS_WIRE_CLEAR_OVERRIDES_ITEMID[] = "canvas.wire.clear.overrides";
inline constexpr char CANVAS_WIRE_TOGGLE_ARROWS_ITEMID[] = "canvas.wire.toggle.arrows";

inline constexpr char CANVAS_VIEW_ZOOM_IN_ITEMID[] = "canvas.view.zoom.in";
inline constexpr char CANVAS_VIEW_ZOOM_OUT_ITEMID[] = "canvas.view.zoom.out";
inline constexpr char CANVAS_VIEW_ZOOM_FIT_ITEMID[] = "canvas.view.zoom.fit";
inline constexpr char CANVAS_VIEW_RESET_ITEMID[] = "canvas.view.reset";

} // namespace Core
} // namespace Constants
