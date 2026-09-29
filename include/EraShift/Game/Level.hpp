// Era Shift - level data.
//
// A level is a grid of tiles plus a list of things placed on it. It is loaded
// from JSON so a new region is a data change rather than a recompile, which is
// the whole reason the engine has a resource manager and an empty `data/`
// directory waiting to be filled.
//
// The tile grid is written as three rows of characters - one per era - because
// that is the form a human can actually edit. See `parseTileRow`.

#pragma once

#include "EraShift/Game/Era.hpp"
#include "EraShift/Game/Enemy.hpp"
#include "EraShift/Game/TileMap.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace EraShift::Game {

using Graphics::Rect;
using Graphics::Vec2;

/// What can be placed on a level.
enum class EntityKind : std::uint8_t {
    PlayerSpawn,
    Enemy,
    Pickup,
    Seal,
};

/// Kinds of collectable.
enum class PickupKind : std::uint8_t {
    ChronoCell,  ///< Refills chrono energy.
    Health,      ///< Restores one health step.
};

/// A single placed thing. `x`/`y` are tile coordinates; the loader converts.
struct PlacedEntity {
    EntityKind  kind = EntityKind::Enemy;
    EnemyKind   enemy = EnemyKind::Sentinel;
    PickupKind  pickup = PickupKind::ChronoCell;
    Era         sealEra = Era::Past;
    std::string label;
    EraMask     existsIn = kEraMaskAll;
    int  x = 0;
    int  y = 0;
};

/// A complete level.
struct Level {
    std::string id;
    std::string name;
    /// One row of characters per era, Past first. Empty when built in code.
    std::vector<std::string> pastRows;
    std::vector<std::string> presentRows;
    std::vector<std::string> futureRows;
    std::vector<PlacedEntity> entities;

    [[nodiscard]] bool empty() const noexcept { return entities.empty() && presentRows.empty(); }

/// Builds the tile grid.
///
/// Each cell of the grid is the *union* of the three era layers at that
/// position: it is solid in exactly those eras where at least one layer says so.
/// That is what lets one grid answer "what is solid right now" for any era
/// without rebuilding anything - and it is why the three layers are combined
/// rather than overwritten. Overwriting would mean the Present layer's empty
/// space erases the Past layer's bridge, and the level would silently lose the
/// era-specific geometry that the whole game is built on.
[[nodiscard]] TileMap buildMap() const;

    [[nodiscard]] Vec2 worldPosition(const PlacedEntity& entity) const noexcept;

    /// The first placed entity of a given kind, or nullptr.
    [[nodiscard]] const PlacedEntity* find(EntityKind kind) const noexcept;
};

/// Decodes one row of tile characters into a row of tiles.
///
/// Unknown characters are treated as empty rather than rejected: a level file
/// with a typo should produce a hole, not a refusal to start. Returns false only
/// for a row that cannot be interpreted at all.
[[nodiscard]] bool parseTileRow(std::string_view row, std::vector<Tile>& out);

/// Reads a level from JSON.
/// @return false and fills `errorOut` when the document is not a usable level.
[[nodiscard]] bool loadLevelFromJson(std::string_view json, Level& out, std::string& errorOut);

/// Reads a level from a file. Missing files are reported, not ignored silently.
[[nodiscard]] bool loadLevelFromFile(const std::filesystem::path& path, Level& out,
                                     std::string& errorOut);

/// The vertical slice the game ships with, built in code.
///
/// It exists so the game is playable even if `data/` has been deleted, and so a
/// fresh checkout has something to run. The file version of the same region is
/// the source of truth; this is a fallback, and it says so.
[[nodiscard]] Level builtInLevel();

} // namespace EraShift::Game
