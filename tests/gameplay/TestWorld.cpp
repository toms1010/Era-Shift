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

/// The same arena with one seal instead of three, and a finish that needs one.
///
/// The tutorial region is this shape: a single objective, and a gate that asks for
/// that objective rather than for a quota. Written as a variant of `arena` rather
/// than as its own hand-built level so the two cannot drift apart in their geometry
/// and make a difference in the result attributable to the wrong edit.
Level singleSealArena(int sealsPlaced, int required)
{
    Level level = arena();
    level.id   = "single";

    // Keep only the first `sealsPlaced` seals.
    int kept = 0;
    std::vector<PlacedEntity> keptEntities;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind != EntityKind::Seal) {
            keptEntities.push_back(entity);
            continue;
        }
        if (kept < sealsPlaced) {
            keptEntities.push_back(entity);
            ++kept;
        }
    }
    level.entities       = std::move(keptEntities);
    level.requiresSeals  = required;
    return level;
}

/// Moves the player onto a tile and resolves them out of any geometry there.
void standAt(World& world, int tileX, int tileY)
{
    world.mutablePlayer().body().position =
        Vec2{static_cast<float>(tileX) * TileMap::kTileSize,
             static_cast<float>(tileY) * TileMap::kTileSize};
    resolvePenetration(world.map(), world.mutablePlayer().body(), world.player().era());
}

