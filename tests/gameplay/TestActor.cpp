// Era Shift - tests for actor physics.
//
// Swept AABB against a tile grid has a small number of failure modes, and each
// one is a bug the player sees immediately: tunnelling through a floor, being
// unable to leave the ground, getting stuck inside geometry after the world
// changes under them. Each is pinned here.

#include "EraShift/Game/Actor.hpp"

#include <cmath>
#include <limits>

#include <doctest/doctest.h>

using namespace EraShift::Game;
using EraShift::Graphics::Rect;
using EraShift::Graphics::Vec2;

namespace {

/// A room with a solid floor at `floorRow` and solid walls at both edges.
TileMap room(int columns, int rows, int floorRow)
{
    TileMap map;
    map.resize(columns, rows);
    for (int x = 0; x < columns; ++x) {
        map.set(x, floorRow, Tile::of(TileKind::Solid));
    }
    // Side walls, so nothing can leave the room horizontally.
    for (int y = 0; y < rows; ++y) {
        map.set(0, y, Tile::of(TileKind::Solid));
        map.set(columns - 1, y, Tile::of(TileKind::Solid));
    }
    return map;
}

/// A body standing on `floorRow`, sized like the player.
Body standingOn(int floorRow)
{
    Body body;
    body.size     = Vec2{28.0f, 44.0f};
    body.position = Vec2{64.0f, static_cast<float>(floorRow) * TileMap::kTileSize - 44.0f};
    return body;
}

constexpr float kFloor = 4 * TileMap::kTileSize;

} // namespace

TEST_CASE("a body falls and comes to rest exactly on the floor")
{
    const TileMap map = room(16, 8, 4);
    Body body = standingOn(4);
    body.position.y = 0.0f;
    body.onGround   = false;

    // Several seconds of simulation at a fixed step, as the real loop does it.
    for (int step = 0; step < 240; ++step) {
        body.velocity.y += 1500.0f / 60.0f;
        moveBody(map, body, Era::Present, 1.0f / 60.0f);
    }

    CHECK(body.bottom() == doctest::Approx(kFloor));
    CHECK(body.onGround);
    CHECK(body.velocity.y == doctest::Approx(0.0f));
}

TEST_CASE("a body already resting stays grounded, which is what keeps jumps working")
{
    const TileMap map = room(16, 8, 4);
    Body body = standingOn(4);
    moveBody(map, body, Era::Present, 1.0f / 60.0f);
    CHECK(body.onGround);

    // Several idle frames with no input at all.
    for (int step = 0; step < 30; ++step) {
        moveBody(map, body, Era::Present, 1.0f / 60.0f);
        CHECK(body.onGround);
        CHECK(body.bottom() == doctest::Approx(kFloor));
    }
}

TEST_CASE("a fast body cannot tunnel through a thin floor")
{
    const TileMap map = room(16, 8, 4);
    Body body = standingOn(4);
    body.position.y = 0.0f;
    body.velocity   = Vec2{0.0f, 9000.0f};   // 150px per 1/60s step

    moveBody(map, body, Era::Present, 1.0f / 60.0f);

    CHECK(body.bottom() <= kFloor + 0.001f);
    CHECK(body.onGround);
}

TEST_CASE("horizontal movement stops at a wall and reports which side")
{
    TileMap map;
    map.resize(16, 8);
    map.set(8, 4, Tile::of(TileKind::Solid));
    map.set(8, 3, Tile::of(TileKind::Solid));
    map.set(8, 5, Tile::of(TileKind::Solid));

    // Start one cell short of the wall, moving fast enough that a single step
    // carries the body into it. A body that never reaches the wall proves
    // nothing about the collision code.
    Body body;
    body.size     = Vec2{32.0f, 32.0f};
    body.position = Vec2{7.0f * TileMap::kTileSize, 3.0f * TileMap::kTileSize};
    body.velocity = Vec2{600.0f, 0.0f};

    const MoveResult result = moveBody(map, body, Era::Present, 1.0f / 60.0f);

    CHECK(result.wallRight);
    CHECK_FALSE(result.wallLeft);
    CHECK(body.hitWall);
    CHECK(body.velocity.x == doctest::Approx(0.0f));
    CHECK(body.right() <= 8.0f * TileMap::kTileSize + 0.001f);
}

TEST_CASE("a body slides along a floor instead of stalling on it")
{
    const TileMap map = room(16, 8, 4);
    Body body = standingOn(4);
    body.velocity = Vec2{250.0f, 0.0f};

    const float startX = body.position.x;
    for (int step = 0; step < 60; ++step) {
        moveBody(map, body, Era::Present, 1.0f / 60.0f);
    }

    CHECK(body.position.x > startX + 200.0f);
    CHECK(body.onGround);
}

