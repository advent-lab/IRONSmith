// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/CanvasBlock.hpp"

#include "canvas/CanvasConstants.hpp"
#include "canvas/CanvasBlockContent.hpp"
#include "canvas/CanvasStyle.hpp"
#include "canvas/utils/CanvasGeometry.hpp"
#include "canvas/utils/CanvasAutoPorts.hpp"

#include <QtCore/QHash>
#include <QtCore/QSizeF>
#include <QtGui/QPainter>
#include <algorithm>
#include <cmath>

namespace Canvas {

bool CanvasBlock::s_globalShowPorts = true;

std::unique_ptr<CanvasItem> CanvasBlock::clone() const
{
    auto blk = std::make_unique<CanvasBlock>(m_boundsScene, m_movable, m_label);
    blk->setStereotype(m_stereotype);
    blk->setSpecId(m_specId);
    blk->setPorts(m_ports);
    blk->m_showPorts = m_showPorts;
    blk->m_allowMultiplePorts = m_allowMultiplePorts;
    blk->m_hasAutoPortRole = m_hasAutoPortRole;
    blk->m_autoPortRole = m_autoPortRole;
    blk->m_autoOppositeProducerPort = m_autoOppositeProducerPort;
    blk->m_showPortLabels = m_showPortLabels;
    blk->m_autoPortLayout = m_autoPortLayout;
    blk->m_portSnapStep = m_portSnapStep;
    blk->m_isLinkHub = m_isLinkHub;
    blk->m_deletable = m_deletable;
    blk->m_contentPadding = m_contentPadding;
    if (m_content)
        blk->m_content = m_content->clone();
    blk->setKeepoutMargin(m_keepoutMarginScene);
    if (m_hasCustomColors)
        blk->setCustomColors(m_outlineColor, m_fillColor, m_labelColor);
    blk->setCornerRadius(m_cornerRadius);
    if (m_coreFunctionConfig.has_value())
        blk->setCoreFunctionConfig(*m_coreFunctionConfig);
    blk->setAssignedKernels(m_assignedKernels);
    blk->setCoreBodyArgs(m_coreBodyArgs);
    blk->setId(id());
    return blk;
}

void CanvasBlock::setPorts(std::vector<CanvasPort> ports)
{
    m_ports = std::move(ports);
}

static PortSide sideFromAngle(double angle)
{
    const double dx = std::cos(angle);
    const double dy = std::sin(angle);
    if (std::abs(dx) >= std::abs(dy))
        return dx >= 0.0 ? PortSide::Right : PortSide::Left;
    return dy >= 0.0 ? PortSide::Bottom : PortSide::Top;
}

static double tFromAngle(double angle, PortSide side)
{
    const double dx = std::cos(angle);
    const double dy = std::sin(angle);
    double t = 0.5;
    switch (side) {
        case PortSide::Left:
        case PortSide::Right:
            t = (dy + 1.0) * 0.5;
            break;
        case PortSide::Top:
        case PortSide::Bottom:
            t = (dx + 1.0) * 0.5;
            break;
    }
    return t;
}

static double snapToStep(double v, double step)
{
    if (step <= 0.0)
        return v;
    return std::llround(v / step) * step;
}

static PortSide oppositeSide(PortSide side)
{
    switch (side) {
        case PortSide::Left: return PortSide::Right;
        case PortSide::Right: return PortSide::Left;
        case PortSide::Top: return PortSide::Bottom;
        case PortSide::Bottom: return PortSide::Top;
    }
    return side;
}

static QString normalizedBoundConsumerName(const QString& rawName)
{
    const QString normalized = rawName.trimmed();
    if (normalized.isEmpty())
        return QStringLiteral("of");
    if (Support::isPairedPortName(normalized) || Support::isLegacyPairedPortName(normalized))
        return QStringLiteral("of");
    return normalized;
}

static bool extractBoundLabelName(const QString& label, QString& outName)
{
    const QString normalized = label.trimmed();
    if (!normalized.startsWith(QStringLiteral("C:")))
        return false;

    const int firstQuote = normalized.indexOf('"');
    const int lastQuote = normalized.lastIndexOf('"');
    if (firstQuote < 0 || lastQuote <= firstQuote)
        return false;

    outName = normalized.mid(firstQuote + 1, lastQuote - firstQuote - 1);
    return true;
}

static QString boundConsumerLabelText(const QString& rawName)
{
    return QStringLiteral("C: \"%1\"").arg(normalizedBoundConsumerName(rawName));
}

PortId CanvasBlock::addPort(PortSide side, double t, PortRole role, QString name)
{
    CanvasPort port;
    port.id = PortId::create();
    port.role = role;
    port.side = side;
    port.t = t;
    port.name = std::move(name);
    m_ports.push_back(port);
    return port.id;
}

PortId CanvasBlock::addPortToward(const QPointF& targetScene, PortRole role, QString name)
{
    const QPointF center = m_boundsScene.center();
    const QPointF d = targetScene - center;
    const double len2 = d.x() * d.x() + d.y() * d.y();
    double angle = 0.0;
    if (len2 > 1e-6)
        angle = std::atan2(d.y(), d.x());

    const PortSide side = sideFromAngle(angle);
    const double t = tFromAngle(angle, side);
    return addPort(side, t, role, std::move(name));
}

bool CanvasBlock::updatePort(PortId id, PortSide side, double t)
{
    for (auto& port : m_ports) {
        if (port.id == id) {
            port.side = side;
            port.t = t;
            return true;
        }
    }
    return false;
}

bool CanvasBlock::updatePortName(PortId id, QString name)
{
    for (auto& port : m_ports) {
        if (port.id == id) {
            if (port.name == name)
                return false;
            port.name = std::move(name);
            return true;
        }
    }
    return false;
}

bool CanvasBlock::updatePortBinding(PortId id, ObjectId bindingItemId, PortId bindingPortId)
{
    if (bindingItemId.isNull() || bindingPortId.isNull())
        return clearPortBinding(id);

    for (auto& port : m_ports) {
        if (port.id != id)
            continue;
        const bool unchanged = port.hasBinding &&
                               port.bindingItemId == bindingItemId &&
                               port.bindingPortId == bindingPortId;
        if (unchanged)
            return false;
        port.hasBinding = true;
        port.bindingItemId = bindingItemId;
        port.bindingPortId = bindingPortId;
        return true;
    }
    return false;
}

bool CanvasBlock::clearPortBinding(PortId id)
{
    for (auto& port : m_ports) {
        if (port.id != id)
            continue;
        if (!port.hasBinding)
            return false;
        port.hasBinding = false;
        port.bindingItemId = ObjectId{};
        port.bindingPortId = PortId{};
        return true;
    }
    return false;
}

std::optional<CanvasPort> CanvasBlock::removePort(PortId id, size_t* indexOut)
{
    for (size_t i = 0; i < m_ports.size(); ++i) {
        if (m_ports[i].id == id) {
            if (indexOut)
                *indexOut = i;
            CanvasPort removed = m_ports[i];
            m_ports.erase(m_ports.begin() + static_cast<std::ptrdiff_t>(i));
            return removed;
        }
    }
    return std::nullopt;
}

bool CanvasBlock::insertPort(size_t index, CanvasPort port)
{
    if (index > m_ports.size())
        index = m_ports.size();
    m_ports.insert(m_ports.begin() + static_cast<std::ptrdiff_t>(index), std::move(port));
    return true;
}

QPointF CanvasBlock::portAnchorScene(PortId id) const
{
    for (const auto& port : m_ports) {
        if (port.id != id) continue;

        const double t = Support::clampT(port.t);
        const QRectF r = m_boundsScene;
        const double step = m_portSnapStep;

        switch (port.side) {
            case PortSide::Left: {
                double y = r.top() + t * r.height();
                if (step > 0.0) {
                    const double minY = r.top() + step;
                    const double maxY = r.bottom() - step;
                    if (minY <= maxY)
                        y = std::clamp(snapToStep(y, step), minY, maxY);
                    else
                        y = (r.top() + r.bottom()) * 0.5;
                }
                return QPointF(r.left(), y);
            }
            case PortSide::Right: {
                double y = r.top() + t * r.height();
                if (step > 0.0) {
                    const double minY = r.top() + step;
                    const double maxY = r.bottom() - step;
                    if (minY <= maxY)
                        y = std::clamp(snapToStep(y, step), minY, maxY);
                    else
                        y = (r.top() + r.bottom()) * 0.5;
                }
                return QPointF(r.right(), y);
            }
            case PortSide::Top: {
                double x = r.left() + t * r.width();
                if (step > 0.0) {
                    const double minX = r.left() + step;
                    const double maxX = r.right() - step;
                    if (minX <= maxX)
                        x = std::clamp(snapToStep(x, step), minX, maxX);
                    else
                        x = (r.left() + r.right()) * 0.5;
                }
                return QPointF(x, r.top());
            }
            case PortSide::Bottom: {
                double x = r.left() + t * r.width();
                if (step > 0.0) {
                    const double minX = r.left() + step;
                    const double maxX = r.right() - step;
                    if (minX <= maxX)
                        x = std::clamp(snapToStep(x, step), minX, maxX);
                    else
                        x = (r.left() + r.right()) * 0.5;
                }
                return QPointF(x, r.bottom());
            }
        }
    }
    return QPointF();
}

void CanvasBlock::draw(QPainter& p, const CanvasRenderContext& ctx) const
{
    const double radius = (m_cornerRadius >= 0.0) ? m_cornerRadius : Constants::kBlockCornerRadius;
    if (m_hasCustomColors) {
        CanvasStyle::drawBlockFrame(p, m_boundsScene, ctx.zoom, m_outlineColor, m_fillColor,
                                    radius);
    } else {
        CanvasStyle::drawBlockFrame(p, m_boundsScene, ctx.zoom,
                                    QColor(Constants::kBlockOutlineColor),
                                    QColor(Constants::kBlockFillColor),
                                    radius);
    }
    if (ctx.selected(id()))
        CanvasStyle::drawBlockSelection(p, m_boundsScene, ctx.zoom);
    // Kernel chip layout rules:
    //   1 kernel  → chip covers 50% of tile height; AIE label visible
    //   2 kernels → chips cover 50% of tile height (25% each); AIE label visible
    //   3 kernels → chips cover 75% of tile height (25% each); AIE label hidden
    //   4 kernels → chips cover 100% of tile height (25% each); AIE label hidden
    const int kernelCount = std::min(static_cast<int>(m_assignedKernels.size()), 4);
    const bool hasKernelChips = (kernelCount > 0) && m_content;

    const bool showLabel = !hasKernelChips || kernelCount < 3;
    if (!m_label.isEmpty() && showLabel)
        CanvasStyle::drawBlockLabel(p, m_boundsScene, ctx.zoom, m_label,
                                    m_hasCustomColors ? m_labelColor : QColor(Constants::kBlockTextColor));

    if (hasKernelChips) {
        const double tileH  = m_boundsScene.height();
        const double margin = 4.0 * Constants::kWorldScale;
        const double gap    = 3.0 * Constants::kWorldScale;

        // Total height the chip stack occupies (25% per chip, except 1 chip = 50%).
        const double chipFraction = (kernelCount == 1) ? 0.50 : 0.25;
        const double stackH = kernelCount * chipFraction * tileH
                              + (kernelCount - 1) * gap;

        // For 1-2 kernels anchor to the bottom so the label stays visible at the top.
        // For 3-4 kernels fill from the top (label is suppressed).
        const double topY = (kernelCount >= 3)
            ? (m_boundsScene.top() + margin)
            : (m_boundsScene.bottom() - stackH - margin);

        const QRectF chipRect(m_boundsScene.left()  + margin,
                              topY,
                              m_boundsScene.width() - 2.0 * margin,
                              stackH);

        m_content->layout(chipRect, ctx);
        m_content->draw(p, ctx);
    } else if (m_content) {
        const QRectF contentRect = m_boundsScene.adjusted(m_contentPadding.left(),
                                                          m_contentPadding.top(),
                                                          -m_contentPadding.right(),
                                                          -m_contentPadding.bottom());
        m_content->layout(contentRect, ctx);
        m_content->draw(p, ctx);
    }

    if (m_showPorts && s_globalShowPorts) {
        for (const auto& port : m_ports) {
            const QPointF a = portAnchorScene(port.id);
            const bool hovered = ctx.portHovered(id(), port.id);
            const bool selected = ctx.portSelected(id(), port.id);
            CanvasStyle::drawPort(p, a, port.side, port.role, ctx.zoom, hovered || selected);

            QString label;
            PortSide labelSide = port.side;
            if (port.role == PortRole::Producer && port.hasBinding) {
                QString boundName;
                if (ctx.objectFifoNameForEndpoint(port.bindingItemId, port.bindingPortId, boundName)) {
                    label = boundConsumerLabelText(boundName);
                } else {
                    QString legacyName;
                    if (extractBoundLabelName(port.name, legacyName))
                        label = boundConsumerLabelText(legacyName);
                    else
                        label = boundConsumerLabelText(port.name);
                }
                labelSide = oppositeSide(port.side);
            } else if (m_showPortLabels) {
                label = port.name.trimmed();
                if (Support::isPairedPortName(label) || Support::isLegacyPairedPortName(label))
                    label.clear();
            }

            if (!label.isEmpty()) {
                const QColor labelColor = m_hasCustomColors ? m_labelColor
                                                            : QColor(Constants::kBlockTextColor);
                CanvasStyle::drawPortLabel(p, a, labelSide, ctx.zoom, label, labelColor);
            }
        }
    }
}

void CanvasBlock::setCustomColors(const QColor& outline, const QColor& fill, const QColor& label)
{
    m_outlineColor = outline;
    m_fillColor = fill;
    m_labelColor = label;
    m_hasCustomColors = true;
}

void CanvasBlock::clearCustomColors()
{
    m_hasCustomColors = false;
    m_outlineColor = QColor();
    m_fillColor = QColor();
    m_labelColor = QColor();
}

QRectF CanvasBlock::keepoutSceneRect() const
{
	double m = (m_keepoutMarginScene >= 0.0) ? m_keepoutMarginScene : Constants::kBlockKeepoutMargin;
	const double step = Constants::kGridStep;
	if (step > 0.0)
		m = std::ceil(m / step) * step;
    return m_boundsScene.adjusted(-m, -m, m, m);
}

void CanvasBlock::setContent(std::unique_ptr<BlockContent> content)
{
    m_content = std::move(content);
}

void CanvasBlock::setCoreFunctionConfig(CoreFunctionConfig config)
{
    m_coreFunctionConfig = std::move(config);
}

void CanvasBlock::clearCoreFunctionConfig()
{
    m_coreFunctionConfig.reset();
}

// ── Assigned kernels ─────────────────────────────────────────────────────────

const QStringList& CanvasBlock::assignedKernels() const noexcept
{
    return m_assignedKernels;
}

void CanvasBlock::setAssignedKernels(QStringList kernels)
{
    m_assignedKernels = std::move(kernels);

    if (m_assignedKernels.isEmpty()) {
        clearContent();
        return;
    }

    // Chips are laid out dynamically in draw() based on live tile bounds.
    // Here we just build the chip objects with flexible size (preferred = 0)
    // so the Vertical container distributes height equally among them.
    const int count = std::min(static_cast<int>(m_assignedKernels.size()), 4);

    auto container = std::make_unique<BlockContentContainer>(
        BlockContentContainer::Layout::Vertical);
    container->setGap(3.0 * Constants::kWorldScale);
    container->setPadding(QMarginsF(0.0, 0.0, 0.0, 0.0)); // draw() owns the rect

    for (int i = 0; i < count; ++i) {
        const QString displayName = m_assignedKernels[i].section(u'_', 0, 0);
        BlockContentStyle chipStyle;
        chipStyle.fill         = QColor(QStringLiteral("#2D7A45"));
        chipStyle.outline      = QColor(QStringLiteral("#1E5430"));
        chipStyle.text         = QColor(QStringLiteral("#FFFFFF"));
        chipStyle.cornerRadius = 3.0 * Constants::kWorldScale;
        chipStyle.fontSize     = 7.0 * Constants::kWorldScale;
        auto chip = std::make_unique<BlockContentBlock>(displayName, chipStyle);
        chip->setPreferredSize(QSizeF(0.0, 0.0)); // flexible — fills equally
        container->addChild(std::move(chip));
    }

    setContent(std::move(container));
}

void CanvasBlock::addAssignedKernel(const QString& kernelId)
{
    if (kernelId.isEmpty() || m_assignedKernels.contains(kernelId))
        return;
    m_assignedKernels.append(kernelId);
    setAssignedKernels(m_assignedKernels); // rebuild content
}

void CanvasBlock::removeAssignedKernel(const QString& kernelId)
{
    if (!m_assignedKernels.removeOne(kernelId))
        return;
    setAssignedKernels(m_assignedKernels); // rebuild content
}

} // namespace Canvas
