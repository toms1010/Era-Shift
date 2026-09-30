#include "EraShift/Application/Engine.hpp"

#include "EraShift/Game/GameApp.hpp"
#include "EraShift/Graphics/BitmapFont.hpp"
#include "EraShift/Graphics/Color.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>

namespace EraShift {

using Core::ConfigStore;
using Core::LogLevel;
using Core::Stopwatch;

namespace {

/// Subset of SDL subsystems the engine needs at start-up.
constexpr SDL_InitFlags kRequiredSubsystems =
    SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD;

/// Converts an SDL_InitFlags value to the text used in log messages.
std::string describeSubsystems(SDL_InitFlags flags)
{
    std::string result;
    const auto append = [&result](const char* name) {
        if (!result.empty()) {
            result += "|";
        }
        result += name;
    };
    if (flags & SDL_INIT_VIDEO)   { append("VIDEO"); }
    if (flags & SDL_INIT_AUDIO)   { append("AUDIO"); }
    if (flags & SDL_INIT_EVENTS)  { append("EVENTS"); }
    if (flags & SDL_INIT_GAMEPAD) { append("GAMEPAD"); }
    if (flags & SDL_INIT_SENSOR)  { append("SENSOR"); }
    return result;
}

} // namespace

Core::ConfigManager& Engine::config() noexcept
{
    static Core::ConfigManager fallback(m_log);
    return m_config != nullptr ? *m_config : fallback;
}

Engine::Engine() = default;

Engine::~Engine()
{
    shutdown();
}

bool Engine::initialise(Core::ConfigManager& configManager,
                        const std::filesystem::path& contentRoot,
                        const std::string& projectRoot,
                        std::string& errorOut)
{
    if (m_initialised) {
        return true;
    }
    m_projectRoot = projectRoot;
    m_config      = &configManager;
    const Core::ConfigStore& config = configManager.store();

#if defined(NDEBUG)
    m_debugBuild = false;
    m_buildLabel = "Release Build";
#else
    m_debugBuild = true;
    m_buildLabel = "Development Build";
#endif

    // Sink order matters: console first so start-up problems are visible even
    // if the run is about to fail, then the in-memory ring the debug overlay
    // reads from.
    m_log.addSink(std::make_shared<Core::ConsoleLogSink>());

    m_memoryLog = std::make_shared<Core::MemoryLogSink>(256);
    m_log.addSink(m_memoryLog);

    // The default level is taken from config/debug.json so a developer can turn
    // on trace logging without rebuilding.
    Core::LogLevel level = m_debugBuild ? LogLevel::Debug : LogLevel::Info;
    const std::string configured = config.getString("debug", "logLevel", "");
    if (!configured.empty() && Core::parseLogLevel(configured, level)) {
        // explicit setting wins
    } else if (!configured.empty()) {
        m_log.warn("Engine", "unknown debug.logLevel '{}', using {}", configured,
                   Core::logLevelName(level));
    }
    m_log.setMinLevel(level);
    m_log.info("Engine", "Era Shift {} - {} build", m_buildLabel, "0.1.0");

    // --- shutdown signals ---------------------------------------------------
    // Ctrl-C, `kill` and `timeout` all set a flag the main loop polls. Relying
    // on the default disposition would tear the process down mid-frame and cut
    // the log off mid-line.
    if (Core::installShutdownHandlers()) {
        m_log.debug("Engine", "shutdown signal handlers installed");
    } else {
        m_log.warn("Engine", "could not install every shutdown signal handler");
    }

    // --- SDL -----------------------------------------------------------------
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "1");

    if (!SDL_Init(kRequiredSubsystems)) {
        errorOut = std::string("SDL_Init failed: ") + SDL_GetError();
        m_log.fatal("Engine", "{}", errorOut);
        return false;
    }
    m_log.info("Engine", "SDL initialised ({})", describeSubsystems(kRequiredSubsystems));

