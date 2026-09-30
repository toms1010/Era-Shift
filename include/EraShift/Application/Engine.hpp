// Era Shift - engine context.
//
// `Engine` owns every long-lived subsystem and wires them together. It is a
// plain aggregate with explicit members rather than a god object: the class has
// no behaviour of its own beyond construction order, and every system receives
// the ones it needs through its constructor.
//
// Lifetime: `Engine` must outlive everything it owns. Application holds it by
// value for the whole session.

#pragma once

#include "EraShift/Core/Config.hpp"
#include "EraShift/Core/DemoDriver.hpp"
#include "EraShift/Core/GameLoop.hpp"
#include "EraShift/Core/GameState.hpp"
#include "EraShift/Core/Log.hpp"
#include "EraShift/Core/SignalHandler.hpp"
#include "EraShift/Core/Time.hpp"
#include "EraShift/Audio/AudioManager.hpp"
#include "EraShift/Application/EventBus.hpp"
#include "EraShift/Application/StateContext.hpp"
#include "EraShift/Debug/DebugOverlay.hpp"
#include "EraShift/Debug/PerformanceStats.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"
#include "EraShift/Graphics/ResourceManager.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"
#include "EraShift/Graphics/Window.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace EraShift {

namespace Game {
class GameApp;
}

/// Everything that lives for the duration of a session.
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    /// Creates the window and every subsystem. Returns false and fills `errorOut`
    /// on failure; the Engine is left safely destructible.
    /// `config` is kept, not just read: the states use it to find the save
    /// directory, to persist rebinds and to resolve the UI scale. Handing the
    /// engine a bare store leaves it owning an *unloaded* ConfigManager, and
    /// every one of those writes then lands somewhere relative to whatever
    /// directory the game happened to be launched from.
    bool initialise(Core::ConfigManager& config,
                    const std::filesystem::path& contentRoot,
                    const std::string& projectRoot,
                    std::string& errorOut);

    /// Level file to load, overriding `data/levels/ancient_forest.json`.
    /// Empty means the shipped region.
    void setLevelPath(std::filesystem::path path) { m_levelPath = std::move(path); }

    /// Releases everything, in reverse order of construction.
    void shutdown() noexcept;

    /// Runs until the game requests a quit.
    void run();

    /// Requests a clean shutdown at the end of the current frame.
    void requestExit() noexcept;


    /// Quits automatically once this many frames have been presented. Zero
    /// disables the limit. Used by the headless smoke test.
    void setFrameLimit(unsigned long frames) noexcept { m_frameLimit = frames; }

    /// Starts the game on a screen other than the title screen.
    ///
    /// A developer and screenshot aid, not a feature of the game: it exists so
    /// every screen can be captured and smoke-tested without playing through to
    /// it, which is how the earlier screenshots of this project ended up only
    /// ever showing the main menu. Must be set before `run()`.
    void setStartState(std::string state) { m_startState = std::move(state); }
    [[nodiscard]] const std::string& startState() const noexcept { return m_startState; }

    /// Writes the next presented frame to `path`, replacing any pending request.
    void requestScreenshot(std::filesystem::path path) { m_screenshotPath = std::move(path); }

    /// Logs the audio subsystem's own state at start-up and again at shutdown.
    ///
    /// Must be set before `initialise()`. Exists because "the audio is wrong" is
    /// otherwise undiagnosable: the symptom is silence or a click, and neither
    /// says which of a dozen possible causes produced it.
    void setAudioDebug(bool enabled) noexcept { m_audioDebug = enabled; }
    [[nodiscard]] bool audioDebug() const noexcept { return m_audioDebug; }

    /// Replaces real input with the built-in attract script.
    ///
    /// For the attract loop on the title screen and for automated visual
    /// capture, both of which need the game to be playing itself.
    void setDemo(bool enabled) { m_demo = enabled ? Core::DemoDriver::attract() : Core::DemoDriver{}; }
    [[nodiscard]] bool demoEnabled() const noexcept { return m_demo.active(); }

    /// Writes the next presented frame to `path` as a PNG. Used by the
    /// automated visual smoke test.
    void setScreenshotRequest(std::filesystem::path path, unsigned long frame = 30)
    {
        m_screenshotPath = std::move(path);
        m_screenshotFrame = frame;
    }
    [[nodiscard]] const std::filesystem::path& screenshotRequest() const noexcept
    {
        return m_screenshotPath;
    }

    /// Services handed to the states. Valid for the whole session.
    [[nodiscard]] const StateContext& context() const noexcept { return m_context; }

    /// Repopulates the context after a subsystem is created or replaced.
    void rebuildContext() noexcept;
    [[nodiscard]] Core::Logger&             log()       noexcept { return m_log; }
    [[nodiscard]] Core::SystemClock&        clock()     noexcept { return m_clock; }
    [[nodiscard]] Core::GameLoop&           loop()      noexcept { return m_gameLoop; }
    [[nodiscard]] Core::StateMachine&       states()    noexcept { return m_states; }
    [[nodiscard]] Input::InputManager&      input()     noexcept { return m_input; }
    [[nodiscard]] Graphics::Window&         window()    noexcept { return m_window; }
    [[nodiscard]] Graphics::Renderer2D&     renderer()  noexcept { return m_renderer2D; }
    [[nodiscard]] Graphics::ResourceManager* resources() noexcept { return m_resources.get(); }
    [[nodiscard]] Application::EventBus&    events()    noexcept { return m_eventBus; }
    [[nodiscard]] Debug::DebugOverlay&      overlay()   noexcept { return m_overlay; }
    [[nodiscard]] Debug::PerformanceStats&  stats()     noexcept { return m_stats; }
    [[nodiscard]] Audio::AudioManager&      audio()     noexcept { return m_audio; }

    [[nodiscard]] const std::string& buildLabel() const noexcept { return m_buildLabel; }
    [[nodiscard]] bool isDebugBuild() const noexcept { return m_debugBuild; }
    [[nodiscard]] Core::ConfigManager& config() noexcept;

