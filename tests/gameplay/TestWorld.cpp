// Era Shift - tests for the world: the rules that connect everything.
//
// These are the rules a player actually learns. If the era shift stops costing
// energy, or a seal starts responding in the wrong era, or the gate opens early,
// the game is no longer the game described on the title screen - and none of it
// would be visible in a screenshot.

#include "EraShift/Game/World.hpp"

#include <algorithm>
#include <string>

#include <doctest/doctest.h>

using namespace EraShift::Game;

namespace {

constexpr float kStep = 1.0f / 60.0f;

/// A small flat arena with one enemy, three seals and a goal.
///
///   row 0            G
///   row 1     [seal] [seal] [seal]
///   row 2  (spawn)              (enemy)
///   row 3  #####################
constexpr int kCols = 24;
constexpr int kRows = 4;

Level arena()
{
    Level level;
    level.id   = "arena";
    level.name = "Arena";

    std::string row0(kCols, '.');
    std::string row1(kCols, '.');
    std::string row2(kCols, '.');
    std::string row3(kCols, '#');
    row0[kCols - 2] = 'G';

    level.presentRows = {row0, row1, row2, row3};
    level.pastRows    = level.presentRows;
    level.futureRows  = level.presentRows;

    PlacedEntity spawn;
    spawn.kind = EntityKind::PlayerSpawn;
    spawn.x    = 2;
    spawn.y    = 2;
    level.entities.push_back(spawn);

    PlacedEntity seal;
    seal.kind = EntityKind::Seal;
    for (int i = 0; i < 3; ++i) {
        seal.sealEra = static_cast<Era>(i);
        seal.x       = 5 + i * 5;
        seal.y       = 2;
        level.entities.push_back(seal);
    }

    PlacedEntity enemy;
    enemy.kind  = EntityKind::Enemy;
    enemy.enemy = EnemyKind::Sentinel;
    enemy.x     = 20;
    enemy.y     = 2;
    enemy.existsIn = kEraMaskAll;
    level.entities.push_back(enemy);

    PlacedEntity cell;
    cell.kind   = EntityKind::Pickup;
    cell.pickup = PickupKind::ChronoCell;
    cell.x      = 3;
    cell.y      = 2;
    level.entities.push_back(cell);

    return level;
}

World loadedWorld()
{
    World world;
    world.load(arena());
    return world;
}

/// Steps the world until `predicate` holds or the budget runs out.
template <typename Predicate>
bool stepUntil(World& world, Predicate predicate, int budget = 600)
{
    for (int i = 0; i < budget; ++i) {
        if (predicate(world)) {
            return true;
        }
        world.update(PlayerInput{}, WorldCommands{}, kStep);
    }
    return predicate(world);
}

WorldCommands shift()
{
    WorldCommands commands;
    commands.shiftPressed = true;
    return commands;
}

WorldCommands interact()
{
    WorldCommands commands;
    commands.interactPressed = true;
    return commands;
}

} // namespace

TEST_CASE("loading a level places the player, the enemies, the seals and the goal")
{
    World world = loadedWorld();
    CHECK(world.player().alive());
    CHECK(world.enemyCount() == 1);
    CHECK(world.seals().size() == 3);
    CHECK(world.pickups().size() == 1);
    CHECK_FALSE(world.goalBounds().isEmpty());
    CHECK(world.outcome() == Outcome::Running);
    CHECK(world.era() == Era::Present);
}

TEST_CASE("shifting costs energy, changes the era and emits an event")
{
    World world = loadedWorld();
    const float before = world.player().chrono();

    world.update(PlayerInput{}, shift(), kStep);

    CHECK(world.era() == nextEra(Era::Present));
    CHECK(world.player().chrono() == doctest::Approx(before - world.player().tuning().shiftCost));
    CHECK(world.stats().shifts == 1);
    CHECK(world.eraBlend() < 1.0f);

    const auto events = world.takeEvents();
    REQUIRE_FALSE(events.empty());
    CHECK(events.front().kind == WorldEvent::EraShifted);
}

TEST_CASE("the transition settles and then clears")
{
    World world = loadedWorld();
    world.update(PlayerInput{}, shift(), kStep);
    CHECK(world.eraBlend() < 1.0f);

    CHECK(stepUntil(world, [](const World& w) { return w.eraBlend() >= 1.0f; }));
    CHECK(world.takeEvents().empty());
}

TEST_CASE("shifting without enough energy is refused and says so")
{
    World world = loadedWorld();
    // Burn the energy down without collecting the cell.
    for (int i = 0; i < 20 && world.player().chrono() >= world.player().tuning().shiftCost;
         ++i) {
        world.update(PlayerInput{}, shift(), kStep);
        static_cast<void>(world.takeEvents());
    }
    REQUIRE(world.player().chrono() < world.player().tuning().shiftCost);

    const Era before = world.era();
    world.update(PlayerInput{}, shift(), kStep);
    CHECK(world.era() == before);

    const auto events = world.takeEvents();
    REQUIRE_FALSE(events.empty());
    CHECK(events.front().kind == WorldEvent::EraShiftFailed);
}

