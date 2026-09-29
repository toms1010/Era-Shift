#include "EraShift/Input/InputManager.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace EraShift::Input {

// ---------------------------------------------------------------------------
// Name tables
//
// These arrays are the single source of truth for the enum <-> string mapping
// so the two can never drift apart. Index == enum value.
// ---------------------------------------------------------------------------
namespace {

using Graphics::clampValue;
using Graphics::Vec2;

/// Mirrors InputManager::kActionCount. A static_assert in the member functions
/// below keeps the two from drifting apart.
constexpr std::size_t kActionCount = 17;
static_assert(static_cast<std::size_t>(Action::Count) == kActionCount,
              "kActionNames table is out of sync with the Action enum");

constexpr std::array<std::string_view, kActionCount> kActionNames = {
    "MoveLeft", "MoveRight", "MoveUp", "MoveDown",
    "Jump", "Dash", "Interact", "Attack", "HeavyAttack", "Aim",
    "ShiftEra", "Inventory", "Map", "QuestLog", "Pause",
    "DebugOverlay", "Screenshot",
};

constexpr std::array<std::string_view, static_cast<std::size_t>(Key::Count)> kKeyNames = {
    "Unknown",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "Escape", "Enter", "Space", "Tab", "Backspace",
    "Up", "Down", "Left", "Right",
    "Left Shift", "Left Ctrl", "Left Alt", "Left Super",
    "Right Shift", "Right Ctrl", "Right Alt", "Right Super",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Comma", "Period", "Slash", "Semicolon", "Apostrophe",
    "Left Bracket", "Right Bracket", "Minus", "Equals", "Backslash", "Grave",
    "Caps Lock", "Scroll Lock", "Num Lock", "Print Screen", "Pause", "Menu",
};

constexpr std::array<std::string_view, static_cast<std::size_t>(MouseButton::Count)> kMouseNames = {
    "Mouse Left", "Mouse Middle", "Mouse Right", "Mouse 4", "Mouse 5",
};

std::string normalise(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '_' || c == '-') {
            out.push_back(' ');
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    // Collapse runs of whitespace.
    std::string collapsed;
    collapsed.reserve(out.size());
    bool previousSpace = false;
    for (const char c : out) {
        const bool isSpace = (c == ' ');
        if (!isSpace || !previousSpace) {
            collapsed.push_back(c);
        }
        previousSpace = isSpace;
    }
    return collapsed;
}

} // namespace

// ---------------------------------------------------------------------------
// Key / button names
// ---------------------------------------------------------------------------
std::string_view keyName(Key key) noexcept
{
    const auto index = static_cast<std::size_t>(key);
    return index < kKeyNames.size() ? kKeyNames[index] : std::string_view{"Unknown"};
}

std::string_view mouseButtonName(MouseButton button) noexcept
{
    const auto index = static_cast<std::size_t>(button);
    return index < kMouseNames.size() ? kMouseNames[index] : std::string_view{"Unknown"};
}

Key parseKey(std::string_view name) noexcept
{
    const std::string needle = normalise(name);
    for (std::size_t i = 1; i < kKeyNames.size(); ++i) {
        if (normalise(kKeyNames[i]) == needle) {
            return static_cast<Key>(i);
        }
    }

    // Single letters and digits are the most common binding shorthand.
    if (needle.size() == 1) {
        const char c = needle[0];
        if (c >= 'a' && c <= 'z') {
            return static_cast<Key>(static_cast<std::size_t>(Key::A) + (c - 'a'));
        }
        if (c >= '0' && c <= '9') {
            return static_cast<Key>(static_cast<std::size_t>(Key::Num0) + (c - '0'));
        }
    }
    return Key::Unknown;
}

MouseButton parseMouseButton(std::string_view name) noexcept
{
    const std::string needle = normalise(name);
    for (std::size_t i = 0; i < kMouseNames.size(); ++i) {
        if (normalise(kMouseNames[i]) == needle) {
            return static_cast<MouseButton>(i);
        }
    }
    return MouseButton::Count;
}

// ---------------------------------------------------------------------------
// Action names
// ---------------------------------------------------------------------------
std::string_view actionName(Action action) noexcept
{
    const auto index = static_cast<std::size_t>(action);
    return index < kActionNames.size() ? kActionNames[index] : std::string_view{"Unknown"};
}

