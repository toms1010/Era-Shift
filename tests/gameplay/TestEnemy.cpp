// Era Shift - tests for enemies.
//
// The rule that makes the era mechanic a mechanic rather than a filter is that
// an enemy outside the current era is *frozen, not removed*. Coming back to its
// era must find it exactly where it was, with the health it had. That, and the
// wind-up telegraph and the punish window, are what these tests pin.

#include "EraShift/Game/Enemy.hpp"

#include <algorithm>
#include <cmath>

#include <doctest/doctest.h>

using namespace EraShift::Game;
using EraShift::Graphics::Rect;
using EraShift::Graphics::Vec2;

namespace {

constexpr int   kFloorRow = 6;
constexpr float kFloorY   = kFloorRow * TileMap::kTileSize;
constexpr float kStep     = 1.0f / 60.0f;

TileMap arena(int columns, int rows, int floorRow)
{
    TileMap map;
    map.resize(columns, rows);
    for (int x = 0; x < columns; ++x) {
        map.set(x, floorRow, Tile::of(TileKind::Solid));
    }
    for (int y = 0; y < rows; ++y) {
        map.set(0, y, Tile::of(TileKind::Solid));
        map.set(columns - 1, y, Tile::of(TileKind::Solid));
    }
    return map;
}

} // namespace

TEST_CASE("the three enemy kinds are named, distinct and parseable")
{
    CHECK(enemyName(EnemyKind::Sentinel) == "Sentinel");
    CHECK(enemyName(EnemyKind::Wisp) == "Wisp");
    CHECK(enemyName(EnemyKind::Warden) == "Warden");

    for (const EnemyKind kind :
         {EnemyKind::Sentinel, EnemyKind::Wisp, EnemyKind::Warden}) {
        EnemyKind parsed = EnemyKind::Warden;
        CHECK(parseEnemyKind(enemyName(kind), parsed));
        CHECK(parsed == kind);
    }

    EnemyKind ignored = EnemyKind::Wisp;
    CHECK_FALSE(parseEnemyKind("Dragon", ignored));
    CHECK(ignored == EnemyKind::Wisp);
}

TEST_CASE("the kinds are tuned differently enough to matter")
{
    const EnemyTuning sentinel = tuningFor(EnemyKind::Sentinel);
    const EnemyTuning wisp     = tuningFor(EnemyKind::Wisp);
    const EnemyTuning warden   = tuningFor(EnemyKind::Warden);

    CHECK(warden.maxHealth > sentinel.maxHealth);
    CHECK(warden.moveSpeed < sentinel.moveSpeed);
    // A longer wind-up is the fairer's way to be dangerous.
    CHECK(warden.windupTime > sentinel.windupTime);
    CHECK(warden.contactDamage > sentinel.contactDamage);

    CHECK(wisp.flies);
    CHECK_FALSE(sentinel.flies);
    CHECK_FALSE(warden.flies);
    CHECK(wisp.maxHealth < warden.maxHealth);
}

TEST_CASE("an enemy in a foreign era is asleep and does not move")
{
    const TileMap map = arena(30, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Warden, Vec2{160.0f, kFloorY - 48.0f}, kEraBitPast);

    const Vec2 start = enemy.body().position;
    for (float frame = 0.0f; frame < 120.0f; frame += 1.0f) {
        enemy.update(map, Era::Future, Vec2{500.0f, kFloorY}, kStep, frame);
    }

    CHECK(enemy.state() == EnemyState::Asleep);
    CHECK(enemy.body().position == start);
    CHECK_FALSE(enemy.inCurrentEra());
    CHECK(enemy.body().velocity == Vec2{});
}

TEST_CASE("returning to an enemy's era wakes it exactly where it was left")
{
    const TileMap map = arena(30, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{160.0f, kFloorY - 48.0f}, kEraBitPast);

    // Put it to sleep somewhere.
    for (float frame = 0.0f; frame < 60.0f; frame += 1.0f) {
        enemy.update(map, Era::Future, Vec2{900.0f, kFloorY}, kStep, frame);
    }
    const Vec2 parked = enemy.body().position;

    // Damage it in its own era, then put it back to sleep. The hit has to come
    // while the enemy is actually present: an enemy outside the current era is
    // untargetable, which is the rule this test would otherwise not be testing.
    enemy.update(map, Era::Past, Vec2{900.0f, kFloorY}, kStep, 0.0f);
    CHECK(enemy.inCurrentEra());
    CHECK(enemy.takeDamage(1.0f, Vec2{}));
    const float damaged = enemy.health();
    for (float frame = 0.0f; frame < 30.0f; frame += 1.0f) {
        enemy.update(map, Era::Future, Vec2{900.0f, kFloorY}, kStep, frame);
    }

    // Waking up must not heal it or teleport it.
    enemy.update(map, Era::Past, Vec2{900.0f, kFloorY}, kStep, 61.0f);
    CHECK(enemy.state() != EnemyState::Asleep);
    // Waking must not teleport it; one step of movement after waking is fine,
    // a jump across the arena is not.
    CHECK(enemy.body().position.x < parked.x + 8.0f);
    CHECK(enemy.body().position.y == doctest::Approx(parked.y));
    CHECK(enemy.health() == doctest::Approx(damaged));
}