    // SDL3_image 3.4 links its codecs directly and detects support at runtime,
    // so there is nothing to initialise. The capability functions take a stream
    // to sniff; passing null reports "unsupported" for every format, which is
    // how a previous version of this line ended up logging a hard-coded `true`
    // and reporting codecs the build does not have.
    m_log.info("Engine", "SDL3_image ready (compiled against {}.{}.{}, runtime {})",
               SDL_IMAGE_MAJOR_VERSION, SDL_IMAGE_MINOR_VERSION, SDL_IMAGE_MICRO_VERSION,
               IMG_Version());

    if (!TTF_Init()) {
        // Fonts are optional because the engine has a built-in bitmap font.
        m_log.warn("Engine", "SDL3_ttf unavailable: {}", SDL_GetError());
    } else {
        m_log.info("Engine", "SDL3_ttf ready");
    }

    // --- window --------------------------------------------------------------
    Graphics::WindowConfig windowConfig;
    windowConfig.title      = config.getString("graphics", "windowTitle", "Era Shift");
    windowConfig.width      = config.clampInt("graphics", "windowWidth", 1280, 640, 7680, m_log);
    windowConfig.height     = config.clampInt("graphics", "windowHeight", 720, 480, 4320, m_log);
    windowConfig.resizable  = config.getBool("graphics", "resizable", true);
    windowConfig.fullscreen = config.getBool("graphics", "fullscreen", false);
    windowConfig.vsync      = config.getBool("graphics", "vsync", true);
    windowConfig.highDpi    = config.getBool("graphics", "highDpi", true);

    const int logicalW = config.getInt("graphics", "logicalWidth", 0);
    const int logicalH = config.getInt("graphics", "logicalHeight", 0);
    if (logicalW > 0 && logicalH > 0) {
        windowConfig.logicalWidth  = logicalW;
        windowConfig.logicalHeight = logicalH;
    }

    if (!m_window.create(windowConfig, errorOut)) {
        m_log.fatal("Engine", "{}", errorOut);
        return false;
    }
    m_log.info("Engine", "window '{}' created at {}x{}", windowConfig.title,
               windowConfig.width, windowConfig.height);

    // Frame background, configurable so a bright base can be used to spot
    // regions a state forgets to cover.
    m_baseColor = Graphics::Color::fromPacked(
        static_cast<std::uint32_t>(config.getInt("graphics", "clearColor", 0x000000FFu)));

    // --- systems -------------------------------------------------------------
    m_renderer2D.attach(m_window);
    m_resources = std::make_unique<Graphics::ResourceManager>(m_log, m_window, contentRoot);
    m_text = std::make_unique<Graphics::TextRenderer>(*m_resources, m_log);
    m_text->loadFont(config.getString("graphics", "uiFont", "assets/fonts/SpaceGrotesk.ttf"),
                     config.getInt("graphics", "uiFontSize", 20));

    m_eventPump = std::make_unique<Application::EventPump>(m_input, m_eventBus, m_log);
    m_eventPump->setViewportSize(m_window.logicalWidth() > 0 ? m_window.logicalWidth()
                                                            : windowConfig.width,
                                 m_window.logicalHeight() > 0 ? m_window.logicalHeight()
                                                              : windowConfig.height);

    m_input.setDefaultBindings();
    m_input.loadBindings(config, m_log);
    m_input.setViewportSize(static_cast<float>(m_eventPump != nullptr ? windowConfig.width : 0),
                            static_cast<float>(windowConfig.height));

    // Closing the window or receiving SIGTERM must end the session. Without
    // these subscriptions the game kept running after the window disappeared
    // and after `timeout` sent SIGTERM, because the quit event was published to
    // a bus nobody was listening to.
    m_subscriptions.push_back(m_eventBus.subscribe(
        Application::EventType::QuitRequested, [this](const Application::Event&) { requestExit(); }));
    m_subscriptions.push_back(m_eventBus.subscribe(
        Application::EventType::WindowClosed, [this](const Application::Event&) { requestExit(); }));

    // The F12 key asks for a screenshot here rather than reaching into the
    // engine from gameplay code.
    m_subscriptions.push_back(m_eventBus.subscribe(
        Application::EventType::ScreenshotRequested, [this](const Application::Event& event) {
            if (event.path.empty()) {
                return;
            }
            // Taken on the next presented frame, so the capture is a complete
            // frame rather than whatever was half-drawn when the key was hit.
            requestScreenshot(event.path);
            m_log.info("Engine", "screenshot queued for {}", event.path.string());
        }));

