// Era Shift - scripted input for demos and smoke tests.
//
// This exists for two reasons, both of them practical:
//
//   * Attract mode. A game sitting on a menu is a game nobody can tell works.
//   * The visual smoke test. Screenshotting the title screen proves the title
//     screen renders; it proves nothing about whether the player, the era
//     shift and the combat work. Driving them from a script means every frame
//     the automated capture looks at is one a human would see.
//
// The script is deterministic and frame-indexed rather than time-indexed, so it
// replays identically on any machine.

#pragma once

#include "EraShift/Input/InputManager.hpp"

#include <cstdint>
#include <vector>

namespace EraShift::Core {

/// One control transition at a simulation step.
///
/// The attack is bound to the mouse rather than the keyboard, so a script that
/// only knew about keys could not demonstrate combat at all.
struct DemoEvent {
    enum class Control : std::uint8_t { Key, Mouse };

    std::uint64_t step    = 0;   ///< Fixed-step index to apply on.
    Control       control = Control::Key;
    Input::Key    key     = Input::Key::Unknown;
    Input::MouseButton mouse = Input::MouseButton::Left;
    bool          down    = false;
};

/// Replays a fixed key sequence into an InputManager.
class DemoDriver {
public:
    DemoDriver() = default;

    /// The built-in attract script: walk right, jump, swing, shift era, dash.
    /// Chosen to touch every system a screenshot can show.
    [[nodiscard]] static DemoDriver attract();

    [[nodiscard]] bool active() const noexcept { return m_active; }

    /// Feeds the script's transitions for `step` into `input`, and releases
    /// anything still held from a previous step.
    ///
    /// Real device input is deliberately ignored while a demo is running: a
    /// stray keypress during an attract loop must not fight the script.
    void apply(Input::InputManager& input, std::uint64_t step);

private:
    std::vector<DemoEvent> m_events;
    /// Controls currently held down by the script.
    std::vector<DemoEvent> m_held;
    bool m_active = false;
};

} // namespace EraShift::Core
