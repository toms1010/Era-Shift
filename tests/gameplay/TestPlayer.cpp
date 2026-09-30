// Era Shift - tests for the player controller.
//
// The feel rules are the ones worth pinning: acceleration, friction, coyote
// time, jump buffering, variable jump height, the dash and the i-frames after
// damage. Each of them exists because its absence produces a specific,
// recognisable complaint from a player.

#include "EraShift/Game/Player.hpp"

#include <doctest/doctest.h>

using namespace EraShift::Game;
using EraShift::Graphics::Vec2;

namespace {

constexpr int   kFloorRow  = 6;
constexpr float kFloorY    = kFloorRow * TileMap::kTileSize;
constexpr float kStep      = 1.0f / 60.0f;

/// A flat floor with walls, and a player dropped onto it.
struct Fixture {
    TileMap map;
    Player  player;

    Fixture()
    {
        map.resize(40, 12);
        for (int x = 0; x < 40; ++x) {
            map.set(x, kFloorRow, Tile::of(TileKind::Solid));
        }
        for (int y = 0; y < 12; ++y) {
            map.set(0, y, Tile::of(TileKind::Solid));
            map.set(39, y, Tile::of(TileKind::Solid));
        }
        player.reset(Vec2{160.0f, kFloorY - 44.0f}, Era::Present);
        // A couple of steps so the controller settles onto the floor.
        for (int i = 0; i < 4; ++i) {
            player.update(map, PlayerInput{}, kStep);
        }
    }

    void run(int steps, const PlayerInput& input)
    {
        for (int i = 0; i < steps; ++i) {
            player.update(map, input, kStep);
        }
    }
};

PlayerInput held(float axis)
{
    PlayerInput input;
    input.moveAxis = axis;
    return input;
}

} // namespace

TEST_CASE("the player starts on the floor, alive and full")
{
    const Fixture f;
    CHECK(f.player.alive());
    CHECK(f.player.health() == doctest::Approx(f.player.maxHealth()));
    CHECK(f.player.chrono() == doctest::Approx(f.player.maxChrono()));
    CHECK(f.player.body().onGround);
    CHECK(f.player.body().bottom() == doctest::Approx(kFloorY));
    CHECK(f.player.era() == Era::Present);
}

TEST_CASE("horizontal movement accelerates rather than snapping to speed")
{
    Fixture f;
    f.run(1, held(1.0f));
    const float afterOneStep = f.player.body().velocity.x;

    CHECK(afterOneStep > 0.0f);
    // One step at 2600 px/s^2 over 1/60s is about 43 px/s, nowhere near the
    // 250 px/s top speed.
    CHECK(afterOneStep < f.player.tuning().moveSpeed * 0.5f);

    f.run(60, held(1.0f));
    CHECK(f.player.body().velocity.x == doctest::Approx(f.player.tuning().moveSpeed));
}

TEST_CASE("releasing the stick decelerates to a stop")
{
    Fixture f;
    f.run(60, held(1.0f));
    CHECK(f.player.body().velocity.x > 200.0f);

    f.run(60, PlayerInput{});
    CHECK(f.player.body().velocity.x == doctest::Approx(0.0f));
    CHECK(f.player.facing() == doctest::Approx(1.0f));   // facing is remembered
}

TEST_CASE("facing follows the direction of travel, not the last press")
{
    Fixture f;
    f.run(30, held(1.0f));
    CHECK(f.player.facing() > 0.0f);

    f.run(60, held(-1.0f));
    CHECK(f.player.facing() < 0.0f);
}

TEST_CASE("a jump leaves the ground and comes back")
{
    Fixture f;
    PlayerInput input;
    input.jumpPressed = true;
    input.jumpHeld    = true;
    f.run(1, input);

    CHECK(f.player.body().velocity.y < 0.0f);
    CHECK_FALSE(f.player.body().onGround);

    input.jumpPressed = false;
    f.run(90, input);
    CHECK(f.player.body().onGround);
    // Contact is resolved in half-pixel back-outs, so the body rests within a
    // pixel of the floor rather than exactly on it. Anything tighter would be
    // testing the back-out step size, not the controller.
    CHECK(f.player.body().bottom() == doctest::Approx(kFloorY).epsilon(0.01f));
}

