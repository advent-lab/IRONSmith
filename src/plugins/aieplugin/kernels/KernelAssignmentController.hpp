// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/kernels/KernelCatalog.hpp"
#include "aieplugin/state/AieKernelAssignmentState.hpp"
#include "canvas/CanvasTypes.hpp"

#include <utils/Result.hpp>

#include <QtCore/QHash>
#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/qnamespace.h>

QT_BEGIN_NAMESPACE
class QEvent;
class QRegularExpression;
class QWidget;
QT_END_NAMESPACE

namespace Aie {
class AieCanvasCoordinator;
}

namespace Canvas {
class CanvasBlock;
}

namespace Canvas::Api {
class ICanvasHost;
}

namespace CodeEditor::Api {
class ICodeEditorService;
}

namespace Aie::Internal {

class KernelRegistryService;

class KernelAssignmentController final : public QObject
{
    Q_OBJECT

public:
    explicit KernelAssignmentController(QObject* parent = nullptr);

    void setRegistry(KernelRegistryService* registry);
    void setCoordinator(AieCanvasCoordinator* coordinator);
    void setCanvasHost(Canvas::Api::ICanvasHost* host);
    void setCodeEditorService(CodeEditor::Api::ICodeEditorService* service);

    QString selectedKernelId() const { return m_selectedKernelId; }
    void setSelectedKernelId(const QString& kernelId);
    void clearSelectedKernel();

    QHash<QString, QString> assignments() const { return m_assignmentsByTileSpecId; }

    Utils::Result assignKernelToTile(const QString& tileSpecId, const QString& kernelId);
    void clearAssignments();

    void rehydrateAssignmentsFromCanvas();

signals:
    void selectedKernelChanged(const QString& kernelId);
    void assignmentsChanged();
    void assignmentFailed(const QString& message);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    void reconnectCanvasSignals();
    void updateCanvasCursor();
    void updateStereotypeLinkHover(const QPointF& scenePos);
    void clearStereotypeLinkHover();
    void applyLabelOverrides();
    bool handleStereotypeLinkClick(const QPointF& scenePos);
    bool openKernelLink(const QUrl& url);
    bool confirmReassignment(const QString& tileSpecId,
                             const QString& currentKernelId,
                             const QString& nextKernelId);
    const Canvas::CanvasBlock* clickableStereotypeBlockAt(const QPointF& scenePos,
                                                          QUrl* urlOut = nullptr) const;
    static QRectF stereotypeRectForBlock(const Canvas::CanvasBlock& block);
    void showPreviewDialog(const QString& kernelId, bool openMetadataTab = false);
    void openKernelInEditor(const QString& kernelId, bool forceReadOnly);
    void copyKernelToScope(const QString& kernelId,
                           KernelSourceScope scope,
                           bool openCopiedKernelInEditor);
    const KernelAsset* kernelById(const QString& kernelId) const;
    QUrl kernelLinkUrl(const QString& kernelId) const;
    QString kernelIdFromLink(const QUrl& url) const;

    bool isAssignableTile(const QString& tileSpecId) const;
    QString stereotypeLabelFor(const QString& kernelId) const;
    QString parseKernelIdFromLabel(const QString& label) const;

    void onCanvasMousePressed(const QPointF& scenePos,
                              Qt::MouseButtons buttons,
                              Qt::KeyboardModifiers mods);
    void onCanvasMouseMoved(const QPointF& scenePos,
                            Qt::MouseButtons buttons,
                            Qt::KeyboardModifiers mods);

    QPointer<KernelRegistryService> m_registry;
    QPointer<AieCanvasCoordinator> m_coordinator;
    QPointer<Canvas::Api::ICanvasHost> m_canvasHost;
    QPointer<CodeEditor::Api::ICodeEditorService> m_codeEditorService;
    QPointer<QWidget> m_canvasViewWidget;

    QMetaObject::Connection m_canvasMousePressConnection;
    QMetaObject::Connection m_canvasMouseMoveConnection;

    QString m_selectedKernelId;
    Canvas::ObjectId m_hoveredStereotypeItemId{};
    QHash<QString, QString> m_assignmentsByTileSpecId;
    AieKernelAssignmentState m_assignmentState;
};

} // namespace Aie::Internal
