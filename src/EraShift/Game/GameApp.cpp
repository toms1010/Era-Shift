#include "EraShift/Game/GameApp.hpp"

#include "EraShift/Game/states/MainMenuState.hpp"

namespace EraShift::Game {

GameApp::~GameApp()
{
    shutdown();
}

void GameApp::initialise(StateContext& context)
{
    m_context      = &context;
    m_stateMachine = context.states;

    if (m_stateMachine == nullptr) {
        context.log->error("Game", "initialise called without a state machine");
        return;
    }

    Core::StateMachine& machine = *context.states;
    machine.bindContext(context);

    m_mainMenu = std::make_shared<MainMenuState>();
    machine.push(m_mainMenu);

    context.log->info("Game", "game initialised, {} state(s) on the stack", machine.depth());
}

void GameApp::shutdown() noexcept
{
    m_mainMenu.reset();
    m_stateMachine = nullptr;
    m_context      = nullptr;
}

void GameApp::updateGlobalInput(StateContext& context)
{
    if (context.input == nullptr || context.overlay == nullptr) {
        return;
    }

    // The debug overlay works in every state, including the main menu, because
    // it is the tool used to diagnose the main menu.
    if (context.input->wasPressed(Input::Action::DebugOverlay)) {
        context.overlay->toggle();
        context.log->debug("Game", "debug overlay {}", context.overlay->visible() ? "enabled" : "disabled");
    }
}

} // namespace EraShift::Game
