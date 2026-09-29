// Era Shift - game facade.
//
// `Game` owns the concrete state objects and decides which one the player
// should be looking at. It sits between the engine (which knows about frames)
// and the states (which know about the game). Keeping the decision here means
// the states never have to know about each other.

#pragma once

#include "EraShift/Application/Engine.hpp"
#include "EraShift/Core/GameState.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <memory>
#include <string>

namespace EraShift::Game {

class MainMenuState;

/// Entry point for everything that is "the game" as opposed to "the engine".
///
/// Named `GameApp` rather than `Game` because `EraShift::Game` is already
/// the namespace that holds the individual states.
class GameApp {
public:
    GameApp() = default;
    ~GameApp();

    GameApp(const GameApp&)            = delete;
    GameApp& operator=(const GameApp&) = delete;

    /// Builds the initial state stack. Must be called after the renderer and
    /// input systems exist.
    void initialise(StateContext& context);

    /// Screen to open on instead of the title screen, or empty for the normal
    /// start. See `Engine::setStartState`.
    void setStartState(std::string state) { m_startState = std::move(state); }

    /// Tears down any state that holds resources.
    void shutdown() noexcept;

    /// Called once per frame before the state machine ticks. Handles global
    /// input that works in every state (the debug overlay, for example).
    void updateGlobalInput(StateContext& context);

private:
    Core::StateMachine* m_stateMachine = nullptr;
    StateContext*       m_context      = nullptr;
    std::shared_ptr<MainMenuState> m_mainMenu;
    std::string m_startState;
};

} // namespace EraShift::Game