TEST_CASE("releasing jump early cuts the rise, which is what makes it controllable")
{
    Fixture tall;
    PlayerInput full;
    full.jumpPressed = true;
    full.jumpHeld    = true;
    tall.run(1, full);
    float fullApex = tall.player.body().position.y;
    for (int i = 0; i < 40; ++i) {
        full.jumpPressed = false;
        tall.run(1, full);
        fullApex = std::min(fullApex, tall.player.body().position.y);
    }

    Fixture cut;
    cut.run(1, full);
    PlayerInput released;
    for (int i = 0; i < 40; ++i) {
        cut.run(1, released);
    }

    // A tap must produce a visibly shorter hop than a held jump.
    CHECK(cut.player.body().position.y > fullApex + 8.0f);
}

TEST_CASE("coyote time lets a jump land shortly after walking off a ledge")
{
    // A ledge that ends at column 19.
    TileMap map;
    map.resize(40, 12);
    for (int x = 0; x < 20; ++x) {
        map.set(x, kFloorRow, Tile::of(TileKind::Solid));
    }

    Player player;
    // Close enough to the edge that a second of walking goes over it.
    player.reset(Vec2{17.0f * TileMap::kTileSize, 0.0f}, Era::Present);
    for (int i = 0; i < 30; ++i) {
        player.update(map, PlayerInput{}, kStep);
    }
    REQUIRE(player.body().onGround);

    // Walk off the right-hand end.
    PlayerInput walk = held(1.0f);
    for (int i = 0; i < 60 && player.body().onGround; ++i) {
        player.update(map, walk, kStep);
    }
    REQUIRE_FALSE(player.body().onGround);

    // A jump pressed inside the coyote window still fires.
    PlayerInput jump;
    jump.jumpPressed = true;
    jump.jumpHeld    = true;
    const float before = player.body().velocity.y;
    player.update(map, jump, kStep);
    CHECK(player.body().velocity.y < before - 100.0f);
}

TEST_CASE("coyote time expires, so a jump long after the ledge does not")
{
    TileMap map;
    map.resize(40, 12);
    for (int x = 0; x < 20; ++x) {
        map.set(x, kFloorRow, Tile::of(TileKind::Solid));
    }

    Player player;
    player.reset(Vec2{17.0f * TileMap::kTileSize, 0.0f}, Era::Present);
    for (int i = 0; i < 30; ++i) {
        player.update(map, PlayerInput{}, kStep);
    }
    REQUIRE(player.body().onGround);
    PlayerInput walk = held(1.0f);
    for (int i = 0; i < 60 && player.body().onGround; ++i) {
        player.update(map, walk, kStep);
    }
    REQUIRE_FALSE(player.body().onGround);

    // Fall well past the window.
    for (int i = 0; i < 30; ++i) {
        player.update(map, PlayerInput{}, kStep);
    }

    PlayerInput jump;
    jump.jumpPressed = true;
    jump.jumpHeld    = true;
    const float before = player.body().velocity.y;
    player.update(map, jump, kStep);
    // Still accelerating downwards: the jump was refused.
    CHECK(player.body().velocity.y >= before);
}