Action parseAction(std::string_view name, bool& ok) noexcept
{
    const std::string needle = normalise(name);
    for (std::size_t i = 0; i < kActionNames.size(); ++i) {
        if (normalise(kActionNames[i]) == needle) {
            ok = true;
            return static_cast<Action>(i);
        }
    }
    ok = false;
    return Action::Count;
}

// ---------------------------------------------------------------------------
// InputState
// ---------------------------------------------------------------------------
void InputState::clearEdges()
{
    keysPressed.fill(false);
    keysReleased.fill(false);
    mousePressed.fill(false);
    mouseReleased.fill(false);
}

void InputState::clearDeltas()
{
    mouseDeltaX = 0.0f;
    mouseDeltaY = 0.0f;
    wheelX      = 0.0f;
    wheelY      = 0.0f;
    textInput.clear();
}

// ---------------------------------------------------------------------------
// InputManager
// ---------------------------------------------------------------------------
void InputManager::setDefaultBindings()
{
    m_bindings = {};

    const auto key = [](Key k) {
        return InputBinding{InputBinding::Type::Key, k, MouseButton::Left};
    };
    const auto mouse = [](MouseButton b) {
        return InputBinding{InputBinding::Type::MouseButton, Key::Unknown, b};
    };

    // WASD and arrows both drive movement: the game should feel right on a
    // keyboard and on a controller without a separate code path.
    m_bindings[static_cast<std::size_t>(Action::MoveLeft)]  = {key(Key::A), key(Key::Left)};
    m_bindings[static_cast<std::size_t>(Action::MoveRight)] = {key(Key::D), key(Key::Right)};
    m_bindings[static_cast<std::size_t>(Action::MoveUp)]    = {key(Key::W), key(Key::Up)};
    m_bindings[static_cast<std::size_t>(Action::MoveDown)]  = {key(Key::S), key(Key::Down)};

    m_bindings[static_cast<std::size_t>(Action::Jump)]       = {key(Key::Space), key(Key::Z)};
    m_bindings[static_cast<std::size_t>(Action::Dash)]       = {key(Key::LeftShift), key(Key::C)};
    m_bindings[static_cast<std::size_t>(Action::Interact)]   = {key(Key::E), key(Key::F)};
    m_bindings[static_cast<std::size_t>(Action::Attack)]     = {mouse(MouseButton::Left), key(Key::J)};
    m_bindings[static_cast<std::size_t>(Action::HeavyAttack)] = {mouse(MouseButton::Right), key(Key::K)};
    m_bindings[static_cast<std::size_t>(Action::Aim)]        = {mouse(MouseButton::Middle), {}};

    // Era Shift is the signature verb, so it gets the most reachable key.
    m_bindings[static_cast<std::size_t>(Action::ShiftEra)]  = {key(Key::Q), key(Key::R)};
    m_bindings[static_cast<std::size_t>(Action::Inventory)] = {key(Key::I), key(Key::Tab)};
    m_bindings[static_cast<std::size_t>(Action::Map)]       = {key(Key::M), {}};
    m_bindings[static_cast<std::size_t>(Action::QuestLog)]  = {key(Key::J), {}};
    m_bindings[static_cast<std::size_t>(Action::Pause)]     = {key(Key::Escape), key(Key::P)};

    m_bindings[static_cast<std::size_t>(Action::DebugOverlay)] = {key(Key::F3), {}};
    m_bindings[static_cast<std::size_t>(Action::Screenshot)]   = {key(Key::F12), {}};
}

