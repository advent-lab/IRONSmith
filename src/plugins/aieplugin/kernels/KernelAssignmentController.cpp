// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/kernels/KernelAssignmentController.hpp"

#include "aieplugin/AieCanvasCoordinator.hpp"
#include "aieplugin/NpuProfileCanvasMapper.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"
#include "aieplugin/panels/KernelPreviewDialog.hpp"
#include "aieplugin/panels/KernelPreviewUtils.hpp"

#include "canvas/CanvasBlock.hpp"
#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasDocument.hpp"
#include "canvas/CanvasView.hpp"
#include "canvas/api/ICanvasHost.hpp"

#include "codeeditor/api/CodeEditorTypes.hpp"
#include "codeeditor/api/ICodeEditorService.hpp"

#include <utils/Result.hpp>
#include <utils/PathUtils.hpp>

#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtGui/QCursor>
#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>

namespace Aie::Internal {

namespace {

const QRegularExpression kTileSpecIdPattern(QStringLiteral("^(aie|shim|mem)\\d+_\\d+$"),
                                            QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kComputeTileSpecIdPattern(QStringLiteral("^aie\\d+_\\d+$"),
                                                   QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kKernelAnnotationPattern(
    QStringLiteral("<<\\s*kernel\\s*:\\s*([A-Za-z0-9_.-]+)\\s*>>"),
    QRegularExpression::CaseInsensitiveOption);

QString cleanedId(const QString& text)
{
    return Utils::PathUtils::normalizePath(text.trimmed());
}

QString normalizedKernelId(const QString& text)
{
    return text.trimmed();
}

bool hasPreviewModifier(Qt::KeyboardModifiers mods)
{
    return mods.testFlag(Qt::ControlModifier) || mods.testFlag(Qt::MetaModifier);
}

} // namespace

KernelAssignmentController::KernelAssignmentController(QObject* parent)
    : QObject(parent)
{
}

void KernelAssignmentController::setRegistry(KernelRegistryService* registry)
{
    m_registry = registry;
}

void KernelAssignmentController::setCoordinator(AieCanvasCoordinator* coordinator)
{
    if (m_coordinator == coordinator)
        return;

    m_coordinator = coordinator;
    applyLabelOverrides();
}

void KernelAssignmentController::setCanvasHost(Canvas::Api::ICanvasHost* host)
{
    if (m_canvasHost == host)
        return;

    if (m_canvasViewWidget) {
        clearStereotypeLinkHover();
        m_canvasViewWidget->removeEventFilter(this);
    }

    m_canvasHost = host;
    reconnectCanvasSignals();
    updateCanvasCursor();
}

void KernelAssignmentController::setCodeEditorService(CodeEditor::Api::ICodeEditorService* service)
{
    m_codeEditorService = service;
}

void KernelAssignmentController::setSelectedKernelId(const QString& kernelId)
{
    const QString cleaned = normalizedKernelId(kernelId);
    if (cleaned == m_selectedKernelId)
        return;

    if (!cleaned.isEmpty() && (!m_registry || !m_registry->kernelById(cleaned))) {
        emit assignmentFailed(QStringLiteral("Kernel '%1' is not registered.").arg(cleaned));
        return;
    }

    m_selectedKernelId = cleaned;
    updateCanvasCursor();
    emit selectedKernelChanged(m_selectedKernelId);
}

void KernelAssignmentController::clearSelectedKernel()
{
    setSelectedKernelId(QString());
}

Utils::Result KernelAssignmentController::assignKernelToTile(const QString& tileSpecId,
                                                             const QString& kernelId)
{
    const QString cleanedTile = cleanedId(tileSpecId);
    if (!isAssignableTile(cleanedTile)) {
        return Utils::Result::failure(QStringLiteral("Tile '%1' does not accept kernel assignments.")
                                          .arg(cleanedTile));
    }

    const QString cleanedKernel = normalizedKernelId(kernelId);
    if (cleanedKernel.isEmpty())
        return Utils::Result::failure(QStringLiteral("Kernel id is empty."));

    if (!m_registry || !m_registry->kernelById(cleanedKernel)) {
        return Utils::Result::failure(QStringLiteral("Kernel '%1' is not registered.")
                                          .arg(cleanedKernel));
    }

    if (m_assignmentsByTileSpecId.value(cleanedTile) == cleanedKernel)
        return Utils::Result::success();

    m_assignmentsByTileSpecId.insert(cleanedTile, cleanedKernel);
    applyLabelOverrides();
    emit assignmentsChanged();
    return Utils::Result::success();
}

void KernelAssignmentController::clearAssignments()
{
    if (m_assignmentsByTileSpecId.isEmpty())
        return;

    m_assignmentsByTileSpecId.clear();
    applyLabelOverrides();
    emit assignmentsChanged();
}

void KernelAssignmentController::rehydrateAssignmentsFromCanvas()
{
    m_assignmentsByTileSpecId.clear();

    auto* host = m_canvasHost.data();
    auto* document = host ? host->document() : nullptr;
    if (!document) {
        applyLabelOverrides();
        emit assignmentsChanged();
        return;
    }

    for (const auto& item : document->items()) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block)
            continue;

        const QString specId = cleanedId(block->specId());
        if (!isAssignableTile(specId))
            continue;

        QString kernelId = parseKernelIdFromLabel(block->stereotype());
        if (kernelId.isEmpty())
            kernelId = parseKernelIdFromLabel(block->label());
        if (kernelId.isEmpty())
            continue;

        if (m_registry && !m_registry->kernelById(kernelId))
            continue;

        m_assignmentsByTileSpecId.insert(specId, kernelId);
    }

