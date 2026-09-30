// Era Shift - input.
//
// Devices are polled once per frame into plain state structs; gameplay code
// never sees an SDL event. An `InputMap` translates raw device state into named
// actions ("jump", "interact", "shiftEra") so rebinding is a data change rather
// than a code change.
//
// Edge detection (`pressed`, `released`) is computed once, when the event is
// ingested, and stored in the frame's state. Queries are therefore free and
// correct however many times they are made during a frame - and a query made
// from two different places cannot disagree about the same press.

#pragma once

#include "EraShift/Core/Config.hpp"
#include "EraShift/Core/Log.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace EraShift::Input {

using Graphics::Vec2;

/// Stable identifiers for physical buttons. Values are persisted in the
/// control configuration, so existing entries must never be renumbered.
enum class Key : std::uint16_t {
    Unknown = 0,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    Escape, Enter, Space, Tab, Backspace,
    Up, Down, Left, Right,
    LeftShift, LeftControl, LeftAlt, LeftSuper,
    RightShift, RightControl, RightAlt, RightSuper,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Comma, Period, Slash, Semicolon, Apostrophe,
    LeftBracket, RightBracket, Minus, Equals, Backslash, Grave,

    CapsLock, ScrollLock, NumLock, PrintScreen, Pause, Menu,

    Count
};

enum class MouseButton : std::uint8_t {
    Left = 0,
    Middle,
    Right,
    Extra1,
    Extra2,
    Count
};

/// Human readable name of a key, e.g. "Left Shift". Used by the rebinding UI.
[[nodiscard]] std::string_view keyName(Key key) noexcept;
[[nodiscard]] std::string_view mouseButtonName(MouseButton button) noexcept;

/// Parses a key from a configuration name. Returns Key::Unknown when unknown.
[[nodiscard]] Key parseKey(std::string_view name) noexcept;
[[nodiscard]] MouseButton parseMouseButton(std::string_view name) noexcept;

/// One source of truth for an action: a key, a mouse button, or an axis.
struct InputBinding {
    enum class Type : std::uint8_t {
        None,
        Key,
        MouseButton,
    };

    Type type = Type::None;
    Key  key   = Key::Unknown;
    MouseButton button = MouseButton::Left;

    [[nodiscard]] bool empty() const noexcept { return type == Type::None; }
    [[nodiscard]] bool operator==(const InputBinding& o) const noexcept
    {
        return type == o.type && key == o.key && button == o.button;
    }
};

/// Logical controls. Extend as the player gains verbs.
enum class Action : std::uint16_t {
    MoveLeft = 0,
    MoveRight,
    MoveUp,
    MoveDown,
    Jump,
    Dash,
    Interact,
    Attack,
    HeavyAttack,
    Aim,
    ShiftEra,
    Inventory,
    Map,
    QuestLog,
    Pause,
    DebugOverlay,
    Screenshot,

    Count
};

[[nodiscard]] std::string_view actionName(Action action) noexcept;
[[nodiscard]] Action parseAction(std::string_view name, bool& ok) noexcept;

/// A named action with one or two bindings (primary + secondary) plus an
/// optional mouse wheel axis.
struct ActionBinding {
    InputBinding primary;
    InputBinding secondary;
};

