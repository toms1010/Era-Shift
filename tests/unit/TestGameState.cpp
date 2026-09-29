// Era Shift - tests for the game state machine.

#include "EraShift/Application/StateContext.hpp"
#include "EraShift/Core/GameState.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

using namespace EraShift::Core;
using EraShift::StateContext;

namespace {

/// Records the lifecycle callbacks it receives, in order.
class RecordingState final : public IGameState {
public:
    explicit RecordingState(GameState id, std::vector<std::string>* log)
        : IGameState(id), m_log(log) {}

    void onEnter(StateContext&) override  { m_log->push_back("enter:" + name()); }
    void onResume(StateContext&) override { m_log->push_back("resume:" + name()); }
    void onPause(StateContext&) override  { m_log->push_back("pause:" + name()); }
    void onExit(StateContext&) override   { m_log->push_back("exit:" + name()); }
    void update(StateContext&, double dt) override
    {
        m_log->push_back("update:" + name());
        m_lastDelta = dt;
        ++m_updateCount;
    }
    void render(StateContext&, double) override { m_log->push_back("render:" + name()); }

    [[nodiscard]] int updateCount() const noexcept { return m_updateCount; }
    [[nodiscard]] double lastDelta() const noexcept { return m_lastDelta; }

private:
    [[nodiscard]] std::string name() const
    {
        return std::string(stateName(id()));
    }

    std::vector<std::string>* m_log;
    int    m_updateCount = 0;
    double m_lastDelta   = 0.0;
};

bool contains(const std::vector<std::string>& log, const std::string& entry)
{
    for (const auto& item : log) {
        if (item == entry) {
            return true;
        }
    }
    return false;
}

size_t countOf(const std::vector<std::string>& log, const std::string& entry)
{
    size_t count = 0;
    for (const auto& item : log) {
        if (item == entry) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("GameState converts to a readable name")
{
    CHECK(stateName(GameState::MainMenu) == "MainMenu");
    CHECK(stateName(GameState::Playing)  == "Playing");
    CHECK(stateName(GameState::Paused)   == "Paused");
    CHECK(stateName(GameState::GameOver) == "GameOver");
}

TEST_CASE("pushing a state pauses the one below it")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    machine.push(std::make_shared<RecordingState>(GameState::Playing, &log));

    CHECK(machine.currentId() == GameState::Playing);
    CHECK(machine.depth() == 1);
    CHECK(contains(log, "enter:Playing"));

    log.clear();
    machine.push(std::make_shared<RecordingState>(GameState::Paused, &log));

    CHECK(machine.currentId() == GameState::Paused);
    CHECK(machine.depth() == 2);
    CHECK(contains(log, "pause:Playing"));
    CHECK(contains(log, "enter:Paused"));
    CHECK_FALSE(contains(log, "update:Playing"));
}

TEST_CASE("only the top state simulates")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    auto playing = std::make_shared<RecordingState>(GameState::Playing, &log);
    auto paused  = std::make_shared<RecordingState>(GameState::Paused, &log);
    machine.push(playing);
    machine.push(paused);

    log.clear();
    machine.update(1.0 / 60.0);

    CHECK(playing->updateCount() == 0);
    CHECK(paused->updateCount() == 1);
    CHECK(paused->lastDelta() == doctest::Approx(1.0 / 60.0));
    CHECK(machine.isSimulationActive() == false);
}

TEST_CASE("popping resumes the state underneath")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    auto playing = std::make_shared<RecordingState>(GameState::Playing, &log);
    machine.push(playing);
    machine.push(std::make_shared<RecordingState>(GameState::Paused, &log));

    log.clear();
    CHECK(machine.pop());

    CHECK(machine.currentId() == GameState::Playing);
    CHECK(contains(log, "exit:Paused"));
    CHECK(contains(log, "resume:Playing"));
    CHECK(machine.isSimulationActive());
}

TEST_CASE("the root state cannot be popped")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    machine.push(std::make_shared<RecordingState>(GameState::MainMenu, &log));

    CHECK_FALSE(machine.pop());
    CHECK(machine.currentId() == GameState::MainMenu);
    CHECK(machine.depth() == 1);
}

