// Era Shift - SDL event pump.
//
// Translates SDL events into engine-level events and feeds the input manager.
// Keeping this in one place means the rest of the engine never includes <SDL3>
// and the translation table is easy to audit.

#pragma once

#include "EraShift/Core/Log.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

union SDL_Event;

namespace EraShift::Input {
class InputManager;
}

namespace EraShift::Application {

/// Engine-level events. Platform events are mapped onto these; gameplay
/// systems subscribe to this enum rather than to SDL events.
enum class EventType : std::uint16_t {
    None = 0,
    QuitRequested,
    WindowResized,
    WindowFocusGained,
    WindowFocusLost,
    WindowClosed,
    KeyPressed,
    KeyReleased,
    TextInput,
    MouseMoved,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseWheel,
    ScreenshotRequested,
};

struct Event {
    EventType type = EventType::None;

    // Key / mouse payload
    Input::Key          key      = Input::Key::Unknown;
    Input::MouseButton  button   = Input::MouseButton::Left;

    // Pointer payload, in logical (render) coordinates.
    Graphics::Vec2 position;
    Graphics::Vec2 delta;
    float            wheelX = 0.0f;
    float            wheelY = 0.0f;

    // Window payload
    int  width  = 0;
    int  height = 0;

    std::string text;
    /// Filesystem path, for ScreenshotRequested.
    std::filesystem::path path;

    [[nodiscard]] bool isValid() const noexcept { return type != EventType::None; }
};

/// Simple multicast event bus.
///
/// A deliberate choice over a more elaborate dispatcher: the number of
/// subscribers in Era Shift is small and the lifetime is easy to reason about
/// because handlers are invoked synchronously in registration order.
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    /// Subscribes to a single event type.
    /// @return a token that unsubscribes on destruction.
    class Subscription {
    public:
        Subscription() = default;
        Subscription(EventBus* bus, EventType type, std::uint64_t id) noexcept
            : m_bus(bus), m_type(type), m_id(id) {}
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept { *this = std::move(other); }
        Subscription& operator=(Subscription&& other) noexcept;
        ~Subscription() { reset(); }

        void reset() noexcept;
        [[nodiscard]] bool active() const noexcept { return m_bus != nullptr; }

    private:
        EventBus* m_bus = nullptr;
        EventType m_type = EventType::None;
        std::uint64_t m_id = 0;
    };

    [[nodiscard]] Subscription subscribe(EventType type, Handler handler);

    void publish(const Event& event);

    /// Number of live subscriptions for a type; used by tests and debug tools.
    [[nodiscard]] std::size_t subscriberCount(EventType type) const;

    void clear();

private:
    void unsubscribe(EventType type, std::uint64_t id);

    std::unordered_map<int, std::unordered_map<std::uint64_t, Handler>> m_handlers;
    std::uint64_t m_nextId = 1;
};

/// Drains SDL's event queue each frame.
class EventPump {
public:
    EventPump(Input::InputManager& input, EventBus& bus, Core::Logger& log);

    /// Converts the `count` most recent SDL events.
    /// @return number of events dispatched.
    std::size_t pump(SDL_Event* events, int count);

    /// Pulls events from SDL itself. Used by the standard application loop.
    ///
    /// Quitting arrives on the bus like everything else, so there is no
    /// out-parameter for it: the previous one was discarded by every caller, so
    /// anything relying on it would silently never quit.
    ///
    /// @return number of events dispatched.
    std::size_t pumpFromSDL();

    void setViewportSize(int width, int height) noexcept;

private:
    Input::InputManager* m_input = nullptr;
    EventBus*            m_bus   = nullptr;
    Core::Logger*        m_log   = nullptr;
    int m_viewportWidth  = 0;
    int m_viewportHeight = 0;
};

} // namespace EraShift::Application
