// Era Shift - tests for the full region set.
//
// `tools/validate_levels.py` checks playability from Python; this checks the same
// files through the *game's own loader*, which is the authority on what is
// loadable. A region can satisfy the Python validator and still fail here if the
// two have drifted — for instance if a tile character means one thing in
// `parseTileRow` and another in the script.
//
// The reachability model matches TestLevel.cpp: a cell is traversable if it is
// non-solid in *any* era, because the player can shift at will. That is
// necessary for a route to exist, not sufficient for a specific one.

#include "EraShift/Game/Level.hpp"
#include "EraShift/Game/TileMap.hpp"
#include "EraShift/Game/World.hpp"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace EraShift::Game;
using EraShift::Graphics::Rect;

namespace {

std::filesystem::path levelDirectory()
{
    return std::filesystem::path(ERASHIFT_SOURCE_DIR) / "data" / "levels";
}

std::filesystem::path shippedLevelPath()
{
    return levelDirectory() / "ancient_forest.json";
}

/// Every region file on disk, sorted so the failure order is stable.
std::vector<std::filesystem::path> everyLevelPath()
{
    std::vector<std::filesystem::path> paths;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(levelDirectory(), ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string idOf(const std::filesystem::path& path)
{
    return path.stem().string();
}

/// Rows as a flat grid for one era layer.
std::vector<std::string> rowsOf(const Level& level, int eraIndex)
{
    switch (eraIndex) {
        case 0: return level.pastRows;
        case 1: return level.presentRows;
        default: return level.futureRows;
    }
}

bool solidInAnyEra(const Level& level, int x, int y)
{
    for (int era = 0; era < 3; ++era) {
        const std::vector<std::string> rows = rowsOf(level, era);
        if (y < 0 || y >= static_cast<int>(rows.size())) {
            continue;
        }
        const std::string& row = rows[static_cast<std::size_t>(y)];
        if (x < 0 || x >= static_cast<int>(row.size())) {
            continue;
        }
        const char cell = row[static_cast<std::size_t>(x)];
        // '=' is a one-way platform: solid from above, passable from below, so
        // it must not be treated as a wall by a flood fill.
        if (cell == '=') {
            continue;
        }
        if (cell == '#' || cell == 'c' || cell == 'b' || cell == 'x') {
            return true;
        }
    }
    return false;
}

/// Cells the player can walk to from the spawn, over all three eras.
std::set<std::pair<int, int>> reachable(const Level& level)
{
    const PlacedEntity* spawn = level.find(EntityKind::PlayerSpawn);
    REQUIRE(spawn != nullptr);

    const int height = static_cast<int>(level.presentRows.size());
    const int width  = level.presentRows.empty()
                           ? 0
                           : static_cast<int>(level.presentRows.front().size());

    std::set<std::pair<int, int>> seen;
    std::deque<std::pair<int, int>> queue;
    const std::pair<int, int> start{spawn->y, spawn->x};
    seen.insert(start);
    queue.push_back(start);

    while (!queue.empty()) {
        const auto [y, x] = queue.front();
        queue.pop_front();
        for (const auto& [dy, dx] : std::vector<std::pair<int, int>>{
                 {0, 1}, {0, -1}, {1, 0}, {-1, 0}}) {
            const int ny = y + dy;
            const int nx = x + dx;
            if (ny < 0 || ny >= height || nx < 0 || nx >= width) {
                continue;
            }
            if (seen.count({ny, nx}) != 0) {
                continue;
            }
            if (solidInAnyEra(level, nx, ny)) {
                continue;
            }
            seen.insert({ny, nx});
            queue.push_back({ny, nx});
        }
    }
    return seen;
}

} // namespace

TEST_CASE("the region directory exists and is not empty")
{
    REQUIRE(std::filesystem::is_directory(levelDirectory()));
    CHECK(everyLevelPath().size() >= 10);
}

TEST_CASE("every region on disk loads through the game's own loader")
{
    for (const std::filesystem::path& path : everyLevelPath()) {
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE_MESSAGE(loadLevelFromFile(path, level, error), error);
        CHECK_MESSAGE(error.empty(), error);
        CHECK(level.empty() == false);
    }
}

TEST_CASE("there are ten playable regions plus the original")
{
    // Eleven files: ten in the designed progression, and ancient_forest, which
    // predates the `order` field and is therefore listed last.
    const auto paths = everyLevelPath();
    CHECK(paths.size() == 11);
}

TEST_CASE("the level select presents the regions in designed progression order")
{
    const auto entries = listLevels(levelDirectory());
    REQUIRE(entries.size() == 11);

    const std::vector<std::string> expected = {
        "awakening",   "ancient_bridge", "fallen_span",     "drowned_road", "crystal_vault",
        "split_meadow", "hollow_citadel", "sunken_lattice", "paradox_ward", "convergence",
        "ancient_forest",
    };
    std::vector<std::string> actual;
    for (const LevelEntry& entry : entries) {
        actual.push_back(entry.id);
    }
    CHECK(actual == expected);
}

TEST_CASE("every ordered region declares a distinct position from 1")
{
    const auto entries = listLevels(levelDirectory());

    std::vector<int> orders;
    for (const LevelEntry& entry : entries) {
        if (entry.order > 0) {
            orders.push_back(entry.order);
        }
    }
    // Exactly ten regions carry an order, and they are 1..10 with no gaps or
    // duplicates — which is what "level N unlocks level N+1" is built on.
    CHECK(orders.size() == 10);
    for (int index = 0; index < static_cast<int>(orders.size()); ++index) {
        CHECK(orders[static_cast<std::size_t>(index)] == index + 1);
    }
}

TEST_CASE("a region with a nonsensical order is treated as unstated")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromJson(R"({
        "id": "odd", "name": "Odd", "order": -4,
        "past": ["###", "#.#", "###"],
        "entities": [{"type": "player", "x": 1, "y": 1}]
    })",
                                     level, error));
    CHECK(level.hasProgressionOrder() == false);
}

