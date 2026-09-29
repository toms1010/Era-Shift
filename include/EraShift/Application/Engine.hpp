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
#include "EraShift/Core/GameLoop.hpp"
#include "EraShift/Core/GameState.hpp"
#include "EraShift/Core/Log.hpp"
#include "EraShift/Core/SignalHandler.hpp"
#include "EraShift/Core/Time.hpp"
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
    bool initialise(const Core::ConfigStore& config,
                    const std::filesystem::path& contentRoot,
                    const std::string& projectRoot,
                    std::string& errorOut);

    /// Releases everything, in reverse order of construction.
    void shutdown() noexcept;

    /// Runs until the game requests a quit.
    void run();

    /// Requests a clean shutdown at the end of the current frame.
    void requestExit() noexcept;

    /// Quits automatically once this many frames have been presented. Zero
    /// disables the limit. Used by the headless smoke test.
    void setFrameLimit(unsigned long frames) noexcept { m_frameLimit = frames; }

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

    [[nodiscard]] const std::string& buildLabel() const noexcept { return m_buildLabel; }
    [[nodiscard]] bool isDebugBuild() const noexcept { return m_debugBuild; }
    [[nodiscard]] Core::ConfigManager& config() noexcept { return m_config; }

private:
    void pumpEvents(double frameDelta);
    void update(double fixedDelta);
    void render(double frameDelta, double alpha);
    void paceFrame();
    bool saveScreenshot(const std::filesystem::path& path);

    Core::Logger                m_log;
    Core::ConfigManager         m_config{m_log};
    Core::SystemClock           m_clock;
    Core::GameLoop              m_gameLoop;
    Core::StateMachine          m_states;
    Input::InputManager         m_input;
    Graphics::Window            m_window;
    Graphics::Renderer2D        m_renderer2D;
    std::unique_ptr<Graphics::ResourceManager> m_resources;
    std::unique_ptr<Graphics::TextRenderer>     m_text;
    Application::EventBus       m_eventBus;
    std::unique_ptr<Application::EventPump>   m_eventPump;
    std::unique_ptr<Game::GameApp>            m_game;
    Debug::PerformanceStats     m_stats;
    Debug::DebugOverlay         m_overlay;
    std::shared_ptr<Core::MemoryLogSink> m_memoryLog;
    StateContext      m_context;

    std::string m_projectRoot;
    std::string m_buildLabel = "Development Build";
    bool        m_debugBuild = true;
    bool        m_running    = false;
    bool        m_quitRequested = false;
    bool        m_initialised = false;
    /// Keeps the quit/close subscriptions alive for the session.
    std::vector<Application::EventBus::Subscription> m_subscriptions;

    unsigned long m_frameLimit = 0;
    unsigned long m_framesPresented = 0;
    std::filesystem::path m_screenshotPath;
    unsigned long m_screenshotFrame = 30;  ///< Frame index the screenshot is taken on.
    double m_frameBudgetSeconds = 0.0;    ///< Target frame time, for CPU pacing.
    double m_frameStart = 0.0;            ///< Clock value at the top of the frame.
    Graphics::Color m_baseColor = Graphics::Palette::Black;  ///< Cleared before each frame.
};

} // namespace EraShift
