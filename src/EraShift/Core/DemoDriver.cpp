#include "EraShift/Core/DemoDriver.hpp"

#include <algorithm>

namespace EraShift::Core {

namespace {

/// Adds a press-and-release pair so a control is never left stuck down.
void tap(std::vector<DemoEvent>& events, std::uint64_t step, Input::Key key,
         std::uint64_t holdSteps = 6)
{
    DemoEvent press;
    press.step    = step;
    press.control = DemoEvent::Control::Key;
    press.key     = key;
    press.down    = true;
    events.push_back(press);

    DemoEvent release = press;
    release.step = step + holdSteps;
    release.down = false;
    events.push_back(release);
}

void click(std::vector<DemoEvent>& events, std::uint64_t step, std::uint64_t holdSteps = 4)
{
    DemoEvent press;
    press.step    = step;
    press.control = DemoEvent::Control::Mouse;
    press.mouse   = Input::MouseButton::Left;
    press.down    = true;
    events.push_back(press);

    DemoEvent release = press;
    release.step = step + holdSteps;
    release.down = false;
    events.push_back(release);
}

/// Holds a key down for a span.
void hold(std::vector<DemoEvent>& events, std::uint64_t step, Input::Key key,
          std::uint64_t until)
{
    DemoEvent press;
    press.step    = step;
    press.control = DemoEvent::Control::Key;
    press.key     = key;
    press.down    = true;
    events.push_back(press);

    DemoEvent release = press;
    release.step = until;
    release.down = false;
    events.push_back(release);
}

} // namespace

DemoDriver DemoDriver::attract()
{
    DemoDriver driver;
    driver.m_active = true;

    std::vector<DemoEvent>& events = driver.m_events;
    events.reserve(64);

    // The script is built around the one thing the game is about: the first
    // chasm only has a floor in the Past, so the shift has to happen *before*
    // the player reaches it. That makes the attract loop a demonstration of the
    // mechanic rather than a walk into a pit.
    //
    // Present -> Future -> Past is two shifts, because the cycle is Past ->
    // Present -> Future -> Past and the run starts in the Present.

    hold(events, 40, Input::Key::D, 200);

    tap(events, 50, Input::Key::Q);   // Future: the palette and the tile layers change
    tap(events, 70, Input::Key::Q);   // Past: the crumbling bridge exists again
    tap(events, 96, Input::Key::E);   // the Past seal, on the way past

    click(events, 150);
    tap(events, 176, Input::Key::Space, 12);

    tap(events, 210, Input::Key::Q);  // Present
    tap(events, 232, Input::Key::Q);  // Future, where the wisps are awake

    hold(events, 250, Input::Key::D, 320);

    tap(events, 350, Input::Key::Q);
    tap(events, 372, Input::Key::Q);
    tap(events, 404, Input::Key::Escape);

    std::sort(events.begin(), events.end(),
              [](const DemoEvent& a, const DemoEvent& b) { return a.step < b.step; });
    return driver;
}

namespace {

/// True while the script still wants this control held.
bool stillHeld(const std::vector<DemoEvent>& events, const DemoEvent::Control control,
               Input::Key key, Input::MouseButton mouse, std::uint64_t step)
{
    for (const DemoEvent& event : events) {
        if (event.control != control || !event.down || event.step <= step) {
            continue;
        }
        if (control == DemoEvent::Control::Key ? event.key == key : event.mouse == mouse) {
            return true;
        }
    }
    return false;
}

bool sameControl(const DemoEvent& a, const DemoEvent& b)
{
    return a.control == b.control &&
           (a.control == DemoEvent::Control::Key ? a.key == b.key : a.mouse == b.mouse);
}

} // namespace

void DemoDriver::apply(Input::InputManager& input, std::uint64_t step)
{
    if (!m_active) {
        return;
    }

    // Release anything the script is not asking for any more, so a control can
    // never stick down because an event was skipped.
    for (auto it = m_held.begin(); it != m_held.end();) {
        if (stillHeld(m_events, it->control, it->key, it->mouse, step)) {
            ++it;
            continue;
        }
        if (it->control == DemoEvent::Control::Key) {
            input.onKeyUp(it->key);
        } else {
            input.onMouseButtonUp(it->mouse);
        }
        it = m_held.erase(it);
    }

    for (const DemoEvent& event : m_events) {
        if (event.step != step) {
            continue;
        }
        if (event.control == DemoEvent::Control::Mouse) {
            if (event.down) {
                input.onMouseButtonDown(event.mouse);
            } else {
                input.onMouseButtonUp(event.mouse);
            }
            continue;
        }
        if (event.down) {
            input.onKeyDown(event.key);
            const bool known = std::any_of(m_held.begin(), m_held.end(),
                                           [&](const DemoEvent& held) {
                                               return sameControl(held, event);
                                           });
            if (!known) {
                m_held.push_back(event);
            }
        } else {
            input.onKeyUp(event.key);
            m_held.erase(std::remove_if(m_held.begin(), m_held.end(),
                                        [&](const DemoEvent& held) {
                                            return sameControl(held, event);
                                        }),
                         m_held.end());
        }
    }
}

} // namespace EraShift::Core
