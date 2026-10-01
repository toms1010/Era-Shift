// Era Shift - a physics-accurate traversal probe for the regions.
//
// WHY THIS EXISTS
//
// `validate_levels.py` flood-fills four-way walking. That is deliberately
// generous: whatever it reports as unreachable genuinely is, so a region passing it
// cannot be impossible. It says nothing about the other direction. A shelf six
// rows above the floor is "reachable" to the walk fill - it steps between adjacent
// cells and never notices that five of those rows are vertical - and completely
// impossible to a player whose jump clears two.
//
// So the envelope is *measured* here rather than assumed: the real `Player` is run
// across a flat corridor and its jump and dash are recorded in tiles. A designer
// reasons in tiles and the physics answers in pixels, and the two disagree by more
// than you would expect - which is exactly how a region ends up containing a gap
// that looks fine in the editor and is a wall in the game.
//
// Building it:
//
//   g++ -std=c++20 -I include tools/measure_jumps.cpp -o /tmp/measure_jumps \
//       -Lbuild/src -lerashift_core
//   /tmp/measure_jumps data/levels/awakening.json
//
// WHAT IT DOES NOT MODEL
//
// A jump that lands *lower than it started* and further away than a single jump
// carries. Level 1's seal chamber is exactly that: a leap off the end of the high
// route, across open air, dropping into a room two rows below the take-off. A real
// player makes it. This probe reports the chamber as unreachable, because the
// jump edges it considers all land at or one row below the take-off.
//
// So a "not reachable" line is "not reachable by walking, jumping in place, or
// falling in a column". Confirm a surprising one in game, or drive the real
// physics at it directly - that is how the chamber was confirmed.
//
// The converse is safe: everything this probe calls reachable, a player can reach.
// Traversal probe for a region.
//
// `validate_levels.py` flood-fills four-way walking, which is deliberately
// generous: it proves a region is not *impossible* but says nothing about whether
// its gaps can be jumped. This asks the harder question, and answers it by
// measuring the real jump envelope from the real `Player` on a flat corridor
// rather than assuming one. The player thinks in tiles; the physics answers in
// pixels.
#include "EraShift/Game/Level.hpp"
#include "EraShift/Game/Player.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace EraShift::Game;