private:
    void pumpEvents(double frameDelta);
    void update(double fixedDelta);
    void render(double frameDelta, double alpha);
    /// Advances the demo script by one fixed step and feeds it to the input
    /// system.
    void pumpDemo();
    void paceFrame();
    bool saveScreenshot(const std::filesystem::path& path);

    double                      m_updateSeconds = 0.0;

    Core::DemoDriver            m_demo;
    std::uint64_t               m_demoStep = 0;
    Core::Logger                m_log;
    Core::ConfigManager*        m_config = nullptr;
    Core::SystemClock           m_clock;
    Core::GameLoop              m_gameLoop;
    Core::StateMachine          m_states;
    Input::InputManager         m_input;
    Graphics::Window            m_window;
    Graphics::Renderer2D        m_renderer2D;
    std::unique_ptr<Graphics::ResourceManager> m_resources;
    std::unique_ptr<Graphics::TextRenderer>     m_text;
    Application::EventBus       m_eventBus;
    /// Long-lived and owned here, like every other subsystem. It outlives the
    /// state stack, so a sound started by a state finishes playing even as the
    /// state that asked for it is torn down.
    Audio::AudioManager         m_audio;
    std::unique_ptr<Application::EventPump>   m_eventPump;
    std::unique_ptr<Game::GameApp>            m_game;
    Debug::PerformanceStats     m_stats;
    Debug::DebugOverlay         m_overlay;
    std::shared_ptr<Core::MemoryLogSink> m_memoryLog;
    StateContext      m_context;

    std::string m_projectRoot;
    std::string m_buildLabel = "Development Build";
    bool        m_debugBuild = true;
    bool        m_audioDebug = false;
    bool        m_running    = false;
    bool        m_quitRequested = false;
    bool        m_initialised = false;
    /// Keeps the quit/close subscriptions alive for the session.
    std::vector<Application::EventBus::Subscription> m_subscriptions;

    /// Name of the screen to start on. Empty means the normal title screen.
    std::string m_startState;

    /// Overrides which level file the game loads.
    std::filesystem::path m_levelPath;

    unsigned long m_frameLimit = 0;
    unsigned long m_framesPresented = 0;
    std::filesystem::path m_screenshotPath;
    unsigned long m_screenshotFrame = 30;  ///< Frame index the screenshot is taken on.
    double m_frameBudgetSeconds = 0.0;    ///< Target frame time, for CPU pacing.
    double m_frameStart = 0.0;            ///< Clock value at the top of the frame.
    Graphics::Color m_baseColor = Graphics::Palette::Black;  ///< Cleared before each frame.
};

} // namespace EraShift