/// Raw device snapshot for one frame.
struct InputState {
    // Raw state arrays, indexed by the enum values above.
    std::array<bool, static_cast<std::size_t>(Key::Count)> keys{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> mouseButtons{};

    // Derived per-frame edges, filled in by InputManager.
    std::array<bool, static_cast<std::size_t>(Key::Count)> keysPressed{};
    std::array<bool, static_cast<std::size_t>(Key::Count)> keysReleased{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> mousePressed{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> mouseReleased{};

    // Mouse
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    float wheelX = 0.0f;
    float wheelY = 0.0f;

    // Keyboard text entry (used by chat and save-slot naming).
    std::string textInput;

    // True while the window has keyboard focus; input is ignored when false so
    // the player does not walk off a ledge after alt-tabbing.
    bool focused = true;

    void clearEdges();
    void clearDeltas();
};

/// The whole input system: device state plus the action mapping.
class InputManager {
public:
    InputManager() = default;

    /// Installs the default bindings (WASD, arrows, and friends).
    void setDefaultBindings();

    /// Overlays bindings from config/controls.json.
    /// @return number of bindings applied.
    std::size_t loadBindings(const Core::ConfigStore& config, Core::Logger& log);

    /// Replaces the binding of a single action. Used by the rebinding UI.
    void rebind(Action action, InputBinding binding);
    [[nodiscard]] const ActionBinding& bindingOf(Action action) const;
    [[nodiscard]] std::vector<std::pair<Action, ActionBinding>> allBindings() const;

    // --- frame lifecycle ----------------------------------------------------
    /// Clears edges and deltas. Call at the very top of a frame.
    void beginFrame() noexcept;
    /// Latches the current state so next frame's edges are correct.
    void endFrame() noexcept;

    /// Raw device queries.
    [[nodiscard]] bool isKeyDown(Key key) const noexcept;
    [[nodiscard]] bool wasKeyPressed(Key key) const noexcept;
    [[nodiscard]] bool wasKeyReleased(Key key) const noexcept;
    [[nodiscard]] bool isMouseDown(MouseButton button) const noexcept;
    [[nodiscard]] bool wasMousePressed(MouseButton button) const noexcept;
    [[nodiscard]] bool wasMouseReleased(MouseButton button) const noexcept;
    [[nodiscard]] Vec2 mousePosition() const noexcept;
    [[nodiscard]] Vec2 mouseDelta() const noexcept;
    [[nodiscard]] Vec2 wheel() const noexcept;
    [[nodiscard]] const std::string& textInput() const noexcept { return m_current.textInput; }
    [[nodiscard]] bool isFocused() const noexcept { return m_current.focused; }
    void setFocused(bool focused) noexcept;

    // --- action queries -----------------------------------------------------
    [[nodiscard]] bool isDown(Action action) const noexcept;
    [[nodiscard]] bool wasPressed(Action action) const noexcept;
    [[nodiscard]] bool wasReleased(Action action) const noexcept;

    /// -1, 0 or +1 on each axis, the form platformers expect.
    [[nodiscard]] float axisHorizontal() const noexcept;
    [[nodiscard]] float axisVertical() const noexcept;

    /// Normalised mouse position in [0,1] across the viewport.
    [[nodiscard]] Vec2 normalizedMousePosition() const noexcept;
    void setViewportSize(float width, float height) noexcept;

    // --- platform event ingestion -------------------------------------------
    // These are called by the SDL event pump; they are public so the pump can be
    // unit tested with synthetic events.

    void onKeyDown(Key key);
    void onKeyUp(Key key);
    void onMouseButtonDown(MouseButton button);
    void onMouseButtonUp(MouseButton button);
    void onMouseMotion(float x, float y, float deltaX, float deltaY);
    void onMouseWheel(float x, float y);
    void onTextInput(std::string text);

    [[nodiscard]] const InputState& state() const noexcept { return m_current; }

private:
    static constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);
    static constexpr std::size_t kKeyCount    = static_cast<std::size_t>(Key::Count);
    static constexpr std::size_t kMouseCount  = static_cast<std::size_t>(MouseButton::Count);

    [[nodiscard]] bool down(Action action) const noexcept;
    [[nodiscard]] bool pressed(Action action) const noexcept;
    [[nodiscard]] bool released(Action action) const noexcept;

    std::array<ActionBinding, kActionCount> m_bindings{};
    InputState m_current;
    float m_viewportWidth  = 1.0f;
    float m_viewportHeight = 1.0f;
};

} // namespace EraShift::Input
