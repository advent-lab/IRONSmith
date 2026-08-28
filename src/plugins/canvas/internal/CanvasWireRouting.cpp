// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "canvas/internal/CanvasWireRouting.hpp"

#include "canvas/utils/CanvasGeometry.hpp"

#include <QtCore/QPoint>

#include <algorithm>
#include <array>
#include <cmath>

namespace Canvas::Internal {

namespace {

constexpr int kAStarPad = 16;
constexpr int kAStarMaxVisited = 40000;

CoordBounds boundsFromRect(const QRectF& r, double step)
{
    const double left = std::min(r.left(), r.right());
    const double right = std::max(r.left(), r.right());
    const double top = std::min(r.top(), r.bottom());
    const double bottom = std::max(r.top(), r.bottom());

    CoordBounds out;
    out.minX = static_cast<int>(std::floor(left / step));
    out.maxX = static_cast<int>(std::ceil(right / step));
    out.minY = static_cast<int>(std::floor(top / step));
    out.maxY = static_cast<int>(std::ceil(bottom / step));
    return out;
}

int signum(double v)
{
    return (v > 0.0) ? 1 : (v < 0.0) ? -1 : 0;
}

std::vector<QPointF> orthogonalFallback(const QPointF& aFabric, const QPointF& bFabric)
{
    if (aFabric == bFabric)
        return {aFabric, bFabric};
    if (aFabric.x() == bFabric.x() || aFabric.y() == bFabric.y())
        return {aFabric, bFabric};
    const QPointF mid(bFabric.x(), aFabric.y());
    return {aFabric, mid, bFabric};
}

FabricCoord toCoord(const QPointF& s, double step)
{
    return Support::toFabricCoord(s, step);
}

QPointF toScene(const FabricCoord& c, double step)
{
    return Support::toScenePoint(c, step);
}

CoordBounds computeSearchBounds(const FabricCoord& start,
                                const FabricCoord& goal,
                                const CanvasRenderContext& ctx,
                                double step)
{
    CoordBounds bounds;
    bounds.minX = std::min(start.x, goal.x);
    bounds.maxX = std::max(start.x, goal.x);
    bounds.minY = std::min(start.y, goal.y);
    bounds.maxY = std::max(start.y, goal.y);

    if (!ctx.visibleSceneRect.isNull()) {
        const CoordBounds vis = boundsFromRect(ctx.visibleSceneRect, step);
        bounds.minX = std::min(bounds.minX, vis.minX);
        bounds.maxX = std::max(bounds.maxX, vis.maxX);
        bounds.minY = std::min(bounds.minY, vis.minY);
        bounds.maxY = std::max(bounds.maxY, vis.maxY);
    }

    bounds.minX -= kAStarPad;
    bounds.maxX += kAStarPad;
    bounds.minY -= kAStarPad;
    bounds.maxY += kAStarPad;
    return bounds;
}

bool inBounds(const CoordBounds& bounds, int x, int y)
{
    return x >= bounds.minX && x <= bounds.maxX && y >= bounds.minY && y <= bounds.maxY;
}

int manhattanDistance(int x, int y, const FabricCoord& goal)
{
    return std::abs(x - goal.x) + std::abs(y - goal.y);
}

// Order-independent key for the unit edge between two adjacent cells.
EdgeKey makeEdgeKey(const FabricCoord& a, const FabricCoord& b)
{
    if (a.y == b.y)
        return EdgeKey{std::min(a.x, b.x), a.y, 0};
    return EdgeKey{a.x, std::min(a.y, b.y), 1};
}

} // namespace

void addPathEdges(EdgeOccupancy& occupancy, const std::vector<FabricCoord>& coords)
{
    for (size_t i = 1; i < coords.size(); ++i) {
        const FabricCoord& p0 = coords[i - 1];
        const FabricCoord& p1 = coords[i];
        if (p0.x == p1.x && p0.y == p1.y)
            continue; // zero-length — nothing to claim
        if (p0.x != p1.x && p0.y != p1.y)
            continue; // not axis-aligned (can happen at raw port-anchor endpoints) — best effort, skip

        const int dx = (p1.x > p0.x) ? 1 : (p1.x < p0.x ? -1 : 0);
        const int dy = (p1.y > p0.y) ? 1 : (p1.y < p0.y ? -1 : 0);
        FabricCoord cur = p0;
        while (!(cur.x == p1.x && cur.y == p1.y)) {
            const FabricCoord next{cur.x + dx, cur.y + dy};
            occupancy[makeEdgeKey(cur, next)] += 1;
            cur = next;
        }
    }
}

int edgeOccupancyCount(const EdgeOccupancy& occupancy, const FabricCoord& a, const FabricCoord& b)
{
    const auto it = occupancy.find(makeEdgeKey(a, b));
    return it == occupancy.end() ? 0 : it->second;
}

WireRouter::WireRouter(const CanvasRenderContext& ctx)
    : m_ctx(ctx)
{}

std::vector<FabricCoord> WireRouter::routeCoords(const FabricCoord& start, const FabricCoord& goal) const
{
    return routeSegment(start, goal);
}

std::vector<FabricCoord> WireRouter::routeCoordsViaWaypoints(const std::vector<FabricCoord>& waypoints) const
{
    if (waypoints.size() < 2)
        return waypoints;

    std::vector<FabricCoord> coords;
    for (size_t i = 1; i < waypoints.size(); ++i) {
        const FabricCoord start = waypoints[i - 1];
        const FabricCoord goal = waypoints[i];
        std::vector<FabricCoord> seg = routeSegment(start, goal);

        if (coords.empty()) {
            coords = std::move(seg);
        } else if (!seg.empty()) {
            coords.insert(coords.end(), seg.begin() + 1, seg.end());
        }
    }

    if (coords.empty())
        return routeSegment(waypoints.front(), waypoints.back());

    return coords;
}

std::vector<QPointF> WireRouter::routeFabricPath(const QPointF& aFabric, const QPointF& bFabric) const
{
    const double step = m_ctx.fabricStep;
    if (step <= 0.0)
        return orthogonalFallback(aFabric, bFabric);

    const FabricCoord start = toCoord(aFabric, step);
    const FabricCoord goal = toCoord(bFabric, step);
    if (start.x == goal.x && start.y == goal.y)
        return {aFabric, bFabric};

    auto coords = routeCoords(start, goal);
    return simplifyCoordsToScene(coords, step, aFabric, bFabric);
}

std::vector<QPointF> WireRouter::routeViaWaypoints(const std::vector<FabricCoord>& waypoints,
                                                   const QPointF& aFabric,
                                                   const QPointF& bFabric) const
{
    const double step = m_ctx.fabricStep;
    if (step <= 0.0)
        return orthogonalFallback(aFabric, bFabric);

    if (waypoints.size() < 2)
        return routeFabricPath(aFabric, bFabric);

    std::vector<FabricCoord> coords = routeCoordsViaWaypoints(waypoints);
    if (coords.empty())
        coords = routeCoords(toCoord(aFabric, step), toCoord(bFabric, step));

    return simplifyCoordsToScene(coords, step, aFabric, bFabric);
}

std::vector<FabricCoord> WireRouter::routeSegment(const FabricCoord& start, const FabricCoord& goal) const
{
    if (start.x == goal.x && start.y == goal.y)
        return {start};

    if (auto simple = trySimplePath(start, goal); !simple.empty())
        return simple;

    auto coords = aStarPath(start, goal);
    if (coords.empty())
        coords = directManhattanPath(start, goal);
    return smoothPath(coords);
}

std::vector<FabricCoord> WireRouter::aStarPath(const FabricCoord& start, const FabricCoord& goal) const
{
    if (start.x == goal.x && start.y == goal.y)
        return {start};

    const double step = effectiveStep();
    SearchState state = initSearch(start, goal, step);
    OpenSet open;
    ScoreMap gScore;
    CameFromMap cameFrom;

    gScore[state.startKey] = 0;
    open.push(Node{start.x, start.y, kDirNone, 0, manhattanDistance(start.x, start.y, goal)});

    while (!open.empty() && state.visited < kAStarMaxVisited) {
        const Node cur = open.top();
        open.pop();

        const StateKey curKey{cur.x, cur.y, cur.dir};
        if (isStaleNode(gScore, cur, curKey))
            continue;

        if (cur.x == state.goal.x && cur.y == state.goal.y)
            return rebuildPath(cameFrom, state.startKey, curKey);

        ++state.visited;
        expandNode(state, open, gScore, cameFrom, cur, curKey);
    }

    return {};
}

std::vector<FabricCoord> WireRouter::trySimplePath(const FabricCoord& start, const FabricCoord& goal) const
{
    if (isSegmentClear(start, goal, true)) {
        auto direct = directManhattanPath(start, goal);
        // Occupied — reject rather than accept, so routeSegment() falls through to
        // aStarPath(), which can look for a free detour (or, via its own soft cost,
        // fall back to this same occupied path if that's genuinely the only option).
        return pathOccupancyCount(direct) == 0 ? direct : std::vector<FabricCoord>{};
    }

    const FabricCoord midH{goal.x, start.y};
    const FabricCoord midV{start.x, goal.y};

    const bool canHV = isSegmentClear(start, midH, false) && isSegmentClear(midH, goal, true);
    const bool canVH = isSegmentClear(start, midV, false) && isSegmentClear(midV, goal, true);
    if (!canHV && !canVH)
        return {};

    std::vector<FabricCoord> hvPath, vhPath;
    int hvOcc = -1, vhOcc = -1;
    if (canHV) {
        hvPath = concatSegments(directManhattanPath(start, midH), directManhattanPath(midH, goal));
        hvOcc = pathOccupancyCount(hvPath);
    }
    if (canVH) {
        vhPath = concatSegments(directManhattanPath(start, midV), directManhattanPath(midV, goal));
        vhOcc = pathOccupancyCount(vhPath);
    }

    // Prefer whichever hard-clear candidate has fewer already-occupied edges; break ties
    // (including "only one candidate exists") with the original aspect-ratio heuristic.
    const bool preferHorizontal = std::abs(goal.x - start.x) >= std::abs(goal.y - start.y);
    const bool chooseHV = (canHV && canVH) ? (hvOcc != vhOcc ? hvOcc < vhOcc : preferHorizontal) : canHV;

    const std::vector<FabricCoord>& chosen = chooseHV ? hvPath : vhPath;
    const int chosenOcc = chooseHV ? hvOcc : vhOcc;
    return chosenOcc == 0 ? chosen : std::vector<FabricCoord>{};
}

double WireRouter::effectiveStep() const
{
    const double step = m_ctx.fabricStep;
    return step > 0.0 ? step : 1.0;
}

WireRouter::SearchState WireRouter::initSearch(const FabricCoord& start, const FabricCoord& goal, double step) const
{
    SearchState state;
    state.bounds = computeSearchBounds(start, goal, m_ctx, step);
    state.startKey = StateKey{start.x, start.y, kDirNone};
    state.goal = goal;
    return state;
}

bool WireRouter::isStaleNode(const ScoreMap& gScore, const Node& cur, const StateKey& curKey) const
{
    const auto gsIt = gScore.find(curKey);
    return gsIt == gScore.end() || cur.g != gsIt->second;
}

void WireRouter::expandNode(SearchState& state, OpenSet& open,
                            ScoreMap& gScore, CameFromMap& cameFrom,
                            const Node& cur, const StateKey& curKey) const
{
    const std::array<int, 4> dirs = orderedDirs(cur.dir);
    for (int dir : dirs)
        tryEnqueueNeighbor(state, open, gScore, cameFrom, cur, curKey, dir);
}

void WireRouter::tryEnqueueNeighbor(SearchState& state, OpenSet& open,
                                    ScoreMap& gScore, CameFromMap& cameFrom,
                                    const Node& cur, const StateKey& curKey, int dir) const
{
    const QPoint delta = dirDelta(dir);
    const int nx = cur.x + delta.x();
    const int ny = cur.y + delta.y();
    if (!inBounds(state.bounds, nx, ny))
        return;

    const FabricCoord nc{nx, ny};
    if (isBlocked(nc, state.goal))
        return;

    const int ng = cur.g + stepCost(cur.dir, dir) + occupancyPenalty(FabricCoord{cur.x, cur.y}, nc);
    const StateKey nextKey{nx, ny, dir};
    const auto it = gScore.find(nextKey);
    if (it != gScore.end() && ng >= it->second)
        return;

    cameFrom[nextKey] = curKey;
    gScore[nextKey] = ng;
    open.push(Node{nx, ny, dir, ng, ng + manhattanDistance(nx, ny, state.goal)});
}

bool WireRouter::isBlocked(const FabricCoord& coord, const FabricCoord& goal) const
{
    return m_ctx.fabricBlocked(coord) && !(coord.x == goal.x && coord.y == goal.y);
}

int WireRouter::occupancyPenalty(const FabricCoord& a, const FabricCoord& b) const
{
    if (!m_ctx.wireEdgeOccupancy)
        return 0;
    const auto* occ = static_cast<const EdgeOccupancy*>(m_ctx.wireEdgeOccupancy);
    return edgeOccupancyCount(*occ, a, b) > 0 ? kOccupancyPenalty : 0;
}

int WireRouter::pathOccupancyCount(const std::vector<FabricCoord>& path) const
{
    if (!m_ctx.wireEdgeOccupancy || path.size() < 2)
        return 0;
    const auto* occ = static_cast<const EdgeOccupancy*>(m_ctx.wireEdgeOccupancy);
    int total = 0;
    for (size_t i = 1; i < path.size(); ++i)
        total += edgeOccupancyCount(*occ, path[i - 1], path[i]);
    return total;
}

std::vector<FabricCoord> WireRouter::rebuildPath(const CameFromMap& cameFrom,
                                                 const StateKey& startKey,
                                                 StateKey goalKey) const
{
    std::vector<FabricCoord> coords;
    StateKey cur = goalKey;
    while (true) {
        coords.push_back(FabricCoord{cur.x, cur.y});
        if (cur.x == startKey.x && cur.y == startKey.y && cur.dir == startKey.dir)
            break;
        auto it = cameFrom.find(cur);
        if (it == cameFrom.end())
            return {};
        cur = it->second;
    }
    std::reverse(coords.begin(), coords.end());
    return coords;
}

std::vector<FabricCoord> WireRouter::directManhattanPath(const FabricCoord& start, const FabricCoord& goal)
{
    std::vector<FabricCoord> out;
    out.reserve(static_cast<size_t>(std::abs(goal.x - start.x) + std::abs(goal.y - start.y) + 1));
    FabricCoord cur = start;
    out.push_back(cur);
    while (cur.x != goal.x) {
        cur.x += (goal.x > cur.x) ? 1 : -1;
        out.push_back(cur);
    }
    while (cur.y != goal.y) {
        cur.y += (goal.y > cur.y) ? 1 : -1;
        out.push_back(cur);
    }
    return out;
}

std::vector<FabricCoord> WireRouter::concatSegments(std::vector<FabricCoord> a, std::vector<FabricCoord> b)
{
    if (a.empty())
        return b;
    if (!b.empty())
        a.insert(a.end(), b.begin() + 1, b.end());
    return a;
}

std::vector<FabricCoord> WireRouter::smoothPath(const std::vector<FabricCoord>& path) const
{
    if (path.size() <= 2)
        return path;

    std::vector<FabricCoord> out;
    out.reserve(path.size());
    size_t i = 0;
    while (i + 1 < path.size()) {
        size_t best = i + 1;
        for (size_t j = i + 1; j < path.size(); ++j) {
            if (!isAxisAligned(path[i], path[j]))
                continue;
            const bool allowEndBlocked = (j == path.size() - 1);
            if (isSegmentClear(path[i], path[j], allowEndBlocked))
                best = j;
        }
        out.push_back(path[i]);
        i = best;
    }
    out.push_back(path.back());
    return out;
}

bool WireRouter::isAxisAligned(const FabricCoord& a, const FabricCoord& b)
{
    return a.x == b.x || a.y == b.y;
}

bool WireRouter::isSegmentClear(const FabricCoord& start,
                                const FabricCoord& end,
                                bool allowEndBlocked) const
{
    if (!isAxisAligned(start, end))
        return false;

    const int dx = signum(end.x - start.x);
    const int dy = signum(end.y - start.y);
    FabricCoord cur = start;

    while (true) {
        if (!(cur.x == start.x && cur.y == start.y)) {
            const bool isEnd = (cur.x == end.x && cur.y == end.y);
            if (m_ctx.fabricBlocked(cur) && !(allowEndBlocked && isEnd))
                return false;
        }
        if (cur.x == end.x && cur.y == end.y)
            break;
        cur.x += dx;
        cur.y += dy;
    }
    return true;
}

int WireRouter::stepCost(int prevDir, int nextDir) const
{
    if (prevDir == kDirNone || prevDir == nextDir)
        return 1;
    return 1 + kTurnPenalty;
}

std::array<int, 4> WireRouter::orderedDirs(int currentDir) const
{
    std::array<int, 4> dirs = {0, 1, 2, 3};
    if (currentDir == kDirNone)
        return dirs;

    std::array<int, 4> ordered{};
    ordered[0] = currentDir;
    int idx = 1;
    for (int dir : dirs) {
        if (dir == currentDir)
            continue;
        ordered[idx++] = dir;
    }
    return ordered;
}

QPoint WireRouter::dirDelta(int dir) const
{
    switch (dir) {
        case 0: return QPoint(1, 0);
        case 1: return QPoint(-1, 0);
        case 2: return QPoint(0, 1);
        case 3: return QPoint(0, -1);
        default: return QPoint(0, 0);
    }
}

std::vector<QPointF> simplifyCoordsToScene(const std::vector<FabricCoord>& coords,
                                           double step,
                                           const QPointF& aFabric,
                                           const QPointF& bFabric)
{
    if (coords.empty())
        return {};

    std::vector<QPointF> path;
    path.reserve(coords.size());
    for (const auto& c : coords) {
        const QPointF s = toScene(c, step);
        if (path.size() >= 2) {
            const QPointF& p0 = path[path.size() - 2];
            const QPointF& p1 = path[path.size() - 1];
            if ((p0.x() == p1.x() && p1.x() == s.x()) ||
                (p0.y() == p1.y() && p1.y() == s.y())) {
                path.back() = s;
                continue;
            }
        }
        path.push_back(s);
    }

    path.front() = aFabric;
    path.back() = bFabric;
    return path;
}

} // namespace Canvas::Internal