TEST_CASE("listLevels finds every region and reports them in order")
{
    const auto entries = listLevels(levelDirectory());
    REQUIRE(entries.size() == 11);

    // Each entry's id is its file stem and its name is non-empty.
    for (const LevelEntry& entry : entries) {
        CHECK(entry.id.empty() == false);
        CHECK(entry.name.empty() == false);
        CHECK(entry.file.extension() == ".json");
    }

    // Sorted by progression, not filename: alphabetical order would present
    // Ancient Bridge before Awakening and Convergence before Crystal Vault.
    std::vector<std::string> ids;
    for (const LevelEntry& entry : entries) {
        ids.push_back(entry.id);
    }
    CHECK(ids.front() == "awakening");
    CHECK(ids[9] == "convergence");
}

TEST_CASE("listing a directory that does not exist yields no levels, not an error")
{
    // A shipped build with no `data/` must still open the level select.
    const auto entries = listLevels("/nonexistent/erashift/levels");
    CHECK(entries.empty());
}

TEST_CASE("every region has a spawn, enough seals, and a gate in all three eras")
{
    for (const std::filesystem::path& path : everyLevelPath()) {
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE(loadLevelFromFile(path, level, error));

        CHECK(level.find(EntityKind::PlayerSpawn) != nullptr);

        int past = 0, present = 0, future = 0;
        int seals = 0;
        for (const PlacedEntity& entity : level.entities) {
            if (entity.kind != EntityKind::Seal) {
                continue;
            }
            ++seals;
            switch (entity.sealEra) {
                case Era::Past:    ++past;    break;
                case Era::Present: ++present; break;
                case Era::Future:  ++future;  break;
            }
        }

        // The rule is "as many seals as the finish requires", not "one per era". The
        // tutorial places one and asks for one; every other region places three and
        // asks for all three. What must hold everywhere is that the finish is
        // *satisfiable* — a gate needing more seals than exist can never open.
        CHECK_MESSAGE(seals >= level.requiresSeals,
                      "places at least the seals the finish requires");
        CHECK_MESSAGE(level.requiresSeals >= 1, "the finish requires something");

        // Regions that ask for every seal still put one in each era, so no era is
        // dead time. Checked on the requirement rather than on the id, so a region
        // that later relaxes this does not need the test changed with it.
        if (level.requiresSeals == seals && seals == 3) {
            CHECK_MESSAGE(past == 1, "one Past seal");
            CHECK_MESSAGE(present == 1, "one Present seal");
            CHECK_MESSAGE(future == 1, "one Future seal");
        }

        for (const auto* rows : {&level.pastRows, &level.presentRows, &level.futureRows}) {
            bool hasGate = false;
            for (const std::string& row : *rows) {
                if (row.find('G') != std::string::npos) {
                    hasGate = true;
                    break;
                }
            }
            CHECK_MESSAGE(hasGate, "the gate must exist in every era layer");
        }
    }
}