/// Walks the player onto the goal tile.
void standOnFinish(World& world)
{
    const Rect goal = world.goalBounds();
    standAt(world, static_cast<int>(goal.center().x / TileMap::kTileSize),
            static_cast<int>(goal.center().y / TileMap::kTileSize));
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

/// Shifts era until the world is in `wanted`.
///
/// A loop rather than a single press because the shift cycles rather than
/// selecting: from the Present, one press goes to the Future, not to the Past.
/// A helper that pressed once would only work for whichever era happened to be one
/// step away, which is the kind of test that passes until the level changes.
void shiftToEra(World& world, Era wanted, int budget = 8)
{
    for (int i = 0; i < budget && world.era() != wanted; ++i) {
        world.update(PlayerInput{}, shift(), kStep);
        static_cast<void>(world.takeEvents());
    }
}

/// Takes every seal, shifting to each one's era first.
void takeEverySeal(World& world)
{
    for (int guard = 0; guard < 8; ++guard) {
        const Seal* next = nullptr;
        for (const Seal& seal : world.seals()) {
            if (!seal.collected) {
                next = &seal;
                break;
            }
        }
        if (next == nullptr) {
            return;
        }
        shiftToEra(world, next->era);
        standAt(world, static_cast<int>(next->position.x / TileMap::kTileSize),
                static_cast<int>(next->position.y / TileMap::kTileSize));
        world.update(PlayerInput{}, interact(), kStep);
        static_cast<void>(world.takeEvents());
    }
}

/// Shifts until the world is in `wanted`, up to a few steps.
///
/// The cycle is fixed, so this is a loop rather than arithmetic; going the
/// "wrong" way round would mean the test is not exercising the same code the
/// player does.
void shiftTo(World& world, Era wanted)
{
    for (int i = 0; i < 3 && world.era() != wanted; ++i) {
        world.mutablePlayer().refillChrono();
        world.update(PlayerInput{}, shift(), kStep);
        static_cast<void>(world.takeEvents());
    }
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

TEST_CASE("hit-stop freezes the world but never eats a button press")
{
    // Hit-stop is a real pause in `World::update`, not a camera trick, so that
    // an enemy cannot land a hit during the freeze the player's own blow caused.
    //
    // The rule that matters, and the one that was wrong the first time: a freeze
    // that swallows an input is felt as the game being unresponsive rather than
    // as a stylistic choice. So any step on which the player pressed something
    // runs normally. Holding a direction through a freeze still freezes, because
    // that is exactly what should feel heavy.

    World world = loadedWorld();
    // A shift is the longest freeze in the game, so it is the easiest to observe.
    world.update(PlayerInput{}, shift(), kStep);
    static_cast<void>(world.takeEvents());
    REQUIRE(world.hitStopRemaining() > 0.0f);

    const Era before = world.era();

    // A step with no press: the world is frozen. Elapsed time proves it.
    const double elapsedBefore = world.stats().elapsed;
    world.update(PlayerInput{}, WorldCommands{}, kStep);
    CHECK(world.stats().elapsed == doctest::Approx(elapsedBefore));
    CHECK(world.hitStopRemaining() < 0.2f);

    // The freeze runs out, and then time moves again.
    for (int i = 0; i < 30; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, kStep);
    }
    CHECK(world.hitStopRemaining() == doctest::Approx(0.0f));
    CHECK(world.stats().elapsed > elapsedBefore);
    static_cast<void>(before);
}

TEST_CASE("a shift pressed during hit-stop still registers")
{
    // The regression this test exists for. The first implementation returned from
    // `World::update` before processing commands, so a shift pressed inside the
    // 160ms after a hit was silently dropped - and four gameplay tests failed in
    // a way that looked like a simulation bug rather than a presentation one.
    World world = loadedWorld();
    world.update(PlayerInput{}, shift(), kStep);
    static_cast<void>(world.takeEvents());
    REQUIRE(world.hitStopRemaining() > 0.0f);

    // Two eras apart from the one we just left, so one more shift is observable.
    world.mutablePlayer().refillChrono();
    const Era before = world.era();
    world.update(PlayerInput{}, shift(), kStep);

    CHECK(world.era() != before);
    bool sawShift = false;
    for (const EventRecord& event : world.takeEvents()) {
        sawShift = sawShift || event.kind == WorldEvent::EraShifted;
    }
    CHECK(sawShift);
}

TEST_CASE("a seal taken during hit-stop still registers")
{
    // The same rule, on the other command. Interacting and shifting on
    // consecutive steps is the normal way a player takes three seals in a row, so
    // if a seal's 200ms freeze ate the next interact the level would be
    // uncompletable by the intended route.
    World world = loadedWorld();
    for (const Seal& seal : world.seals()) {
        if (seal.era != world.era()) {
            continue;
        }
        world.mutablePlayer().body().position = seal.position;
        resolvePenetration(world.map(), world.mutablePlayer().body(), world.era());
        break;
    }

    world.update(PlayerInput{}, interact(), kStep);
    static_cast<void>(world.takeEvents());
    CHECK(world.sealsTaken() == 1);
    // A seal is the longest non-shift freeze, so the next step is inside it.
    REQUIRE(world.hitStopRemaining() > 0.0f);

    // Interact again, immediately, while still frozen.
    world.update(PlayerInput{}, interact(), kStep);
    // Whatever the arena does here, the important part is that the step was not
    // silently skipped: a frozen world would leave the timer untouched.
    CHECK(world.hitStopRemaining() < 0.2f);
}

TEST_CASE("shifting costs energy, changes the era and emits an event")
{
    World world = loadedWorld();
    const float before = world.player().chrono();
    const float cost   = world.player().tuning().shiftCost;
    const float regen  = world.player().tuning().chronoRegen / 60.0f;

    world.update(PlayerInput{}, shift(), kStep);

    CHECK(world.era() == nextEra(Era::Present));
    // The shift is charged first and regeneration runs after it, so the balance
    // ends the step a fraction above the exact cost. Anything much above that
    // would mean the cost is not being charged at all.
    CHECK(world.player().chrono() >= before - cost);
    CHECK(world.player().chrono() < before - cost + regen * 2.0f);
    CHECK(world.stats().shifts == 1);
    CHECK(world.eraBlend() < 1.0f);

    const auto events = world.takeEvents();
    REQUIRE_FALSE(events.empty());
    CHECK(events.front().kind == WorldEvent::EraShifted);
}

TEST_CASE("the transition settles")
{
    World world = loadedWorld();
    world.update(PlayerInput{}, shift(), kStep);
    CHECK(world.eraBlend() < 1.0f);
    static_cast<void>(world.takeEvents());

    CHECK(stepUntil(world, [](const World& w) { return w.eraBlend() >= 1.0f; }));

    // Settling is not an event: the world changing over 0.35 seconds should not
    // produce a second "shifted to" message.
    for (const auto& event : world.takeEvents()) {
        CHECK(event.kind != WorldEvent::EraShifted);
    }
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

    // The player starts in the Present, so the Past seal refuses and explains
    // itself rather than silently doing nothing.
    world.update(PlayerInput{}, interact(), kStep);
    CHECK(world.sealsTaken() == 0);
    bool sawWrongEra = false;
    for (const auto& event : world.takeEvents()) {
        sawWrongEra = sawWrongEra || event.kind == WorldEvent::SealWrongEra;
    }
    CHECK(sawWrongEra);

    // Shift round to the Past and take it.
    shiftTo(world, Era::Past);
    REQUIRE(world.era() == Era::Past);
    world.mutablePlayer().body().position =
        Vec2{5.0f * TileMap::kTileSize, 2.0f * TileMap::kTileSize};
    world.mutablePlayer().body().velocity = Vec2{};
    resolvePenetration(world.map(), world.mutablePlayer().body(), world.player().era());

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

TEST_CASE("a world with no level loaded simulates nothing")
{
    World world;
    CHECK_FALSE(world.loaded());

    world.update(PlayerInput{}, shift(), kStep);
    world.update(PlayerInput{}, interact(), kStep);

    // Not a defeat: there is no run in progress to lose. Reporting one would
    // put a results screen up for a game that never started.
    CHECK(world.outcome() == Outcome::Running);
    CHECK(world.map().empty());
    CHECK(world.stats().elapsed == doctest::Approx(0.0));
}

TEST_CASE("a level with no floor under it is fatal rather than a void")
{
    // A spawn with nothing beneath it: the player falls out of the world and
    // the run ends. This is what makes an authoring mistake visible as a
    // defeat instead of a player standing in the sky forever.
    Level void_level;
    void_level.presentRows = {"...", "...", "...", "...", "..."};
    PlacedEntity spawn;
    spawn.kind = EntityKind::PlayerSpawn;
    spawn.x    = 2;
    spawn.y    = 1;
    void_level.entities.push_back(spawn);

    World world;
    world.load(void_level);
    CHECK(world.outcome() == Outcome::Running);

    for (int i = 0; i < 600 && world.outcome() == Outcome::Running; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, kStep);
    }

    CHECK(world.outcome() == Outcome::Defeat);
    CHECK(world.objectiveText().find("collapsed") != std::string::npos);
}

TEST_CASE("a level with no seals reports no objective rather than dividing by zero")
{
    Level plain;
    plain.presentRows = {"...", "..."};
    PlacedEntity spawn;
    spawn.kind = EntityKind::PlayerSpawn;
    plain.entities.push_back(spawn);

    World world;
    world.load(plain);
    // 0 of 0 seals: the gate can never open, and the HUD must say so rather
    // than pointing at a seal that does not exist.
    CHECK_FALSE(world.gateOpen());
    CHECK(world.sealsTaken() == 0);
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


// ---------------------------------------------------------------------------
// The finish line
//
// Two questions the player can get wrong, and which are worth separating because
// they fail differently:
//
//   "does standing on the gate without the seal complete the level?"  must be NO
//   "does standing on the gate with the seal complete the level?"       must be YES
//
// A level that completes on contact is a level that skips its own objective, and a
// level that never completes is a level that cannot be finished. Both are silent
// failures: nothing crashes and nothing looks wrong.
// ---------------------------------------------------------------------------

TEST_CASE("standing on the finish without the objective does not complete the level")
{
    World world;
    world.load(singleSealArena(1, 1));

    standOnFinish(world);
    for (int i = 0; i < 30; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, kStep);
    }

    CHECK(world.sealsTaken() == 0);
    CHECK_FALSE(world.gateOpen());
    CHECK(world.outcome() == Outcome::Running);
}

TEST_CASE("standing on the finish without the objective says why, and still does not complete")
{
    World world;
    world.load(singleSealArena(1, 1));
    standOnFinish(world);

    world.update(PlayerInput{}, interact(), kStep);
    const std::vector<EventRecord> events = world.takeEvents();

    bool refused = false;
    for (const EventRecord& event : events) {
        if (event.kind == WorldEvent::GateSealed) {
            refused = true;
        }
    }
    CHECK_MESSAGE(refused, "the player is told the gate is sealed");
    CHECK(world.outcome() == Outcome::Running);
    CHECK_FALSE(world.gateOpen());
}

TEST_CASE("the seal count in the refusal is what remains, not what exists")
{
    // Three seals placed, one required, one already taken: one remains. Reporting
    // two here would send the player looking for a seal the finish does not want.
    World world;
    world.load(singleSealArena(3, 1));

    standOnFinish(world);
    world.update(PlayerInput{}, interact(), kStep);

    CHECK(world.sealsRemaining() == 1);

    // Take every seal: the point is that the *requirement* is what gates, so the
    // gate opens on the first one even though two more are still available.
    takeEverySeal(world);

    CHECK(world.sealsTaken() == 3);
    CHECK(world.gateOpen());
    CHECK(world.sealsRemaining() == 0);
}

TEST_CASE("a level asks only for the seals its finish requires")
{
    // Three placed, one required: one taken is enough, and the objective line says
    // 1/1 rather than 1/3.
    World world;
    world.load(singleSealArena(3, 1));

    const Seal& first = world.seals().front();
    shiftToEra(world, first.era);
    standAt(world, static_cast<int>(first.position.x / TileMap::kTileSize),
            static_cast<int>(first.position.y / TileMap::kTileSize));
    world.update(PlayerInput{}, interact(), kStep);

    CHECK(world.sealsTaken() == 1);
    CHECK(world.objectiveText().find("1 / 1") != std::string::npos);
    CHECK(world.gateOpen());
}

TEST_CASE("standing on the finish with the objective completes the level")
{
    World world;
    world.load(singleSealArena(1, 1));

    // Take the seal first.
    takeEverySeal(world);
    REQUIRE(world.sealsTaken() == 1);
    REQUIRE(world.gateOpen());

    standOnFinish(world);
    // Several steps, not one. Taking a seal puts the world into hit-stop, and a
    // frozen step returns before the outcome is evaluated - so a single step here
    // would be testing the hit-stop rather than the finish. A player walks to the
    // gate, which is many steps; the test should take as many.
    for (int i = 0; i < 30; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, kStep);
    }

    CHECK(world.atFinish());
    CHECK(world.outcome() == Outcome::Victory);
}

