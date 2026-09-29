#include "EraShift/Game/Actor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace EraShift::Game {

namespace {

/// Longest distance a single collision probe moves a body, in world units.
/// Splitting the sweep into steps no longer than this is what stops a fast body
/// passing through a thin wall between two frames.
constexpr float kMaxStep = TileMap::kTileSize * 0.4f;

/// Fineness of the back-out search when a step collides, in world units. Small
/// enough that a body never visibly floats off a wall.
constexpr float kBackOut = 0.5f;

/// True when nothing blocks the rectangle at all.
[[nodiscard]] bool anyBlocked(const TileMap& map, const Rect& rect, Era era)
{
    return map.rectBlocked(rect, era);
}

} // namespace

MoveResult moveBody(const TileMap& map, Body& body, Era era, float dt)
{
    MoveResult result;
    if (dt <= 0.0f || !std::isfinite(dt)) {
        return result;
    }

    const Vec2 delta = body.velocity * dt;
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) {
        // A NaN velocity would poison position permanently, which is far worse
        // than dropping the frame.
        body.velocity = Vec2{};
        return result;
    }

    const float previousBottom = body.bottom();
    const bool movingDown      = delta.y > 0.0f;

    // --- horizontal ---------------------------------------------------------
    // Resolved first so that the vertical pass runs against the corrected
    // horizontal position; doing it the other way round lets a body clip a
    // corner it should have been pushed clear of.
    const auto stepAxis = [&](bool horizontal, float distance) {
        if (distance == 0.0f || !std::isfinite(distance)) {
            return false;
        }
        const float sign = distance > 0.0f ? 1.0f : -1.0f;
        float remaining  = std::fabs(distance);

        while (remaining > 0.0f) {
            const float step = std::min(remaining, kMaxStep);
            remaining -= step;

            const float offset = horizontal ? body.position.x : body.position.y;
            if (horizontal) {
                body.position.x = offset + sign * step;
            } else {
                body.position.y = offset + sign * step;
            }

            if (!anyBlocked(map, body.rect(), era)) {
                continue;
            }

            // Back out in small increments until the body is clear again, so it
            // ends up flush against the surface rather than a whole step away.
            // Sliding along a wall then works instead of stalling.
            float undone = 0.0f;
            while (undone < step) {
                const float undo = std::min(kBackOut, step - undone);
                undone += undo;
                const float back = (horizontal ? body.position.x : body.position.y) - sign * undo;
                if (horizontal) {
                    body.position.x = back;
                } else {
                    body.position.y = back;
                }
                if (!anyBlocked(map, body.rect(), era)) {
                    break;
                }
            }
            return true;
        }
        return false;
    };

    const bool hitWall = stepAxis(/*horizontal=*/true, delta.x);

    // --- vertical -----------------------------------------------------------
    const bool hitVertical = stepAxis(/*horizontal=*/false, delta.y);

    // One-way platforms are resolved against the whole vertical move rather
    // than inline: a body rising through a platform must not be caught by it.
    if (!hitVertical && movingDown) {
        const float surface = map.oneWayLandingSurface(body.rect(), previousBottom, era);
        if (!std::isnan(surface)) {
            body.position.y = surface - body.size.y;
            result.landed   = true;
        }
    }

    if (hitVertical) {
        result.landed = movingDown;
        result.ceiling = !movingDown;
    }

    body.onGround   = result.landed;
    body.hitWall    = hitWall;
    body.hitCeiling = result.ceiling;

    if (hitWall) {
        body.velocity.x = 0.0f;
    }
    if (hitVertical) {
        body.velocity.y = 0.0f;
    }

    // Being flush with a floor must still count as grounded, otherwise
    // standing still loses the jump every time. Probing a hair lower is the
    // cheapest way to find out.
    if (!body.onGround) {
        const Rect probe{body.position.x, body.position.y + kBackOut, body.size.x, body.size.y};
        const bool onPlatform =
            movingDown && !std::isnan(map.oneWayLandingSurface(
                                     probe, body.position.y + body.size.y, era));
        if (anyBlocked(map, probe, era) || onPlatform) {
            body.onGround = true;
            result.landed = true;
        }
    }

    result.wallRight = hitWall && delta.x > 0.0f;
    result.wallLeft  = hitWall && delta.x < 0.0f;
    return result;
}

void resolvePenetration(const TileMap& map, Body& body, Era era)
{
    // Shift the era and the set of solid cells changes under the player's feet:
    // the tile they were standing on can stop existing. Without this they would
    // be stuck inside whatever is solid next, unable to move.
    for (int pass = 0; pass < 4; ++pass) {
        const Rect r = body.rect();

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        map.overlappingCells(r, x0, y0, x1, y1);

        // Sentinel, not zero. Zero is a perfectly valid "smallest push" value,
        // and initialising to it makes the comparison below reject every
        // candidate - which is how this function ended up doing nothing at all.
        float smallest = std::numeric_limits<float>::max();
        Vec2  push{0.0f, 0.0f};

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const Tile tile = map.at(x, y);
                if (!tile.blocksIn(era) || tile.isOneWayIn(era)) {
                    continue;
                }
                const Rect cell = map.cellRect(x, y);
                // Only cells genuinely overlapping on both axes can push.
                if (r.right() <= cell.x || r.left() >= cell.right() ||
                    r.bottom() <= cell.y || r.top() >= cell.bottom()) {
                    continue;
                }

                const float toLeft   = cell.right() - r.left();
                const float toRight  = r.right() - cell.x;
                const float toTop    = cell.bottom() - r.top();
                const float toBottom = r.bottom() - cell.y;

                const float smallest_ = std::min({toLeft, toRight, toTop, toBottom});
                if (smallest_ <= 0.0f || smallest_ >= smallest) {
                    continue;
                }
                smallest = smallest_;
                // Escape along the axis needing the least travel, which is what
                // keeps a body wedged in a corner from being ejected sideways.
                if (smallest_ == toTop) {
                    push = Vec2{0.0f, toTop};
                } else if (smallest_ == toBottom) {
                    push = Vec2{0.0f, -toBottom};
                } else if (smallest_ == toLeft) {
                    push = Vec2{toLeft, 0.0f};
                } else {
                    push = Vec2{-toRight, 0.0f};
                }
            }
        }

        if (smallest == std::numeric_limits<float>::max()) {
            return;   // Nothing solid overlaps: already resolved.
        }

        body.position += push;
        if (push.y > 0.0f) {
            body.velocity.y = 0.0f;
            body.onGround   = true;
        } else if (push.y < 0.0f) {
            body.velocity.y = 0.0f;
        }
        if (push.x != 0.0f) {
            body.velocity.x = 0.0f;
        }
    }
}

} // namespace EraShift::Game