    applyLabelOverrides();
    emit assignmentsChanged();
}

void KernelAssignmentController::reconnectCanvasSignals()
{
    if (m_canvasMousePressConnection)
        disconnect(m_canvasMousePressConnection);
    if (m_canvasMouseMoveConnection)
        disconnect(m_canvasMouseMoveConnection);

    auto* view = m_canvasHost ? qobject_cast<Canvas::CanvasView*>(m_canvasHost->viewWidget()) : nullptr;
    m_canvasViewWidget = view;
    if (!view)
        return;

    view->installEventFilter(this);
    m_canvasMousePressConnection = connect(view,
                                           &Canvas::CanvasView::canvasMousePressed,
                                           this,
                                           &KernelAssignmentController::onCanvasMousePressed);
    m_canvasMouseMoveConnection = connect(view,
                                          &Canvas::CanvasView::canvasMouseMoved,
                                          this,
                                          &KernelAssignmentController::onCanvasMouseMoved);
}

bool KernelAssignmentController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_canvasViewWidget && event) {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape && !m_selectedKernelId.isEmpty()) {
                clearSelectedKernel();
                return true;
            }
            refreshStereotypeLinkHover(keyEvent->modifiers());
        } else if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            refreshStereotypeLinkHover(keyEvent->modifiers());
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::FocusOut) {
            clearStereotypeLinkHover();
        }
    }

    return QObject::eventFilter(watched, event);
}

void KernelAssignmentController::updateCanvasCursor()
{
    if (!m_canvasViewWidget)
        return;

    if (!m_hoveredStereotypeItemId.isNull()) {
        m_canvasViewWidget->setCursor(Qt::PointingHandCursor);
        return;
    }

    if (m_selectedKernelId.isEmpty()) {
        m_canvasViewWidget->unsetCursor();
        return;
    }

    m_canvasViewWidget->setCursor(Qt::CrossCursor);
}

bool KernelAssignmentController::handlePreviewLinkClick(const QPointF& scenePos, Qt::KeyboardModifiers mods)
{
    if (!hasPreviewModifier(mods))
        return false;

    QString kernelId;
    if (!clickableStereotypeBlockAt(scenePos, &kernelId) || kernelId.isEmpty())
        return false;

    showPreviewDialog(kernelId, false);
    return true;
}

const Canvas::CanvasBlock* KernelAssignmentController::clickableStereotypeBlockAt(
    const QPointF& scenePos,
    QString* kernelIdOut) const
{
    auto* host = m_canvasHost.data();
    auto* document = host ? host->document() : nullptr;
    if (!document)
        return nullptr;

    for (const auto& item : document->items()) {
        const auto* block = dynamic_cast<const Canvas::CanvasBlock*>(item.get());
        if (!block)
            continue;

        const QString stereotype = block->stereotype().trimmed();
        if (stereotype.isEmpty())
            continue;

        const QRectF stereotypeRect = stereotypeRectForBlock(*block);
        if (!stereotypeRect.isValid() || !stereotypeRect.contains(scenePos))
            continue;

        const QString kernelId = parseKernelIdFromLabel(stereotype);
        if (kernelId.isEmpty())
            continue;

        if (!kernelById(kernelId))
            continue;

        if (kernelIdOut)
            *kernelIdOut = kernelId;
        return block;
    }

    return nullptr;
}