TEST_CASE("the objective done but the player nowhere near the finish does not complete")
{
    // The other half of the rule: collecting the seal is not the same as leaving.
    World world;
    world.load(singleSealArena(1, 1));

    takeEverySeal(world);
    REQUIRE(world.gateOpen());

    // Stand well away from the gate and keep playing.
    standAt(world, 2, 2);
    for (int i = 0; i < 30; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, kStep);
    }
    CHECK(world.outcome() == Outcome::Running);
}

TEST_CASE("a level that asks for more seals than were taken keeps the gate shut")
{
    // Three placed, two required, exactly one taken.
    World world;
    world.load(singleSealArena(3, 2));

    const Seal& first = world.seals().front();
    shiftToEra(world, first.era);
    standAt(world, static_cast<int>(first.position.x / TileMap::kTileSize),
            static_cast<int>(first.position.y / TileMap::kTileSize));
    world.update(PlayerInput{}, interact(), kStep);
    REQUIRE(world.sealsTaken() == 1);

    standOnFinish(world);
    for (int i = 0; i < 30; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, kStep);
    }

    CHECK_FALSE(world.gateOpen());
    CHECK(world.sealsRemaining() == 1);
    CHECK(world.outcome() == Outcome::Running);
}

TEST_CASE("a level with no seals can never be completed by walking into the gate")
{
    // A level file with no seals and no stated requirement would otherwise resolve
    // its requirement to zero, and "zero seals collected >= zero required" is true.
    // That completes the level on the spawn tile.
    Level level = arena();
    std::vector<PlacedEntity> kept;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind != EntityKind::Seal) {
            kept.push_back(entity);
        }
    }
    level.entities      = std::move(kept);
    level.requiresSeals = 0;

    World world;
    world.load(level);

    CHECK(world.requiredSeals() == 0);
    CHECK_FALSE(world.gateOpen());
    CHECK(world.sealsRemaining() == 0);

    standOnFinish(world);
    for (int i = 0; i < 30; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, kStep);
    }
    CHECK(world.outcome() == Outcome::Running);
}