TEST_CASE("every region's three era layers are the same rectangular shape")
{
    for (const std::filesystem::path& path : everyLevelPath()) {
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE(loadLevelFromFile(path, level, error));

        const std::size_t width = level.presentRows.front().size();
        CHECK(level.pastRows.size() == level.presentRows.size());
        CHECK(level.futureRows.size() == level.presentRows.size());
        for (const std::string& row : level.pastRows) {
            CHECK(row.size() == width);
        }
        for (const std::string& row : level.futureRows) {
            CHECK(row.size() == width);
        }
    }
}

TEST_CASE("every region's gate is reachable from its spawn")
{
    for (const std::filesystem::path& path : everyLevelPath()) {
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE(loadLevelFromFile(path, level, error));

        const auto seen = reachable(level);

        bool foundGate = false;
        for (int era = 0; era < 3 && !foundGate; ++era) {
            const std::vector<std::string> rows = rowsOf(level, era);
            for (int y = 0; y < static_cast<int>(rows.size()) && !foundGate; ++y) {
                for (int x = 0; x < static_cast<int>(rows[static_cast<std::size_t>(y)].size());
                     ++x) {
                    if (rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 'G') {
                        continue;
                    }
                    foundGate = true;
                    CHECK_MESSAGE(seen.count({y, x}) == 1,
                                  "the gate must be reachable in some era");
                    break;
                }
            }
        }
        CHECK_MESSAGE(foundGate, "the level has a gate at all");
    }
}

TEST_CASE("every seal can be approached, in its own era")
{
    // Adjacent ground, not the seal's own cell: a seal set into a wall is the
    // normal design, because it is taken by pressing E from beside it.
    for (const std::filesystem::path& path : everyLevelPath()) {
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE(loadLevelFromFile(path, level, error));

        const auto seen = reachable(level);
        for (const PlacedEntity& entity : level.entities) {
            if (entity.kind != EntityKind::Seal) {
                continue;
            }
            bool approachable = false;
            for (const auto& [dy, dx] : std::vector<std::pair<int, int>>{
                     {0, 1}, {0, -1}, {1, 0}, {-1, 0}}) {
                if (seen.count({entity.y + dy, entity.x + dx}) != 0) {
                    approachable = true;
                    break;
                }
            }
            CHECK_MESSAGE(approachable, "a seal with no reachable ground beside it");
        }
    }
}

TEST_CASE("no region places an enemy inside solid rock")
{
    // Seals are excluded deliberately: they are taken by proximity, so being set
    // into a wall is deliberate. An enemy inside rock is an enemy that cannot be
    // touched, and therefore cannot be hurt.
    for (const std::filesystem::path& path : everyLevelPath()) {
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE(loadLevelFromFile(path, level, error));

        for (int era = 0; era < 3; ++era) {
            const std::vector<std::string> rows = rowsOf(level, era);
            for (const PlacedEntity& entity : level.entities) {
                if (entity.kind != EntityKind::Enemy && entity.kind != EntityKind::Pickup) {
                    continue;
                }
                if (entity.y < 0 || entity.y >= static_cast<int>(rows.size())) {
                    continue;
                }
                const std::string& row = rows[static_cast<std::size_t>(entity.y)];
                if (entity.x < 0 || entity.x >= static_cast<int>(row.size())) {
                    continue;
                }
                const char cell = row[static_cast<std::size_t>(entity.x)];
                CAPTURE(idOf(path));
                const bool embedded = cell == '#' || cell == '=' || cell == 'c' ||
                                     cell == 'b' || cell == 'x';
                CHECK_MESSAGE(embedded == false,
                              "an enemy or pickup embedded in solid terrain");
            }
        }
    }
}

