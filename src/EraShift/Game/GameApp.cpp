#include "EraShift/Game/GameApp.hpp"

#include "EraShift/Game/states/MainMenuState.hpp"
#include "EraShift/Game/states/PausedState.hpp"
#include "EraShift/Game/states/PlayingState.hpp"
#include "EraShift/Game/states/ResultState.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace EraShift::Game {

namespace {

std::string lower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

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

    // Opening directly on another screen is a developer aid; the normal path is
    // always the title screen.
    if (!m_startState.empty()) {
        const std::string requested = lower(m_startState);
        if (requested == "playing") {
            machine.reset(std::make_shared<PlayingState>());
        } else if (requested == "settings") {
            machine.push(std::make_shared<SettingsState>());
        } else if (requested == "credits") {
            machine.push(std::make_shared<CreditsState>());
        } else if (requested == "paused") {
            machine.push(std::make_shared<PausedState>());
        } else {
            context.log->warn("Game", "unknown --start-state '{}', using the title screen",
                              m_startState);
        }
    }

    context.log->info("Game", "game initialised, {} state(s) on the stack{}", machine.depth(),
                      m_startState.empty() ? "" : (", started in " + m_startState).c_str());
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
