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

/// True when a body of the player's size could occupy this cell.
bool fits(const TileMap& map, int x, int y, Era era)
{
    // The player is 28x44 in a 32px cell, so it occupies its own cell and pokes
    // a few pixels into the one below.
    return !map.rectBlocked(Rect{static_cast<float>(x) * TileMap::kTileSize,
                                 static_cast<float>(y) * TileMap::kTileSize, 28.0f, 44.0f},
                            era);
}

/// True when there is something solid directly under this cell to stand on.
bool supported(const TileMap& map, int x, int y, Era era)
{
    const float left  = static_cast<float>(x) * TileMap::kTileSize;
    const float top   = static_cast<float>(y + 1) * TileMap::kTileSize;
    const Rect probe{left, top, 28.0f, 4.0f};
    if (map.rectBlocked(probe, era)) {
        return true;
    }
    // A one-way platform only counts when falling onto it.
    const float feet = static_cast<float>(y + 1) * TileMap::kTileSize - 1.0f;
    return map.oneWayLanding(Rect{left, static_cast<float>(y) * TileMap::kTileSize, 28.0f, 44.0f},
                             feet, era);
}

/// Every cell the player can stand in, over the union of the three eras.
///
/// The player can shift eras at any moment, so for reachability purposes a cell
/// counts if any of the three eras allows it. Enemies are ignored: the point is
/// to prove the geometry has a path, not that the path is safe.
std::set<std::pair<int, int>> reachableStanding(const TileMap& map, EraMask& erasOut,
                                                int spawnX, int spawnY)
{
    constexpr int kMaxRise = 2;    // tiles a jump clears from a standing start
    constexpr int kMaxGap  = 3;    // tiles of horizontal gap a jump covers

    std::set<std::pair<int, int>> visited;
    std::deque<std::pair<int, int>> queue;
    erasOut = 0;

    const auto push = [&](int x, int y, EraMask era) {
        const auto key = std::make_pair(x, y);
        if (visited.count(key) != 0) {
            return;
        }
        visited.insert(key);
        queue.push_back(key);
        erasOut |= era;
    };

    // The spawn may be standing on a platform in one era only.
    for (const Era era : {Era::Past, Era::Present, Era::Future}) {
        if (fits(map, spawnX, spawnY, era)) {
            push(spawnX, spawnY, eraBit(era));
        }
    }

    while (!queue.empty()) {
        const auto [x, y] = queue.front();
        queue.pop_front();

        for (const Era era : {Era::Past, Era::Present, Era::Future}) {
            if (!fits(map, x, y, era)) {
                continue;
            }

            // Walk along the floor.
            for (const int dx : {-1, 1}) {
                const int nx = x + dx;
                if (fits(map, nx, y, era) && supported(map, nx, y, era)) {
                    push(nx, y, eraBit(era));
                }
            }

            // Step up onto something one or two tiles higher.
            for (const int rise : {1, kMaxRise}) {
                for (const int dx : {-1, 0, 1}) {
                    const int nx = x + dx;
                    if (fits(map, nx, y - rise, era) && supported(map, nx, y - rise, era)) {
                        push(nx, y - rise, eraBit(era));
                    }
                }
            }

            // Jump across a gap.
            for (const int dx : {-kMaxGap, kMaxGap}) {
                for (const int rise : {0, 1}) {
                    const int nx = x + dx;
                    if (fits(map, nx, y - rise, era) && supported(map, nx, y - rise, era)) {
                        push(nx, y - rise, eraBit(era));
                    }
                }
            }

            // Fall. This is what links an upper shelf to the floor below it.
            int ny = y + 1;
            while (ny < map.rows() && fits(map, x, ny, era) && !supported(map, x, ny, era)) {
                ny += 1;
            }
            if (ny < map.rows() && fits(map, x, ny, era)) {
                push(x, ny, eraBit(era));
            }
        }
    }

    return visited;
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
        CHECK(fits(map, spawn->x, spawn->y, era));
        CHECK(supported(map, spawn->x, spawn->y, era));
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

    EraMask eras = 0;
    const auto standing = reachableStanding(map, eras, spawn->x, spawn->y);
    REQUIRE_FALSE(standing.empty());

    // Find the gate.
    bool foundGoal = false;
    for (int y = 0; y < map.rows() && !foundGoal; ++y) {
        for (int x = 0; x < map.columns() && !foundGoal; ++x) {
            if (map.at(x, y).kind == TileKind::Goal) {
                foundGoal = true;
                // The cell above the marker, or the marker itself, must be
                // somewhere the player can actually stand or walk through.
                const bool reachable = standing.count({x, y}) > 0 ||
                                       standing.count({x, y - 1}) > 0;
                CHECK_MESSAGE(reachable, "the gate is not reachable from the spawn");
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

    EraMask eras = 0;
    const auto standing = reachableStanding(map, eras, spawn->x, spawn->y);

    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind != EntityKind::Seal) {
            continue;
        }
        const bool here   = standing.count({entity.x, entity.y}) > 0;
        const bool below  = standing.count({entity.x, entity.y + 1}) > 0;
        const bool beside = standing.count({entity.x - 1, entity.y}) > 0 ||
                            standing.count({entity.x + 1, entity.y}) > 0;
        const bool reachable = here || below || beside;
        const std::string where = "seal at (" + std::to_string(entity.x) + ", " +
                                  std::to_string(entity.y) + ") cannot be reached";
        CHECK_MESSAGE(reachable, where);
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

    EraMask eras = 0;
    const auto standing = reachableStanding(map, eras, spawn->x, spawn->y);

    for (const PlacedEntity& entity : level.entities) {
        if (entity.kind == EntityKind::PlayerSpawn) {
            continue;
        }
        // A wisp hovers, so it does not need anything under it.
        const bool hovering = entity.kind == EntityKind::Enemy &&
                              tuningFor(entity.enemy).flies;
        const bool reachable = standing.count({entity.x, entity.y}) > 0 ||
                               standing.count({entity.x, entity.y + 1}) > 0 ||
                               standing.count({entity.x - 1, entity.y}) > 0 ||
                               standing.count({entity.x + 1, entity.y}) > 0;
        if (!hovering) {
            const std::string where = "entity at (" + std::to_string(entity.x) + ", " +
                                      std::to_string(entity.y) + ") is out of reach";
            CHECK_MESSAGE(reachable, where);
        }
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
                if (!fits(map, x, y, era)) {
                    continue;
                }
                const bool roofed = above.blocksIn(era) && !above.oneWay;
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

    EraMask eras = 0;
    const auto standing =
        reachableStanding(map, eras, level.find(EntityKind::PlayerSpawn)->x,
                          level.find(EntityKind::PlayerSpawn)->y);
    REQUIRE_FALSE(standing.empty());

    bool foundGoal = false;
    for (int y = 0; y < map.rows() && !foundGoal; ++y) {
        for (int x = 0; x < map.columns() && !foundGoal; ++x) {
            if (map.at(x, y).kind == TileKind::Goal) {
                foundGoal = true;
                const bool reachable = standing.count({x, y}) > 0 ||
                                       standing.count({x, y - 1}) > 0;
                CHECK_MESSAGE(reachable, "the fallback level's gate is unreachable");
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
    CHECK(tiles[1].oneWay);
    CHECK(tiles[2].blocksIn(Era::Past));
    CHECK_FALSE(tiles[2].blocksIn(Era::Present));
    CHECK(tiles[3].blocksIn(Era::Present));
    CHECK(tiles[4].blocksIn(Era::Future));
    CHECK(tiles[5].hazard);
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