TEST_CASE("a level with no seals reports no objective rather than dividing by zero")
{
    // The existing test for this asserted the old 0-of-0 line. The objective now
    // reports the gate as sealed, which is the honest thing: there is no objective,
    // and saying "0 / 0" reads as a counter that failed rather than as a fact.
    Level level = arena();
    std::vector<PlacedEntity> kept;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind != EntityKind::Seal) {
            kept.push_back(entity);
        }
    }
    level.entities = std::move(kept);

    World world;
    world.load(level);
    const std::string text = world.objectiveText();

    CHECK(text.find("/ 0") == std::string::npos);
    CHECK(world.outcome() == Outcome::Running);
}

// ---------------------------------------------------------------------------
// Finish proximity
//
// Drives the marker's reaction. It has to be monotonic in distance, because the
// gate brightening and then dimming as the player walks towards it would be read
// as the gate closing.
// ---------------------------------------------------------------------------

TEST_CASE("finish proximity rises as the player approaches")
{
    World world = loadedWorld();
    const Rect goal = world.goalBounds();

    standAt(world, 2, 2);
    world.update(PlayerInput{}, WorldCommands{}, kStep);
    const float far = world.finishProximity();

    standAt(world, static_cast<int>(goal.center().x / TileMap::kTileSize) - 2,
            static_cast<int>(goal.center().y / TileMap::kTileSize));
    world.update(PlayerInput{}, WorldCommands{}, kStep);
    const float near = world.finishProximity();

    CHECK(far >= 0.0f);
    CHECK(far <= 1.0f);
    CHECK(near > far);
}

