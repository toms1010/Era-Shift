// Era Shift - tests for level data and level reachability.
//
// The important one here is reachability. A level that loads, parses and looks
// plausible in a text editor can still be unplayable: a shelf four tiles up is
// not reachable from the ground, and a crystal staircase with overlapping
// shelves walls the player into a pocket. Neither shows up until someone plays
// it, which is exactly the failure this test exists to prevent.
//
// The model is deliberately generous: the player can be in any of the three
// eras at any moment (a shift costs energy but is otherwise free), so a tile is
// traversable if it is non-solid in *any* era. That is a necessary condition
// for reachability, not a sufficient one for a specific route - but if the
// generous model cannot reach the goal, no route exists at all.

#include "EraShift/Game/Level.hpp"

#include <algorithm>
#include <deque>
#include <set>
#include <string>

#include <doctest/doctest.h>

using namespace EraShift::Game;
using EraShift::Graphics::Rect;
using EraShift::Graphics::Vec2;

namespace {

std::filesystem::path shippedLevelPath()
{
    return std::filesystem::path(ERASHIFT_SOURCE_DIR) / "data" / "levels" /
           "ancient_forest.json";
}

// The reachability model works in *foot rows*, not cell rows.
//
// A player is 28x44 in a 32px cell, so it never fits inside one cell and
// "which cell is the player in" is not a meaningful question. What is
// meaningful is where their feet are: a body standing on a solid cell `s` has
// its feet at `s * 32`, and that integer is the row the model walks over.

constexpr float kBodyW = 28.0f;
constexpr float kBodyH = 44.0f;

/// True when a body with its feet in row `footRow` would fit, in `era`.
bool fits(const TileMap& map, int x, int footRow, Era era)
{
    const Rect rect{static_cast<float>(x) * TileMap::kTileSize,
                    static_cast<float>(footRow) * TileMap::kTileSize - kBodyH, kBodyW, kBodyH};
    return !map.rectBlocked(rect, era);
}

/// True when row `footRow` at column `x` is something the body can stand on.
///
/// One-way platforms count: they are solid, they are simply only solid from
/// above, and a body resting on one has its feet exactly at that row.
bool supported(const TileMap& map, int x, int footRow, Era era)
{
    return map.at(x, footRow).blocksIn(era);
}

/// Every (column, foot row) the player can get to.
///
/// Three deliberate liberties, each of which makes the check generous rather
/// than strict:
///
///   * The player can be in any era at any moment - a shift costs energy but
///     otherwise goes anywhere - so a cell is traversable if *any* era allows
///     it.
///   * Enemies are ignored. This proves the geometry has a path, not that the
///     path is safe; that is a design question, not a data question.
///   * Jumps are modelled as a 2-tile rise across up to 3 tiles, which is what
///     the controller's tuning actually clears. Slightly steeper or wider would
///     be a false negative; anything the model accepts is a real jump.
///
/// If this model cannot reach the goal, no route exists at all.
std::set<std::pair<int, int>> reachable(const TileMap& map, int spawnX, int spawnY)
{
    constexpr int kMaxRise = 2;
    constexpr int kMaxGap  = 3;
    // A fall costs nothing (there is no fall damage), so a body may drop any
    // distance inside the level.
    constexpr int kMaxDrop = 8;

    std::set<std::pair<int, int>> seen;
    std::deque<std::pair<int, int>> queue;

    const auto push = [&](int x, int footRow) {
        const auto key = std::make_pair(x, footRow);
        if (seen.count(key) != 0) {
            return;
        }
        seen.insert(key);
        queue.push_back(key);
    };

    // The spawn is given as a cell row; a body placed there has its feet in
    // that row or the two below it, depending on how it settles.
    for (const Era era : {Era::Past, Era::Present, Era::Future}) {
        for (int footRow = spawnY; footRow <= spawnY + 2; ++footRow) {
            if (fits(map, spawnX, footRow, era) && supported(map, spawnX, footRow, era)) {
                push(spawnX, footRow);
            }
        }
    }

    while (!queue.empty()) {
        const auto [x, footRow] = queue.front();
        queue.pop_front();

        for (const Era era : {Era::Past, Era::Present, Era::Future}) {
            if (!fits(map, x, footRow, era)) {
                continue;
            }
            for (int dx = -kMaxGap; dx <= kMaxGap; ++dx) {
                const int nx = x + dx;
                if (nx < 0 || nx >= map.columns()) {
                    continue;
                }
                for (int rise = -kMaxRise; rise <= kMaxDrop; ++rise) {
                    const int nfoot = footRow - rise;
                    if (nfoot < 0 || nfoot >= map.rows()) {
                        continue;
                    }
                    if (fits(map, nx, nfoot, era) && supported(map, nx, nfoot, era)) {
                        push(nx, nfoot);
                    }
                }
            }
        }
    }

    return seen;
}

/// True when anything standing at `cellRow` or just below it is reachable.
///
/// Placements are on a whole-tile boundary while bodies are 44px tall, so an
/// entity's own row and the row it settles onto differ. Two rows of slack is
/// generous enough for the snapping and cannot invent a platform.
bool standingNear(const std::set<std::pair<int, int>>& standing, int x, int cellRow)
{
    for (int footRow = cellRow; footRow <= cellRow + 2; ++footRow) {
        if (standing.count({x, footRow}) > 0) {
            return true;
        }
    }
    return false;
}

/// True when a body with its feet in this cell would fit, in `era`.
bool bodyFits(const TileMap& map, int x, int cellRow, Era era)
{
    return fits(map, x, cellRow + 2, era);
}

} // namespace

