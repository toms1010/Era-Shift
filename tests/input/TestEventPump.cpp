// Tests for the SDL event pump.
//
// This is the one part of input that cannot live in `erashift_core`, because it
// is the boundary where `SDL_Event` becomes the game's own types. So it gets its
// own small binary that links the engine.
//
// The bug these tests exist for was not a missing feature but a memory-safety
// one: `pumpFromSDL` read the whole SDL queue into a 64-slot array while counting
// up to 512, so a burst of more than 64 events made it hand several hundred
// `SDL_Event`s to a function that would then read uninitialised stack. Every
// mouse movement is its own event, so passing 64 in a single frame is ordinary
// rather than exceptional - and a garbage `SDL_EVENT_MOUSE_MOTION` writes a
// random position into the input state, which is exactly the "I clicked the
// button and nothing happened" symptom.
//
// No display is needed: `EventPump::pump` takes the events as an argument, so
// these tests construct `SDL_Event` values directly and never initialise SDL.

#include "EraShift/Application/EventBus.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

#include <doctest/doctest.h>

using namespace EraShift;

namespace {

/// A key event, built the way SDL builds one.
SDL_Event keyEvent(SDL_EventType type, SDL_Keycode key, bool repeat = false)
{
    SDL_Event event;
    SDL_zero(event);
    event.type           = type;
    event.key.key        = key;
    event.key.repeat     = repeat;
    event.key.timestamp  = 0;
    return event;
}

SDL_Event buttonEvent(SDL_EventType type, std::uint8_t button, int x, int y)
{
    SDL_Event event;
    SDL_zero(event);
    event.type           = type;
    event.button.button  = button;
    event.button.x       = x;
    event.button.y       = y;
    return event;
}

SDL_Event motionEvent(int x, int y, int xrel, int yrel)
{
    SDL_Event event;
    SDL_zero(event);
    event.type       = SDL_EVENT_MOUSE_MOTION;
    event.motion.x   = x;
    event.motion.y   = y;
    event.motion.xrel = xrel;
    event.motion.yrel = yrel;
    return event;
}

SDL_Event focusEvent(SDL_EventType type)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    return event;
}

} // namespace

TEST_CASE("a key press reaches the input manager as an edge")
{
    Core::Logger log;
    Input::InputManager input;
    // Without bindings, no key maps to any action and every query is trivially
    // false - which is a test that passes for the wrong reason.
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    input.beginFrame();
    SDL_Event events[] = {keyEvent(SDL_EVENT_KEY_DOWN, SDLK_D),
                          keyEvent(SDL_EVENT_KEY_UP, SDLK_D)};
    CHECK(pump.pump(events, 2) == 2);

    // The edge and the held state, from the same press.
    CHECK(input.wasPressed(Input::Action::MoveRight));
    // ... and the key is up, so nothing is being held.
    CHECK_FALSE(input.isDown(Input::Action::MoveRight));

    // The edge survives endFrame. It is cleared by the *next* beginFrame, which
    // is the contract: an edge must stay readable for the whole frame it
    // happened in, including every fixed update step, however many of those the
    // frame contained.
    input.endFrame();
    CHECK(input.wasPressed(Input::Action::MoveRight));

    input.beginFrame();
    input.endFrame();
    CHECK_FALSE(input.wasPressed(Input::Action::MoveRight));
}

TEST_CASE("a burst larger than the pump's buffer loses nothing")
{
    // The regression. 200 events is three times the internal array size, and it
    // is the kind of number a mouse sweep produces in a single frame.
    Core::Logger log;
    Input::InputManager input;
    // Without bindings, no key maps to any action and every query is trivially
    // false - which is a test that passes for the wrong reason.
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    std::vector<SDL_Event> events;
    events.reserve(200);
    for (int i = 0; i < 200; ++i) {
        events.push_back(motionEvent(100 + i, 200 + i, 1, 1));
    }
    // The last position is what the input manager should end up holding.
    events.push_back(motionEvent(640, 480, 0, 0));

    input.beginFrame();
    const std::size_t dispatched = pump.pump(events.data(), static_cast<int>(events.size()));

    // Every event was dispatched. Before the fix this read past the end of a
    // 64-element array, so the count was right by accident and the *contents*
    // were not - which is why the assertions below are about state, not counts.
    CHECK(dispatched == events.size());
    CHECK(input.mousePosition().x == 640.0f);
    CHECK(input.mousePosition().y == 480.0f);
    input.endFrame();
}