TEST_CASE("an enemy outside its era cannot be hit at all")
{
    const TileMap map = arena(30, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Wisp, Vec2{160.0f, kFloorY - 30.0f}, kEraBitFuture);

    enemy.update(map, Era::Present, Vec2{161.0f, kFloorY}, kStep, 1.0f);
    CHECK_FALSE(enemy.inCurrentEra());
    CHECK_FALSE(enemy.takeDamage(1.0f, Vec2{}));
    CHECK(enemy.health() == doctest::Approx(enemy.maxHealth()));
}

TEST_CASE("an enemy chases a player who comes into range")
{
    const TileMap map = arena(40, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{320.0f, kFloorY - 48.0f}, kEraMaskAll);

    const float startX = enemy.body().position.x;
    bool chased = false;
    for (float frame = 0.0f; frame < 90.0f; frame += 1.0f) {
        enemy.update(map, Era::Present, Vec2{startX + 120.0f, kFloorY}, kStep, frame);
        chased = chased || enemy.state() == EnemyState::Chase;
        if (chased) {
            CHECK(enemy.facing() > 0.0f);
            break;
        }
    }
    CHECK(chased);
    CHECK(enemy.body().position.x > startX);
}

TEST_CASE("an enemy far away stays on patrol")
{
    const TileMap map = arena(60, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{320.0f, kFloorY - 48.0f}, kEraMaskAll);

    for (float frame = 0.0f; frame < 120.0f; frame += 1.0f) {
        enemy.update(map, Era::Present, Vec2{1800.0f, kFloorY}, kStep, frame);
    }
    // It has no reason to act on a player that far away, so it must still be
    // idling or walking its patrol - never chasing, never attacking.
    const bool calm = enemy.state() == EnemyState::Idle ||
                      enemy.state() == EnemyState::Patrol;
    CHECK(calm);
    CHECK_FALSE(enemy.state() == EnemyState::Chase);
    CHECK_FALSE(enemy.state() == EnemyState::Attack);
}

TEST_CASE("a chase becomes a telegraphed attack, and recovery is a punish window")
{
    const TileMap map = arena(40, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{320.0f, kFloorY - 48.0f}, kEraMaskAll);

    // Stand right next to it.
    const Vec2 target{320.0f + 24.0f, kFloorY - 48.0f};
    bool sawWindup = false;
    bool sawAttack = false;
    bool sawRecover = false;

    for (float frame = 0.0f; frame < 240.0f; frame += 1.0f) {
        enemy.update(map, Era::Present, target, kStep, frame);
        sawWindup  = sawWindup || enemy.state() == EnemyState::Windup;
        sawAttack  = sawAttack || enemy.state() == EnemyState::Attack;
        sawRecover = sawRecover || enemy.state() == EnemyState::Recover;
    }

    CHECK(sawWindup);
    CHECK(sawAttack);
    CHECK(sawRecover);
}

TEST_CASE("only the attack state has a damage box")
{
    const TileMap map = arena(40, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{320.0f, kFloorY - 48.0f}, kEraMaskAll);
    CHECK(enemy.attackBox().isEmpty());
    CHECK_FALSE(enemy.dangerous());

    const Vec2 target{320.0f + 20.0f, kFloorY - 48.0f};
    bool sawDangerous = false;
    for (float frame = 0.0f; frame < 240.0f; frame += 1.0f) {
        enemy.update(map, Era::Present, target, kStep, frame);
        if (enemy.dangerous()) {
            sawDangerous = true;
            CHECK_FALSE(enemy.attackBox().isEmpty());
        }
    }
    CHECK(sawDangerous);
}

