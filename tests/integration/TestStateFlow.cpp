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


// ---------------------------------------------------------------------------
// What stops when
//
// The rule the pause screen depends on: a menu on top freezes the *simulation*
// and leaves the *presentation* running. Confusing the two is what produces a
// pause screen where the world keeps moving underneath it, or one where the menu's
// own animation is frozen mid-pulse.
//
// This is checked with a state that separates the two explicitly rather than with
// a real PlayingState, which would need a renderer and would be testing that the
// renderer exists rather than that the freeze is in the right place.
// ---------------------------------------------------------------------------

namespace {

/// Counts simulation steps and presentation steps separately, so a test can say
/// which of the two it expects to move.
class SplitClockState final : public IGameState {
public:
    SplitClockState(GameState id, int* simulations, int* presentations)
        : IGameState(id), m_simulations(simulations), m_presentations(presentations) {}

    void update(StateContext&, double) override { ++*m_simulations; }
    void render(StateContext&, double) override { ++*m_presentations; }

private:
    int* m_simulations;
    int* m_presentations;
};

} // namespace

TEST_CASE("render draws every state bottom-up, so an overlay covers the world")
{
    // The freeze stops the world simulating; rendering is a different question. A
    // menu that rendered nothing would freeze the world *and* leave the previous
    // frame on screen, which looks identical to a freeze but is a different bug -
    // and is invisible in a test that only counts updates.
    int plays = 0;
    int paused = 0;
    int playRenders = 0;
    int pauseRenders = 0;
    StateContext ctx{};
    StateMachine machine(ctx);

    machine.push(std::make_shared<SplitClockState>(GameState::Playing, &plays, &playRenders));
    machine.push(std::make_shared<SplitClockState>(GameState::Paused, &paused, &pauseRenders));
    machine.render(0.5);

    CHECK(playRenders == 1);
    CHECK(pauseRenders == 1);
}

TEST_CASE("a paused game simulates nothing while the menu animates")
{
    int sims = 0;
    int menuRenders = 0;
    StateContext ctx{};
    StateMachine machine(ctx);

    auto playing = std::make_shared<SplitClockState>(GameState::Playing, &sims, &menuRenders);
    machine.push(playing);
    machine.update(0.016);
    CHECK(sims == 1);

    // The pause menu is its own state here, and it renders: a menu that does not
    // animate while the game is paused looks like the game has hung on the pause.
    int pauseRenders = 0;
    int pauseSims = 0;
    machine.push(std::make_shared<SplitClockState>(GameState::Paused, &pauseSims, &pauseRenders));

    for (int i = 0; i < 100; ++i) {
        machine.update(0.016);
    }

    CHECK_MESSAGE(sims == 1, "the world does not step while paused");
    CHECK_MESSAGE(pauseRenders == 0, "render is not called by update");

    // Unpausing resumes exactly where it left off, with no drift: the frozen period
    // must not be integrated in one lump on the way back in.
    machine.pop();
    machine.update(0.016);
    CHECK(sims == 2);
}

TEST_CASE("a state on top of the stack is the only one updated")
{
    // Two states with their own counters, so "the top one runs" is checkable
    // without either of them knowing about the other.
    int bottomSims = 0;
    int topSims = 0;
    int bottomRenders = 0;
    StateContext ctx{};
    StateMachine machine(ctx);

    machine.push(std::make_shared<SplitClockState>(GameState::Playing, &bottomSims, &bottomRenders));
    // A step before the second state goes on, so the bottom one is known to have
    // run at all - otherwise "zero" would be true whether or not the freeze works.
    machine.update(0.016);
    REQUIRE(bottomSims == 1);

    machine.push(std::make_shared<SplitClockState>(GameState::Paused, &topSims, &topSims));
    for (int i = 0; i < 10; ++i) {
        machine.update(0.016);
    }

    CHECK(bottomSims == 1);
    CHECK(topSims == 10);
}

TEST_CASE("the results screen does not let the game underneath keep running")
{
    // The Game Over case: gameplay stops, but the state beneath it is not stepped.
    // A game-over screen drawn over a world that is still being simulated is a
    // game that is still playing while telling you it is over.
    int worldSims = 0;
    int resultSims = 0;
    int resultRenders = 0;
    StateContext ctx{};
    StateMachine machine(ctx);

    machine.push(std::make_shared<SplitClockState>(GameState::Playing, &worldSims, &resultSims));
    machine.update(0.016);
    REQUIRE(worldSims == 1);

    // A push, not a switch: the results screen covers the world rather than
    // replacing it, which is what makes the freeze observable.
    machine.push(std::make_shared<SplitClockState>(GameState::GameOver, &resultSims, &resultRenders));
    for (int i = 0; i < 50; ++i) {
        machine.update(0.016);
    }

    CHECK_MESSAGE(worldSims == 1, "the world stops under Game Over");
    CHECK(resultSims == 50);
}

TEST_CASE("switching to the results screen resets the stack rather than stacking on it")
{
    // `reset` replaces everything, which is what stops a Game Over screen being
    // pushed on top of the world that is being reported on and then left behind
    // when the player retries.
    int a = 0;
    int b = 0;
    StateContext ctx{};
    StateMachine machine(ctx);

    machine.push(std::make_shared<SplitClockState>(GameState::Playing, &a, &a));
    machine.update(0.016);
    REQUIRE(machine.depth() == 1);

    machine.switchTo(std::make_shared<SplitClockState>(GameState::GameOver, &b, &b));
    CHECK(machine.depth() == 1);
    CHECK(machine.currentId() == GameState::GameOver);

    machine.update(0.016);
    CHECK(a == 1);
    CHECK(b == 1);
}
