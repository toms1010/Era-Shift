#include "EraShift/Application/EventBus.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace EraShift::Application {

using Input::Key;
using Input::MouseButton;

// ---------------------------------------------------------------------------
// EventBus
// ---------------------------------------------------------------------------
EventBus::Subscription EventBus::subscribe(EventType type, Handler handler)
{
    if (!handler) {
        return Subscription{};
    }
    const std::uint64_t id = m_nextId++;
    m_handlers[static_cast<int>(type)].emplace(id, std::move(handler));
    return Subscription{this, type, id};
}

void EventBus::Subscription::reset() noexcept
{
    if (m_bus != nullptr) {
        m_bus->unsubscribe(m_type, m_id);
        m_bus = nullptr;
    }
}

EventBus::Subscription& EventBus::Subscription::operator=(Subscription&& other) noexcept
{
    if (this != &other) {
        reset();
        m_bus = other.m_bus;
        m_type = other.m_type;
        m_id = other.m_id;
        other.m_bus = nullptr;
    }
    return *this;
}

void EventBus::unsubscribe(EventType type, std::uint64_t id)
{
    const auto it = m_handlers.find(static_cast<int>(type));
    if (it == m_handlers.end()) {
        return;
    }
    it->second.erase(id);
    if (it->second.empty()) {
        m_handlers.erase(it);
    }
}

void EventBus::publish(const Event& event)
{
    const auto it = m_handlers.find(static_cast<int>(event.type));
    if (it == m_handlers.end()) {
        return;
    }
    // Iterate over a copy of the ids: a handler is allowed to subscribe or
    // unsubscribe during dispatch (menus closing on a click, for example).
    std::vector<std::uint64_t> ids;
    ids.reserve(it->second.size());
    for (const auto& [id, handler] : it->second) {
        static_cast<void>(handler);
        ids.push_back(id);
    }
    for (const std::uint64_t id : ids) {
        const auto handler = m_handlers.find(static_cast<int>(event.type));
        if (handler == m_handlers.end()) {
            break;
        }
        const auto entry = handler->second.find(id);
        if (entry == handler->second.end()) {
            continue;
        }
        entry->second(event);
    }
}

std::size_t EventBus::subscriberCount(EventType type) const
{
    const auto it = m_handlers.find(static_cast<int>(type));
    return it == m_handlers.end() ? 0 : it->second.size();
}

void EventBus::clear()
{
    m_handlers.clear();
}