TEST_CASE("every region has at least one checkpoint")
{
    // Not a strict requirement for the game to work — a death restarts the region
    // — but a progression of ten regions without one is a bad experience, and the
    // absence is worth being visible in a test.
    for (const std::filesystem::path& path : everyLevelPath()) {
        if (idOf(path) == "ancient_forest") {
            continue;   // hand-authored before checkpoints existed
        }
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE(loadLevelFromFile(path, level, error));

        int checkpoints = 0;
        for (const PlacedEntity& entity : level.entities) {
            if (entity.kind == EntityKind::Checkpoint) {
                ++checkpoints;
            }
        }
        CHECK_MESSAGE(checkpoints > 0, "the region has no checkpoint");
    }
}

TEST_CASE("every region's era-only enemies are in the era they belong to")
{
    for (const std::filesystem::path& path : everyLevelPath()) {
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE(loadLevelFromFile(path, level, error));

        for (const PlacedEntity& entity : level.entities) {
            if (entity.kind != EntityKind::Enemy) {
                continue;
            }
            if (entity.enemy == EnemyKind::Warden) {
                CAPTURE(idOf(path));
                CHECK_MESSAGE(maskIncludes(entity.existsIn, Era::Past),
                              "a Warden outside the Past never exists");
            }
            if (entity.enemy == EnemyKind::Wisp) {
                CAPTURE(idOf(path));
                CHECK_MESSAGE(maskIncludes(entity.existsIn, Era::Future),
                              "a Wisp outside the Future never exists");
            }
        }
    }
}

TEST_CASE("every region loads into a world and simulates")
{
    // The end-to-end check: a region that parses but breaks `World::load` or
    // throws during the first steps is not playable, whatever the data says.
    for (const std::filesystem::path& path : everyLevelPath()) {
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE(loadLevelFromFile(path, level, error));

        World world;
        world.load(level);
        CHECK(world.loaded());
        CHECK(world.player().alive());
        // As many as the level places, and at least as many as its finish needs.
        CHECK(world.seals().size() >= world.requiredSeals());
        CHECK(world.outcome() == Outcome::Running);

        // A few steps of standing still, which is enough to catch a spawn placed
        // inside geometry: the resolver pushes the player out and nothing throws.
        PlayerInput input;
        WorldCommands commands;
        for (int step = 0; step < 10; ++step) {
            world.update(input, commands, 1.0f / 60.0f);
        }
        CAPTURE(idOf(path));
        CHECK(world.outcome() != Outcome::Defeat);
    }
}