    m_overlay.setLogSink(m_memoryLog.get());
    m_overlay.setVisible(config.getBool("debug", "showStats", false));
    m_stats.reset();

    // --- game ----------------------------------------------------------------
    rebuildContext();

    m_game = std::make_unique<Game::GameApp>();
    m_game->setStartState(m_startState);
    m_game->initialise(m_context);

    // --- loop ----------------------------------------------------------------
    Core::LoopConfig loopConfig;
    loopConfig.fixedDelta = 1.0 / static_cast<double>(
        config.clampInt("engine", "fixedTicksPerSecond", 60, 20, 240, m_log));
    m_gameLoop.setConfig(loopConfig);
    m_gameLoop.setEventFn([this](double dt) { pumpEvents(dt); });
    m_gameLoop.setUpdateFn([this](double dt) {
        const double start = m_clock.seconds();
        update(dt);
        // Accumulated rather than assigned: a frame can run several fixed
        // steps, and the overlay should report the whole frame's simulation
        // cost, not the last one.
        m_updateSeconds += m_clock.seconds() - start;
    });
    m_gameLoop.setRenderFn([this](double dt, double alpha) { render(dt, alpha); });

    m_initialised = true;
    m_running     = true;
    return true;
}

void Engine::rebuildContext() noexcept
{
    m_context.log       = &m_log;
    m_context.states    = &m_states;
    m_context.loop      = &m_gameLoop;
    m_context.input     = &m_input;
    m_context.renderer  = &m_renderer2D;
    m_context.resources = m_resources.get();
    m_context.stats     = &m_stats;
    m_context.overlay   = &m_overlay;
    m_context.events    = &m_eventBus;
    m_context.clock     = &m_clock;
    m_context.config    = m_config;
    m_context.window    = &m_window;
    m_context.window    = &m_window;
    m_context.text      = m_text.get();

    m_context.buildLabel = m_buildLabel;
    m_context.version    = ERASHIFT_VERSION;
    m_context.levelPath  = m_levelPath;
}

void Engine::pumpEvents(double frameDelta)
{
    static_cast<void>(frameDelta);

    // Edges are cleared before the events are drained so this frame's presses
    // survive; endFrame() latches the result once, at the end of the frame.
    m_input.beginFrame();

    if (m_eventPump != nullptr) {
        m_eventPump->pumpFromSDL();
    }

    pumpDemo();
}

void Engine::pumpDemo()
{
    if (!m_demo.active()) {
        return;
    }

    // One script step per fixed simulation step, not per rendered frame: the
    // script then replays identically whatever the display refresh rate is.
    m_demo.apply(m_input, m_demoStep);
    ++m_demoStep;
}

void Engine::update(double fixedDelta)
{
    m_updateSeconds = 0.0;

    // Called once per frame with the *frame's* total, not once per fixed step,
    // so the figure the overlay reports is the cost of a frame rather than the
    // cost of the last slice of it.
    if (m_game != nullptr) {
        m_game->updateGlobalInput(m_context);
    }
    m_states.update(fixedDelta);
}

void Engine::render(double frameDelta, double alpha)
{
    Stopwatch frame;
    frame.bind(m_clock);
    frame.reset();

    const double renderStart = m_clock.seconds();
    m_renderer2D.beginFrame();

    // The engine owns the frame background. A state draws on top of it but
    // never has to remember to clear, which removes a whole class of bug where
    // a state forgets and the previous frame bleeds through.
    m_renderer2D.clear(m_baseColor);
    m_states.render(alpha);
    m_renderer2D.endFrame();
    const double renderSeconds = m_clock.seconds() - renderStart;

    // Latch input state once per presented frame.
    m_input.endFrame();

    m_stats.setDrawCalls(m_renderer2D.drawCalls().value());
    m_stats.sample(frameDelta, frame.elapsed(), m_updateSeconds, renderSeconds);

    m_window.present();

    ++m_framesPresented;
    if (m_frameLimit > 0 && m_framesPresented >= m_frameLimit) {
        m_log.info("Engine", "frame limit of {} reached", m_frameLimit);
        requestExit();
    }
    if (!m_screenshotPath.empty() && m_framesPresented == m_screenshotFrame) {
        if (saveScreenshot(m_screenshotPath)) {
            m_log.info("Engine", "screenshot written to {}", m_screenshotPath.string());
        }
        m_screenshotPath.clear();
    }

    paceFrame();
}

