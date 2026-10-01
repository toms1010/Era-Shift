// Era Shift - tests for the configuration store.

#include "EraShift/Core/Config.hpp"
#include "EraShift/Core/Log.hpp"

#include <doctest/doctest.h>

#include <filesystem>

using namespace EraShift::Core;

namespace {

Logger& quietLog()
{
    static Logger logger;
    logger.setMinLevel(LogLevel::Off);
    return logger;
}

} // namespace

TEST_CASE("ConfigStore parses nested JSON into flat keys")
{
    ConfigStore store;
    REQUIRE(store.mergeJson(R"({
        "graphics": { "windowWidth": 1920, "vsync": true, "scaleMode": "nearest" },
        "engine":  { "fixedTicksPerSecond": 120 }
    })", "test", quietLog()));

    CHECK(store.getInt("graphics", "windowWidth", 0) == 1920);
    CHECK(store.getBool("graphics", "vsync", false));
    CHECK(store.getString("graphics", "scaleMode", "") == "nearest");
    CHECK(store.getInt("engine", "fixedTicksPerSecond", 0) == 120);
}

TEST_CASE("ConfigStore returns fallbacks for missing keys")
{
    ConfigStore store;
    CHECK(store.getInt("nope", "missing", 42) == 42);
    CHECK(store.getBool("nope", "missing", true));
    CHECK(store.getFloat("nope", "missing", 1.5f) == doctest::Approx(1.5f));
    CHECK(store.getString("nope", "missing", "fallback") == "fallback");
    CHECK_FALSE(store.contains("nope", "missing"));
    CHECK_FALSE(store.findString("nope", "missing").has_value());
}

TEST_CASE("ConfigStore rejects malformed JSON without throwing")
{
    ConfigStore store;
    CHECK_FALSE(store.mergeJson("{ not json", "test", quietLog()));
    CHECK_FALSE(store.mergeJson("[1, 2, 3]", "test", quietLog()));  // root must be an object
    CHECK(store.empty());
}

TEST_CASE("ConfigStore later layers override earlier ones")
{
    ConfigStore store;
    REQUIRE(store.mergeJson(R"({"graphics": {"vsync": true, "windowWidth": 1280}})", "base", quietLog()));
    REQUIRE(store.mergeJson(R"({"graphics": {"vsync": false}})", "user", quietLog()));

    CHECK_FALSE(store.getBool("graphics", "vsync", true));
    CHECK(store.getInt("graphics", "windowWidth", 0) == 1280);  // untouched key survives
    CHECK(store.size() == 2);                                   // no duplicate entries
}

TEST_CASE("ConfigStore round trips through JSON")
{
    ConfigStore original;
    original.setInt("graphics", "windowWidth", 1600);
    original.setBool("graphics", "vsync", false);
    original.setString("game", "title", "Era Shift \"Prototype\"");
    original.setFloat("graphics", "uiScale", 125.5f);

    const std::string json = original.toJson();

    ConfigStore reloaded;
    REQUIRE(reloaded.mergeJson(json, "roundtrip", quietLog()));

    CHECK(reloaded.getInt("graphics", "windowWidth", 0) == 1600);
    CHECK(reloaded.getBool("graphics", "vsync", true) == false);
    CHECK(reloaded.getString("game", "title", "") == "Era Shift \"Prototype\"");
    CHECK(reloaded.getFloat("graphics", "uiScale", 0.0f) == doctest::Approx(125.5f));
}

TEST_CASE("ConfigStore round trips through a file")
{
    const auto dir = std::filesystem::temp_directory_path() / "erashift_config_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "settings.json";

    ConfigStore original;
    original.setInt("graphics", "uiScale", 55);
    REQUIRE(original.saveFile(path, quietLog()));
    CHECK(std::filesystem::exists(path));

    ConfigStore reloaded;
    REQUIRE(reloaded.loadFile(path, quietLog()));
    CHECK(reloaded.getInt("graphics", "uiScale", 0) == 55);

    std::filesystem::remove_all(dir);
}

TEST_CASE("ConfigStore reports missing files as non-fatal")
{
    ConfigStore store;
    CHECK_FALSE(store.loadFile("/nonexistent/erashift/definitely/missing.json", quietLog()));
}