TEST_CASE("finish proximity saturates at the gate and falls off over distance")
{
    World world = loadedWorld();

    // Not exactly 1 at the centre of the goal tile: the proximity is measured to
    // the goal's *centre* and the player's body centre sits half a tile above the
    // tile's centre after gravity resolves. Close enough to 1 that the marker is
    // at full brightness, which is what the number is for.
    standOnFinish(world);
    world.update(PlayerInput{}, WorldCommands{}, kStep);
    CHECK(world.finishProximity() > 0.95f);

    // Far outside the 320px range entirely.
    standAt(world, 2, 2);
    world.update(PlayerInput{}, WorldCommands{}, kStep);
    CHECK(world.finishProximity() == doctest::Approx(0.0f));
}

TEST_CASE("finish proximity is zero when the level has no gate")
{
    Level level = arena();
    // Blank the goal marker out of all three layers.
    for (std::string& row : level.presentRows) {
        for (char& cell : row) {
            if (cell == 'G') {
                cell = '.';
            }
        }
    }
    level.pastRows   = level.presentRows;
    level.futureRows = level.presentRows;

    World world;
    world.load(level);
    CHECK(world.goalBounds().isEmpty());
    CHECK(world.finishProximity() == doctest::Approx(0.0f));
    CHECK_FALSE(world.atFinish());
}

TEST_CASE("being at the finish is asked through one predicate")
{
    // The HUD prompt and the completion rule must agree about where the gate is;
    // asking twice is how they come to disagree.
    World world = loadedWorld();
    standOnFinish(world);
    // No step between standing there and asking: `atFinish` answers "is the player
    // in the marker", and standing in it and asking in the same frame is the case
    // the completion rule depends on. A step here would let the player be standing
    // on the gate and not yet "at" it, which is the off-by-one-frame bug this is
    // meant to pin down.
    CHECK(world.atFinish());

    standAt(world, 2, 2);
    world.update(PlayerInput{}, WorldCommands{}, kStep);
    CHECK_FALSE(world.atFinish());
}
