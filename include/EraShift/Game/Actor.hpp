// Era Shift - actor physics.
//
// One body type serves the player, the enemies and the pickups. It is an
// axis-aligned box swept against the tile grid, resolved one axis at a time.
// That is not the most general solution, but it is the one that does not
// produce the classic platformer bugs: a body can never be squeezed through a
// gap by resolving both axes at once, and "did I land" is unambiguous because
// only the vertical pass sets it.
//
// The sweep takes the era, which means every query about what is solid is
// answered against the world *as it currently looks*. Shifting eras mid-flight
// therefore needs no special case at all.

#pragma once

#include "EraShift/Game/Era.hpp"
#include "EraShift/Game/TileMap.hpp"
#include "EraShift/Graphics/Math.hpp"

namespace EraShift::Game {

using Graphics::Rect;
using Graphics::Vec2;

/// An axis-aligned box with a velocity. `size` is the full extent; `position`
/// is the top-left corner, matching `Rect` so no conversion is needed.
struct Body {
    Vec2  position;
    Vec2  velocity;
    Vec2  size{32.0f, 48.0f};

    /// Recomputed by `moveBody`. True when the last move ended resting on a
    /// surface.
    bool  onGround = false;

    /// True when the last move hit a wall on either side. Distinguishes a wall
    /// from a ceiling, which enemies need for wall detection.
    bool  hitWall = false;
    bool  hitCeiling = false;

    [[nodiscard]] Rect rect() const noexcept
    {
        return Rect{position.x, position.y, size.x, size.y};
    }
    [[nodiscard]] Vec2 center() const noexcept { return position + size * 0.5f; }
    [[nodiscard]] float bottom() const noexcept { return position.y + size.y; }
    [[nodiscard]] float right() const noexcept { return position.x + size.x; }

    void setCenter(const Vec2& c) noexcept { position = c - size * 0.5f; }
};

/// What a sweep hit, so callers can react without re-testing the world.
struct MoveResult {
    bool landed  = false;   ///< Stopped against a floor or a one-way platform.
    bool ceiling = false;   ///< Stopped against a ceiling.
    bool wallLeft = false;
    bool wallRight = false;
};

/// Integrates `body.velocity * dt` against the grid, resolving collisions.
///
/// `body.position` is updated in place and `body.onGround` / `hitWall` are
/// recomputed. The body's velocity is zeroed on any axis it collided along, so
/// the caller does not have to clear it.
///
/// The whole level edge counts as solid, so nothing can walk off the world.
MoveResult moveBody(const TileMap& map, Body& body, Era era, float dt);

/// Pushes a body out of any solid cell it is currently inside.
///
/// Needed because the set of solid cells changes under the player's feet when
/// the era shifts: the tile they were standing on stops existing and they would
/// otherwise be stuck inside the next thing that is solid.
void resolvePenetration(const TileMap& map, Body& body, Era era);

} // namespace EraShift::Game