TEST_CASE("a key pressed in a large burst is not dropped")
{
    // The second half of the same bug, and the one a player notices. The key
    // event sits in the middle of a large burst; with the old code the events
    // after the first 64 were never examined, so the press simply did not exist.
    Core::Logger log;
    Input::InputManager input;
    // Without bindings, no key maps to any action and every query is trivially
    // false - which is a test that passes for the wrong reason.
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    std::vector<SDL_Event> events;
    for (int i = 0; i < 100; ++i) {
        events.push_back(motionEvent(i, i, 0, 0));
    }
    events.push_back(keyEvent(SDL_EVENT_KEY_DOWN, SDLK_SPACE));
    for (int i = 0; i < 100; ++i) {
        events.push_back(motionEvent(i, i, 0, 0));
    }

    input.beginFrame();
    pump.pump(events.data(), static_cast<int>(events.size()));
    CHECK(input.wasPressed(Input::Action::Jump));
    input.endFrame();
}

TEST_CASE("auto-repeat does not register as a new press")
{
    // A held key produces repeats. Treating each as a fresh press means one
    // held key triggers an action several times per frame.
    Core::Logger log;
    Input::InputManager input;
    // Without bindings, no key maps to any action and every query is trivially
    // false - which is a test that passes for the wrong reason.
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    input.beginFrame();
    SDL_Event events[] = {
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_D),
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_D, /*repeat=*/true),
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_D, /*repeat=*/true),
    };
    pump.pump(events, 3);
    CHECK(input.wasPressed(Input::Action::MoveRight));
    input.endFrame();

    // And the repeat did not disturb the held state.
    CHECK(input.isDown(Input::Action::MoveRight));
}

TEST_CASE("a mouse click reports a position and a button")
{
    Core::Logger log;
    Input::InputManager input;
    // Without bindings, no key maps to any action and every query is trivially
    // false - which is a test that passes for the wrong reason.
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    input.beginFrame();
    SDL_Event events[] = {
        buttonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 300, 200),
        buttonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 300, 200),
    };
    pump.pump(events, 2);

    CHECK(input.wasMousePressed(Input::MouseButton::Left));
    // The position comes from the event, which is what makes click hit-testing
    // work at all.
    CHECK(input.mousePosition().x == 300.0f);
    CHECK(input.mousePosition().y == 200.0f);
    input.endFrame();
}

TEST_CASE("input works before any focus event has been delivered")
{
    // Not hypothetical: on the Wayland session this was developed against, a
    // window produced zero focus-gained and zero focus-lost events in four
    // seconds. The old queries gated every read on `focused`, so any code path
    // that depended on that event was a game that never responded.
    Core::Logger log;
    Input::InputManager input;
    // Without bindings, no key maps to any action and every query is trivially
    // false - which is a test that passes for the wrong reason.
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    input.beginFrame();
    SDL_Event events[] = {keyEvent(SDL_EVENT_KEY_DOWN, SDLK_D)};
    pump.pump(events, 1);
    CHECK(input.wasPressed(Input::Action::MoveRight));
    input.endFrame();
}

