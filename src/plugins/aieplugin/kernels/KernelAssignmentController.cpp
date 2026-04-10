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
#include <utils/ui/ConfirmationDialog.hpp>

#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QCursor>
#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>

namespace Aie::Internal {

namespace {

const QRegularExpression kTileSpecIdPattern(QStringLiteral("^aie\\d+_\\d+$"),
                                            QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kComputeTileSpecIdPattern(QStringLiteral("^aie\\d+_\\d+$"),
                                                   QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kKernelAnnotationPattern(
    QStringLiteral("<<\\s*kernel\\s*:\\s*([A-Za-z0-9_.-]+)\\s*>>"),
    QRegularExpression::CaseInsensitiveOption);
const QString kApplicationLinkScheme = QStringLiteral("application");
const QString kKernelLinkHost = QStringLiteral("kernel");
const QString kKernelLinkAction = QStringLiteral("/preview");
const QString kKernelLinkQueryKey = QStringLiteral("id");

QString cleanedId(const QString& text)
{
    return Utils::PathUtils::normalizePath(text.trimmed());
}

QString normalizedKernelId(const QString& text)
{
    return text.trimmed();
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

QStringList KernelAssignmentController::kernelsByTile(const QString& tileSpecId) const
{
    return m_assignmentsByTileSpecId.value(cleanedId(tileSpecId));
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

    auto& list = m_assignmentsByTileSpecId[cleanedTile];
    if (list.contains(cleanedKernel))
        return Utils::Result::success();

    if (list.size() >= 4)
        return Utils::Result::failure(QStringLiteral("Tile '%1' already has the maximum of 4 kernels assigned.")
                                          .arg(cleanedTile));

    list.append(cleanedKernel);
    applyLabelOverrides();
    emit assignmentsChanged();
    return Utils::Result::success();
}

Utils::Result KernelAssignmentController::removeKernelFromTile(const QString& tileSpecId,
                                                               const QString& kernelId)
{
    const QString cleanedTile   = cleanedId(tileSpecId);
    const QString cleanedKernel = normalizedKernelId(kernelId);
    auto it = m_assignmentsByTileSpecId.find(cleanedTile);
    if (it == m_assignmentsByTileSpecId.end() || !it->removeOne(cleanedKernel))
        return Utils::Result::success();

    if (it->isEmpty())
        m_assignmentsByTileSpecId.erase(it);

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

        // Prefer the new assignedKernels list; fall back to legacy stereotype for old documents.
        QStringList kernels = block->assignedKernels();
        if (kernels.isEmpty()) {
            const QString legacyId = parseKernelIdFromLabel(block->stereotype());
            if (!legacyId.isEmpty())
                kernels.append(legacyId);
        }

        QStringList valid;
        for (const QString& k : kernels) {
            if (!m_registry || m_registry->kernelById(normalizedKernelId(k)))
                valid.append(normalizedKernelId(k));
        }
        if (!valid.isEmpty())
            m_assignmentsByTileSpecId.insert(specId, valid);
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

bool KernelAssignmentController::handleStereotypeLinkClick(const QPointF& scenePos)
{
    QUrl url;
    if (!clickableStereotypeBlockAt(scenePos, &url) || !url.isValid())
        return false;

    return openKernelLink(url);
}

const Canvas::CanvasBlock* KernelAssignmentController::clickableStereotypeBlockAt(
    const QPointF& scenePos,
    QUrl* urlOut) const
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

        const QUrl url = kernelLinkUrl(parseKernelIdFromLabel(stereotype));
        if (!url.isValid())
            continue;

        if (urlOut)
            *urlOut = url;
        return block;
    }

    return nullptr;
}

void KernelAssignmentController::updateStereotypeLinkHover(const QPointF& scenePos)
{
    Canvas::ObjectId hoveredItemId{};
    auto* view = qobject_cast<Canvas::CanvasView*>(m_canvasViewWidget.data());

    if (const Canvas::CanvasBlock* block = clickableStereotypeBlockAt(scenePos))
        hoveredItemId = block->id();

    if (hoveredItemId == m_hoveredStereotypeItemId) {
        updateCanvasCursor();
        return;
    }

    m_hoveredStereotypeItemId = hoveredItemId;
    if (view) {
        if (hoveredItemId.isNull())
            view->clearHoveredStereotype();
        else
            view->setHoveredStereotype(hoveredItemId);
    }
    updateCanvasCursor();
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

bool KernelAssignmentController::openKernelLink(const QUrl& url)
{
    const QString kernelId = kernelIdFromLink(url);
    if (kernelId.isEmpty())
        return false;

    showPreviewDialog(kernelId, false);
    return true;
}

bool KernelAssignmentController::confirmReassignment(const QString& tileSpecId,
                                                     const QString& currentKernelId,
                                                     const QString& nextKernelId)
{
    const QString currentId = normalizedKernelId(currentKernelId);
    const QString nextId = normalizedKernelId(nextKernelId);
    if (currentId.isEmpty() || nextId.isEmpty() || currentId == nextId)
        return true;

    if (!m_assignmentState.confirmReassignment())
        return true;

    const KernelAsset* currentKernel = kernelById(currentId);
    const KernelAsset* nextKernel = kernelById(nextId);

    const QString currentName = currentKernel && !currentKernel->name.trimmed().isEmpty()
        ? currentKernel->name.trimmed()
        : currentId;
    const QString nextName = nextKernel && !nextKernel->name.trimmed().isEmpty()
        ? nextKernel->name.trimmed()
        : nextId;

    Utils::ConfirmationDialogConfig config;
    config.title = QStringLiteral("Reassign Kernel");
    config.message = QStringLiteral("Replace the kernel assignment on '%1'?").arg(tileSpecId);
    config.informativeText = QStringLiteral("This tile is already assigned to '%1'.")
                                 .arg(currentName);
    config.details = QStringLiteral("New kernel: %1").arg(nextName);
    config.confirmText = QStringLiteral("Replace");
    config.cancelText = QStringLiteral("Cancel");
    config.checkBoxText = QStringLiteral("Don't show this again.");

    QWidget* parent = m_canvasViewWidget ? m_canvasViewWidget.data() : QApplication::activeWindow();
    const Utils::ConfirmationDialogResult result = Utils::ConfirmationDialog::run(parent, config);
    if (!result.accepted)
        return false;

    if (result.checkBoxChecked)
        m_assignmentState.setConfirmReassignment(false);

    return true;
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

QUrl KernelAssignmentController::kernelLinkUrl(const QString& kernelId) const
{
    const QString cleanedKernelId = normalizedKernelId(kernelId);
    if (cleanedKernelId.isEmpty() || !kernelById(cleanedKernelId))
        return {};

    QUrl url;
    url.setScheme(kApplicationLinkScheme);
    url.setHost(kKernelLinkHost);
    url.setPath(kKernelLinkAction);

    QUrlQuery query;
    query.addQueryItem(kKernelLinkQueryKey, cleanedKernelId);
    url.setQuery(query);
    return url;
}

QString KernelAssignmentController::kernelIdFromLink(const QUrl& url) const
{
    if (!url.isValid()
        || url.scheme() != kApplicationLinkScheme
        || url.host() != kKernelLinkHost
        || url.path() != kKernelLinkAction) {
        return {};
    }

    const QUrlQuery query(url);
    const QString kernelId = normalizedKernelId(query.queryItemValue(kKernelLinkQueryKey));
    if (kernelId.isEmpty() || !kernelById(kernelId))
        return {};

    return kernelId;
}

void KernelAssignmentController::applyLabelOverrides()
{
    if (!m_coordinator)
        return;

    m_coordinator->setBlockLabelOverrides({});
    m_coordinator->setBlockStereotypeOverrides({}); // clear legacy stereotype display

    auto* host     = m_canvasHost.data();
    auto* document = host ? host->document() : nullptr;
    if (!document)
        return;

    // First clear kernel chips from all assignable tiles.
    for (const auto& item : document->items()) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (block && isAssignableTile(cleanedId(block->specId())))
            block->setAssignedKernels({});
    }

    // Then apply current assignments.
    for (const auto& item : document->items()) {
        auto* block = dynamic_cast<Canvas::CanvasBlock*>(item.get());
        if (!block)
            continue;
        const QString specId = cleanedId(block->specId());
        auto it = m_assignmentsByTileSpecId.constFind(specId);
        if (it != m_assignmentsByTileSpecId.constEnd() && !it->isEmpty())
            block->setAssignedKernels(*it);
    }

    document->notifyChanged();
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

    if (handleStereotypeLinkClick(scenePos))
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

    const QString tileSpecId = cleanedId(block->specId());
    if (!isAssignableTile(tileSpecId))
        return;

    // Multiple kernels can be assigned to the same tile — just append (duplicates are silently
    // ignored inside assignKernelToTile).
    const Utils::Result assignmentResult = assignKernelToTile(tileSpecId, m_selectedKernelId);
    if (!assignmentResult)
        emit assignmentFailed(assignmentResult.errors.join("\n"));
}

void KernelAssignmentController::onCanvasMouseMoved(const QPointF& scenePos,
                                                    Qt::MouseButtons buttons,
                                                    Qt::KeyboardModifiers mods)
{
    Q_UNUSED(buttons);
    Q_UNUSED(mods);
    updateStereotypeLinkHover(scenePos);
}

} // namespace Aie::Internal