// ---------------------------------------------------------------------------
// SDL key translation
// ---------------------------------------------------------------------------
namespace {

Key translateKey(SDL_Keycode code) noexcept
{
    using K = Key;

    switch (code) {
        case SDLK_A: return K::A;
        case SDLK_B: return K::B;
        case SDLK_C: return K::C;
        case SDLK_D: return K::D;
        case SDLK_E: return K::E;
        case SDLK_F: return K::F;
        case SDLK_G: return K::G;
        case SDLK_H: return K::H;
        case SDLK_I: return K::I;
        case SDLK_J: return K::J;
        case SDLK_K: return K::K;
        case SDLK_L: return K::L;
        case SDLK_M: return K::M;
        case SDLK_N: return K::N;
        case SDLK_O: return K::O;
        case SDLK_P: return K::P;
        case SDLK_Q: return K::Q;
        case SDLK_R: return K::R;
        case SDLK_S: return K::S;
        case SDLK_T: return K::T;
        case SDLK_U: return K::U;
        case SDLK_V: return K::V;
        case SDLK_W: return K::W;
        case SDLK_X: return K::X;
        case SDLK_Y: return K::Y;
        case SDLK_Z: return K::Z;

        case SDLK_0: return K::Num0;
        case SDLK_1: return K::Num1;
        case SDLK_2: return K::Num2;
        case SDLK_3: return K::Num3;
        case SDLK_4: return K::Num4;
        case SDLK_5: return K::Num5;
        case SDLK_6: return K::Num6;
        case SDLK_7: return K::Num7;
        case SDLK_8: return K::Num8;
        case SDLK_9: return K::Num9;

        case SDLK_ESCAPE:    return K::Escape;
        case SDLK_RETURN:    return K::Enter;
        case SDLK_SPACE:     return K::Space;
        case SDLK_TAB:       return K::Tab;
        case SDLK_BACKSPACE: return K::Backspace;
        case SDLK_UP:        return K::Up;
        case SDLK_DOWN:      return K::Down;
        case SDLK_LEFT:      return K::Left;
        case SDLK_RIGHT:     return K::Right;

        case SDLK_LSHIFT: return K::LeftShift;
        case SDLK_LCTRL:  return K::LeftControl;
        case SDLK_LALT:   return K::LeftAlt;
        case SDLK_LGUI:   return K::LeftSuper;
        case SDLK_RSHIFT: return K::RightShift;
        case SDLK_RCTRL:  return K::RightControl;
        case SDLK_RALT:   return K::RightAlt;
        case SDLK_RGUI:   return K::RightSuper;

        case SDLK_F1:  return K::F1;
        case SDLK_F2:  return K::F2;
        case SDLK_F3:  return K::F3;
        case SDLK_F4:  return K::F4;
        case SDLK_F5:  return K::F5;
        case SDLK_F6:  return K::F6;
        case SDLK_F7:  return K::F7;
        case SDLK_F8:  return K::F8;
        case SDLK_F9:  return K::F9;
        case SDLK_F10: return K::F10;
        case SDLK_F11: return K::F11;
        case SDLK_F12: return K::F12;

        case SDLK_COMMA:     return K::Comma;
        case SDLK_PERIOD:    return K::Period;
        case SDLK_SLASH:     return K::Slash;
        case SDLK_SEMICOLON: return K::Semicolon;
        case SDLK_APOSTROPHE: return K::Apostrophe;
        case SDLK_LEFTBRACKET:  return K::LeftBracket;
        case SDLK_RIGHTBRACKET: return K::RightBracket;
        case SDLK_MINUS:     return K::Minus;
        case SDLK_EQUALS:    return K::Equals;
        case SDLK_BACKSLASH: return K::Backslash;
        case SDLK_GRAVE:     return K::Grave;

        case SDLK_CAPSLOCK:   return K::CapsLock;
        case SDLK_SCROLLLOCK: return K::ScrollLock;
        case SDLK_NUMLOCKCLEAR:return K::NumLock;
        case SDLK_PRINTSCREEN: return K::PrintScreen;
        case SDLK_PAUSE:      return K::Pause;
        case SDLK_MENU:       return K::Menu;

        default: return K::Unknown;
    }
}

MouseButton translateMouseButton(std::uint8_t button) noexcept
{
    switch (button) {
        case SDL_BUTTON_LEFT:   return MouseButton::Left;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        case SDL_BUTTON_RIGHT:  return MouseButton::Right;
        case SDL_BUTTON_X1:     return MouseButton::Extra1;
        case SDL_BUTTON_X2:     return MouseButton::Extra2;
        default:                return MouseButton::Count;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// EventPump
// ---------------------------------------------------------------------------
EventPump::EventPump(Input::InputManager& input, EventBus& bus, Core::Logger& log)
    : m_input(&input), m_bus(&bus), m_log(&log)
{
}

void EventPump::setViewportSize(int width, int height) noexcept
{
    m_viewportWidth  = width;
    m_viewportHeight = height;
    if (m_input != nullptr) {
        m_input->setViewportSize(static_cast<float>(width), static_cast<float>(height));
    }
}

std::size_t EventPump::pump(SDL_Event* events, int count)
{
    if (events == nullptr || count <= 0) {
        return 0;
    }

    std::size_t dispatched = 0;

    for (int i = 0; i < count; ++i) {
        SDL_Event& sdl = events[i];
        Event event;

        switch (sdl.type) {
            case SDL_EVENT_QUIT:
                event.type = EventType::QuitRequested;
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                event.type = EventType::WindowClosed;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                event.type  = EventType::WindowResized;
                event.width  = sdl.window.data1;
                event.height = sdl.window.data2;
                setViewportSize(event.width, event.height);
                break;
            }

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                event.type = EventType::WindowFocusGained;
                if (m_input != nullptr) {
                    m_input->setFocused(true);
                }
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                event.type = EventType::WindowFocusLost;
                if (m_input != nullptr) {
                    m_input->setFocused(false);
                }
                break;

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                const bool down = (sdl.type == SDL_EVENT_KEY_DOWN);
                // Ignore auto-repeat for the "pressed" edge; the held state
                // still updates so movement is smooth.
                if (!down && sdl.key.repeat) {
                    continue;
                }
                event.key  = translateKey(sdl.key.key);
                event.type = down ? EventType::KeyPressed : EventType::KeyReleased;
                if (event.key == Key::Unknown) {
                    continue;
                }
                if (m_input != nullptr) {
                    if (down) {
                        m_input->onKeyDown(event.key);
                    } else {
                        m_input->onKeyUp(event.key);
                    }
                }
                break;
            }

            case SDL_EVENT_TEXT_INPUT: {
                event.type = EventType::TextInput;
                event.text = (sdl.text.text != nullptr) ? sdl.text.text : "";
                if (m_input != nullptr) {
                    m_input->onTextInput(event.text);
                }
                break;
            }

            case SDL_EVENT_MOUSE_MOTION: {
                event.type     = EventType::MouseMoved;
                event.position = {sdl.motion.x, sdl.motion.y};
                event.delta    = {sdl.motion.xrel, sdl.motion.yrel};
                if (m_input != nullptr) {
                    m_input->onMouseMotion(event.position.x, event.position.y,
                                           event.delta.x, event.delta.y);
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const bool down = (sdl.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                event.button  = translateMouseButton(sdl.button.button);
                event.position = {sdl.button.x, sdl.button.y};
                event.type    = down ? EventType::MouseButtonPressed : EventType::MouseButtonReleased;
                if (event.button == MouseButton::Count) {
                    continue;
                }
                if (m_input != nullptr) {
                    if (down) {
                        m_input->onMouseButtonDown(event.button);
                    } else {
                        m_input->onMouseButtonUp(event.button);
                    }
                }
                break;
            }

            case SDL_EVENT_MOUSE_WHEEL: {
                event.type   = EventType::MouseWheel;
                event.wheelX = sdl.wheel.x;
                event.wheelY = sdl.wheel.y;
                if (m_input != nullptr) {
                    m_input->onMouseWheel(event.wheelX, event.wheelY);
                }
                break;
            }

            default:
                continue;
        }

        if (m_bus != nullptr) {
            m_bus->publish(event);
        }
        ++dispatched;
    }

    return dispatched;
}

std::size_t EventPump::pumpFromSDL()
{
    SDL_Event events[64];
    // Drain the queue so a burst of input (a key held through a frame hitch) is
    // not spread across several frames. The cap stops a runaway event producer
    // from starving the simulation.
    int total = 0;
    int count = 0;
    do {
        count = SDL_PollEvent(events);
        total += count;
    } while (count > 0 && total < 512);

    const std::size_t dispatched = pump(events, total);
    return dispatched;
}

} // namespace EraShift::Application