TEST_CASE("losing focus releases held keys so nothing is left running")
{
    // The reason focus is tracked at all. This is the behaviour the gating was
    // supposed to provide, and it is provided directly by the release instead.
    Core::Logger log;
    Input::InputManager input;
    // Without bindings, no key maps to any action and every query is trivially
    // false - which is a test that passes for the wrong reason.
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    input.beginFrame();
    SDL_Event down[] = {keyEvent(SDL_EVENT_KEY_DOWN, SDLK_D)};
    pump.pump(down, 1);
    input.endFrame();
    CHECK(input.isDown(Input::Action::MoveRight));

    input.beginFrame();
    SDL_Event lost[] = {focusEvent(SDL_EVENT_WINDOW_FOCUS_LOST)};
    pump.pump(lost, 1);
    input.endFrame();

    // Alt-tabbed away mid-stride: the character must not keep running.
    CHECK_FALSE(input.isDown(Input::Action::MoveRight));

    // And focus coming back must not require a key event to restore input.
    input.beginFrame();
    SDL_Event gained[] = {focusEvent(SDL_EVENT_WINDOW_FOCUS_GAINED)};
    pump.pump(gained, 1);
    SDL_Event pressed[] = {keyEvent(SDL_EVENT_KEY_DOWN, SDLK_D)};
    pump.pump(pressed, 1);
    CHECK(input.wasPressed(Input::Action::MoveRight));
    input.endFrame();
}

TEST_CASE("an unknown key is ignored rather than indexing out of bounds")
{
    Core::Logger log;
    Input::InputManager input;
    // Without bindings, no key maps to any action and every query is trivially
    // false - which is a test that passes for the wrong reason.
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    input.beginFrame();
    SDL_Event events[] = {keyEvent(SDL_EVENT_KEY_DOWN, SDLK_UNKNOWN)};
    pump.pump(events, 1);
    // Nothing to assert beyond surviving: the key count is an array index, and
    // an unfiltered unknown key is a buffer overrun in InputManager.
    input.endFrame();
    // Nothing was latched, and nothing outside the key array was written.
    for (int i = 0; i < 6; ++i) {
        const auto action = static_cast<Input::Action>(i);
        CHECK_FALSE(input.isDown(action));
    }
}

TEST_CASE("null and empty input are no-ops")
{
    Core::Logger log;
    Input::InputManager input;
    // Without bindings, no key maps to any action and every query is trivially
    // false - which is a test that passes for the wrong reason.
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    CHECK(pump.pump(nullptr, 0) == 0);
    CHECK(pump.pump(nullptr, 5) == 0);
    SDL_Event event = keyEvent(SDL_EVENT_KEY_DOWN, SDLK_D);
    CHECK(pump.pump(&event, 0) == 0);
}

TEST_CASE("F12 reaches the screenshot action, and no dead key is bound to anything")
{
    // F12 was documented in the README and bound in the config, and pressing it
    // did nothing: the event that the engine was subscribed to had no publisher.
    // This test only covers the half that a platform test can honestly reach -
    // that the key becomes the action. The publish -> subscribe -> save half is
    // the same `requestScreenshot` path the `--screenshot` flag exercises, so it
    // is verified by running the binary rather than by this test.
    Core::Logger log;
    Input::InputManager input;
    input.setDefaultBindings();
    Application::EventBus bus;
    Application::EventPump pump(input, bus, log);

    input.beginFrame();
    SDL_Event down[] = {keyEvent(SDL_EVENT_KEY_DOWN, SDLK_F12)};
    pump.pump(down, 1);
    CHECK(input.wasPressed(Input::Action::Screenshot));
    input.endFrame();

    // Held, not re-pressed, and released on the way up - the same edge handling
    // every other action gets, so a held F12 cannot queue a screenshot per frame.
    input.beginFrame();
    pump.pump(down, 1);
    CHECK_FALSE(input.wasPressed(Input::Action::Screenshot));
    CHECK(input.isDown(Input::Action::Screenshot));
    input.endFrame();

    input.beginFrame();
    SDL_Event up[] = {keyEvent(SDL_EVENT_KEY_UP, SDLK_F12)};
    pump.pump(up, 1);
    CHECK_FALSE(input.isDown(Input::Action::Screenshot));
    input.endFrame();
}
