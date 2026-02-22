// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/CanvasTypes.hpp"
#include "canvas/CanvasPorts.hpp"
#include "canvas/utils/CanvasLinkHubStyle.hpp"

#include <utils/contextmenu/ContextMenu.hpp>

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QPointF>
#include <QtCore/QSet>
#include <QtCore/Qt>

#include <optional>

namespace Canvas {
class CanvasBlock;
class CanvasDocument;
class CanvasView;
}

namespace Canvas::Controllers {

class CanvasSelectionController;

class CanvasContextMenuController final : public QObject
{
    Q_OBJECT

public:
    CanvasContextMenuController(CanvasDocument* doc,
                                CanvasView* view,
                                CanvasSelectionController* selection,
                                QObject* parent = nullptr);

    void showContextMenu(const QPointF& scenePos, const QPoint& globalPos, Qt::KeyboardModifiers mods);
    bool tryPlacePendingBoundProducer(const QPointF& scenePos);
    bool hasPendingBoundProducerPlacement() const noexcept;
    void clearPendingBoundProducerPlacement();

private slots:
    void handleMenuAction(const QString& actionId);

private:
    enum class TargetKind : uint8_t {
        Empty,
        Selection,
        Block,
        LinkHub,
        Wire,
        Port
    };

    struct MenuTarget final {
        TargetKind kind = TargetKind::Empty;
        QPointF scenePos;
        QPoint globalPos;
        ObjectId itemId{};
        PortId portId{};
    };

    struct PendingBoundProducerPlacement final {
        ObjectId consumerItemId{};
        PortId consumerPortId{};
    };

    MenuTarget resolveTarget(const QPointF& scenePos, const QPoint& globalPos, Qt::KeyboardModifiers mods);
    void populateMenu(const MenuTarget& target);

    void appendEditActions(bool canUndo, bool canRedo);
    void appendEmptyCanvasActions();
    void appendSelectionActions();
    void appendBlockActions(CanvasBlock* block, bool linkHub);
    void appendWireActions(ObjectId wireId);
    void appendPortActions(ObjectId itemId, PortId portId);

    void appendSeparator();

    bool executeDeletePort(ObjectId itemId, PortId portId);
    bool executeDeleteItems(const QSet<ObjectId>& ids);
    bool executeDeleteSingleItem(ObjectId itemId);
    bool executeAddBlockAt(const QPointF& scenePos);
    bool executeAddHubAt(const QPointF& scenePos, Support::LinkHubKind kind);
    bool executeSetHubKind(ObjectId itemId, Support::LinkHubKind kind);
    bool executeClearWireRoute(ObjectId wireId);
    bool executeFrameAll();
    bool executeFrameSelection();
    bool executeFrameItems(const QSet<ObjectId>& ids);
    bool executeFrameRect(const QRectF& bounds);

    CanvasBlock* findBlock(ObjectId itemId) const;
    std::optional<CanvasPort> findPort(ObjectId itemId, PortId portId) const;

    std::optional<Support::LinkHubKind> hubKindForBlock(const CanvasBlock& block) const;

    CanvasDocument* m_doc = nullptr;
    CanvasView* m_view = nullptr;
    CanvasSelectionController* m_selection = nullptr;
    Utils::ContextMenu* m_menu = nullptr;
    QList<Utils::ContextMenuAction> m_actions;
    std::optional<MenuTarget> m_activeTarget;
    std::optional<PendingBoundProducerPlacement> m_pendingBoundProducerPlacement;
};

} // namespace Canvas::Controllers