void KernelAssignmentController::updateStereotypeLinkHover(const QPointF& scenePos, Qt::KeyboardModifiers mods)
{
    Canvas::ObjectId hoveredItemId{};
    auto* view = qobject_cast<Canvas::CanvasView*>(m_canvasViewWidget.data());

    if (hasPreviewModifier(mods)) {
        if (const Canvas::CanvasBlock* block = clickableStereotypeBlockAt(scenePos))
            hoveredItemId = block->id();
    }

    if (hoveredItemId == m_hoveredStereotypeItemId)
        return;

    m_hoveredStereotypeItemId = hoveredItemId;
    if (view) {
        if (hoveredItemId.isNull())
            view->clearHoveredStereotype();
        else
            view->setHoveredStereotype(hoveredItemId);
    }
    updateCanvasCursor();
}

void KernelAssignmentController::refreshStereotypeLinkHover(Qt::KeyboardModifiers mods)
{
    auto* view = qobject_cast<Canvas::CanvasView*>(m_canvasViewWidget.data());
    if (!view) {
        clearStereotypeLinkHover();
        return;
    }

    const QPoint localPos = view->mapFromGlobal(QCursor::pos());
    if (!view->rect().contains(localPos)) {
        clearStereotypeLinkHover();
        return;
    }

    updateStereotypeLinkHover(view->viewToScene(QPointF(localPos)), mods);
}

void KernelAssignmentController::clearStereotypeLinkHover()
{
    auto* view = qobject_cast<Canvas::CanvasView*>(m_canvasViewWidget.data());
    if (view)
        view->clearHoveredStereotype();
    m_hoveredStereotypeItemId = Canvas::ObjectId{};
    updateCanvasCursor();
}

QRectF KernelAssignmentController::stereotypeRectForBlock(const Canvas::CanvasBlock& block)
{
    const QString stereotype = block.stereotype().trimmed();
    if (stereotype.isEmpty())
        return {};

    QFont font = QApplication::font();
    font.setPointSizeF(Canvas::Constants::kBlockStereotypePointSize);
    font.setBold(false);
    font.setItalic(true);

    QFontMetricsF metrics(font);
    const QSizeF size = metrics.size(Qt::TextSingleLine, stereotype);
    const QRectF bounds = block.boundsScene();

    const double x = bounds.center().x() - (size.width() * 0.5);
    const double y = bounds.top() - Canvas::Constants::kBlockStereotypeOffsetY - size.height();
    return QRectF(x, y, size.width(), size.height());
}

void KernelAssignmentController::showPreviewDialog(const QString& kernelId, bool openMetadataTab)
{
    const KernelAsset* kernel = kernelById(kernelId);
    if (!kernel)
        return;

    QWidget* parent = m_canvasViewWidget ? m_canvasViewWidget.data() : QApplication::activeWindow();
    KernelPreviewDialog dialog(*kernel, parent);
    KernelPreview::initializeDialogContent(dialog, *kernel, m_codeEditorService);
    dialog.setActiveTab(openMetadataTab
                            ? KernelPreviewDialog::Tab::Metadata
                            : KernelPreviewDialog::Tab::Code);

    connect(dialog.openInEditorButton(), &QPushButton::clicked, &dialog,
            [this, &dialog, kernelId]() {
                dialog.accept();
                QTimer::singleShot(0, this, [this, kernelId]() { openKernelInEditor(kernelId, false); });
            });
    connect(dialog.copyToWorkspaceButton(), &QPushButton::clicked, &dialog,
            [this, kernelId]() { copyKernelToScope(kernelId, KernelSourceScope::Workspace, true); });
    connect(dialog.copyToGlobalButton(), &QPushButton::clicked, &dialog,
            [this, kernelId]() { copyKernelToScope(kernelId, KernelSourceScope::Global, true); });

    dialog.exec();
}