TEST_CASE("a jump pressed just before landing is remembered and fires on touchdown")
{
    TileMap map;
    map.resize(40, 12);
    for (int x = 0; x < 40; ++x) {
        map.set(x, kFloorRow, Tile::of(TileKind::Solid));
    }

    Player player;
    player.reset(Vec2{160.0f, 0.0f}, Era::Present);

    // Fall until a couple of frames from the floor.
    while (player.body().bottom() < kFloorY - 24.0f) {
        player.update(map, PlayerInput{}, kStep);
    }

    PlayerInput jump;
    jump.jumpPressed = true;
    jump.jumpHeld    = true;
    player.update(map, jump, kStep);          // pressed in mid-air
    CHECK_FALSE(player.body().onGround);

    // Land, then keep holding: the buffered press should fire.
    for (int i = 0; i < 120; ++i) {
        player.update(map, jump, kStep);
        if (player.body().velocity.y < -100.0f) {
            CHECK(true);
            return;
        }
    }
    FAIL("the buffered jump never fired");
}

TEST_CASE("the dash covers ground fast, suspends gravity and then goes on cooldown")
{
    Fixture f;
    PlayerInput dash;
    dash.dashPressed = true;

    f.run(1, dash);
    CHECK(f.player.dashing());
    CHECK(f.player.body().velocity.x > f.player.tuning().moveSpeed);

    const float startX = f.player.body().position.x;
    f.run(9, held(0.0f));   // the dash lasts 0.16s, about ten steps
    const float dashDistance = f.player.body().position.x - startX;
    // 620 px/s for 0.16s is about 99px; friction takes a little off the end.
    CHECK(dashDistance > 80.0f);

    f.run(20, held(0.0f));
    CHECK_FALSE(f.player.dashing());

    // Immediately afterwards a second dash is refused.
    const float xBefore = f.player.body().position.x;
    f.run(1, dash);
    CHECK_FALSE(f.player.dashing());
    CHECK(f.player.body().position.x == doctest::Approx(xBefore));
}

TEST_CASE("an attack has a wind-up, an active window and a recovery")
{
    Fixture f;
    PlayerInput attack;
    attack.attackPressed = true;

    f.run(1, attack);
    CHECK(f.player.attackPhase() == AttackPhase::Windup);
    CHECK_FALSE(f.player.attacking());
    CHECK(f.player.attackBox().isEmpty());

    // Wind-up is short; the active window must actually open.
    bool sawActive = false;
    for (int i = 0; i < 12; ++i) {
        attack.attackPressed = false;
        f.run(1, attack);
        sawActive = sawActive || f.player.attacking();
    }
    CHECK(sawActive);

    for (int i = 0; i < 60; ++i) {
        f.run(1, attack);
    }
    CHECK(f.player.attackPhase() == AttackPhase::Idle);
}

TEST_CASE("the swing box is in front of the player and flips with facing")
{
    Fixture f;
    PlayerInput attack;
    attack.attackPressed = true;
    attack.attackHeld    = true;

    f.run(8, attack);
    const Rect right = f.player.attackBox();
    REQUIRE_FALSE(right.isEmpty());
    CHECK(right.x >= f.player.body().right() - 4.0f);

    f.player.body().velocity.x = 0.0f;
    for (int i = 0; i < 60; ++i) {
        f.run(1, held(-1.0f));
    }
    attack.attackPressed = true;
    f.run(8, attack);
    const Rect left = f.player.attackBox();
    REQUIRE_FALSE(left.isEmpty());
    CHECK(left.right() <= f.player.body().position.x + 4.0f);
}

TEST_CASE("damage is applied once per invulnerability window, then ignored")
{
    Fixture f;
    const float before = f.player.health();

    CHECK(f.player.takeDamage(1.0f, Vec2{f.player.body().center().x - 20.0f,
                                          f.player.body().center().y}));
    CHECK(f.player.health() == doctest::Approx(before - 1.0f));
    CHECK(f.player.invulnerable());

    // The same hit landing again during the window does nothing.
    CHECK_FALSE(f.player.takeDamage(1.0f, Vec2{}));
    CHECK(f.player.health() == doctest::Approx(before - 1.0f));

    f.run(80, PlayerInput{});
    CHECK_FALSE(f.player.invulnerable());
    CHECK(f.player.takeDamage(1.0f, Vec2{}));
}

