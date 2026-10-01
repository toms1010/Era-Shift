// Era Shift - the services available to a game state.
//
// This is a plain aggregate of pointers, not a class with behaviour. That is a
// deliberate decision:
//
//   * A state can reach exactly what it needs and nothing else, so the
//     dependency surface of each state is visible by inspection.
//   * The type is trivially copyable, so Engine can hold one instance and hand
//     the same object to every state without ownership questions.
//   * Gameplay code can be unit tested with a context whose members point at
//     fakes, or at nothing at all, without needing a running engine.
//
// It lives in Core (not Application) because the state machine in
// Core/GameState.hpp needs the real type, not a forward declaration. It stays
// free of SDL despite naming SDL-backed systems, because every member is a
// pointer to an incomplete-at-this-point type.

#pragma once

#include <filesystem>
#include <string_view>

namespace EraShift {

namespace Core {
class Logger;
class ConfigManager;
class SystemClock;
class GameLoop;
class StateMachine;
class ProgressDatabase;
}
namespace Input {
class InputManager;
}
namespace Graphics {
class Renderer2D;
class ResourceManager;
class TextRenderer;
class Window;
}
namespace Debug {
class PerformanceStats;
class DebugOverlay;
}
namespace Application {
class EventBus;
}

struct StateContext {
    Core::Logger*              log       = nullptr;
    Core::StateMachine*        states    = nullptr;
    Core::GameLoop*            loop      = nullptr;
    Input::InputManager*       input     = nullptr;
    Graphics::Renderer2D*      renderer  = nullptr;
    Graphics::ResourceManager* resources = nullptr;
    Debug::PerformanceStats*   stats     = nullptr;
    Debug::DebugOverlay*       overlay   = nullptr;
    Application::EventBus*     events    = nullptr;
    Core::SystemClock*         clock     = nullptr;
    Core::ConfigManager*       config    = nullptr;
    /// Progression: unlocks, per-level results, checkpoints, tutorial completion.
    ///
    /// Optional. Null when SQLite could not be opened, which every caller treats
    /// as "no progression to record" rather than as an error — a player with no
    /// database still plays the game, they just do not get unlocks.
    Core::ProgressDatabase*   progress  = nullptr;
    Graphics::Window*          window    = nullptr;
    Graphics::TextRenderer*    text      = nullptr;

    /// Build identification shown on the title screen and in the overlay.
    std::string_view buildLabel = "Development Build";
    std::string_view version    = "0.0.0";

    /// Level file to load. Empty means the shipped region, which is what the
    /// state falls back to when the file is missing or malformed.
    ///
    /// Content rather than a service, alongside the build label: the states
    /// cannot reach the engine, and reaching the game facade to ask it what
    /// level to load would be a back door round the state machine.
    std::filesystem::path levelPath;

    /// Directory the level select lists, and the directory `levelPath` is
    /// resolved against.
    ///
    /// Relative paths elsewhere in the context are resolved against the
    /// content root, so this belongs next to them. Defaulting to
    /// `data/levels` rather than leaving it empty means a caller that sets only
    /// this gets the shipped regions.
    std::filesystem::path levelDirectory = "data/levels";

    /// A context is usable once the log and the renderer exist.
    [[nodiscard]] bool valid() const noexcept
    {
        return log != nullptr && states != nullptr && renderer != nullptr;
    }
};

} // namespace EraShift
