// Era Shift - process entry point.
//
// main() does four things and nothing else:
//
//   1. Parse the command line into a config overlay
//   2. Build the layered configuration
//   3. Start the engine
//   4. Translate the exit code
//
// Everything else lives in EraShift::Application::Engine. Keeping this file
// thin is what makes the engine testable without a process.

#include "EraShift/Application/Engine.hpp"
#include "EraShift/Core/Config.hpp"
#include "EraShift/Core/Log.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

void printUsage(const char* executable)
{
    std::printf(
        "Era Shift\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --set <key=value>   Override a configuration value, e.g.\n"
        "                      --set graphics.windowWidth=1920\n"
        "  --content <dir>     Load assets and data from <dir> instead of the\n"
        "                      automatically discovered content root\n"
        "  --log-level <level> TRACE | DEBUG | INFO | WARN | ERROR | FATAL | OFF\n"
        "  --log-file <path>   Also write the log to <path>\n"
        "  --frames <n>        Quit after n frames. 0 means no limit.\n"
        "  --headless          Use the dummy video driver (CI, SSH, containers).\n"
    "  --start-state <s>   Open on a screen other than the title screen:\n"
    "                      playing | settings | credits | paused. A developer\n"
    "                      and screenshot aid, not part of the game.\n"
        "  --screenshot <png>  Write a PNG of the window to <png>.\n"
        "  --screenshot-frame <n>  Take the screenshot on frame <n> (default 30).\n"
        "  --help, -h          Show this message\n"
        "\n"
        "Configuration is layered: defaults, then ./config, then the user\n"
        "directory (~/.config/EraShift), then --set overrides.\n",
        executable);
}

} // namespace