/// Sleeps off whatever is left of the frame budget.
///
/// With vsync on, RenderPresent() already blocked for most of the budget and
/// this is a no-op. With vsync off it is what stops the loop from spinning a
/// core at 100% while producing thousands of redundant frames.
void Engine::paceFrame()
{
    if (m_frameBudgetSeconds <= 0.0) {
        return;
    }
    const double spent = m_clock.seconds() - m_frameStart;
    const double remaining = m_frameBudgetSeconds - spent;
    if (remaining > 0.0005) {
        SDL_DelayNS(static_cast<Sint64>(remaining * 1.0e9));
    }
}

bool Engine::saveScreenshot(const std::filesystem::path& path)
{
    SDL_Renderer* renderer = m_window.renderer();
    if (renderer == nullptr) {
        m_log.error("Engine", "cannot take a screenshot without a renderer");
        return false;
    }

    // SDL 3.4 hands back a freshly allocated surface sized to the output.
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr) {
        m_log.error("Engine", "RenderReadPixels failed: {}", SDL_GetError());
        return false;
    }

    bool ok = false;
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    ok = IMG_SavePNG(surface, path.string().c_str());
    if (!ok) {
        m_log.error("Engine", "failed to write '{}': {}", path.string(), SDL_GetError());
    } else {
        m_log.info("Engine", "screenshot {}x{} written to {}",
                   surface->w, surface->h, path.string());
    }

    SDL_DestroySurface(surface);
    return ok;
}

void Engine::run()
{
    if (!m_initialised) {
        m_log.error("Engine", "run() called before initialise()");
        return;
    }

    m_gameLoop.resync(m_clock.seconds());
    m_frameBudgetSeconds = 1.0 / static_cast<double>(
        m_config != nullptr ? m_config->store().getInt("graphics", "targetFps", 60) : 60);
    m_log.info("Engine", "entering the main loop at a {} FPS budget", 1.0 / m_frameBudgetSeconds);

    while (!m_gameLoop.stopRequested() && !m_quitRequested) {
        if (Core::shutdownRequested()) {
            m_log.info("Engine", "{} received, shutting down", Core::shutdownSignalName());
            m_quitRequested = true;
            break;
        }

        m_frameStart = m_clock.seconds();
        // SDL's own frame pacing (vsync) determines when a frame is presented;
        // paceFrame() covers the case where vsync is off.
        m_gameLoop.tick(m_clock.seconds());
    }

    m_log.info("Engine", "leaving the main loop after {} frames (avg {:.1f} FPS)",
               m_stats.totalFrames(), m_stats.averageFps());
}

void Engine::requestExit() noexcept
{
    m_quitRequested = true;
    m_gameLoop.requestStop();
}

void Engine::shutdown() noexcept
{
    if (!m_initialised) {
        return;
    }
    m_initialised = false;

    m_log.info("Engine", "shutting down");

    m_stats.reset();
    m_subscriptions.clear();

    // Resetting the stack runs onExit on every live state, releasing anything
    // they hold, and clears the context binding.
    m_states.reset(nullptr);

    if (m_game != nullptr) {
        m_game->shutdown();
    }
    m_game.reset();

    m_eventPump.reset();
    m_input.setDefaultBindings();
    m_renderer2D.detach();
    m_resources.reset();
    m_text.reset();
    m_window.destroy();

    TTF_Quit();
    SDL_Quit();

    m_log.info("Engine", "shutdown complete");
    m_log.clearSinks();
    m_memoryLog.reset();
}

} // namespace EraShift