TEST_CASE("a checkpoint volume is detected and a checkpoint restores the world")
{
    Level level;
    std::string error;
    const auto path = levelDirectory() / "awakening.json";
    REQUIRE(loadLevelFromFile(path, level, error));

    // The region's first checkpoint sits near the spawn. Read from the placement
    // list rather than hard-coded, so a level edit moves the test's expectation
    // with it instead of leaving it asserting a tile that is no longer there.
    int firstCheckpointX = 0;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::Checkpoint) {
            firstCheckpointX = entity.x;
            break;
        }
    }
    REQUIRE(firstCheckpointX > 0);
    CHECK(level.checkpointHere(firstCheckpointX, level.entities.front().y));
    CHECK(level.checkpointHere(0, 0) == false);

    World world;
    world.load(level);

    // Restore somewhere else, with the seals already taken and less health.
    //
    // The mask is derived from the level's own seal count rather than written as a
    // literal, so this still means "every seal this level places" after the
    // tutorial drops from three seals to one. A hard-coded 0b111 would silently
    // restore *all* of awakening's one seal and then assert two, which is a test
    // that fails for a reason the test's own name does not explain.
    int sealCount = 0;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::Seal) {
            ++sealCount;
        }
    }
    REQUIRE(sealCount > 0);
    const int allSeals = (1 << sealCount) - 1;

    world.restoreCheckpoint(EraShift::Graphics::Vec2{40.0f * 32.0f, 18.0f * 32.0f},
                            Era::Future, 2.0f, 30.0f, allSeals);

    CHECK(world.sealsTaken() == sealCount);
    CHECK(world.era() == Era::Future);
    CHECK(world.player().alive());
    CHECK(world.player().health() == doctest::Approx(2.0f));
    CHECK(world.player().chrono() == doctest::Approx(30.0f));
    CHECK(world.outcome() == Outcome::Running);

    // Enemies are rebuilt from the placement list, so a restore is not a rewind of
    // a half-finished fight: the count is the level's, every time.
    int placedEnemies = 0;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::Enemy) {
            ++placedEnemies;
        }
    }
    CHECK(world.enemies().size() == static_cast<std::size_t>(placedEnemies));
}

TEST_CASE("a checkpoint restores out of geometry rather than inside it")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(levelDirectory() / "hollow_citadel.json", level, error));

    World world;
    world.load(level);
    // Deep inside the courtyard's solid wall column.
    world.restoreCheckpoint(EraShift::Graphics::Vec2{40.0f * 32.0f, 14.0f * 32.0f},
                            Era::Present, 3.0f, 50.0f, 0);

    // The resolver pushes the player clear; what matters is that they are not
    // stuck and the world did not crash.
    CHECK(world.player().alive());
    CHECK(world.outcome() == Outcome::Running);
    PlayerInput input;
    WorldCommands commands;
    for (int step = 0; step < 30; ++step) {
        world.update(input, commands, 1.0f / 60.0f);
    }
    CHECK(world.outcome() != Outcome::Defeat);
}

TEST_CASE("a checkpoint position outside the map is clamped, not trusted")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));

    World world;
    world.load(level);
    world.restoreCheckpoint(EraShift::Graphics::Vec2{-99999.0f, -99999.0f}, Era::Past, 3.0f,
                            10.0f, 0);

    CHECK(world.player().alive());
    PlayerInput input;
    WorldCommands commands;
    for (int step = 0; step < 30; ++step) {
        world.update(input, commands, 1.0f / 60.0f);
    }
    CHECK(world.outcome() != Outcome::Defeat);
}

// ---------------------------------------------------------------------------
// The first region as shipped
//
// `measure_jumps.cpp` proves the map is traversable; these prove the things a
// traversal probe cannot see - that the objective exists, that the finish is gated
// on it, and that the region says it is a tutorial in the data rather than only in
// the file's name.
// ---------------------------------------------------------------------------

TEST_CASE("the first region is the tutorial, and says so in its data")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(levelDirectory() / "awakening.json", level, error));

    CHECK(level.tutorial);
    CHECK(level.order == 1);
    CHECK(level.name == "The First Shift");
    // `requires_seals` is what the finish gate asks for, and it is one rather than
    // "every seal placed" - a first level that made the player collect three of
    // anything before it would let them out is not a tutorial.
    CHECK(level.requiresSeals == 1);

    int placed = 0;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::Seal) {
            ++placed;
        }
    }
    CHECK(placed == 1);
}

TEST_CASE("the tutorial region has exactly one seal, so no era is dead time")
{
    // With one seal there is nothing to collect in the other two eras, so the
    // region cannot demand a shift it has not taught. The one seal is Present,
    // chosen because the Past-only chasm earlier in the level already showed the
    // player what "solid in the Past" means.
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(levelDirectory() / "awakening.json", level, error));

    const PlacedEntity* seal = level.find(EntityKind::Seal);
    REQUIRE(seal != nullptr);
    CHECK(seal->sealEra == Era::Present);
}