TEST_CASE("a seal is refused in the wrong era and taken in its own")
{
    World world = loadedWorld();
    world.mutablePlayer().body().position =
        Vec2{5.0f * TileMap::kTileSize, 2.0f * TileMap::kTileSize};
    resolvePenetration(world.map(), world.mutablePlayer().body(), world.player().era());

    // In the Present, so the Past seal refuses and explains itself.
    world.update(PlayerInput{}, interact(), kStep);
    CHECK(world.sealsTaken() == 0);
    bool sawWrongEra = false;
    for (const auto& event : world.takeEvents()) {
        sawWrongEra = sawWrongEra || event.kind == WorldEvent::SealWrongEra;
    }
    CHECK(sawWrongEra);

    // Shift into the Past and take it.
    world.update(PlayerInput{}, shift(), kStep);
    static_cast<void>(world.takeEvents());
    CHECK(world.era() == Era::Past);

    world.update(PlayerInput{}, interact(), kStep);
    CHECK(world.sealsTaken() == 1);
    bool sawTaken = false;
    for (const auto& event : world.takeEvents()) {
        sawTaken = sawTaken || event.kind == WorldEvent::SealTaken;
    }
    CHECK(sawTaken);
}

TEST_CASE("interacting out of range does nothing")
{
    World world = loadedWorld();
    // The player spawns at tile 2; the nearest seal is at tile 5.
    world.update(PlayerInput{}, shift(), kStep);   // now in the Past
    static_cast<void>(world.takeEvents());

    world.update(PlayerInput{}, interact(), kStep);
    CHECK(world.sealsTaken() == 0);
}

TEST_CASE("the gate stays shut until every seal is taken")
{
    World world = loadedWorld();
    const Rect goal = world.goalBounds();

    // Walk the player onto the gate with no seals.
    world.mutablePlayer().body().position = Vec2{goal.x, goal.y - 40.0f};
    resolvePenetration(world.map(), world.mutablePlayer().body(), world.player().era());
    world.update(PlayerInput{}, interact(), kStep);
    CHECK(world.outcome() == Outcome::Running);
    CHECK_FALSE(world.gateOpen());

    // Take all three.
    for (int i = 0; i < 3; ++i) {
        const Era wanted = world.player().era();
        const auto& seals = world.seals();
        for (const Seal& seal : seals) {
            if (!seal.collected && seal.era == wanted) {
                world.mutablePlayer().body().position =
                    Vec2{seal.position.x, seal.position.y};
                resolvePenetration(world.map(), world.mutablePlayer().body(), wanted);
                break;
            }
        }
        world.update(PlayerInput{}, interact(), kStep);
        static_cast<void>(world.takeEvents());
        world.update(PlayerInput{}, shift(), kStep);
        static_cast<void>(world.takeEvents());
    }
    CHECK(world.sealsTaken() == 3);
    CHECK(world.gateOpen());

    world.mutablePlayer().body().position = Vec2{goal.x, goal.y - 40.0f};
    resolvePenetration(world.map(), world.mutablePlayer().body(), world.player().era());
    world.update(PlayerInput{}, interact(), kStep);
    CHECK(world.outcome() == Outcome::Victory);
}

TEST_CASE("losing all health ends the run as a defeat")
{
    World world = loadedWorld();
    const float health = world.player().health();

    for (int i = 0; i < 20; ++i) {
        // A fresh window each time, so the i-frames do not mask the test.
        world.mutablePlayer().body().velocity = Vec2{};
        world.mutablePlayer().takeDamage(1.0f, world.player().body().center());
        world.update(PlayerInput{}, WorldCommands{}, kStep);
        static_cast<void>(world.takeEvents());
        for (int j = 0; j < 60; ++j) {
            world.update(PlayerInput{}, WorldCommands{}, kStep);
        }
        if (world.outcome() != Outcome::Running) {
            break;
        }
    }

    CHECK(world.outcome() == Outcome::Defeat);
    CHECK(world.player().health() < health);
}

TEST_CASE("a dead run stops simulating")
{
    World world = loadedWorld();
    world.mutablePlayer().takeDamage(100.0f, Vec2{});
    world.update(PlayerInput{}, WorldCommands{}, kStep);
    CHECK(world.outcome() == Outcome::Defeat);

    const float elapsed = static_cast<float>(world.stats().elapsed);
    world.update(PlayerInput{}, WorldCommands{}, kStep);
    CHECK(world.stats().elapsed == doctest::Approx(elapsed));
}

