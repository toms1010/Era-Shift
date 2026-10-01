// Era Shift - game state machine.
//
// States form an explicit, inspectable stack. Pushing a state pauses the one
// below it by default, which is exactly the behaviour required for
// MainMenu -> Playing -> Paused -> Playing without any per-state bookkeeping.
//
// This file deliberately contains no SDL types so the transition rules can be
// unit tested headlessly.

#pragma once

#include "EraShift/Application/StateContext.hpp"

#include <cstdint>
#include <iosfwd>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace EraShift::Core {

/// Identifies a state. Values are stable and are written to save files.
enum class GameState : std::uint8_t {
    None = 0,
    Boot,
    MainMenu,
    Settings,
    Playing,
    Paused,
    Inventory,
    Map,
    QuestLog,
    Loading,
    GameOver,
    /// Appended rather than inserted: the values above are written to save
    /// files, so their numbers must never move.
    Credits,
    /// Also appended for the same reason.
    LevelSelect,
};

[[nodiscard]] std::string_view stateName(GameState state) noexcept;

/// Stream inserter so states print readably in logs and test failures.
std::ostream& operator<<(std::ostream& os, GameState state);

class StateMachine;

/// Base class for a game state.
///
/// States are value-semantics handles: the machine owns them via
/// `std::shared_ptr` because a state may be re-entered from several places and
/// must keep its state (menu selection, scroll offset) while inactive.
class IGameState {
public:
    explicit IGameState(GameState id) noexcept;
    virtual ~IGameState();

    IGameState(const IGameState&)            = delete;
    IGameState& operator=(const IGameState&) = delete;

    [[nodiscard]] GameState id() const noexcept { return m_id; }

    /// Called once when the state is pushed onto the stack.
    virtual void onEnter(StateContext& /*ctx*/) {}
    /// Called when a state above this one is popped and this state resumes.
    virtual void onResume(StateContext& /*ctx*/) {}
    /// Called when another state is pushed on top; simulation should stop.
    virtual void onPause(StateContext& /*ctx*/) {}
    /// Called once when the state is popped off the stack.
    virtual void onExit(StateContext& /*ctx*/) {}

    /// Fixed-rate simulation. Only called while the state is the active one.
    virtual void update(StateContext& /*ctx*/, double /*fixedDelta*/) {}
    /// Per-frame presentation.
    virtual void render(StateContext& /*ctx*/, double /*alpha*/) {}

    [[nodiscard]] std::uint64_t enterCount() const noexcept { return m_enterCount; }
    [[nodiscard]] std::uint64_t pauseCount() const noexcept { return m_pauseCount; }

    /// Bookkeeping used by StateMachine; states should not call these.
    void noteEnter() noexcept { ++m_enterCount; }
    void notePause() noexcept { ++m_pauseCount; }

private:
    GameState    m_id;
    std::uint64_t m_enterCount = 0;
    std::uint64_t m_pauseCount = 0;
};

/// Stack of states with automatic pause/resume bookkeeping.
class StateMachine {
public:
    using UpdateFn = std::function<void(StateContext&, double)>;
    using RenderFn = std::function<void(StateContext&, double)>;

    StateMachine() = default;
    explicit StateMachine(StateContext& context) noexcept : m_context(&context) {}

    void bindContext(StateContext& context) noexcept { m_context = &context; }

    /// Pushes a state, pausing the current one.
    void push(std::shared_ptr<IGameState> state);

    /// Pops the top state. Returns false when only the root remains.
    bool pop();

    /// Replaces the top state with another one (single atomic transition).
    void switchTo(std::shared_ptr<IGameState> state);

    /// Removes every state above the root, resuming the root. Used when a level
    /// finishes and the game returns to the main menu.
    void popToRoot();

    /// Replaces the entire stack with a single state.
    void reset(std::shared_ptr<IGameState> state);

    [[nodiscard]] IGameState* current() const noexcept;
    [[nodiscard]] GameState currentId() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept { return m_stack.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_stack.empty(); }

    /// True when the world is running, i.e. `Playing` is the top state.
    /// A menu on top of it keeps rendering and updating its own cursor, but the
    /// world below is frozen.
    [[nodiscard]] bool isSimulationActive() const noexcept
    {
        return m_stack.size() == 1 && currentId() == GameState::Playing;
    }

    /// True when `state` is anywhere in the stack (used to block illegal pushes,
    /// e.g. opening the pause menu twice).
    [[nodiscard]] bool isInStack(GameState state) const noexcept;

    void update(double fixedDelta);
    void render(double alpha);

    /// Names of the stacked states, bottom to top. Used by the debug overlay.
    [[nodiscard]] std::vector<std::string> stackTrace() const;

private:
    StateContext* m_context = nullptr;
    std::vector<std::shared_ptr<IGameState>> m_stack;
};

} // namespace EraShift::Core
