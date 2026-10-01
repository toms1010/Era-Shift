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
    /// A volume that records a checkpoint when the player walks through it.
    ///
    /// Appended rather than inserted: `nameToKind` maps these by name in the
    /// level file, and any code that persists an ordinal keeps working.
    Checkpoint,
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
int   x = 0;
    int   y = 0;
};

/// A complete level.
struct Level {
    std::string id;
    std::string name;
    /// Position in the designed progression, 1-based. Zero means unstated.
    ///
    /// Read from the file's `order` field. Sorting by this rather than by filename
    /// is what keeps `Awakening` first and `Convergence` last instead of the two
    /// ends swapping with the alphabet.
    int order = 0;

    /// Whether this region runs the first-play tutorial.
    ///
    /// Read from the file's `tutorial` field. The tutorial itself is driven by the
    /// progression database's flag rather than by this — a player who has finished
    /// the lessons should not see them again, and a player who has not should see
    /// them in a region that is not the first one. What this *does* decide is
    /// whether the region is laid out to teach, which is a design fact about the
    /// map rather than a fact about the player.
    bool tutorial = false;

    /// How many seals the finish line needs before it will open.
    ///
    /// Read from `finish.requires_seals`. Zero means "every seal placed", which is
    /// what an older file with no `finish` block means and is what every region
    /// except the tutorial wants.
    ///
    /// Separate from the seal *count* on purpose. A tutorial with one seal should
    /// not have to place three of them and then ignore two, and a region whose
    /// seals are optional collectables wants a gate that needs one of them rather
    /// than all of them.
    int requiresSeals = 0;

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

    /// True when `(tileX, tileY)` is inside a checkpoint volume.
    ///
    /// Checkpoints are placed as entities rather than baked into the tile grid,
    /// because a trigger is a gameplay fact and a character in the grid is a
    /// geometry fact. Putting them in the same place would mean the loader had to
    /// guess which `#` was a wall and which was a checkpoint.
    [[nodiscard]] bool checkpointHere(int tileX, int tileY) const;

    /// How far from a checkpoint's tile the player may be and still trigger it,
    /// in tiles.
    ///
    /// Named so a caller answering "which volume am I in?" uses the same number as
    /// the trigger does. Duplicating the constant is how a caller and the trigger
    /// disagree, and a checkpoint then fires twice — or never.
    static constexpr int kCheckpointReach = 2;

    /// Position in the designed progression, 1-based. Zero means unstated.
    ///
    /// A named accessor rather than reading `order` at the call sites, because
    /// "zero means this file does not participate in the progression" is a rule
    /// worth stating once.
    [[nodiscard]] bool hasProgressionOrder() const noexcept { return order > 0; }
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

/// Victory and defeat, with the run's numbers.
///
/// Declared here rather than in `ResultState.hpp` because `PlayingState` reads
/// the region a level select handed over and does not otherwise need that
/// translation unit. Same declaration, one include instead of two.
class LevelSelectState;

/// A level the player can pick, as listed by `listLevels`.
struct LevelEntry {
    /// File name without the extension, which is also the level's `id`.
    std::string id;
    /// The level's display name, falling back to the id.
    std::string name;
    /// Position in the designed progression, 1-based.
    ///
    /// Read from the file's `order` field. Filename order would shuffle the
    /// regions alphabetically, which is not the order they are meant to be
    /// played in. `0` means "unstated", which sorts last.
    int order = 0;
    /// Path relative to the directory that was listed.
    std::filesystem::path file;
};

/// Lists the levels in a directory, in designed progression order.
///
/// Every `*.json` in `directory` is a candidate. A file that does not parse is
/// **skipped rather than reported**, because a level select must not be
/// unopenable on account of one bad file — the level that is merely missing is
/// the one the player notices. `brokenOut`, when non-null, collects the paths
/// that were skipped so a caller that wants to log them can.
///
/// Sorted by the file's `order` field, then by filename. An unordered file sorts
/// after the ordered ones rather than among them, so a region that predates the
/// field still appears — at the end, which is where a legacy region belongs.
///
/// The directory itself missing is not an error: the result is simply empty,
/// which is what a shipped game with no `data/` directory should show.
[[nodiscard]] std::vector<LevelEntry> listLevels(const std::filesystem::path& directory,
                                                 std::vector<std::filesystem::path>* brokenOut = nullptr);

/// The vertical slice the game ships with, built in code.
///
/// It exists so the game is playable even if `data/` has been deleted, and so a
/// fresh checkout has something to run. The file version of the same region is
/// the source of truth; this is a fallback, and it says so.
[[nodiscard]] Level builtInLevel();

} // namespace EraShift::Game