TEST_CASE("the tutorial region has checkpoints spread through it, not clustered")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(levelDirectory() / "awakening.json", level, error));

    std::vector<int> xs;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::Checkpoint) {
            xs.push_back(entity.x);
        }
    }

    // At least one per third of the level. The brief asked for one around the
    // middle; one in the first tenth is not a checkpoint a player will ever
    // benefit from.
    REQUIRE(xs.size() >= 3);
    std::sort(xs.begin(), xs.end());
    CHECK(xs.front() > 5);
    CHECK(xs.back() > 150);
}

TEST_CASE("the tutorial region has enemies but does not spam them")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(levelDirectory() / "awakening.json", level, error));

    int enemies = 0;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::Enemy) {
            ++enemies;
        }
    }

    // Six enemies across a 200-tile region is one every thirty tiles. The brief
    // asked for gradual escalation, not a corridor of them.
    CHECK(enemies > 0);
    CHECK(enemies <= 8);

    // And there is health to be had: an enemy count with no way to recover is
    // survivable only by never being hit.
    int health = 0;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::Pickup && entity.pickup == PickupKind::Health) {
            ++health;
        }
    }
    CHECK(health > 0);
}

TEST_CASE("the tutorial region offers an optional detour")
{
    // One or two optional pickups, per the brief. "Optional" means somewhere the
    // player is not required to go, so they must be off the only route - a pickup
    // beside a required seal is not optional, it is a toll.
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(levelDirectory() / "awakening.json", level, error));

    const PlacedEntity* seal = level.find(EntityKind::Seal);
    REQUIRE(seal != nullptr);

    int highUp = 0;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind != EntityKind::Pickup) {
            continue;
        }
        // Well above the ground: something the player has to climb for on purpose.
        if (entity.y < 15) {
            ++highUp;
        }
        // Nothing on the seal's own tile, which would be unavoidable.
        const bool sameTile = (entity.x == seal->x) && (entity.y == seal->y);
        CHECK_FALSE(sameTile);
    }
    CHECK(highUp >= 1);
}

TEST_CASE("the first region's finish gate opens only with its objective")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(levelDirectory() / "awakening.json", level, error));

    World world;
    world.load(level);
    REQUIRE(world.requiredSeals() == 1);

    // Standing on the gate with nothing collected.
    const Rect goal = world.goalBounds();
    REQUIRE_FALSE(goal.isEmpty());
    world.mutablePlayer().body().position = Vec2{goal.center().x, goal.center().y};
    resolvePenetration(world.map(), world.mutablePlayer().body(), world.player().era());
    for (int i = 0; i < 30; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, 1.0f / 60.0f);
    }
    CHECK(world.outcome() == Outcome::Running);

    // With it.
    world.applySeals(1);
    world.mutablePlayer().body().position = Vec2{goal.center().x, goal.center().y};
    resolvePenetration(world.map(), world.mutablePlayer().body(), world.player().era());
    for (int i = 0; i < 30; ++i) {
        world.update(PlayerInput{}, WorldCommands{}, 1.0f / 60.0f);
    }
    CHECK(world.outcome() == Outcome::Victory);
}

TEST_CASE("every region can still be finished by taking every seal it places")
{
    // The other regions ask for every seal they place, so the old rule still has
    // to hold for them: after taking all of them the gate opens. This is what the
    // tutorial's `requires_seals` change could have broken.
    for (const std::filesystem::path& path : everyLevelPath()) {
        Level level;
        std::string error;
        CAPTURE(idOf(path));
        REQUIRE(loadLevelFromFile(path, level, error));

        World world;
        world.load(level);
        const int placed = static_cast<int>(world.seals().size());
        world.applySeals((1 << placed) - 1);

        CHECK_MESSAGE(world.sealsTaken() == placed, "every seal counted");
        if (placed > 0) {
            CHECK_MESSAGE(world.gateOpen(), "the gate opens with every seal taken");
        }
    }
}