std::size_t InputManager::loadBindings(const Core::ConfigStore& config, Core::Logger& log)
{
    std::size_t applied = 0;

    // Expected shape:
    //   "controls": { "Jump": "Space", "Attack": "Mouse Left" }
    // An optional "Secondary" suffix on the action name sets the second binding.
    for (const auto& name : config.keys("controls")) {
        const auto parseName = [&](std::string_view raw, Action& action) {
            const std::string cleaned = normalise(raw);
            const bool secondary = cleaned.ends_with(" secondary");
            const std::string_view base =
                secondary ? std::string_view{cleaned}.substr(0, cleaned.size() - 10) : std::string_view{cleaned};
            bool ok = false;
            action = parseAction(base, ok);
            if (!ok) {
                log.warn("Input", "unknown action '{}' in controls config", raw);
            }
            return ok ? secondary : false;
        };

        Action action = Action::Count;
        const bool secondary = parseName(name, action);
        if (action == Action::Count) {
            continue;
        }

        const std::string raw = config.getString("controls", name, "");
        InputBinding binding{};
        if (const Key asKey = parseKey(raw); asKey != Key::Unknown) {
            binding = InputBinding{InputBinding::Type::Key, asKey, MouseButton::Left};
        } else if (const MouseButton asButton = parseMouseButton(raw);
                   asButton != MouseButton::Count) {
            binding = InputBinding{InputBinding::Type::MouseButton, Key::Unknown, asButton};
        } else {
            log.warn("Input", "control '{}' has unrecognised binding '{}'", name, raw);
            continue;
        }

        if (secondary) {
            m_bindings[static_cast<std::size_t>(action)].secondary = binding;
        } else {
            m_bindings[static_cast<std::size_t>(action)].primary = binding;
        }
        ++applied;
    }

    if (applied > 0) {
        log.info("Input", "loaded {} control bindings from config", applied);
    }
    return applied;
}

void InputManager::rebind(Action action, InputBinding binding)
{
    if (action == Action::Count) {
        return;
    }
    m_bindings[static_cast<std::size_t>(action)].primary = binding;
}

const ActionBinding& InputManager::bindingOf(Action action) const
{
    static const ActionBinding kEmpty{};
    if (action == Action::Count) {
        return kEmpty;
    }
    return m_bindings[static_cast<std::size_t>(action)];
}

std::vector<std::pair<Action, ActionBinding>> InputManager::allBindings() const
{
    std::vector<std::pair<Action, ActionBinding>> result;
    result.reserve(kActionCount);
    for (std::size_t i = 0; i < kActionCount; ++i) {
        result.emplace_back(static_cast<Action>(i), m_bindings[i]);
    }
    return result;
}

void InputManager::beginFrame() noexcept
{
    m_current.clearEdges();
    m_current.clearDeltas();
}

void InputManager::endFrame() noexcept
{
    m_previous = m_current;
}

void InputManager::setFocused(bool focused) noexcept
{
    if (m_current.focused == focused) {
        return;
    }
    if (!focused) {
        // Dropping focus must release every held button, otherwise the player
        // returns to a character that keeps running.
        m_current.keys.fill(false);
        m_current.mouseButtons.fill(false);
    }
    m_current.focused = focused;
}

void InputManager::onKeyDown(Key key)
{
    const auto index = static_cast<std::size_t>(key);
    if (index >= kKeyCount) {
        return;
    }
    if (!m_current.keys[index]) {
        m_current.keysPressed[index] = true;
    }
    m_current.keys[index] = true;
}

void InputManager::onKeyUp(Key key)
{
    const auto index = static_cast<std::size_t>(key);
    if (index >= kKeyCount) {
        return;
    }
    if (m_current.keys[index]) {
        m_current.keysReleased[index] = true;
    }
    m_current.keys[index] = false;
}

void InputManager::onMouseButtonDown(MouseButton button)
{
    const auto index = static_cast<std::size_t>(button);
    if (index >= kMouseCount) {
        return;
    }
    if (!m_current.mouseButtons[index]) {
        m_current.mousePressed[index] = true;
    }
    m_current.mouseButtons[index] = true;
}

void InputManager::onMouseButtonUp(MouseButton button)
{
    const auto index = static_cast<std::size_t>(button);
    if (index >= kMouseCount) {
        return;
    }
    if (m_current.mouseButtons[index]) {
        m_current.mouseReleased[index] = true;
    }
    m_current.mouseButtons[index] = false;
}

void InputManager::onMouseMotion(float x, float y, float deltaX, float deltaY)
{
    m_current.mouseX = x;
    m_current.mouseY = y;
    m_current.mouseDeltaX += deltaX;
    m_current.mouseDeltaY += deltaY;
}

void InputManager::onMouseWheel(float x, float y)
{
    m_current.wheelX += x;
    m_current.wheelY += y;
}

void InputManager::onTextInput(std::string text)
{
    if (m_current.textInput.size() < 256) {
        m_current.textInput += std::move(text);
    }
}

bool InputManager::isKeyDown(Key key) const noexcept
{
    const auto index = static_cast<std::size_t>(key);
    return index < kKeyCount && m_current.keys[index];
}

