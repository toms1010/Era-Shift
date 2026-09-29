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

#include <string_view>

namespace EraShift {

namespace Core {
class Logger;
class ConfigManager;
class SystemClock;
class GameLoop;
class StateMachine;
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
    Graphics::Window*          window    = nullptr;
    Graphics::TextRenderer*    text      = nullptr;

    /// Build identification shown on the title screen and in the overlay.
    std::string_view buildLabel = "Development Build";
    std::string_view version    = "0.0.0";

    /// A context is usable once the log and the renderer exist.
    [[nodiscard]] bool valid() const noexcept
    {
        return log != nullptr && states != nullptr && renderer != nullptr;
    }
};

} // namespace EraShift
