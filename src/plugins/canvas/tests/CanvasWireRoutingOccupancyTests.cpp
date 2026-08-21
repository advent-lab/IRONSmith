// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "canvas/CanvasRenderContext.hpp"
#include "canvas/internal/CanvasWireRouting.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace Canvas::Tests {

namespace {

bool noBlockThunk(const FabricCoord&, void*)
{
    return false;
}

// Blocks two vertical walls (x = leftWallX and x = rightWallX, for y in [minY,maxY]),
// simulating a pair of tiles that box a wire into a single free column between them.
struct CorridorBlocker final {
    int leftWallX = 0;
    int rightWallX = 0;
    int minY = 0;
    int maxY = 0;
};

bool corridorBlockedThunk(const FabricCoord& c, void* user)
{
    const auto* blocker = static_cast<const CorridorBlocker*>(user);
    if (c.y < blocker->minY || c.y > blocker->maxY)
        return false;
    return c.x == blocker->leftWallX || c.x == blocker->rightWallX;
}

using EdgeSet = std::set<std::pair<long long, long long>>;

EdgeSet edgeSet(const std::vector<FabricCoord>& coords)
{
    EdgeSet edges;
    for (size_t i = 1; i < coords.size(); ++i) {
        const FabricCoord& a = coords[i - 1];
        const FabricCoord& b = coords[i];
        if (a.x == b.x && a.y == b.y)
            continue;
        long long ka = static_cast<long long>(a.x) * 100000 + a.y;
        long long kb = static_cast<long long>(b.x) * 100000 + b.y;
        if (ka > kb)
            std::swap(ka, kb);
        edges.insert({ka, kb});
    }
    return edges;
}

bool edgesShareAny(const std::vector<FabricCoord>& a, const std::vector<FabricCoord>& b)
{
    const EdgeSet ea = edgeSet(a);
    const EdgeSet eb = edgeSet(b);
    for (const auto& e : ea) {
        if (eb.count(e) > 0)
            return true;
    }
    return false;
}

// FabricCoord has no operator== (plain aggregate), so std::vector<FabricCoord> can't be
// compared with EXPECT_EQ/EXPECT_NE directly.
bool coordsEqual(const std::vector<FabricCoord>& a, const std::vector<FabricCoord>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y)
            return false;
    }
    return true;
}

} // namespace

// Two wires whose natural (unoccupied) routes would coincide for several cells: the
// first wire routed gets the direct path since nothing is claimed yet, and the second
// (now occupancy-aware) prefers the alternative bend that shares zero edges with it,
// since open space exists on both sides.
TEST(CanvasWireRoutingOccupancyTests, UnrelatedWiresAvoidOverlapWhenRoomExists)
{
    CanvasRenderContext ctx{};
    ctx.fabricStep = 10.0;
    ctx.isFabricBlocked = &noBlockThunk;

    Internal::EdgeOccupancy occupancy;
    ctx.wireEdgeOccupancy = &occupancy;
    Internal::WireRouter router(ctx);

    const auto path1 = router.routeCoords(FabricCoord{0, 0}, FabricCoord{5, 5});
    ASSERT_FALSE(path1.empty());
    Internal::addPathEdges(occupancy, path1);

    const auto path2 = router.routeCoords(FabricCoord{1, 0}, FabricCoord{6, 5});
    ASSERT_FALSE(path2.empty());

    EXPECT_FALSE(edgesShareAny(path1, path2));
}

// Two tile walls leave only a single free column between them — there is no room for a
// second wire to take a parallel lane, so both wires must still resolve (not fail) even
// though they end up sharing the same corridor.
TEST(CanvasWireRoutingOccupancyTests, WiresBoxedIntoSingleCorridorStillResolveAndMayOverlap)
{
    CanvasRenderContext ctx{};
    ctx.fabricStep = 10.0;

    CorridorBlocker blocker{/*leftWallX=*/0, /*rightWallX=*/2, /*minY=*/-5, /*maxY=*/10};
    ctx.isFabricBlocked = &corridorBlockedThunk;
    ctx.isFabricBlockedUser = &blocker;

    Internal::EdgeOccupancy occupancy;
    ctx.wireEdgeOccupancy = &occupancy;
    Internal::WireRouter router(ctx);

    const auto path1 = router.routeCoords(FabricCoord{1, -5}, FabricCoord{1, 5});
    ASSERT_FALSE(path1.empty());
    Internal::addPathEdges(occupancy, path1);

    const auto path2 = router.routeCoords(FabricCoord{1, -5}, FabricCoord{1, 5});
    ASSERT_FALSE(path2.empty());

    EXPECT_TRUE(edgesShareAny(path1, path2));
}

// Same document, routed twice from a fresh (non-mutating) context — the router is a
// pure function of its inputs, so this must be identical every time. A regression guard
// for the doc-order-sequential pre-pass, whose outcome depends on routing being stable.
TEST(CanvasWireRoutingOccupancyTests, RoutingIsDeterministicAcrossRepeatedCalls)
{
    CanvasRenderContext ctx{};
    ctx.fabricStep = 10.0;
    ctx.isFabricBlocked = &noBlockThunk;

    Internal::WireRouter router(ctx);
    const auto first = router.routeCoords(FabricCoord{0, 0}, FabricCoord{5, 5});
    const auto second = router.routeCoords(FabricCoord{0, 0}, FabricCoord{5, 5});

    EXPECT_TRUE(coordsEqual(first, second));
}

// Mirrors the rule-3 hub-trunk feature: two "sibling" wires forced through a shared
// waypoint (as CanvasWire::setRouteOverride does) with no occupancy context of their own
// — matching the real pre-pass exempting override wires from *receiving* the penalty —
// end up overlapping on that shared segment, which is the intended behavior. A third,
// unrelated wire routed afterward with the siblings' claimed edges visible steers away
// from a naive straight line through the same corridor when free space exists.
TEST(CanvasWireRoutingOccupancyTests, HubTrunkSiblingsOverlapAndSteerUnrelatedWireAway)
{
    CanvasRenderContext ctx{};
    ctx.fabricStep = 10.0;
    ctx.isFabricBlocked = &noBlockThunk;

    Internal::WireRouter router(ctx);
    const auto sibling1 = router.routeCoordsViaWaypoints(
        {FabricCoord{0, 0}, FabricCoord{5, 5}, FabricCoord{10, 0}});
    const auto sibling2 = router.routeCoordsViaWaypoints(
        {FabricCoord{1, 0}, FabricCoord{5, 5}, FabricCoord{11, 0}});
    ASSERT_FALSE(sibling1.empty());
    ASSERT_FALSE(sibling2.empty());

    EXPECT_TRUE(edgesShareAny(sibling1, sibling2));

    Internal::EdgeOccupancy occupancy;
    Internal::addPathEdges(occupancy, sibling1);
    Internal::addPathEdges(occupancy, sibling2);

    ctx.wireEdgeOccupancy = &occupancy;
    Internal::WireRouter routerWithOccupancy(ctx);
    const auto independent = routerWithOccupancy.routeCoords(FabricCoord{-3, 5}, FabricCoord{13, 5});
    ASSERT_FALSE(independent.empty());

    // A naive straight line from (-3,5) to (13,5) would run directly through the
    // siblings' shared trunk row — the occupancy-aware route must do something else.
    std::vector<FabricCoord> straightLine;
    for (int x = -3; x <= 13; ++x)
        straightLine.push_back(FabricCoord{x, 5});
    EXPECT_FALSE(coordsEqual(independent, straightLine));
}

} // namespace Canvas::Tests