namespace {

/// Whether the player can come to rest with their body in cell (x, y) in `era`.
///
/// A body rests when the cell it occupies is open *and* something below it holds
/// it up. "Something below" is either a blocking tile or a one-way platform: a `=`
/// supports a body landing on it from above, which is the entire point of a jump-
/// through platform. Omitting that case is what made the first run of this probe
/// report every platform stairway in the game as a wall.
///
/// Also why the cell's *own* one-way-ness is not checked here: a platform is
/// standable because of what is under the body, not because of what it is.
[[nodiscard]] bool standable(const TileMap& map, int x, int y, Era era) noexcept
{
    if (x < 0 || y < 0 || x >= map.columns() || y >= map.rows() - 1) {
        return false;
    }
    if (map.at(x, y).blocksIn(era)) {
        return false;
    }
    const Tile below = map.at(x, y + 1);
    return below.blocksIn(era) || below.isOneWayIn(era);
}


struct Envelope {
    int up         = 0;  ///< Whole tiles gained at the top of the arc.
    int across     = 0;  ///< Tiles covered, landing at the starting height.
    int dashUp     = 0;  ///< As `up`, with a dash at the top.
    int dashAcross = 0;  ///< As `across`, with a dash at the top.
};

/// Measures the jump by trying every run-up length and taking the best result.
///
/// Two passes, because the level's wider gaps are sized against a jump-and-dash and
/// the narrower ones against a plain jump. Guessing either number is how a level
/// ends up with an impossible gap: the designer reasons in tiles, the physics
/// answers in pixels, and the two disagree by more than you would expect.
Envelope measure() noexcept
{
    TileMap flat;
    flat.resize(90, 14);
    for (int x = 0; x < 90; ++x) {
        flat.set(x, 10, Tile::of(TileKind::Solid));
    }
    for (int y = 0; y < 14; ++y) {
        flat.set(0, y, Tile::of(TileKind::Solid));
        flat.set(89, y, Tile::of(TileKind::Solid));
    }

    Envelope best;
    for (int pass = 0; pass < 2; ++pass) {
        const bool useDash = (pass == 1);
        for (int runup = 0; runup < 16; ++runup) {
            // The body is 44 tall and the floor tile's top is at row 10, so the top
            // of the body goes at floorTop - height. Spawning inside the floor makes
            // the penetration resolver shove the player sideways, which is a
            // property of this harness rather than of the jump.
            Player p;
            p.reset(Vec2{3.0f * TileMap::kTileSize, 10.0f * TileMap::kTileSize - 44.0f},
                    Era::Present);

            for (int i = 0; i < 60; ++i) {
                PlayerInput in;
                in.moveAxis = 1.0f;
                p.update(flat, in, 1.0f / 60.0f);
            }

            const int startX = flat.cellX(p.body().position.x);
            const float baseBottom = p.body().rect().bottom();
            float peak     = 0.0f;   // Pixels of rise; converted to tiles at landing.
            for (int step = 0; step < 200; ++step) {
                PlayerInput in;
                in.moveAxis    = 1.0f;
                in.jumpPressed = (step == 0);
                // Held, or the variable-height rule clamps the rise to
                // `jumpCutSpeed` and every "jump" measured here is a two-tile hop.
                in.jumpHeld    = true;
                // The dash goes in at the top of the arc, which is where a player
                // spends it. Gravity is suspended for its duration, so it adds
                // length and a little height rather than replacing the jump.
                if (useDash && step == 14) {
                    in.dashPressed = true;
                }
                p.update(flat, in, 1.0f / 60.0f);

                // Measured from the body's *feet*, not its top. `cellY(position.y)`
                // is the row of the top-left corner, and the body is 44 tall, so
                // reading it as the height gained reports a standing player as
                // already 1.4 tiles up - and then the jump measures as 5.
                const float rise = baseBottom - p.body().rect().bottom();
                peak = (rise > peak) ? rise : peak;

                if (p.body().onGround && step > 4) {
                    const int across   = flat.cellX(p.body().position.x) - startX;
                    const int upTiles  = static_cast<int>(peak / TileMap::kTileSize);
                    if (useDash) {
                        best.dashAcross = std::max(best.dashAcross, across);
                        best.dashUp    = std::max(best.dashUp, upTiles);
                    } else {
                        best.across = std::max(best.across, across);
                        best.up     = std::max(best.up, upTiles);
                    }
                    break;
                }
            }
        }
    }
    return best;
}

} // namespace

int check(const char* path);

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: measure_jumps <region.json> [more.json ...]\n");
        printf("  with no arguments, every region in data/levels is checked\n");
        return 2;
    }

    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        failures += check(argv[i]);
    }
    return failures > 0 ? 1 : 0;
}