bool InputManager::wasKeyPressed(Key key) const noexcept
{
    const auto index = static_cast<std::size_t>(key);
    return index < kKeyCount && m_current.keysPressed[index];
}

bool InputManager::wasKeyReleased(Key key) const noexcept
{
    const auto index = static_cast<std::size_t>(key);
    return index < kKeyCount && m_current.keysReleased[index];
}

bool InputManager::isMouseDown(MouseButton button) const noexcept
{
    const auto index = static_cast<std::size_t>(button);
    return index < kMouseCount && m_current.mouseButtons[index];
}

bool InputManager::wasMousePressed(MouseButton button) const noexcept
{
    const auto index = static_cast<std::size_t>(button);
    return index < kMouseCount && m_current.mousePressed[index];
}

bool InputManager::wasMouseReleased(MouseButton button) const noexcept
{
    const auto index = static_cast<std::size_t>(button);
    return index < kMouseCount && m_current.mouseReleased[index];
}

Vec2 InputManager::mousePosition() const noexcept
{
    return {m_current.mouseX, m_current.mouseY};
}

Vec2 InputManager::mouseDelta() const noexcept
{
    return {m_current.mouseDeltaX, m_current.mouseDeltaY};
}

Vec2 InputManager::wheel() const noexcept
{
    return {m_current.wheelX, m_current.wheelY};
}

bool InputManager::isDown(Action action) const noexcept
{
    return m_current.focused && down(action);
}

bool InputManager::wasPressed(Action action) const noexcept
{
    return m_current.focused && pressed(action);
}

bool InputManager::wasReleased(Action action) const noexcept
{
    return m_current.focused && released(action);
}

bool InputManager::down(Action action) const noexcept
{
    const auto& binding = m_bindings[static_cast<std::size_t>(action)];
    const auto test = [this](const InputBinding& b) {
        switch (b.type) {
            case InputBinding::Type::Key:         return isKeyDown(b.key);
            case InputBinding::Type::MouseButton: return isMouseDown(b.button);
            case InputBinding::Type::None:        break;
        }
        return false;
    };
    return test(binding.primary) || test(binding.secondary);
}

bool InputManager::pressed(Action action) const noexcept
{
    const auto& binding = m_bindings[static_cast<std::size_t>(action)];
    const auto test = [this](const InputBinding& b) {
        switch (b.type) {
            case InputBinding::Type::Key:         return wasKeyPressed(b.key);
            case InputBinding::Type::MouseButton: return wasMousePressed(b.button);
            case InputBinding::Type::None:        break;
        }
        return false;
    };
    return test(binding.primary) || test(binding.secondary);
}

bool InputManager::released(Action action) const noexcept
{
    const auto& binding = m_bindings[static_cast<std::size_t>(action)];
    const auto test = [this](const InputBinding& b) {
        switch (b.type) {
            case InputBinding::Type::Key:         return wasKeyReleased(b.key);
            case InputBinding::Type::MouseButton: return wasMouseReleased(b.button);
            case InputBinding::Type::None:        break;
        }
        return false;
    };
    return test(binding.primary) || test(binding.secondary);
}

float InputManager::axisHorizontal() const noexcept
{
    const float axis = static_cast<float>(isDown(Action::MoveRight)) -
                       static_cast<float>(isDown(Action::MoveLeft));
    return clampValue(axis, -1.0f, 1.0f);
}

float InputManager::axisVertical() const noexcept
{
    const float axis = static_cast<float>(isDown(Action::MoveDown)) -
                       static_cast<float>(isDown(Action::MoveUp));
    return clampValue(axis, -1.0f, 1.0f);
}

Vec2 InputManager::normalizedMousePosition() const noexcept
{
    if (m_viewportWidth <= 0.0f || m_viewportHeight <= 0.0f) {
        return {0.0f, 0.0f};
    }
    return {clampValue(m_current.mouseX / m_viewportWidth, 0.0f, 1.0f),
            clampValue(m_current.mouseY / m_viewportHeight, 0.0f, 1.0f)};
}

void InputManager::setViewportSize(float width, float height) noexcept
{
    m_viewportWidth  = (width > 0.0f) ? width : 1.0f;
    m_viewportHeight = (height > 0.0f) ? height : 1.0f;
}

} // namespace EraShift::Input