TEST_CASE("switchTo replaces the whole stack atomically")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    machine.push(std::make_shared<RecordingState>(GameState::MainMenu, &log));
    machine.push(std::make_shared<RecordingState>(GameState::Playing, &log));

    log.clear();
    machine.switchTo(std::make_shared<RecordingState>(GameState::GameOver, &log));

    CHECK(machine.depth() == 1);
    CHECK(machine.currentId() == GameState::GameOver);
    // The states that were discarded are exited in top-down order.
    CHECK(countOf(log, "exit:Playing") == 1);
    CHECK(countOf(log, "exit:MainMenu") == 1);
    CHECK(contains(log, "enter:GameOver"));
}

TEST_CASE("popToRoot unwinds to the main menu and resumes it")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    auto mainMenu = std::make_shared<RecordingState>(GameState::MainMenu, &log);
    machine.push(mainMenu);
    machine.push(std::make_shared<RecordingState>(GameState::Playing, &log));
    machine.push(std::make_shared<RecordingState>(GameState::Paused, &log));
    machine.push(std::make_shared<RecordingState>(GameState::Settings, &log));

    log.clear();
    machine.popToRoot();

    CHECK(machine.depth() == 1);
    CHECK(machine.currentId() == GameState::MainMenu);
    CHECK(contains(log, "exit:Settings"));
    CHECK(contains(log, "exit:Paused"));
    CHECK(contains(log, "exit:Playing"));
    CHECK(contains(log, "resume:MainMenu"));
}

TEST_CASE("rendering visits every state bottom-up so overlays draw last")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    machine.push(std::make_shared<RecordingState>(GameState::Playing, &log));
    machine.push(std::make_shared<RecordingState>(GameState::Paused, &log));

    log.clear();
    machine.render(0.5);

    REQUIRE(log.size() == 2);
    CHECK(log[0] == "render:Playing");
    CHECK(log[1] == "render:Paused");
}

TEST_CASE("isInStack blocks illegal repeated pushes")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    machine.push(std::make_shared<RecordingState>(GameState::Playing, &log));
    machine.push(std::make_shared<RecordingState>(GameState::Paused, &log));

    CHECK(machine.isInStack(GameState::Paused));
    CHECK(machine.isInStack(GameState::Playing));
    CHECK_FALSE(machine.isInStack(GameState::Inventory));

    // A second pause menu must not be pushed.
    if (!machine.isInStack(GameState::Paused)) {
        machine.push(std::make_shared<RecordingState>(GameState::Paused, &log));
    }
    CHECK(machine.depth() == 2);
}

TEST_CASE("null states are rejected")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    machine.push(nullptr);
    CHECK(machine.empty());
    CHECK(machine.currentId() == GameState::None);
    CHECK(machine.current() == nullptr);

    machine.switchTo(nullptr);
    CHECK(machine.empty());
}

TEST_CASE("the machine is safe with no context bound")
{
    std::vector<std::string> log;
    StateMachine machine;   // no context

    machine.push(std::make_shared<RecordingState>(GameState::Playing, &log));
    machine.update(0.016);
    machine.render(0.0);
    machine.pop();

    CHECK(machine.depth() == 1);
    CHECK(log.empty());
}

TEST_CASE("stackTrace reports the stack bottom-up")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    machine.push(std::make_shared<RecordingState>(GameState::MainMenu, &log));
    machine.push(std::make_shared<RecordingState>(GameState::Playing, &log));
    machine.push(std::make_shared<RecordingState>(GameState::Paused, &log));

    const auto trace = machine.stackTrace();
    REQUIRE(trace.size() == 3);
    CHECK(trace[0] == "MainMenu");
    CHECK(trace[1] == "Playing");
    CHECK(trace[2] == "Paused");
}

TEST_CASE("enter and pause counters are tracked per state instance")
{
    std::vector<std::string> log;
    StateContext ctx{};

    StateMachine machine(ctx);
    auto playing = std::make_shared<RecordingState>(GameState::Playing, &log);
    machine.push(playing);
    machine.push(std::make_shared<RecordingState>(GameState::Paused, &log));
    machine.pop();
    machine.push(std::make_shared<RecordingState>(GameState::Paused, &log));

    CHECK(playing->enterCount() == 1);
    CHECK(playing->pauseCount() == 2);
}
