// Era Shift - the tile grid.
//
// The world is stored as one grid of cells. Each cell carries a *mask* of the
// eras in which it is solid, plus a flag for one-way platforms and a flag for
// contact damage. That single representation is what makes the era mechanic
// cheap: shifting eras does not rebuild the level, it only changes which cells
// answer "solid" to a query, and the physics resolves against a different world
// without anything being reallocated.
//
// A fixed tile size keeps the grid collision-friendly. At 32px with a 6400px
// wide level that is 200 columns, so a full sweep of one row is a few hundred
// comparisons - far cheaper than maintaining a broadphase for a level this size.

#pragma once

#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Game/Era.hpp"

#include <cstdint>
#include <vector>

namespace EraShift::Game {

using Graphics::Rect;
using Graphics::Vec2;

/// What a cell is, independent of which era it is being viewed in.
enum class TileKind : std::uint8_t {
    Empty = 0,   ///< Never solid.
    Solid,       ///< Solid in every era.
    Platform,    ///< Solid in every era, but only from above (can jump through).
    Crumble,     ///< Solid in the Past only: the ruins that have not rotted.
    Bridge,      ///< Solid in the Present only: the structure still standing.
    Crystal,     ///< Solid in the Future only: matter grown after the fact.
    Hazard,      ///< Not solid; damages whatever stands in it.
    Goal,        ///< Not solid; the level exit marker.
};

/// One cell of the grid.
struct Tile {
    TileKind kind = TileKind::Empty;

    /// Eras in which this cell is solid. Ignored for `Empty` and `Hazard`.
    EraMask solidIn = kEraMaskAll;

    /// Damages on contact rather than blocking.
    bool hazard = false;

    /// Blocks movement only when falling onto it from above.
    bool oneWay = false;

    [[nodiscard]] constexpr bool blocksIn(Era era) const noexcept
    {
        // Markers are never solid: a goal has to be walkable through, or the
        // player would be blocked one tile before reaching it.
        if (kind == TileKind::Empty || kind == TileKind::Goal || hazard) {
            return false;
        }
        return maskIncludes(solidIn, era);
    }

    [[nodiscard]] static constexpr Tile of(TileKind kind) noexcept
    {
        Tile tile;
        tile.kind = kind;
        switch (kind) {
            case TileKind::Crumble: tile.solidIn = kEraBitPast;    break;
            case TileKind::Bridge:  tile.solidIn = kEraBitPresent; break;
            case TileKind::Crystal: tile.solidIn = kEraBitFuture;  break;
            case TileKind::Hazard:  tile.hazard  = true;           break;
            case TileKind::Platform:tile.oneWay  = true;           break;
            default:                                               break;
        }
        return tile;
    }
};

/// A rectangular grid of tiles with a fixed cell size in world units.
class TileMap {
public:
    /// Edge length of one cell in world units. Fixed so that actors and the
    /// renderer agree without having to agree on anything else.
    static constexpr float kTileSize = 32.0f;

    TileMap() = default;
    TileMap(int columns, int rows) { resize(columns, rows); }

    /// Resizes the grid, discarding contents.
    void resize(int columns, int rows);

    void clear();

    [[nodiscard]] int columns() const noexcept { return m_columns; }
    [[nodiscard]] int rows() const noexcept { return m_rows; }
    [[nodiscard]] bool empty() const noexcept { return m_columns <= 0 || m_rows <= 0; }

    /// Writes a cell. Out-of-range coordinates are ignored rather than
    /// growing the map, so level data cannot silently reshape the world.
    void set(int x, int y, const Tile& tile);

    /// Reads a cell. Out-of-range reads return an empty tile, which means
    /// "nothing there" - the correct answer at the edges of the world.
    [[nodiscard]] Tile at(int x, int y) const noexcept;

    [[nodiscard]] bool inBounds(int x, int y) const noexcept
    {
        return x >= 0 && y >= 0 && x < m_columns && y < m_rows;
    }

    /// World-space rectangle of a cell.
    [[nodiscard]] Rect cellRect(int x, int y) const noexcept;

    /// World-space rectangle of the whole grid.
    [[nodiscard]] Rect bounds() const noexcept;

    /// Converts a world position to the cell containing it.
    [[nodiscard]] Vec2 worldToCell(const Vec2& world) const noexcept;
    [[nodiscard]] int  cellX(float worldX) const noexcept;
    [[nodiscard]] int  cellY(float worldY) const noexcept;

    // --- queries used by physics -------------------------------------------

    /// True when any cell overlapping `rect` blocks movement in `era`.
    /// One-way cells do not count, because whether they block depends on the
    /// direction of travel, which only the sweep knows.
    [[nodiscard]] bool rectBlocked(const Rect& rect, Era era) const;

    /// True when a one-way platform would catch a body landing on it.
    ///
    /// A platform only supports a body that was entirely above it before the
    /// move and is overlapping it now. Anything else - rising into it from
    /// below, already standing on it, moving sideways along it - passes through,
    /// which is what makes jump-through platforms feel right.
    ///
    /// `previousBottom` is where the body's bottom edge was before the move, and
    /// `rect` is where it is now.
    [[nodiscard]] bool oneWayLanding(const Rect& rect, float previousBottom, Era era) const;

    /// The world Y of the one-way platform a body would land on, or NaN if none.
    ///
    /// The caller needs the *height*, not just the fact: snapping to "the row the
    /// body's bottom is currently in" puts a body that has only just crossed a
    /// surface one row too high, which reads as the floor swallowing it.
    [[nodiscard]] float oneWayLandingSurface(const Rect& rect, float previousBottom,
                                            Era era) const;

    /// True when `rect` overlaps any hazard in `era`. Hazards exist in all eras,
    /// so `era` only selects which cells are present at all.
    [[nodiscard]] bool rectOverHazard(const Rect& rect) const;

    /// True when the cell at `rect`'s centre is a goal marker.
    [[nodiscard]] bool rectOverGoal(const Rect& rect) const;

    /// Fills `out` with the cell range covering `rect`. Callers use it to walk
    /// only the cells that can possibly matter instead of the whole grid.
    void overlappingCells(const Rect& rect, int& x0, int& y0, int& x1, int& y1) const noexcept;

private:
    [[nodiscard]] std::size_t index(int x, int y) const noexcept
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(m_columns) +
               static_cast<std::size_t>(x);
    }

    int m_columns = 0;
    int m_rows    = 0;
    std::vector<Tile> m_tiles;
};

} // namespace EraShift::Game
