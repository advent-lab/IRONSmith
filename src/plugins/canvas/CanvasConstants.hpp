// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace Canvas::Constants {

inline constexpr char CANVAS_BACKGROUND_COLOR[] = "#121316";

inline constexpr double kWorldScale = 3.0;
inline constexpr double kGridStep = 8.0 * kWorldScale;

inline constexpr double kMinZoom = 0.01;
inline constexpr double kMaxZoom = 4.00;
inline constexpr double kZoomStep = 1.10;

// Block styling (world-space colors; widths are adjusted in CanvasStyle).
inline constexpr char kBlockOutlineColor[] = "#2EC27E";
inline constexpr char kBlockFillColor[] = "#0E1B18";
inline constexpr char kBlockTextColor[] = "#B8C2CC";
inline constexpr char kBlockSelectionColor[] = "#5DA9FF";
inline constexpr char kWireColor[] = "#8D99A8";
inline constexpr char kWireAnnotationFillColor[] = "#C8D4E8";
inline constexpr char kWireAnnotationOutlineColor[] = "#8A98AF";
inline constexpr char kWireAnnotationTextColor[] = "#1E2430";
inline constexpr char kWireAnnotationSelectedFillColor[] = "#D5E4FF";
inline constexpr char kWireAnnotationSelectedOutlineColor[] = "#79A6FF";
inline constexpr char kWireAnnotationSelectedTextColor[] = "#0F1B33";
inline constexpr char kWireAnnotationForwardFillColor[] = "#F6D5AF";
inline constexpr char kWireAnnotationForwardOutlineColor[] = "#D79A57";
inline constexpr char kWireAnnotationForwardTextColor[] = "#2E1E09";
inline constexpr char kWireAnnotationForwardSelectedFillColor[] = "#FFE7C9";
inline constexpr char kWireAnnotationForwardSelectedOutlineColor[] = "#E7A660";
inline constexpr char kWireAnnotationForwardSelectedTextColor[] = "#2A1703";
inline constexpr char kDynamicPortColor[] = "#D2B36A";
inline constexpr double kBlockCornerRadius = 6.0 * kWorldScale;
inline constexpr double kBlockLabelPointSize = 10.0 * kWorldScale;
inline constexpr double kBlockLabelPadX = 6.0 * kWorldScale;
inline constexpr double kBlockLabelPadY = 4.0 * kWorldScale;
inline constexpr double kBlockStereotypePointSize = 8.0 * kWorldScale;
inline constexpr double kBlockStereotypeOffsetY = 2.0 * kWorldScale;
inline constexpr char kBlockStereotypeColor[] = "#88919A";
inline constexpr char kBlockStereotypeLinkColor[] = "#4A90E2";
inline constexpr double kPortHitRadiusPx = 8.0;
inline constexpr double kEdgeHoverRadiusPx = 14.0;
inline constexpr double kEndpointHitRadiusPx = 10.0;
inline constexpr double kEndpointDragThresholdPx = 4.0;
inline constexpr double kMarqueeDragThresholdPx = 4.0;
inline constexpr double kPortActivationBandPx = 20.0;

inline constexpr double kPortStubLength = 10.0 * kWorldScale;
inline constexpr double kPortStubLengthHover = 14.0 * kWorldScale;
inline constexpr double kPortBoxHalf = 3.0 * kWorldScale;
inline constexpr double kPortBoxHalfHover = 4.0 * kWorldScale;
inline constexpr double kPortHitStubLengthPx = kPortStubLengthHover;
inline constexpr double kPortHitBoxHalfPx = kPortBoxHalfHover;
inline constexpr double kPortLabelPointSize = 8.0 * kWorldScale;
inline constexpr double kPortLabelOffset = 6.0 * kWorldScale;
inline constexpr double kWireAnnotationPointSize = 7.0 * kWorldScale;
inline constexpr double kWireAnnotationPadX = 4.0 * kWorldScale;
inline constexpr double kWireAnnotationPadY = 2.0 * kWorldScale;
inline constexpr double kWireAnnotationCornerRadius = 2.0 * kWorldScale;
inline constexpr double kWireAnnotationOffsetY = 4.0 * kWorldScale;
inline constexpr double kWireAnnotationBaseNormalOffset = 9.0 * kWorldScale;
inline constexpr double kWireAnnotationLaneOffset = 6.0 * kWorldScale;
inline constexpr double kWireAnnotationTangentOffset = 6.0 * kWorldScale;
inline constexpr double kWireAnnotationHideZoom = 0.35;
inline constexpr double kWireAnnotationCompactZoom = 0.85;

inline constexpr double kBlockKeepoutMargin = kGridStep;

inline constexpr double kLinkHubSize = kGridStep * 2.0;

inline constexpr double kContentPadding = 6.0 * kWorldScale;
inline constexpr double kContentGap = 6.0 * kWorldScale;

inline constexpr char kLinkHubSplitFill[] = "#1B1422";
inline constexpr char kLinkHubSplitOutline[] = "#C089FF";
inline constexpr char kLinkHubSplitText[] = "#F1E6FF";
inline constexpr char kLinkHubJoinFill[] = "#201A12";
inline constexpr char kLinkHubJoinOutline[] = "#F2B65E";
inline constexpr char kLinkHubJoinText[] = "#FFE6B8";
inline constexpr char kLinkHubBroadcastFill[] = "#141D24";
inline constexpr char kLinkHubBroadcastOutline[] = "#58B5FF";
inline constexpr char kLinkHubBroadcastText[] = "#D5ECFF";

inline constexpr char kLinkWireProducerColor[] = "#E55353";
inline constexpr char kLinkWireConsumerColor[] = "#5CCB7A";
inline constexpr char kLinkWireBroadcastColor[] = "#58B5FF";
inline constexpr char kLinkWireBroadcastProducerColor[] = "#F2B65E";

} // namespace Canvas::Constants