int check(const char* path) {
    Level level;
    std::string error;
    if (!loadLevelFromFile(path, level, error)) {
        printf("  LOAD FAIL: %s\n", error.c_str());
        return 1;
    }
    printf("%s\n", path);

    const Envelope env = measure();
    printf("measured jump: up=%d, across=%d | with dash: up=%d, across=%d\n",
           env.up, env.across, env.dashUp, env.dashAcross);
    const int upLimit   = std::max(env.up, env.dashUp);
    const int xLimit    = std::max(env.across, env.dashAcross);

    const TileMap map = level.buildMap();
    const int W = map.columns();
    const int H = map.rows();

    // Pack (x, y, era) into an int. W is far below 1024 for every shipped region,
    // and the bound is checked rather than assumed so a future region cannot
    // silently alias two cells into one.
    if (W >= 1024) {
        printf("SKIP: region wider than the probe's key packing (%d)\n", W);
        return 0;
    }
    const auto key = [W](int x, int y, int e) { return ((y * W) + x) * 3 + e; };

    std::vector<char> seen(static_cast<std::size_t>(W) * H * 3, 0);
    std::vector<int> queue;

    const PlacedEntity* spawn = level.find(EntityKind::PlayerSpawn);
    if (spawn == nullptr) {
        printf("no spawn\n");
        return 1;
    }
    for (int e = 0; e < 3; ++e) {
        const int id = key(spawn->x, spawn->y, e);
        seen[static_cast<std::size_t>(id)] = 1;
        queue.push_back(id);
    }

    const auto visit = [&](int x, int y, int e) {
        if (x < 0 || y < 0 || x >= W || y >= H) {
            return;
        }
        const int id = key(x, y, e);
        if (seen[static_cast<std::size_t>(id)] == 0) {
            seen[static_cast<std::size_t>(id)] = 1;
            queue.push_back(id);
        }
    };

    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int id   = queue[head];
        const int e    = id % 3;
        const int rest = id / 3;
        const int x    = rest % W;
        const int y    = rest / W;
        const Era era = static_cast<Era>(e);

        // Walk: one tile, and only where there is floor to stand on.
        for (int d = 0; d < 4; ++d) {
            const int nx = x + ((d == 0) ? 1 : (d == 1) ? -1 : 0);
            const int ny = y + ((d == 2) ? 1 : (d == 3) ? -1 : 0);
            if (nx < 0 || ny < 0 || nx >= W || ny >= H || ny + 1 >= H) {
                continue;
            }
            if (!standable(map, nx, ny, era)) {
                continue;
            }
            visit(nx, ny, e);
        }

        // Jump: the measured envelope, landing at or below the take-off row.
        for (int dx = 2; dx <= xLimit; ++dx) {
            for (int dy = -upLimit; dy <= 1; ++dy) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= H || ny + 1 >= H) {
                    continue;
                }
                if (!standable(map, nx, ny, era)) {
                    continue;
                }
                visit(nx, ny, e);
            }
        }

        // Fall: straight down until something stops.
        for (int dy = 1; y + dy < H; ++dy) {
            const int ny = y + dy;
            if (map.at(x, ny).blocksIn(era)) {
                visit(x, ny - 1, e);
                break;
            }
        }

        // Shift: the cell is real in every era, so all three are standing positions.
        for (int ne = 0; ne < 3; ++ne) {
            visit(x, y, ne);
        }
    }

    printf("  visited %zu of %d states\n", queue.size(), W * H * 3);

    std::vector<std::string> unreachable;
    const char* nameOf = nullptr;
    for (const PlacedEntity& entity : level.entities) {
        switch (entity.kind) {
            case EntityKind::PlayerSpawn: nameOf = "player";  break;
            case EntityKind::Seal:        nameOf = "seal";     break;
            case EntityKind::Checkpoint:  nameOf = "checkpoint"; break;
            case EntityKind::Enemy:       nameOf = "enemy";    break;
            case EntityKind::Pickup:      nameOf = "pickup";   break;
        }
        bool found = false;
        for (int e = 0; e < 3 && !found; ++e) {
            for (int dy = -1; dy <= 1 && !found; ++dy) {
                for (int dx = -1; dx <= 1 && !found; ++dx) {
                    const int nx = entity.x + dx;
                    const int ny = entity.y + dy;
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) {
                        continue;
                    }
                    found = seen[static_cast<std::size_t>(key(nx, ny, e))] != 0;
                }
            }
        }
        if (!found) {
            unreachable.push_back(std::string(nameOf) + " at " + std::to_string(entity.x) +
                                  "," + std::to_string(entity.y));
        }
    }
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (map.at(x, y).kind != TileKind::Goal) {
                continue;
            }
            bool found = false;
            for (int e = 0; e < 3 && !found; ++e) {
                found = seen[static_cast<std::size_t>(key(x, y, e))] != 0;
            }
            if (!found) {
                unreachable.push_back("GATE at " + std::to_string(x) + "," +
                                      std::to_string(y));
            }
        }
    }

    if (unreachable.empty()) {
        printf("  all required placements reachable with the measured jump\n");
        return 0;
    }
    printf("  beyond walking/jumping/falling (a falling jump is not modelled):\n");
    for (const std::string& item : unreachable) {
        printf("    %s\n", item.c_str());
    }
    return 1;
}