TEST_CASE("ConfigStore lists the keys of a section")
{
    ConfigStore store;
    REQUIRE(store.mergeJson(R"({
        "graphics": { "vsync": true, "windowWidth": 1280, "fullscreen": false },
        "engine":  { "fixedTicksPerSecond": 60 }
    })", "test", quietLog()));

    const auto keys = store.keys("graphics");
    REQUIRE(keys.size() == 3);
    CHECK(keys[0] == "fullscreen");
    CHECK(keys[1] == "vsync");
    CHECK(keys[2] == "windowWidth");
}

TEST_CASE("ConfigStore clamps out-of-range values")
{
    ConfigStore store;
    REQUIRE(store.mergeJson(R"({"graphics": {"windowWidth": 99999}})", "test", quietLog()));

    CHECK(store.clampInt("graphics", "windowWidth", 1280, 640, 3840, quietLog()) == 3840);
    // 99999 sits below the new minimum, so it is clamped up.
    CHECK(store.clampInt("graphics", "windowWidth", 1280, 100000, 200000, quietLog()) == 100000);

    REQUIRE(store.mergeJson(R"({"graphics": {"uiScale": -5}})", "test", quietLog()));
    CHECK(store.clampFloat("graphics", "uiScale", 100.0f, 50.0f, 200.0f, quietLog()) == doctest::Approx(50.0f));
}

TEST_CASE("ConfigStore accepts float text where an int is expected")
{
    ConfigStore store;
    REQUIRE(store.mergeJson(R"({"graphics": {"windowWidth": 1920.0}})", "test", quietLog()));
    CHECK(store.getInt("graphics", "windowWidth", 0) == 1920);
}

TEST_CASE("ConfigStore handles arrays as compact JSON text")
{
    ConfigStore store;
    REQUIRE(store.mergeJson(R"({"data": {"spawnOrder": ["guard", "hound"]}})", "test", quietLog()));
    const std::string raw = store.getString("data", "spawnOrder", "");
    CHECK(raw == "[\"guard\",\"hound\"]");
}

TEST_CASE("ConfigStore keeps strings that look like numbers as strings")
{
    ConfigStore store;
    store.setString("game", "version", "0.1.0");

    CHECK(store.getString("game", "version", "") == "0.1.0");

    // Numeric getters must parse the *whole* value: a partial parse would turn
    // "0.1.0" into 0 and silently corrupt a version string.
    CHECK(store.getInt("game", "version", -1) == -1);
    CHECK(store.getFloat("game", "version", -1.0f) == doctest::Approx(-1.0f));

    // A string that is genuinely a number is coerced, so a hand-written config
    // with "windowWidth": "1920" still works.
    store.setString("graphics", "windowWidth", "1920");
    CHECK(store.getInt("graphics", "windowWidth", 0) == 1920);
}

TEST_CASE("ConfigStore dump exposes every value for debug tooling")
{
    ConfigStore store;
    store.setInt("a", "b", 1);
    store.setString("c", "d", "e");

    const auto all = store.dump();
    CHECK(all.size() == 2);
    CHECK(all.at("a.b") == "1");
    CHECK(all.at("c.d") == "\"e\"");
}

TEST_CASE("defaultUserConfigDir respects XDG_CONFIG_HOME")
{
    // The function reads the environment each time, so this test is safe to run
    // in any order as long as it restores the previous value.
    const char* previous = std::getenv("XDG_CONFIG_HOME");
    const std::string saved = (previous != nullptr) ? previous : "";

    setenv("XDG_CONFIG_HOME", "/tmp/erashift-xdg", 1);
    CHECK(ConfigManager::defaultUserConfigDir() == std::filesystem::path("/tmp/erashift-xdg/EraShift"));

    // Without XDG_CONFIG_HOME the fallback is $HOME/.config/EraShift, or
    // <cwd>/config when there is no HOME either. Both end in the app name.
    unsetenv("XDG_CONFIG_HOME");
    CHECK(ConfigManager::defaultUserConfigDir().filename() == "EraShift");

    if (previous != nullptr) {
        setenv("XDG_CONFIG_HOME", saved.c_str(), 1);
    }
}
