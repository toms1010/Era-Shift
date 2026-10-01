#include "EraShift/Game/GameApp.hpp"

#include "EraShift/Game/states/MainMenuState.hpp"
#include "EraShift/Game/states/PausedState.hpp"
#include "EraShift/Game/states/PlayingState.hpp"
#include "EraShift/Core/SaveGame.hpp"
#include "EraShift/Game/states/ResultState.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
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
        } else if (requested == "levelselect" || requested == "levels" ||
                   requested == "regions") {
            machine.push(std::make_shared<LevelSelectState>());
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
    if (context.input == nullptr) {
        return;
    }

    // The debug overlay works in every state, including the main menu, because
    // it is the tool used to diagnose the main menu.
    if (context.input->wasPressed(Input::Action::DebugOverlay)) {
        context.overlay->toggle();
        context.log->debug("Game", "debug overlay {}", context.overlay->visible() ? "enabled" : "disabled");
    }

    // F12.
    //
    // This handler is the entire reason the key was not working. The binding to
    // F12 existed, the key reached the InputManager, and `Engine` had a
    // subscription for `ScreenshotRequested` ready to write the file - but
    // nothing ever *published* that event, so the chain dead-ended at the input
    // manager and the key did nothing. A subscription with no publisher is a
    // feature that reads as implemented and is not, which is worse than an
    // obviously missing one: the README documented F12, the key was bound, and
    // pressing it did nothing.
    //
    // The event is published rather than the file written here, because the
    // Engine owns the renderer and the capture has to happen on a presented
    // frame rather than mid-draw.
    if (context.input->wasPressed(Input::Action::Screenshot)) {
        publishScreenshot(context);
    }
}

void GameApp::publishScreenshot(StateContext& context)
{
    // Where a screenshot goes. Beside the user's other content rather than the
    // working directory, so it is findable afterwards and so pressing F12 while
    // the game was launched from somewhere unusual does not scatter PNGs about.
    std::filesystem::path directory;
    if (context.config != nullptr) {
        directory = context.config->userConfigDir() / "screenshots";
    }
    if (directory.empty()) {
        directory = std::filesystem::temp_directory_path() / "erashift-screenshots";
    }

    // A name that sorts chronologically and cannot collide with an existing
    // file, because silently overwriting the previous screenshot is how you lose
    // the one you actually wanted.
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                             now.time_since_epoch())
                             .count();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch() - std::chrono::seconds(seconds))
                            .count();

    std::filesystem::path path = directory / ("erashift-" + std::to_string(seconds) + "-" +
                                              std::to_string(millis) + ".png");
    for (int suffix = 1; std::filesystem::exists(path) && suffix < 1000; ++suffix) {
        path = directory / ("erashift-" + std::to_string(seconds) + "-" +
                            std::to_string(millis) + "-" + std::to_string(suffix) + ".png");
    }

    Application::Event event;
    event.type = Application::EventType::ScreenshotRequested;
    event.path = path;
    context.events->publish(event);
    context.log->info("Game", "screenshot requested: {}", path.string());
}

} // namespace EraShift::Game
