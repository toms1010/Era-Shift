#include "EraShift/Game/TileMap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace EraShift::Game {

void TileMap::resize(int columns, int rows)
{
    m_columns = std::max(0, columns);
    m_rows    = std::max(0, rows);
    m_tiles.assign(static_cast<std::size_t>(m_columns) * static_cast<std::size_t>(m_rows),
                   Tile{});
}

void TileMap::clear()
{
    m_columns = 0;
    m_rows    = 0;
    m_tiles.clear();
}

void TileMap::set(int x, int y, const Tile& tile)
{
    if (!inBounds(x, y)) {
        return;
    }
    m_tiles[index(x, y)] = tile;
}

Tile TileMap::at(int x, int y) const noexcept
{
    if (!inBounds(x, y)) {
        return {};
    }
    return m_tiles[index(x, y)];
}

Rect TileMap::cellRect(int x, int y) const noexcept
{
    return Rect{static_cast<float>(x) * kTileSize, static_cast<float>(y) * kTileSize, kTileSize,
                kTileSize};
}

Rect TileMap::bounds() const noexcept
{
    return Rect{0.0f, 0.0f, static_cast<float>(m_columns) * kTileSize,
                static_cast<float>(m_rows) * kTileSize};
}

int TileMap::cellX(float worldX) const noexcept
{
    return static_cast<int>(std::floor(worldX / kTileSize));
}

int TileMap::cellY(float worldY) const noexcept
{
    return static_cast<int>(std::floor(worldY / kTileSize));
}

Vec2 TileMap::worldToCell(const Vec2& world) const noexcept
{
    return Vec2{static_cast<float>(cellX(world.x)), static_cast<float>(cellY(world.y))};
}

void TileMap::overlappingCells(const Rect& rect, int& x0, int& y0, int& x1, int& y1) const noexcept
{
    // The right/bottom edge is exclusive: a rect that starts exactly at a cell
    // boundary does not touch the cell beyond it.
    x0 = cellX(rect.left());
    y0 = cellY(rect.top());
    x1 = cellX(std::nextafter(rect.right(), -std::numeric_limits<float>::infinity()));
    y1 = cellY(std::nextafter(rect.bottom(), -std::numeric_limits<float>::infinity()));
}

bool TileMap::rectBlocked(const Rect& rect, Era era) const
{
    if (rect.isEmpty()) {
        return false;
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    overlappingCells(rect, x0, y0, x1, y1);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Tile tile = at(x, y);
            if (tile.blocksIn(era) && !tile.isOneWayIn(era)) {
                return true;
            }
        }
    }
    return false;
}

float TileMap::oneWayLandingSurface(const Rect& rect, float previousBottom, Era era) const
{
    const float none = std::numeric_limits<float>::quiet_NaN();
    if (rect.isEmpty()) {
        return none;
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    overlappingCells(rect, x0, y0, x1, y1);

    float highest = none;
    for (int y = y0; y <= y1; ++y) {
        const float surface = static_cast<float>(y) * kTileSize;
        // The body has to have *crossed* this surface during the move. Testing
        // against the body's own top edge instead would refuse every landing
        // from the first frame, because a falling body is almost always already
        // overlapping the row it is about to land on.
        if (previousBottom > surface || rect.bottom() < surface) {
            continue;
        }
        for (int x = x0; x <= x1; ++x) {
            const Tile tile = at(x, y);
            if (!tile.isOneWayIn(era) || !tile.blocksIn(era)) {
                continue;
            }
            // Landing on the topmost platform in the overlap, not whichever the
            // scan reaches first.
            if (std::isnan(highest) || surface < highest) {
                highest = surface;
            }
        }
    }
    return highest;
}

bool TileMap::oneWayLanding(const Rect& rect, float previousBottom, Era era) const
{
    return !std::isnan(oneWayLandingSurface(rect, previousBottom, era));
}

bool TileMap::rectOverHazard(const Rect& rect, Era era) const
{
    if (rect.isEmpty()) {
        return false;
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    overlappingCells(rect, x0, y0, x1, y1);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (at(x, y).isHazardIn(era)) {
                return true;
            }
        }
    }
    return false;
}

bool TileMap::rectOverGoal(const Rect& rect) const
{
    if (rect.isEmpty()) {
        return false;
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    overlappingCells(rect, x0, y0, x1, y1);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (at(x, y).kind == TileKind::Goal) {
                return true;
            }
        }
    }
    return false;
}

} // namespace EraShift::Game