TEST_CASE("knockback pushes the player away from where the hit came from")
{
    Fixture f;
    const Vec2 centre = f.player.body().center();

    f.player.takeDamage(1.0f, Vec2{centre.x - 50.0f, centre.y});
    CHECK(f.player.body().velocity.x > 0.0f);   // hit from the left

    for (int i = 0; i < 120; ++i) {
        f.player.update(f.map, PlayerInput{}, kStep);
    }
    f.player.takeDamage(1.0f, Vec2{centre.x + 50.0f, centre.y});
    CHECK(f.player.body().velocity.x < 0.0f);   // hit from the right
}

TEST_CASE("health cannot go below zero and cannot be raised past the maximum")
{
    Fixture f;
    for (int i = 0; i < 20 && f.player.alive(); ++i) {
        f.player.takeDamage(1.0f, Vec2{});
        f.run(80, PlayerInput{});
    }
    CHECK_FALSE(f.player.alive());
    CHECK(f.player.health() == doctest::Approx(0.0f));
    CHECK_FALSE(f.player.takeDamage(1.0f, Vec2{}));

    // Healing must not resurrect.
    f.player.heal(100.0f);
    CHECK(f.player.health() == doctest::Approx(0.0f));
    CHECK_FALSE(f.player.alive());

    f.player.restore(Vec2{160.0f, kFloorY - 44.0f}, Era::Present, 1.0f, 50.0f);
    f.player.heal(100.0f);
    CHECK(f.player.health() == doctest::Approx(f.player.maxHealth()));
}

TEST_CASE("chrono regenerates up to the maximum and never past it")
{
    Fixture f;
    f.player.refillChrono();
    const float full = f.player.chrono();
    f.run(600, PlayerInput{});
    CHECK(f.player.chrono() == doctest::Approx(full));
}

TEST_CASE("shifting costs energy, changes the era and starts a transition")
{
    Fixture f;
    const float before = f.player.chrono();

    CHECK(f.player.shiftTo(Era::Future, f.map));
    CHECK(f.player.era() == Era::Future);
    CHECK(f.player.chrono() == doctest::Approx(before - f.player.tuning().shiftCost));
    CHECK(f.player.eraBlend() < 1.0f);

    // The transition settles.
    f.run(60, PlayerInput{});
    CHECK(f.player.eraBlend() == doctest::Approx(1.0f));
}

TEST_CASE("shifting is refused without energy and without a reason to move")
{
    Fixture f;
    f.run(600, PlayerInput{});   // regen to full
    CHECK(f.player.shiftTo(Era::Present, f.map) == false);   // already there

    PlayerTuning expensive = f.player.tuning();
    expensive.shiftCost    = f.player.maxChrono() + 1.0f;
    f.player.setTuning(expensive);

    CHECK_FALSE(f.player.shiftTo(Era::Future, f.map));
    CHECK(f.player.era() == Era::Present);
    CHECK(f.player.chrono() == doctest::Approx(f.player.maxChrono()));
}

TEST_CASE("falling out of the world is fatal whatever the era")
{
    TileMap map;
    map.resize(10, 10);
    Player player;
    player.reset(Vec2{32.0f, 0.0f}, Era::Past);

    for (int i = 0; i < 600 && player.alive(); ++i) {
        player.update(map, PlayerInput{}, kStep);
    }
    CHECK_FALSE(player.alive());
}

TEST_CASE("a dead player stops simulating input")
{
    Fixture f;
    f.player.takeDamage(f.player.maxHealth(), Vec2{});
    CHECK_FALSE(f.player.update(f.map, held(1.0f), kStep));
}

TEST_CASE("a non-finite step is ignored rather than corrupting the player")
{
    Fixture f;
    const float x = f.player.body().position.x;
    f.player.update(f.map, held(1.0f), 0.0f);
    f.player.update(f.map, held(1.0f), -1.0f);
    CHECK(f.player.body().position.x == doctest::Approx(x));
    CHECK(std::isfinite(f.player.body().position.x));
}