TEST_CASE("one-way platforms catch a falling body and let a rising one through")
{
    TileMap map;
    map.resize(16, 8);
    for (int x = 1; x < 15; ++x) {
        map.set(x, 4, Tile::of(TileKind::Platform));
    }

    // Rising: starts below the platform and jumps up through it.
    Body rising;
    rising.size     = Vec2{28.0f, 44.0f};
    rising.position = Vec2{128.0f, 5.0f * TileMap::kTileSize};
    rising.velocity = Vec2{0.0f, -600.0f};
    for (int step = 0; step < 30; ++step) {
        moveBody(map, rising, Era::Present, 1.0f / 60.0f);
    }
    CHECK(rising.position.y < 4.0f * TileMap::kTileSize);
    CHECK_FALSE(rising.onGround);

    // Falling from above: lands exactly on the platform's surface, not one row
    // above it. Landing high is the failure mode of snapping to whichever row the
    // body's bottom currently happens to be in.
    Body falling;
    falling.size     = Vec2{28.0f, 44.0f};
    falling.position = Vec2{128.0f, 2.0f * TileMap::kTileSize};
    falling.velocity = Vec2{0.0f, 200.0f};
    bool landed      = false;
    for (int step = 0; step < 240 && !landed; ++step) {
        landed = moveBody(map, falling, Era::Present, 1.0f / 60.0f).landed;
    }
    CHECK(landed);
    CHECK(falling.bottom() == doctest::Approx(4.0f * TileMap::kTileSize));
    CHECK(falling.onGround);
}

TEST_CASE("geometry changes under a resting body are resolved, not fatal")
{
    TileMap map = room(16, 8, 4);
    Body body = standingOn(4);
    moveBody(map, body, Era::Present, 1.0f / 60.0f);
    CHECK(body.onGround);

    // The era shifts and the tile the body was standing on stops existing, so
    // the body is now floating above a hole it will fall into.
    map.set(2, 4, Tile::of(TileKind::Crumble));
    map.set(3, 4, Tile::of(TileKind::Crumble));

    // In the Present those cells are empty; the body must not be stuck inside
    // anything and must start falling.
    for (int step = 0; step < 60; ++step) {
        resolvePenetration(map, body, Era::Present);
        body.velocity.y += 1500.0f / 60.0f;
        moveBody(map, body, Era::Present, 1.0f / 60.0f);
    }
    CHECK_FALSE(map.rectBlocked(body.rect(), Era::Present));
    CHECK(body.bottom() > kFloor);
}

TEST_CASE("resolvePenetration pushes a body out of solid geometry")
{
    TileMap map;
    map.resize(8, 8);
    map.set(4, 4, Tile::of(TileKind::Solid));

    Body body;
    body.size     = Vec2{28.0f, 44.0f};
    body.position = Vec2{4.0f * TileMap::kTileSize + 4.0f, 4.0f * TileMap::kTileSize};

    resolvePenetration(map, body, Era::Present);
    CHECK_FALSE(map.rectBlocked(body.rect(), Era::Present));

    // A body already in the clear is left exactly where it is.
    const Vec2 clear{64.0f, 320.0f};
    body.position = clear;
    resolvePenetration(map, body, Era::Present);
    CHECK(body.position.x == doctest::Approx(clear.x));
    CHECK(body.position.y == doctest::Approx(clear.y));
}

TEST_CASE("the era decides what counts as a wall")
{
    TileMap map;
    map.resize(16, 8);
    map.set(6, 4, Tile::of(TileKind::Crumble));
    map.set(6, 5, Tile::of(TileKind::Crumble));

    Body body;
    body.size     = Vec2{32.0f, 32.0f};
    body.position = Vec2{5.0f * TileMap::kTileSize, 4.0f * TileMap::kTileSize};
    body.velocity = Vec2{600.0f, 0.0f};

    CHECK(moveBody(map, body, Era::Past, 1.0f / 60.0f).wallRight);
    CHECK(body.right() <= 6.0f * TileMap::kTileSize + 0.001f);

    // The same wall in the Present is not there at all.
    Body open = body;
    open.velocity   = Vec2{600.0f, 0.0f};
    const MoveResult through = moveBody(map, open, Era::Present, 1.0f / 60.0f);
    CHECK_FALSE(through.wallRight);
    CHECK(open.position.x > body.position.x);
}

TEST_CASE("a non-finite or zero step is ignored rather than corrupting the body")
{
    const TileMap map = room(16, 8, 4);
    Body body = standingOn(4);

    body.velocity = Vec2{100.0f, 0.0f};
    moveBody(map, body, Era::Present, 0.0f);
    CHECK(body.position.x == doctest::Approx(64.0f));

    moveBody(map, body, Era::Present, -1.0f);
    CHECK(body.position.x == doctest::Approx(64.0f));

    // A NaN velocity is dropped rather than written into position, which would
    // be permanent.
    body.velocity = Vec2{std::numeric_limits<float>::quiet_NaN(), 0.0f};
    moveBody(map, body, Era::Present, 1.0f / 60.0f);
    CHECK(std::isfinite(body.position.x));
    CHECK(std::isfinite(body.position.y));
}

TEST_CASE("falling out of the world is detectable from the grid's bounds")
{
    // No floor at all: the body falls straight through and out of the level.
    // This is what makes a pit fatal rather than a soft-lock, so it needs to be
    // detectable without the tile map having to be solid all round.
    TileMap map;
    map.resize(6, 6);

    Body body;
    body.size     = Vec2{28.0f, 44.0f};
    body.position = Vec2{32.0f, 32.0f};
    body.velocity = Vec2{0.0f, 400.0f};

    for (int step = 0; step < 240; ++step) {
        moveBody(map, body, Era::Present, 1.0f / 60.0f);
        if (body.position.y > map.bounds().bottom()) {
            break;
        }
    }

    CHECK(body.position.y > map.bounds().bottom());
    CHECK(std::isfinite(body.position.y));
}
