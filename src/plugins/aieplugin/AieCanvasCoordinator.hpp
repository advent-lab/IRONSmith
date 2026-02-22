// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/NpuProfileCanvasMapper.hpp"

#include "canvas/api/ICanvasGridHost.hpp"
#include "canvas/api/ICanvasHost.hpp"
#include "canvas/api/ICanvasStyleHost.hpp"
#include "canvas/api/CanvasStyleTypes.hpp"
#include "utils/async/DebouncedInvoker.hpp"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QHash>
#include <QtCore/QtGlobal>
#include <QtGui/QColor>
#include <memory>

namespace Aie {

class AieCanvasCoordinator final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double tileSpacing READ tileSpacing WRITE setTileSpacing NOTIFY tileSpacingChanged)
    Q_PROPERTY(double horizontalSpacing READ horizontalSpacing WRITE setHorizontalSpacing NOTIFY horizontalSpacingChanged)
    Q_PROPERTY(double verticalSpacing READ verticalSpacing WRITE setVerticalSpacing NOTIFY verticalSpacingChanged)
    Q_PROPERTY(double outwardSpread READ outwardSpread WRITE setOutwardSpread NOTIFY outwardSpreadChanged)
    Q_PROPERTY(double outerMargin READ outerMargin WRITE setOuterMargin NOTIFY outerMarginChanged)
    Q_PROPERTY(bool autoCellSize READ autoCellSize WRITE setAutoCellSize NOTIFY autoCellSizeChanged)
    Q_PROPERTY(double cellSize READ cellSize WRITE setCellSize NOTIFY cellSizeChanged)
    Q_PROPERTY(bool showPorts READ showPorts WRITE setShowPorts NOTIFY showPortsChanged)
    Q_PROPERTY(bool showLabels READ showLabels WRITE setShowLabels NOTIFY showLabelsChanged)
    Q_PROPERTY(bool showAnnotations READ showAnnotations WRITE setShowAnnotations NOTIFY showAnnotationsChanged)
    Q_PROPERTY(double keepoutMargin READ keepoutMargin WRITE setKeepoutMargin NOTIFY keepoutMarginChanged)
    Q_PROPERTY(bool useCustomColors READ useCustomColors WRITE setUseCustomColors NOTIFY useCustomColorsChanged)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY fillColorChanged)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY outlineColorChanged)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY labelColorChanged)

public:
    enum class WireAnnotationVisibilityMode : quint8 {
        Auto,
        ShowAll,
        Hidden
    };

    enum class WireAnnotationDetailMode : quint8 {
        Adaptive,
        Compact,
        Full
    };

    enum class SelectionSpacingAxis {
        Horizontal,
        Vertical,
        Outward
    };
    Q_ENUM(SelectionSpacingAxis)

    explicit AieCanvasCoordinator(QObject* parent = nullptr);
    ~AieCanvasCoordinator() override;

    void setCanvasHost(Canvas::Api::ICanvasHost* host);
    Canvas::Api::ICanvasHost* canvasHost() const { return m_canvasHost; }
    void setGridHost(Canvas::Api::ICanvasGridHost* host);
    Canvas::Api::ICanvasGridHost* gridHost() const { return m_gridHost; }
    void setStyleHost(Canvas::Api::ICanvasStyleHost* host);
    Canvas::Api::ICanvasStyleHost* styleHost() const { return m_styleHost; }

    void setBaseModel(const CanvasGridModel& model);
    const CanvasGridModel& baseModel() const { return m_baseModel; }

    void setBaseStyles(const QHash<QString, Canvas::Api::CanvasBlockStyle>& styles);
    QHash<QString, Canvas::Api::CanvasBlockStyle> baseStyles() const { return m_baseStyles; }
    void setBlockLabelOverrides(const QHash<QString, QString>& overrides);
    void setBlockLabelOverride(const QString& blockId, const QString& label);
    void clearBlockLabelOverride(const QString& blockId);
    void clearBlockLabelOverrides();
    void setBlockStereotypeOverrides(const QHash<QString, QString>& overrides);
    void setBlockStereotypeOverride(const QString& blockId, const QString& stereotype);
    void clearBlockStereotypeOverride(const QString& blockId);
    void clearBlockStereotypeOverrides();

    double tileSpacing() const;
    void setTileSpacing(double spacing);

    double horizontalSpacing() const { return m_layout.horizontalSpacing; }
    void setHorizontalSpacing(double spacing);

    double verticalSpacing() const { return m_layout.verticalSpacing; }
    void setVerticalSpacing(double spacing);

    double outwardSpread() const { return m_layout.outwardSpread; }
    void setOutwardSpread(double spread);

    double outerMargin() const { return m_layout.outwardSpread; }
    void setOuterMargin(double margin);

    bool autoCellSize() const { return hasFlag(FlagBit::AutoCellSize); }
    void setAutoCellSize(bool enabled);

    double cellSize() const { return m_layout.cellSize; }
    void setCellSize(double size);

    bool showPorts() const { return hasFlag(FlagBit::ShowPorts); }
    void setShowPorts(bool enabled);

    bool showLabels() const { return hasFlag(FlagBit::ShowLabels); }
    void setShowLabels(bool enabled);

    bool showAnnotations() const { return hasFlag(FlagBit::ShowAnnotations); }
    void setShowAnnotations(bool enabled);

    bool showStereotypes() const { return hasFlag(FlagBit::ShowStereotypes); }
    void setShowStereotypes(bool enabled);

    bool showPortAnnotations() const { return hasFlag(FlagBit::ShowPortAnnotations); }
    void setShowPortAnnotations(bool enabled);

    WireAnnotationVisibilityMode wireAnnotationVisibilityMode() const { return m_wireAnnotationVisibilityMode; }
    void setWireAnnotationVisibilityMode(WireAnnotationVisibilityMode mode);

    WireAnnotationDetailMode wireAnnotationDetailMode() const { return m_wireAnnotationDetailMode; }
    void setWireAnnotationDetailMode(WireAnnotationDetailMode mode);

    bool wireAnnotationsScaleWithZoom() const { return m_wireAnnotationsScaleWithZoom; }
    void setWireAnnotationsScaleWithZoom(bool enabled);

    double keepoutMargin() const { return m_layout.keepoutMargin; }
    void setKeepoutMargin(double margin);

    bool useCustomColors() const { return hasFlag(FlagBit::UseCustomColors); }
    void setUseCustomColors(bool enabled);

    QColor fillColor() const { return m_colors.fill; }
    void setFillColor(const QColor& color);

    QColor outlineColor() const { return m_colors.outline; }
    void setOutlineColor(const QColor& color);

    QColor labelColor() const { return m_colors.label; }
    void setLabelColor(const QColor& color);

    void apply();
    void flushApply();
    void beginSelectionSpacing(SelectionSpacingAxis axis);
    void updateSelectionSpacing(SelectionSpacingAxis axis, double value);
    void endSelectionSpacing(SelectionSpacingAxis axis);
    void nudgeSelection(double dx, double dy);