int main(int argc, char* argv[])
{
    using namespace EraShift;

    std::string contentOverride;
    std::string logFilePath;
    std::string logLevelName;
    std::filesystem::path screenshotPath;
    std::string startState;
    long frameLimit = 0;          ///< 0 means "run until quit".
    long screenshotFrame = 30;

    // A parse error is reported and terminates start-up. Silently ignoring a
    // malformed argument is how a typo turns into an unkillable process.
    const auto usageError = [](const std::string& message) {
        std::fprintf(stderr, "error: %s\n", message.c_str());
        return 3;
    };

    /// Parses a non-negative decimal integer. Returns false on anything else.
    const auto parseCount = [](const char* text, long& out) {
        if (text == nullptr || *text == '\0') {
            return false;
        }
        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(text, &end, 10);
        if (errno == ERANGE || end == text || *end != '\0' || value < 0) {
            return false;
        }
        out = value;
        return true;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        // Every option that takes a value needs one.
        const auto takeValue = [&](std::string_view name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %.*s requires a value\n",
                             static_cast<int>(name.size()), name.data());
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "--set") {
            // Consumed by ConfigManager; skip the value so it is not mistaken
            // for another option.
            if (i + 1 < argc) {
                ++i;
            }
            continue;
        }
        if (arg == "--content") {
            const char* value = takeValue("--content");
            if (value == nullptr) return usageError("--content requires a value");
            contentOverride = value;
            continue;
        }
        if (arg == "--log-file") {
            const char* value = takeValue("--log-file");
            if (value == nullptr) return usageError("--log-file requires a value");
            logFilePath = value;
            continue;
        }
        if (arg == "--log-level") {
            const char* value = takeValue("--log-level");
            if (value == nullptr) return usageError("--log-level requires a value");
            logLevelName = value;
            continue;
        }
        if (arg == "--screenshot") {
            const char* value = takeValue("--screenshot");
            if (value == nullptr) return usageError("--screenshot requires a value");
            screenshotPath = value;
            continue;
        }
        if (arg == "--screenshot-frame") {
            const char* value = takeValue("--screenshot-frame");
            if (value == nullptr) return usageError("--screenshot-frame requires a value");
            if (!parseCount(value, screenshotFrame)) {
                return usageError("--screenshot-frame expects a non-negative integer");
            }
            continue;
        }
        if (arg == "--frames") {
            const char* value = takeValue("--frames");
            if (value == nullptr) return usageError("--frames requires a value");
            if (!parseCount(value, frameLimit)) {
                return usageError("--frames expects a non-negative integer");
            }
            if (frameLimit == 0) {
                std::fprintf(stderr,
                             "note: --frames 0 means no frame limit; the game runs "
                             "until the window is closed or it is interrupted\n");
            }
            continue;
        }
        if (arg == "--start-state") {
            const char* value = takeValue("--start-state");
            if (value == nullptr) return usageError("--start-state requires a value");
            startState = value;
            continue;
        }
        if (arg == "--headless") {
            // Forces the dummy video driver so the smoke test works over SSH and
            // in CI containers with no display server.
            setenv("SDL_VIDEODRIVER", "dummy", 1);
            continue;
        }

        std::fprintf(stderr, "error: unknown argument '%.*s' (try --help)\n",
                     static_cast<int>(arg.size()), arg.data());
        return 3;
    }

    // --- configuration -------------------------------------------------------
    // The project root is the directory the binary was launched from when it
    // sits next to `assets/`, otherwise the current working directory.
    const std::filesystem::path projectRoot = std::filesystem::current_path();

    Core::Logger bootstrapLog;
    bootstrapLog.setMinLevel(Core::LogLevel::Info);

    Core::ConfigManager config(bootstrapLog);
    config.load(projectRoot, argc, argv);

    Core::Logger log;
    log.setMinLevel(Core::LogLevel::Info);

    if (!logLevelName.empty()) {
        Core::LogLevel level = Core::LogLevel::Info;
        if (Core::parseLogLevel(logLevelName, level)) {
            log.setMinLevel(level);
        } else {
            std::fprintf(stderr, "warning: unknown log level '%s'\n", logLevelName.c_str());
        }
    } else {
        Core::LogLevel level = Core::LogLevel::Info;
        if (config.store().getInt("debug", "logLevel", -1) >= 0) {
            const std::string configured = config.store().getString("debug", "logLevel", "info");
            if (Core::parseLogLevel(configured, level)) {
                log.setMinLevel(level);
            }
        }
    }

    std::shared_ptr<Core::LogSink> fileSink;
    if (!logFilePath.empty()) {
        fileSink = std::make_shared<Core::FileLogSink>(logFilePath, log.minLevel());
        log.addSink(fileSink);
    }

    // --- engine --------------------------------------------------------------
    std::filesystem::path contentRoot = contentOverride.empty()
                                            ? config.contentRoot()
                                            : std::filesystem::path(contentOverride);

    if (!std::filesystem::exists(contentRoot)) {
        std::fprintf(stderr, "error: content root '%s' does not exist\n", contentRoot.string().c_str());
        return 2;
    }

    Engine engine;
    std::string error;

    // Must be set before initialise(): the game builds its state stack there.
    if (!startState.empty()) {
        engine.setStartState(startState);
    }

    try {
        if (!engine.initialise(config.store(), contentRoot, projectRoot.string(), error)) {
            log.fatal("main", "engine initialisation failed: {}", error);
            std::fprintf(stderr, "error: %s\n", error.c_str());
            engine.shutdown();
            return 1;
        }

        if (frameLimit > 0) {
            log.info("main", "smoke test: quitting after {} frames", frameLimit);
            // A headless smoke test still needs a real event pump, so the limit
            // is enforced from the render callback rather than a separate loop.
            engine.setFrameLimit(static_cast<unsigned long>(frameLimit));
        }

        if (!screenshotPath.empty()) {
            // The frame is honoured here rather than left at its default, which
            // is what `--screenshot-frame N` used to do: parse the number, then
            // ignore it.
            engine.setScreenshotRequest(screenshotPath,
                                        static_cast<unsigned long>(screenshotFrame));
        }

        engine.run();
    } catch (const std::exception& ex) {
        log.fatal("main", "unhandled exception: {}", ex.what());
        std::fprintf(stderr, "fatal: %s\n", ex.what());
        engine.shutdown();
        return 3;
    } catch (...) {
        log.fatal("main", "unhandled non-standard exception");
        engine.shutdown();
        return 3;
    }

    engine.shutdown();
    return 0;
}