void KernelAssignmentController::openKernelInEditor(const QString& kernelId, bool forceReadOnly)
{
    const KernelAsset* kernel = kernelById(kernelId);
    if (!kernel)
        return;

    if (!m_codeEditorService) {
        QMessageBox::warning(m_canvasViewWidget,
                             QStringLiteral("Code Editor"),
                             QStringLiteral("Code editor service is not available."));
        return;
    }

    CodeEditor::Api::CodeEditorOpenRequest request;
    request.filePath = kernel->absoluteEntryPath();
    request.activate = true;
    request.readOnly = forceReadOnly || kernel->scope == KernelSourceScope::BuiltIn;

    CodeEditor::Api::CodeEditorSessionHandle handle;
    const Utils::Result openResult = m_codeEditorService->openFile(request, handle);
    if (!openResult) {
        QMessageBox::warning(m_canvasViewWidget,
                             QStringLiteral("Open Kernel"),
                             openResult.errors.join(QStringLiteral("\n")));
    }
}

void KernelAssignmentController::copyKernelToScope(const QString& kernelId,
                                                   KernelSourceScope scope,
                                                   bool openCopiedKernelInEditor)
{
    if (!m_registry)
        return;

    KernelAsset copiedKernel;
    const Utils::Result copyResult = m_registry->copyKernelToScope(kernelId, scope, &copiedKernel);
    if (!copyResult) {
        QMessageBox::warning(m_canvasViewWidget,
                             QStringLiteral("Kernel Copy"),
                             copyResult.errors.join(QStringLiteral("\n")));
        return;
    }

    if (!copiedKernel.id.trimmed().isEmpty())
        setSelectedKernelId(copiedKernel.id);

    if (openCopiedKernelInEditor)
        openKernelInEditor(copiedKernel.id, false);
}

const KernelAsset* KernelAssignmentController::kernelById(const QString& kernelId) const
{
    if (!m_registry)
        return nullptr;
    return m_registry->kernelById(kernelId);
}

void KernelAssignmentController::applyLabelOverrides()
{
    if (!m_coordinator)
        return;

    m_coordinator->setBlockLabelOverrides({});

    QHash<QString, QString> stereotypeOverrides;
    for (auto it = m_assignmentsByTileSpecId.constBegin(); it != m_assignmentsByTileSpecId.constEnd(); ++it) {
        const QString tileSpecId = cleanedId(it.key());
        const QString kernelId = normalizedKernelId(it.value());
        if (!isAssignableTile(tileSpecId) || kernelId.isEmpty())
            continue;

        stereotypeOverrides.insert(tileSpecId, stereotypeLabelFor(kernelId));
    }

    m_coordinator->setBlockStereotypeOverrides(stereotypeOverrides);
}

bool KernelAssignmentController::isAssignableTile(const QString& tileSpecId) const
{
    // Only AIE compute tiles (prefix "aie") accept kernel assignments.
    return kComputeTileSpecIdPattern.match(tileSpecId).hasMatch();
}

QString KernelAssignmentController::stereotypeLabelFor(const QString& kernelId) const
{
    return QStringLiteral("<<kernel: %1>>").arg(kernelId);
}

QString KernelAssignmentController::parseKernelIdFromLabel(const QString& label) const
{
    const QRegularExpressionMatch match = kKernelAnnotationPattern.match(label);
    if (!match.hasMatch())
        return {};

    return normalizedKernelId(match.captured(1));
}

void KernelAssignmentController::onCanvasMousePressed(const QPointF& scenePos,
                                                      Qt::MouseButtons buttons,
                                                      Qt::KeyboardModifiers mods)
{
    if (!buttons.testFlag(Qt::LeftButton))
        return;

    if (handlePreviewLinkClick(scenePos, mods))
        return;

    if (mods != Qt::NoModifier)
        return;
    if (m_selectedKernelId.isEmpty())
        return;

    auto* host = m_canvasHost.data();
    auto* document = host ? host->document() : nullptr;
    if (!host || !document || !host->canvasActive())
        return;

    auto* item = document->hitTest(scenePos);
    auto* block = dynamic_cast<Canvas::CanvasBlock*>(item);
    if (!block)
        return;

    const Utils::Result assignmentResult = assignKernelToTile(block->specId(), m_selectedKernelId);
    if (!assignmentResult)
        emit assignmentFailed(assignmentResult.errors.join("\n"));
}

void KernelAssignmentController::onCanvasMouseMoved(const QPointF& scenePos,
                                                    Qt::MouseButtons buttons,
                                                    Qt::KeyboardModifiers mods)
{
    Q_UNUSED(buttons);
    updateStereotypeLinkHover(scenePos, mods);
}

} // namespace Aie::Internal
