// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasController.hpp"

#include <QtCore/QPointF>

#include <memory>
#include <optional>

namespace Canvas {
class CanvasBlock;
class CanvasDocument;
class CanvasView;
struct PortRef;
}

namespace Canvas::Controllers {

class CanvasSelectionController;
class CanvasDragController;

class CanvasLinkingController final
{
public:
    enum class LinkingPressResult {
        NotHandled,
        Handled,
        RequestLinkingModeReset
    };

    CanvasLinkingController(CanvasDocument* doc,
                            CanvasView* view,
                            CanvasSelectionController* selection,
                            CanvasDragController* drag);

    CanvasController::LinkingMode linkingMode() const noexcept { return m_linkingMode; }
    void setLinkingMode(CanvasController::LinkingMode mode);
    const CanvasController::ObjectFifoDefaults& objectFifoDefaults() const noexcept { return m_objectFifoDefaults; }
    void setObjectFifoDefaults(CanvasController::ObjectFifoDefaults defaults);

    bool isLinkingInProgress() const noexcept { return m_wiring; }
    ObjectId linkStartItem() const noexcept { return m_wireStartItem; }
    PortId linkStartPort() const noexcept { return m_wireStartPort; }
    QPointF linkPreviewScene() const noexcept { return m_wirePreviewScene; }

    void resetLinkingSession();

    LinkingPressResult handleLinkingPress(const QPointF& scenePos,
                                          CanvasController::Mode mode);

    void updateLinkingHoverAndPreview(const QPointF& scenePos,
                                      CanvasController::Mode mode,
                                      bool panning,
                                      bool dragEndpoint);

private:
    LinkingPressResult handleLinkingHubPress(const QPointF& scenePos, const PortRef& hitPort);
    bool beginLinkingFromPort(const PortRef& hitPort, const QPointF& scenePos);
    bool resolvePortTerminal(const PortRef& port,
                             QPointF& outAnchor,
                             QPointF& outBorder,
                             QPointF& outFabric) const;
    std::unique_ptr<CanvasWire> buildWire(const PortRef& a, const PortRef& b) const;
    void applyLinkingModeDefaults(CanvasWire& wire, const PortRef& start, const PortRef& end) const;
    std::optional<CanvasWire::ObjectFifoConfig> defaultObjectFifoConfig(const PortRef& start, const PortRef& end) const;
    CanvasBlock* findLinkHub() const;
    bool connectToExistingHub(const QPointF& scenePos, const PortRef& hitPort);
    bool createHubAndWires(const QPointF& scenePos, const PortRef& hitPort);

    CanvasDocument* m_doc = nullptr;
    CanvasView* m_view = nullptr;
    CanvasSelectionController* m_selection = nullptr;
    CanvasDragController* m_drag = nullptr;

    CanvasController::LinkingMode m_linkingMode = CanvasController::LinkingMode::Normal;
    ObjectId m_linkHubId{};

    bool m_wiring = false;
    ObjectId m_wireStartItem{};
    PortId m_wireStartPort{};
    QPointF m_wirePreviewScene{};

    std::optional<EdgeCandidate> m_hoverEdge;
    CanvasController::ObjectFifoDefaults m_objectFifoDefaults;
};

} // namespace Canvas::Controllers
