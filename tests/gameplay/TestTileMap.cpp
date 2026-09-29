// Era Shift - tests for the tile grid.
//
// The grid is where the era mechanic actually lives, so the rules that matter
// are: a tile can be solid in some eras and not others, and out-of-bounds is
// always "nothing there" rather than a crash or a wall.

#include "EraShift/Game/TileMap.hpp"

#include <doctest/doctest.h>

using namespace EraShift::Game;
using EraShift::Graphics::Rect;
using EraShift::Graphics::Vec2;

namespace {

/// A flat floor across the bottom row, which is the shape most of these tests
/// need.
TileMap flatFloor(int columns, int rows)
{
    TileMap map;
    map.resize(columns, rows);
    for (int x = 0; x < columns; ++x) {
        map.set(x, rows - 1, Tile::of(TileKind::Solid));
    }
    return map;
}

} // namespace

TEST_CASE("era-specific tiles are solid only where they should be")
{
    CHECK(Tile::of(TileKind::Solid).blocksIn(Era::Past));
    CHECK(Tile::of(TileKind::Solid).blocksIn(Era::Present));
    CHECK(Tile::of(TileKind::Solid).blocksIn(Era::Future));

    CHECK(Tile::of(TileKind::Crumble).blocksIn(Era::Past));
    CHECK_FALSE(Tile::of(TileKind::Crumble).blocksIn(Era::Present));
    CHECK_FALSE(Tile::of(TileKind::Crumble).blocksIn(Era::Future));

    CHECK_FALSE(Tile::of(TileKind::Bridge).blocksIn(Era::Past));
    CHECK(Tile::of(TileKind::Bridge).blocksIn(Era::Present));
    CHECK_FALSE(Tile::of(TileKind::Bridge).blocksIn(Era::Future));

    CHECK_FALSE(Tile::of(TileKind::Crystal).blocksIn(Era::Past));
    CHECK_FALSE(Tile::of(TileKind::Crystal).blocksIn(Era::Present));
    CHECK(Tile::of(TileKind::Crystal).blocksIn(Era::Future));
}

TEST_CASE("markers and hazards are never solid")
{
    // A goal that blocked movement would stop the player one tile short.
    CHECK_FALSE(Tile::of(TileKind::Goal).blocksIn(Era::Past));
    CHECK_FALSE(Tile::of(TileKind::Goal).blocksIn(Era::Present));
    CHECK_FALSE(Tile::of(TileKind::Empty).blocksIn(Era::Future));
    // A hazard must let the player into it, or it cannot hurt them.
    CHECK_FALSE(Tile::of(TileKind::Hazard).blocksIn(Era::Past));
    CHECK(Tile::of(TileKind::Hazard).isHazardIn(Era::Past));
    CHECK(Tile::of(TileKind::Hazard).isHazardIn(Era::Future));
    CHECK(Tile::of(TileKind::Hazard).isHazardIn(Era::Future));
}

TEST_CASE("one-way platforms are flagged and solid in every era")
{
    const Tile platform = Tile::of(TileKind::Platform);
    CHECK(platform.isOneWayIn(Era::Past));
    CHECK(platform.isOneWayIn(Era::Future));
    CHECK(platform.blocksIn(Era::Past));
    CHECK(platform.blocksIn(Era::Future));
    CHECK_FALSE(platform.isHazardIn(Era::Past));
}

TEST_CASE("a cell can be solid in one era and a hazard in another")
{
    // This is the shape the whole level format depends on: the same tile is
    // walkable stone in the Past and a pit in the Present. A single boolean per
    // property could not express it.
    Tile tile;
    tile.solidIn   = kEraBitPast;
    tile.hazardIn  = kEraBitPresent | kEraBitFuture;

    CHECK(tile.blocksIn(Era::Past));
    CHECK_FALSE(tile.isHazardIn(Era::Past));

    CHECK_FALSE(tile.blocksIn(Era::Present));
    CHECK(tile.isHazardIn(Era::Present));

    CHECK_FALSE(tile.blocksIn(Era::Future));
    CHECK(tile.isHazardIn(Era::Future));
}

TEST_CASE("reads outside the grid are empty rather than fatal")
{
    TileMap map;
    map.resize(4, 4);
    map.set(1, 1, Tile::of(TileKind::Solid));

    CHECK(map.inBounds(1, 1));
    CHECK_FALSE(map.inBounds(-1, 0));
    CHECK_FALSE(map.inBounds(0, -1));
    CHECK_FALSE(map.inBounds(4, 0));
    CHECK_FALSE(map.inBounds(0, 4));

    CHECK_FALSE(map.at(-5, -5).blocksIn(Era::Past));
    CHECK_FALSE(map.at(99, 99).blocksIn(Era::Past));
    // Writing out of range is ignored rather than growing the map.
    map.set(-1, -1, Tile::of(TileKind::Solid));
    map.set(99, 99, Tile::of(TileKind::Solid));
    CHECK(map.columns() == 4);
    CHECK(map.rows() == 4);
}