TEST_CASE("a level file with valid JSON loads")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));
    CHECK(error.empty());
    CHECK(level.id == "ancient_forest");
    CHECK(level.empty() == false);
    CHECK(level.entities.size() > 10);
}

TEST_CASE("the shipped level has three seals, one per era, and a spawn")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));

    int past = 0;
    int present = 0;
    int future = 0;
    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::Seal) {
            switch (entity.sealEra) {
                case Era::Past:    ++past;    break;
                case Era::Present: ++present; break;
                case Era::Future:  ++future;  break;
            }
        }
    }
    CHECK(past == 1);
    CHECK(present == 1);
    CHECK(future == 1);

    REQUIRE(level.find(EntityKind::PlayerSpawn) != nullptr);
}

TEST_CASE("every era layer is a full, rectangular grid")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));

    const std::size_t width = level.presentRows[0].size();
    CHECK(width > 32);

    for (const auto* rows : {&level.pastRows, &level.presentRows, &level.futureRows}) {
        CHECK(rows->size() > 8);
        for (const std::string& row : *rows) {
            CHECK(row.size() == width);
        }
    }
}

TEST_CASE("a cell is only as hazardous as the era that says so")
{
    // Three cells at the same position: solid stone in the Past, a pit in the
    // Present and the Future.
    Level level;
    level.id = "eras";
    level.pastRows    = {"...", "###", "###"};
    level.presentRows = {"...", "^##", "###"};
    level.futureRows  = {"...", "^##", "###"};

    PlacedEntity spawn;
    spawn.kind = EntityKind::PlayerSpawn;
    level.entities.push_back(spawn);

    const TileMap map = level.buildMap();
    const Tile cell = map.at(0, 1);

    CHECK(cell.blocksIn(Era::Past));
    CHECK_FALSE(cell.isHazardIn(Era::Past));
    // And the pit is not solid, so the player falls into it rather than
    // walking over a hazard they cannot see.
    CHECK_FALSE(cell.blocksIn(Era::Present));
    CHECK(cell.isHazardIn(Era::Present));
    CHECK_FALSE(cell.blocksIn(Era::Future));
    CHECK(cell.isHazardIn(Era::Future));
}

TEST_CASE("a one-way platform in one era does not become one in another")
{
    Level level;
    level.id = "platform";
    level.pastRows    = {"...", "...", "###"};
    level.presentRows = {"...", "=..", "###"};
    level.futureRows  = {"...", "=..", "###"};

    PlacedEntity spawn;
    spawn.kind = EntityKind::PlayerSpawn;
    level.entities.push_back(spawn);

    const TileMap map = level.buildMap();
    const Tile cell = map.at(0, 1);

    CHECK(cell.isOneWayIn(Era::Present));
    CHECK(cell.isOneWayIn(Era::Future));
    // In the Past that cell is open air, so a player rising through it must not
    // be caught by a platform that does not exist there.
    CHECK_FALSE(cell.blocksIn(Era::Past));
    CHECK_FALSE(cell.isOneWayIn(Era::Past));
}