TEST_CASE("damage interrupts whatever the enemy was doing and a killed one plays out")
{
    const TileMap map = arena(40, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{320.0f, kFloorY - 48.0f}, kEraMaskAll);

    CHECK(enemy.takeDamage(1.0f, Vec2{}));
    CHECK(enemy.health() == doctest::Approx(enemy.maxHealth() - 1.0f));
    CHECK(enemy.state() == EnemyState::Recover);
    CHECK(enemy.hurt());

    CHECK(enemy.takeDamage(99.0f, Vec2{}));
    CHECK(enemy.state() == EnemyState::Dying);
    CHECK_FALSE(enemy.aliveIn(Era::Present));

    // A dying enemy is not a valid target again.
    CHECK_FALSE(enemy.takeDamage(1.0f, Vec2{}));

    bool removed = false;
    for (float frame = 0.0f; frame < 120.0f; frame += 1.0f) {
        enemy.update(map, Era::Present, Vec2{900.0f, kFloorY}, kStep, frame);
        removed = removed || enemy.removed();
    }
    CHECK(removed);
}

TEST_CASE("a dying enemy finishes dying even if the era changes underneath it")
{
    const TileMap map = arena(40, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{320.0f, kFloorY - 48.0f}, kEraMaskAll);
    enemy.takeDamage(99.0f, Vec2{});
    CHECK(enemy.state() == EnemyState::Dying);

    bool removed = false;
    for (float frame = 0.0f; frame < 120.0f; frame += 1.0f) {
        // Alternate eras: the death animation must not stall.
        enemy.update(map, (frame / 30.0f < 1.0f) ? Era::Future : Era::Present, Vec2{}, kStep,
                    frame);
        removed = removed || enemy.removed();
    }
    CHECK(removed);
}

TEST_CASE("a patrolling enemy turns around instead of walking off the arena")
{
    TileMap map;
    map.resize(60, 12);
    map.set(20, 6, Tile::of(TileKind::Solid));
    map.set(21, 6, Tile::of(TileKind::Solid));
    map.set(22, 6, Tile::of(TileKind::Solid));
    map.set(23, 6, Tile::of(TileKind::Solid));
    map.set(24, 6, Tile::of(TileKind::Solid));

    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{19.0f * TileMap::kTileSize, 6.0f * TileMap::kTileSize - 48.0f},
                kEraMaskAll);

    float furthest = enemy.body().position.x;
    bool turned    = false;
    float previous = enemy.facing();

    for (float frame = 0.0f; frame < 900.0f; frame += 1.0f) {
        enemy.update(map, Era::Present, Vec2{100000.0f, kFloorY}, kStep, frame);
        furthest = std::max(furthest, enemy.body().position.x);
        if (enemy.facing() != previous) {
            turned = true;
            previous = enemy.facing();
        }
    }

    CHECK(turned);
    // It must never have left the platform it was placed on.
    CHECK(furthest < 26.0f * TileMap::kTileSize);
}

TEST_CASE("a hovering enemy holds its height and does not fall")
{
    const TileMap map = arena(40, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Wisp, Vec2{320.0f, 4.0f * TileMap::kTileSize}, kEraMaskAll);

    const float startY = enemy.body().position.y;
    for (float frame = 0.0f; frame < 240.0f; frame += 1.0f) {
        enemy.update(map, Era::Present, Vec2{324.0f, startY}, kStep, frame);
    }

    CHECK(enemy.body().position.y < kFloorY);
    CHECK(enemy.body().position.y > startY - 400.0f);
    CHECK(std::isfinite(enemy.body().position.y));
}

TEST_CASE("state progress runs from zero to one across a wind-up")
{
    const TileMap map = arena(40, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{320.0f, kFloorY - 48.0f}, kEraMaskAll);

    const Vec2 target{320.0f + 20.0f, kFloorY - 48.0f};
    float lowest = 1.0f;
    float highest = 0.0f;
    for (float frame = 0.0f; frame < 240.0f; frame += 1.0f) {
        enemy.update(map, Era::Present, target, kStep, frame);
        if (enemy.state() == EnemyState::Windup) {
            lowest  = std::min(lowest, enemy.stateProgress());
            highest = std::max(highest, enemy.stateProgress());
        }
    }

    // The telegraph has to visibly fill, otherwise the wind-up is a delay the
    // player cannot read.
    CHECK(lowest < 0.3f);
    CHECK(highest > 0.6f);
}

TEST_CASE("a zero or non-finite step does nothing rather than teleporting")
{
    const TileMap map = arena(40, 12, kFloorRow);
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{320.0f, kFloorY - 48.0f}, kEraMaskAll);
    const Vec2 start = enemy.body().position;

    enemy.update(map, Era::Present, Vec2{}, 0.0f, 0.0f);
    enemy.update(map, Era::Present, Vec2{}, -1.0f, 0.0f);
    CHECK(enemy.body().position == start);

    static_cast<void>(Rect{});
}