TEST_CASE("world and cell coordinates agree in both directions")
{
    TileMap map;
    map.resize(10, 5);

    CHECK(map.cellX(0.0f) == 0);
    CHECK(map.cellX(31.9f) == 0);
    CHECK(map.cellX(32.0f) == 1);
    CHECK(map.cellX(-0.1f) == -1);

    CHECK(map.cellY(64.0f) == 2);

    // The map-to-cell helper agrees with the scalar ones.
    const Vec2 cellIndex = map.worldToCell(Vec2{100.0f, 70.0f});
    CHECK(cellIndex.x == doctest::Approx(3.0f));
    CHECK(cellIndex.y == doctest::Approx(2.0f));

    const Rect cellRect = map.cellRect(3, 2);
    CHECK(cellRect.x == doctest::Approx(96.0f));
    CHECK(cellRect.y == doctest::Approx(64.0f));
    CHECK(cellRect.w == doctest::Approx(TileMap::kTileSize));

    const Rect bounds = map.bounds();
    CHECK(bounds.w == doctest::Approx(10.0f * TileMap::kTileSize));
    CHECK(bounds.h == doctest::Approx(5.0f * TileMap::kTileSize));
}

TEST_CASE("a rect reports blocked only when the current era says so")
{
    TileMap map;
    map.resize(8, 4);
    for (int x = 0; x < 8; ++x) {
        map.set(x, 1, Tile::of(TileKind::Crumble));
    }

    const Rect probe{64.0f, 32.0f, 32.0f, 32.0f};   // straddles cells 2 and 3
    CHECK(map.rectBlocked(probe, Era::Past));
    CHECK_FALSE(map.rectBlocked(probe, Era::Present));
    CHECK_FALSE(map.rectBlocked(probe, Era::Future));

    // A rect in the clear above is never blocked.
    const Rect air{0.0f, 0.0f, 32.0f, 32.0f};
    CHECK_FALSE(map.rectBlocked(air, Era::Past));
}

TEST_CASE("a rect touching a wall only from the far side is not blocked")
{
    TileMap map;
    map.resize(8, 4);
    map.set(4, 2, Tile::of(TileKind::Solid));

    // Ends exactly on the cell boundary: not overlapping.
    const Rect before{64.0f, 64.0f, 64.0f, 32.0f};
    CHECK_FALSE(map.rectBlocked(before, Era::Present));

    // Overlaps by a hair: blocked.
    const Rect touching{64.1f, 64.0f, 64.1f, 32.0f};
    CHECK(map.rectBlocked(touching, Era::Present));
}

TEST_CASE("one-way platforms do not block sideways or upward movement")
{
    TileMap map;
    map.resize(8, 4);
    for (int x = 0; x < 8; ++x) {
        map.set(x, 2, Tile::of(TileKind::Platform));
    }

    const Rect at = {64.0f, 64.0f, 32.0f, 32.0f};   // inside the platform row
    CHECK_FALSE(map.rectBlocked(at, Era::Present));

    // Falling onto it: the body was entirely above before the move.
    CHECK(map.oneWayLanding(at, 63.0f, Era::Present));
    // Rising through it: the body was already below the surface.
    CHECK_FALSE(map.oneWayLanding(at, 65.0f, Era::Present));
    // Already standing on it.
    CHECK_FALSE(map.oneWayLanding(at, 96.0f, Era::Present));
    // And the era mask still applies.
    map.set(4, 1, Tile::of(TileKind::Crystal));
    const Rect crystal{128.0f, 32.0f, 32.0f, 32.0f};
    CHECK_FALSE(map.oneWayLanding(crystal, 31.0f, Era::Future));
}

TEST_CASE("hazards and goals are found by overlap")
{
    TileMap map;
    map.resize(8, 4);
    map.set(3, 1, Tile::of(TileKind::Hazard));
    map.set(6, 2, Tile::of(TileKind::Goal));

    CHECK(map.rectOverHazard(Rect{96.0f, 32.0f, 8.0f, 8.0f}, Era::Present));
    CHECK_FALSE(map.rectOverHazard(Rect{0.0f, 0.0f, 16.0f, 8.0f}, Era::Present));

    CHECK(map.rectOverGoal(Rect{192.0f, 64.0f, 32.0f, 32.0f}));
    CHECK_FALSE(map.rectOverGoal(Rect{0.0f, 0.0f, 32.0f, 32.0f}));

    // A hazard is not solid, so a body inside one is not blocked by it.
    CHECK_FALSE(map.rectBlocked(Rect{96.0f, 32.0f, 8.0f, 8.0f}, Era::Present));
}

TEST_CASE("an empty map blocks nothing and a cleared one forgets everything")
{
    TileMap map = flatFloor(6, 3);
    CHECK(map.rectBlocked(Rect{0.0f, 64.0f, 32.0f, 32.0f}, Era::Past));

    map.clear();
    CHECK(map.empty());
    CHECK_FALSE(map.rectBlocked(Rect{0.0f, 64.0f, 32.0f, 32.0f}, Era::Past));
    CHECK(map.at(0, 0).kind == TileKind::Empty);
}

TEST_CASE("resizing discards the old contents instead of keeping them")
{
    TileMap map;
    map.resize(4, 4);
    map.set(1, 1, Tile::of(TileKind::Solid));

    map.resize(8, 8);
    CHECK(map.at(1, 1).kind == TileKind::Empty);
    CHECK(map.columns() == 8);

    // A negative size produces an empty map rather than a huge allocation.
    map.resize(-4, -4);
    CHECK(map.empty());
    CHECK(map.columns() == 0);
}
