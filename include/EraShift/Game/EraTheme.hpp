// Era Shift - era presentation.
//
// The gameplay layer decides *what* the world is; this decides how it looks.
// Split into its own header because more than one thing needs it now: the
// playing state draws with it, and the feedback system needs the accent colour
// of the current era to tint particles, glows and impact flashes. Neither of
// those is really the "playing state"'s business, and having the feedback system
// reach into PlayingState.hpp for a colour would be the wrong dependency.

#pragma once

#include "EraShift/Game/Era.hpp"
#include "EraShift/Graphics/Color.hpp"

namespace EraShift::Game {

using Graphics::Color;

/// Presentation values for one era.
struct EraTheme {
    Color sky;
    Color skyLow;
    Color solid;
    Color solidEdge;
    Color oneWay;
    Color hazard;
    Color actor;
    Color accent;
};

/// The three era themes, in `Era` order.
[[nodiscard]] const EraTheme& themeFor(Era era);

/// Cross-dissolves two themes. `t` of 0 is `from`, 1 is `to`.
///
/// A shift draws both eras at once and slides between them, which is what makes
/// a shift read as the world changing rather than the palette being swapped
/// under a still frame.
[[nodiscard]] EraTheme blendThemes(const EraTheme& from, const EraTheme& to, float t);

} // namespace EraShift::Game