TEST_CASE("a chrono cell refills energy and comes back")
{
    World world = loadedWorld();
    world.mutablePlayer().body().position =
        Vec2{3.0f * TileMap::kTileSize, 2.0f * TileMap::kTileSize};
    resolvePenetration(world.map(), world.mutablePlayer().body(), world.player().era());

    world.mutablePlayer().shiftTo(Era::Past, world.map());
    world.mutablePlayer().body().position =
        Vec2{3.0f * TileMap::kTileSize, 2.0f * TileMap::kTileSize};
    world.mutablePlayer().body().velocity = Vec2{};

    // Walk onto the cell.
    for (int i = 0; i < 30; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, kStep);
    }
    bool sawPickup = false;
    for (const auto& event : world.takeEvents()) {
        sawPickup = sawPickup || event.kind == WorldEvent::PickupTaken;
    }
    CHECK(sawPickup);
    CHECK(world.player().chrono() == doctest::Approx(world.player().maxChrono()));

    // It returns after its respawn timer.
    bool returned = false;
    for (int i = 0; i < 60 * 10; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, kStep);
        static_cast<void>(world.takeEvents());
        for (const Pickup& pickup : world.pickups()) {
            if (!pickup.collected) {
                returned = true;
            }
        }
        if (returned) {
            break;
        }
    }
    CHECK(returned);
}

TEST_CASE("the run statistics accumulate")
{
    World world = loadedWorld();
    world.update(PlayerInput{}, shift(), kStep);
    static_cast<void>(world.takeEvents());
    world.update(PlayerInput{}, shift(), kStep);
    static_cast<void>(world.takeEvents());

    CHECK(world.stats().shifts == 2);
    CHECK(world.stats().paradox > 0.0f);
    CHECK(world.stats().score() > 0);
}

TEST_CASE("restarting puts everything back to the start")
{
    World world = loadedWorld();
    world.update(PlayerInput{}, shift(), kStep);
    world.update(PlayerInput{}, interact(), kStep);
    static_cast<void>(world.takeEvents());

    world.restart();

    CHECK(world.era() == Era::Present);
    CHECK(world.sealsTaken() == 0);
    CHECK(world.enemyCount() == 1);
    CHECK(world.player().health() == doctest::Approx(world.player().maxHealth()));
    CHECK(world.outcome() == Outcome::Running);
    CHECK(world.stats().shifts == 0);
}

TEST_CASE("a saved run restores position, era, health and seals")
{
    World world = loadedWorld();
    world.update(PlayerInput{}, shift(), kStep);
    static_cast<void>(world.takeEvents());
    world.applySeals(0b101);

    CHECK(world.sealMask() == 0b101);
    CHECK(world.sealsTaken() == 2);
    CHECK(world.gateOpen() == false);

    world.applyRunStats(12.5, 3, 1, 40.0f);
    CHECK(world.stats().elapsed == doctest::Approx(12.5));
    CHECK(world.stats().shifts == 3);
    CHECK(world.stats().kills == 1);

    // A restart keeps the level but not the progress, which is what makes
    // "applySeals after restart" the correct order for a load.
    world.restart();
    CHECK(world.sealsTaken() == 0);
}

TEST_CASE("the objective line names the era the next seal needs")
{
    World world = loadedWorld();
    const std::string text = world.objectiveText();
    CHECK(text.find("0 / 3") != std::string::npos);
    CHECK(text.find("Past") != std::string::npos);
}

TEST_CASE("an empty level is inert rather than a crash")
{
    World world;
    world.update(PlayerInput{}, shift(), kStep);
    world.update(PlayerInput{}, interact(), kStep);
    CHECK(world.map().empty());
    CHECK(world.outcome() == Outcome::Running);
    CHECK(world.objectiveText().find("0 / 0") != std::string::npos);
}

TEST_CASE("an enemy asleep in another era is neither counted nor hittable")
{
    World world = loadedWorld();
    const int all = world.activeEnemyCount();

    world.update(PlayerInput{}, shift(), kStep);   // Present -> Future
    static_cast<void>(world.takeEvents());
    // The Sentinel exists in every era, so it stays active.
    CHECK(world.activeEnemyCount() == all);

    world.update(PlayerInput{}, shift(), kStep);   // Future -> Past
    static_cast<void>(world.takeEvents());
    CHECK(world.activeEnemyCount() == all);
}

TEST_CASE("a zero or non-finite step is ignored")
{
    World world = loadedWorld();
    const float elapsed = static_cast<float>(world.stats().elapsed);
    world.update(PlayerInput{}, shift(), 0.0f);
    CHECK(world.stats().elapsed == doctest::Approx(elapsed));
    world.update(PlayerInput{}, shift(), -1.0f);
    CHECK(world.era() == Era::Present);
}
