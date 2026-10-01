// Era Shift - integration test: configuration survives a load/save cycle.
//
// This is the shape the real save system will use, so the behaviour is pinned
// down before save files exist: user settings written to disk must be
// re-loaded unchanged and must still let the shipped defaults through.

#include "EraShift/Core/Config.hpp"
#include "EraShift/Core/Log.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace EraShift::Core;

namespace {

class ScopedTempDir {
public:
    explicit ScopedTempDir(const char* name)
        : m_path(std::filesystem::temp_directory_path() / name)
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }
    ~ScopedTempDir() { std::filesystem::remove_all(m_path); }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

Logger& quietLog()
{
    static Logger logger;
    logger.setMinLevel(LogLevel::Off);
    return logger;
}

} // namespace

TEST_CASE("a settings file written by the game is read back unchanged")
{
    ScopedTempDir dir("erashift_integration_settings");

    // Simulate what SettingsState does on exit.
    ConfigStore store;
    store.setInt("graphics", "windowWidth", 1600);
    store.setInt("graphics", "windowHeight", 900);
    store.setBool("graphics", "fullscreen", true);
    store.setBool("graphics", "vsync", false);
    store.setInt("graphics", "uiScale", 125);
    store.setInt("engine", "fixedTicksPerSecond", 30);
    store.setString("game", "language", "en");

    const auto path = dir.path() / "settings.json";
    REQUIRE(store.saveFile(path, quietLog()));

    ConfigStore reloaded;
    REQUIRE(reloaded.loadFile(path, quietLog()));

    CHECK(reloaded.getInt("graphics", "windowWidth", 0) == 1600);
    CHECK(reloaded.getInt("graphics", "windowHeight", 0) == 900);
    CHECK(reloaded.getBool("graphics", "fullscreen", false));
    CHECK(reloaded.getBool("graphics", "vsync", true) == false);
    CHECK(reloaded.getInt("graphics", "uiScale", 0) == 125);
    CHECK(reloaded.getInt("engine", "fixedTicksPerSecond", 0) == 30);
    CHECK(reloaded.getString("game", "language", "") == "en");
}

TEST_CASE("shipped defaults are preserved for keys the user never touched")
{
    ScopedTempDir dir("erashift_integration_defaults");

    // 1. The defaults that ship with the game.
    ConfigStore defaults;
    REQUIRE(defaults.mergeJson(R"({
        "graphics": { "windowWidth": 1280, "windowHeight": 720, "vsync": true },
        "engine":  { "fixedTicksPerSecond": 60 }
    })", "defaults", quietLog()));

    // 2. The user changes exactly one value and saves.
    defaults.setInt("graphics", "windowWidth", 1920);
    const auto path = dir.path() / "settings.json";
    REQUIRE(defaults.saveFile(path, quietLog()));

    // 3. Next launch: a fresh defaults layer is loaded, then the user file.
    ConfigStore nextLaunch;
    REQUIRE(nextLaunch.mergeJson(R"({
        "graphics": { "windowWidth": 1280, "windowHeight": 720, "vsync": true },
        "engine":  { "fixedTicksPerSecond": 60 }
    })", "defaults", quietLog()));
    REQUIRE(nextLaunch.loadFile(path, quietLog()));

    CHECK(nextLaunch.getInt("graphics", "windowWidth", 0) == 1920);   // user's change
    CHECK(nextLaunch.getInt("graphics", "windowHeight", 0) == 720);   // default survives
    CHECK(nextLaunch.getBool("graphics", "vsync", false));           // default survives
    CHECK(nextLaunch.getInt("engine", "fixedTicksPerSecond", 0) == 60);
}

TEST_CASE("a future version adding a new default does not disturb saved values")
{
    ScopedTempDir dir("erashift_integration_migration");

    ConfigStore oldSave;
    oldSave.setInt("graphics", "windowWidth", 1600);
    const auto path = dir.path() / "settings.json";
    REQUIRE(oldSave.saveFile(path, quietLog()));

    // A later build ships a defaults file with additional keys.
    ConfigStore upgraded;
    REQUIRE(upgraded.mergeJson(R"({
        "graphics": { "windowWidth": 1280, "windowHeight": 720, "hdr": true },
        "graphics2": { "upscaler": "fsr3" }
    })", "defaults-v2", quietLog()));
    REQUIRE(upgraded.loadFile(path, quietLog()));

    CHECK(upgraded.getInt("graphics", "windowWidth", 0) == 1600);     // save wins
    CHECK(upgraded.getInt("graphics", "windowHeight", 0) == 720);     // new default applied
    CHECK(upgraded.getBool("graphics", "hdr", false));               // new default applied
    CHECK(upgraded.getString("graphics2", "upscaler", "") == "fsr3");
}

TEST_CASE("command line overrides beat every file")
{
    ScopedTempDir dir("erashift_integration_overrides");

    ConfigStore store;
    REQUIRE(store.mergeJson(R"({"graphics": {"windowWidth": 1280}})", "defaults", quietLog()));
    store.setInt("graphics", "windowWidth", 1600);   // user file
    store.setString("", "graphics.windowWidth", "1920");  // --set graphics.windowWidth=1920

    CHECK(store.getInt("graphics", "windowWidth", 0) == 1920);
    static_cast<void>(dir);
}

TEST_CASE("a corrupt user file does not stop the game from starting")
{
    ScopedTempDir dir("erashift_integration_corrupt");
    const auto path = dir.path() / "settings.json";

    {
        std::ofstream file(path);
        file << "{ this is not valid json";
    }

    ConfigStore store;
    REQUIRE(store.mergeJson(R"({"graphics": {"windowWidth": 1280}})", "defaults", quietLog()));

    // The bad file is rejected, the good defaults still apply.
    CHECK_FALSE(store.loadFile(path, quietLog()));
    CHECK(store.getInt("graphics", "windowWidth", 0) == 1280);
}
