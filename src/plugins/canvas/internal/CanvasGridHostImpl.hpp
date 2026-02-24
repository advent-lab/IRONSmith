// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "canvas/api/ICanvasGridHost.hpp"
#include "canvas/CanvasGlobal.hpp"
#include "utils/async/DebouncedInvoker.hpp"

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QVector>

namespace Utils {
class GridLayout;
}

namespace Canvas {
class CanvasDocument;
class CanvasView;
namespace Api {
class ICanvasStyleHost;
}
}

namespace Canvas::Internal {

class CanvasBlockHandleImpl;

class CANVAS_EXPORT CanvasGridHostImpl final : public Canvas::Api::ICanvasGridHost
{
    Q_OBJECT

public:
    explicit CanvasGridHostImpl(CanvasDocument* document,
                                CanvasView* view,
                                Canvas::Api::ICanvasStyleHost* styleHost = nullptr,
                                QObject* parent = nullptr);

    void setGridSpec(const Utils::GridSpec& spec) override;
    Utils::GridSpec gridSpec() const override;

    void setBlocks(const QVector<Canvas::Api::CanvasBlockSpec>& blocks) override;
    void clearBlocks() override;

    Canvas::Api::ICanvasBlockHandle* blockHandle(const QString& id) const override;
    QVector<Canvas::Api::ICanvasBlockHandle*> blockHandles() const override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void scheduleRebuild();
    void rebuildBlocks();
    void removeAllBlocks();
    QSizeF resolveCellSize() const;
    QRectF rectForBlock(const Canvas::Api::CanvasBlockSpec& spec, const QSizeF& cellSize) const;

    QPointer<CanvasDocument> m_document;
    QPointer<CanvasView> m_view;
    QPointer<Canvas::Api::ICanvasStyleHost> m_styleHost;

    Utils::GridSpec m_gridSpec;
    QVector<Canvas::Api::CanvasBlockSpec> m_blockSpecs;
    QHash<QString, CanvasBlockHandleImpl*> m_handles;
    Utils::Async::DebouncedInvoker m_rebuildDebounce;
};

} // namespace Canvas::Internal