TEST_CASE("the three eras are genuinely different worlds")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));

    const TileMap map = level.buildMap();
    int pastOnly = 0;
    int presentOnly = 0;
    int futureOnly = 0;

    for (int y = 0; y < map.rows(); ++y) {
        for (int x = 0; x < map.columns(); ++x) {
            const bool p = map.at(x, y).blocksIn(Era::Past);
            const bool n = map.at(x, y).blocksIn(Era::Present);
            const bool f = map.at(x, y).blocksIn(Era::Future);
            pastOnly    += (p && !n && !f) ? 1 : 0;
            presentOnly += (n && !p && !f) ? 1 : 0;
            futureOnly  += (f && !p && !n) ? 1 : 0;
        }
    }

    // If these were zero the level would be one world with a recolour, and the
    // whole premise would be a lie.
    CHECK(pastOnly > 4);
    CHECK(presentOnly > 4);
    CHECK(futureOnly > 4);
}

TEST_CASE("the player spawn is standing on something in every era")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));

    const PlacedEntity* spawn = level.find(EntityKind::PlayerSpawn);
    REQUIRE(spawn != nullptr);

    const TileMap map = level.buildMap();
    for (const Era era : {Era::Past, Era::Present, Era::Future}) {
        CHECK(bodyFits(map, spawn->x, spawn->y, era));
        // There has to be floor within a cell of the spawn in every era, or the
        // player starts by falling - or by dying.
        bool nearFloor = false;
        for (int row = spawn->y; row <= spawn->y + 2; ++row) {
            nearFloor = nearFloor || supported(map, spawn->x, row, era);
        }
        CHECK_MESSAGE(nearFloor, "the player spawn has no floor in this era");
    }
}

TEST_CASE("the gate can be reached from the spawn")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));

    const TileMap map = level.buildMap();
    const PlacedEntity* spawn = level.find(EntityKind::PlayerSpawn);
    REQUIRE(spawn != nullptr);

    const auto standing = reachable(map, spawn->x, spawn->y);
    REQUIRE_FALSE(standing.empty());

    // Find the gate.
    bool foundGoal = false;
    for (int y = 0; y < map.rows() && !foundGoal; ++y) {
        for (int x = 0; x < map.columns() && !foundGoal; ++x) {
            if (map.at(x, y).kind == TileKind::Goal) {
                foundGoal = true;
                // The cell above the marker, or the marker itself, must be
                // somewhere the player can actually stand or walk through.
                // The marker cell itself, or the cell it is standing on.
                const bool reachesGate = standingNear(standing, x, y);
                CHECK_MESSAGE(reachesGate, "the gate is not reachable from the spawn");
            }
        }
    }
    CHECK(foundGoal);
}

TEST_CASE("every seal is reachable")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));

    const TileMap map = level.buildMap();
    const PlacedEntity* spawn = level.find(EntityKind::PlayerSpawn);
    REQUIRE(spawn != nullptr);

    const auto standing = reachable(map, spawn->x, spawn->y);

    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind != EntityKind::Seal) {
            continue;
        }
        // A seal's row is its base, so standing level with it is reachable if
        // anything adjacent is: the reach check is generous in every direction
        // because the interaction has a radius anyway.
        const bool here = standingNear(standing, entity.x, entity.y);
        const bool beside = standingNear(standing, entity.x + 1, entity.y) ||
                            standingNear(standing, entity.x - 1, entity.y);
        const bool canReach = here || beside;
        const std::string where = "seal at (" + std::to_string(entity.x) + ", " +
                                  std::to_string(entity.y) + ") cannot be reached";
        CHECK_MESSAGE(canReach, where);
    }
}

TEST_CASE("every pickup and enemy sits on or next to reachable ground")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));

    const TileMap map = level.buildMap();
    const PlacedEntity* spawn = level.find(EntityKind::PlayerSpawn);
    REQUIRE(spawn != nullptr);

    const auto standing = reachable(map, spawn->x, spawn->y);

    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::PlayerSpawn) {
            continue;
        }
        // A wisp hovers, so it does not need anything under it.
        const bool hovering = entity.kind == EntityKind::Enemy &&
                              tuningFor(entity.enemy).flies;
        if (hovering) {
            continue;
        }
        const bool reachable = standingNear(standing, entity.x, entity.y);
        const std::string where = "entity at (" + std::to_string(entity.x) + ", " +
                                  std::to_string(entity.y) + ") is out of reach";
        CHECK_MESSAGE(reachable, where);
    }
}