signals:
    void tileSpacingChanged(double spacing);
    void horizontalSpacingChanged(double spacing);
    void verticalSpacingChanged(double spacing);
    void outwardSpreadChanged(double spread);
    void outerMarginChanged(double margin);
    void autoCellSizeChanged(bool enabled);
    void cellSizeChanged(double size);
    void showPortsChanged(bool enabled);
    void showLabelsChanged(bool enabled);
    void showAnnotationsChanged(bool enabled);
    void keepoutMarginChanged(double margin);
    void useCustomColorsChanged(bool enabled);
    void fillColorChanged(const QColor& color);
    void outlineColorChanged(const QColor& color);
    void labelColorChanged(const QColor& color);

private:
    struct LayoutState final {
        double horizontalSpacing = 0.0;
        double verticalSpacing = 0.0;
        double outwardSpread = 0.0;
        double cellSize = 0.0;
        double keepoutMargin = -1.0;
    };

    struct ColorState final {
        QColor fill;
        QColor outline;
        QColor label;
    };

    enum class FlagBit : quint8 {
        Dirty = 1u << 0,
        AutoCellSize = 1u << 1,
        ShowPorts = 1u << 2,
        ShowLabels = 1u << 3,
        ShowAnnotations = 1u << 4,
        UseCustomColors = 1u << 5,
        ShowStereotypes = 1u << 6,
        ShowPortAnnotations = 1u << 7
    };

    static constexpr quint8 kDefaultFlags =
        static_cast<quint8>(FlagBit::AutoCellSize) |
        static_cast<quint8>(FlagBit::ShowPorts) |
        static_cast<quint8>(FlagBit::ShowLabels) |
        static_cast<quint8>(FlagBit::ShowStereotypes) |
        static_cast<quint8>(FlagBit::ShowPortAnnotations);

    bool hasFlag(FlagBit flag) const;
    bool setFlag(FlagBit flag, bool enabled);
    void applyWireAnnotationSettings();

    struct SelectionSnapshot;
    void requestApply();
    void applyNow();

    QPointer<Canvas::Api::ICanvasHost> m_canvasHost;
    QPointer<Canvas::Api::ICanvasGridHost> m_gridHost;
    QPointer<Canvas::Api::ICanvasStyleHost> m_styleHost;
    CanvasGridModel m_baseModel;
    QHash<QString, Canvas::Api::CanvasBlockStyle> m_baseStyles;
    QHash<QString, QString> m_blockLabelOverrides;
    QHash<QString, QString> m_blockStereotypeOverrides;
    QHash<QString, QPointF> m_blockOffsets;

    Utils::Async::DebouncedInvoker m_applyDebounce;

    quint8 m_flags = kDefaultFlags;
    LayoutState m_layout;
    ColorState m_colors;
    WireAnnotationVisibilityMode m_wireAnnotationVisibilityMode = WireAnnotationVisibilityMode::Auto;
    WireAnnotationDetailMode m_wireAnnotationDetailMode = WireAnnotationDetailMode::Adaptive;
    bool m_wireAnnotationsScaleWithZoom = true;

    std::unique_ptr<SelectionSnapshot> m_selectionSnapshot;
};

} // namespace Aie
