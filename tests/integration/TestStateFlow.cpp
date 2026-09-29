// Era Shift - integration tests for the full state flow.
//
// These exercise the transition sequence from the design document:
//
//   MainMenu -> Playing -> Paused -> Playing -> MainMenu
//
// with a null StateContext, which is what the engine passes until a renderer
// exists. The transitions themselves do not touch the renderer, so the whole
// flow is testable headlessly.

#include "EraShift/Application/StateContext.hpp"
#include "EraShift/Core/GameState.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

using namespace EraShift::Core;
using EraShift::StateContext;

namespace {

class JourneyState final : public IGameState {
public:
    JourneyState(GameState id, std::vector<std::string>* log) : IGameState(id), m_log(log) {}
    void onEnter(StateContext&) override { m_log->push_back(std::string("enter ") + std::string(stateName(id()))); }
    void onExit(StateContext&) override  { m_log->push_back(std::string("exit ") + std::string(stateName(id()))); }
    void onPause(StateContext&) override { m_log->push_back(std::string("pause ") + std::string(stateName(id()))); }
    void onResume(StateContext&) override{ m_log->push_back(std::string("resume ") + std::string(stateName(id()))); }
    void update(StateContext&, double) override { ++m_updates; }

    [[nodiscard]] int updateCount() const noexcept { return m_updates; }

private:
    std::vector<std::string>* m_log;
    int m_updates = 0;
};

int indexOf(const std::vector<std::string>& log, const std::string& entry)
{
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i] == entry) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

TEST_CASE("the documented main flow transitions cleanly")
{
    std::vector<std::string> log;
    StateContext ctx{};
    StateMachine machine(ctx);

    // MainMenu
    machine.push(std::make_shared<JourneyState>(GameState::MainMenu, &log));
    CHECK(machine.currentId() == GameState::MainMenu);

    // MainMenu -> Playing
    machine.switchTo(std::make_shared<JourneyState>(GameState::Playing, &log));
    CHECK(machine.currentId() == GameState::Playing);
    CHECK(machine.depth() == 1);

    // Playing -> Paused
    machine.push(std::make_shared<JourneyState>(GameState::Paused, &log));
    CHECK(machine.currentId() == GameState::Paused);
    CHECK(indexOf(log, "pause Playing") >= 0);

    // Paused -> Playing
    machine.pop();
    CHECK(machine.currentId() == GameState::Playing);
    CHECK(indexOf(log, "resume Playing") >= 0);

    // Playing -> GameOver
    machine.switchTo(std::make_shared<JourneyState>(GameState::GameOver, &log));
    CHECK(machine.currentId() == GameState::GameOver);

    // GameOver -> MainMenu
    machine.switchTo(std::make_shared<JourneyState>(GameState::MainMenu, &log));
    CHECK(machine.currentId() == GameState::MainMenu);

    // Every state that was left has a matching exit. The final MainMenu is
    // still on the stack, so it has an enter with no exit yet.
    int enters = 0;
    int exits  = 0;
    for (const auto& entry : log) {
        enters += (entry.rfind("enter ", 0) == 0) ? 1 : 0;
        exits  += (entry.rfind("exit ", 0) == 0) ? 1 : 0;
    }
    CHECK(enters == exits + 1);
    CHECK(machine.currentId() == GameState::MainMenu);
}

TEST_CASE("a pause storm does not leak states")
{
    std::vector<std::string> log;
    StateContext ctx{};
    StateMachine machine(ctx);

    machine.push(std::make_shared<JourneyState>(GameState::Playing, &log));

    // The player mashes Escape; only one pause state may exist at a time.
    for (int i = 0; i < 20; ++i) {
        if (!machine.isInStack(GameState::Paused)) {
            machine.push(std::make_shared<JourneyState>(GameState::Paused, &log));
        } else {
            machine.pop();
        }
    }

    CHECK(machine.depth() <= 2);
    CHECK(machine.currentId() == GameState::Playing);
}

TEST_CASE("nesting menus over a paused game unwinds in order")
{
    std::vector<std::string> log;
    StateContext ctx{};
    StateMachine machine(ctx);

    machine.push(std::make_shared<JourneyState>(GameState::Playing, &log));
    machine.push(std::make_shared<JourneyState>(GameState::Paused, &log));
    machine.push(std::make_shared<JourneyState>(GameState::Settings, &log));
    machine.push(std::make_shared<JourneyState>(GameState::Inventory, &log));

    CHECK(machine.depth() == 4);

    machine.popToRoot();

    CHECK(machine.depth() == 1);
    CHECK(machine.currentId() == GameState::Playing);

    // Overlays are torn down top-down. Playing is the root of this stack, so it
    // is resumed rather than exited.
    CHECK(indexOf(log, "exit Inventory") < indexOf(log, "exit Settings"));
    CHECK(indexOf(log, "exit Settings") < indexOf(log, "exit Paused"));
    CHECK(indexOf(log, "resume Playing") > indexOf(log, "exit Paused"));
    CHECK(indexOf(log, "exit Playing") == -1);
}

TEST_CASE("a state stack rebuilds identically after reset")
{
    std::vector<std::string> log;
    StateContext ctx{};
    StateMachine machine(ctx);

    auto build = [&log](StateMachine& m) {
        m.reset(nullptr);
        m.push(std::make_shared<JourneyState>(GameState::MainMenu, &log));
        m.switchTo(std::make_shared<JourneyState>(GameState::Playing, &log));
        m.push(std::make_shared<JourneyState>(GameState::Paused, &log));
    };

    build(machine);
    const std::vector<std::string> firstTrace = machine.stackTrace();
    const std::size_t firstDepth = machine.depth();

    build(machine);
    const std::vector<std::string> secondTrace = machine.stackTrace();

    CHECK(machine.depth() == firstDepth);
    CHECK(firstTrace == secondTrace);
}

TEST_CASE("simulation is frozen for the whole time a menu is open")
{
    std::vector<std::string> log;
    StateContext ctx{};
    StateMachine machine(ctx);

    auto playing = std::make_shared<JourneyState>(GameState::Playing, &log);
    machine.push(playing);
    machine.update(0.016);
    CHECK(playing->updateCount() == 1);

    machine.push(std::make_shared<JourneyState>(GameState::Paused, &log));
    for (int i = 0; i < 100; ++i) {
        machine.update(0.016);
    }
    CHECK(playing->updateCount() == 1);

    machine.pop();
    machine.update(0.016);
    CHECK(playing->updateCount() == 2);
}