TEST_CASE("no cell in the shipped level traps the player inside solid geometry")
{
    Level level;
    std::string error;
    REQUIRE(loadLevelFromFile(shippedLevelPath(), level, error));
    const TileMap map = level.buildMap();

    // Any cell that is non-solid but has solid above and below in the same era
    // is a pocket a body cannot leave by jumping.
    for (int y = 1; y < map.rows() - 1; ++y) {
        for (int x = 1; x < map.columns() - 1; ++x) {
            for (const Era era : {Era::Past, Era::Present, Era::Future}) {
                const Tile above = map.at(x, y - 1);
                const Tile below = map.at(x, y + 1);
                if (!bodyFits(map, x, y, era)) {
                    continue;
                }
                const bool roofed = above.blocksIn(era) && !above.isOneWayIn(era);
                const bool walled = roofed && below.blocksIn(era);
                const std::string where =
                    "cell (" + std::to_string(x) + ", " + std::to_string(y) +
                    ") is a sealed pocket in this era";
                CHECK_MESSAGE(!walled, where);
            }
        }
    }
}

TEST_CASE("the built-in fallback level is playable on its own terms")
{
    const Level level = builtInLevel();
    CHECK_FALSE(level.empty());

    const TileMap map = level.buildMap();
    REQUIRE(level.find(EntityKind::PlayerSpawn) != nullptr);

    const auto standing =
        reachable(map, level.find(EntityKind::PlayerSpawn)->x,
                  level.find(EntityKind::PlayerSpawn)->y);
    REQUIRE_FALSE(standing.empty());

    bool foundGoal = false;
    for (int y = 0; y < map.rows() && !foundGoal; ++y) {
        for (int x = 0; x < map.columns() && !foundGoal; ++x) {
            if (map.at(x, y).kind == TileKind::Goal) {
                foundGoal = true;
                const bool reachesGate = standingNear(standing, x, y);
                CHECK_MESSAGE(reachesGate, "the fallback level's gate is unreachable");
            }
        }
    }
    CHECK(foundGoal);
}

TEST_CASE("tile rows parse, and unknown characters become holes rather than errors")
{
    std::vector<Tile> tiles;
    REQUIRE(parseTileRow("#=cbx^G.", tiles));
    CHECK(tiles.size() == 8);
    CHECK(tiles[0].blocksIn(Era::Past));
    CHECK(tiles[0].blocksIn(Era::Future));
    CHECK(tiles[1].isOneWayIn(Era::Present));
    CHECK(tiles[2].blocksIn(Era::Past));
    CHECK_FALSE(tiles[2].blocksIn(Era::Present));
    CHECK(tiles[3].blocksIn(Era::Present));
    CHECK(tiles[4].blocksIn(Era::Future));
    CHECK(tiles[5].isHazardIn(Era::Present));
    CHECK(tiles[6].kind == TileKind::Goal);
    CHECK(tiles[7].kind == TileKind::Empty);

    // A typo in level data should produce a hole, not a refusal to start.
    REQUIRE(parseTileRow("#~#", tiles));
    CHECK(tiles[1].kind == TileKind::Empty);

    CHECK_FALSE(parseTileRow("", tiles));
}

TEST_CASE("malformed level documents are refused with a reason, not a crash")
{
    Level level;
    std::string error;

    CHECK_FALSE(loadLevelFromJson("not json at all", level, error));
    CHECK_FALSE(error.empty());

    CHECK_FALSE(loadLevelFromJson("[]", level, error));
    CHECK_FALSE(loadLevelFromJson("{}", level, error));
    CHECK(error.find("tile rows") != std::string::npos);

    // A level with no spawn would drop the player into nowhere.
    CHECK_FALSE(loadLevelFromJson(R"({"present": ["###", "###"]})", level, error));
    CHECK(error.find("spawn") != std::string::npos);

    // A row that is not a string.
    CHECK_FALSE(loadLevelFromJson(R"({"present": [1, 2]})", level, error));

    // An unknown entity type is skipped rather than fatal, so a level written
    // for a later build still loads.
    Level tolerant;
    REQUIRE(loadLevelFromJson(
        R"({"present": ["...", "###"], "entities": [
               {"type": "beacon", "x": 1, "y": 0},
               {"type": "player", "x": 1, "y": 0}]})",
        tolerant, error));
    CHECK(tolerant.entities.size() == 1);
}

TEST_CASE("a level with an unknown enemy kind is reported rather than guessed")
{
    Level level;
    std::string error;
    CHECK_FALSE(loadLevelFromJson(
        R"({"present": ["...", "###"], "entities": [
               {"type": "player", "x": 1, "y": 0},
               {"type": "enemy", "kind": "Dragon", "x": 2, "y": 0}]})",
        level, error));
    CHECK(error.find("enemy kind") != std::string::npos);
}
