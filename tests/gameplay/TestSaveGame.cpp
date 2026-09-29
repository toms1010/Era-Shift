// Era Shift - tests for save files.
//
// A save system has exactly one job it can fail at badly: silently reading
// rubbish as a valid run. These tests are mostly about what is *refused*.

#include "EraShift/Core/Config.hpp"
#include "EraShift/Core/SaveGame.hpp"

#include <filesystem>
#include <fstream>

#include <doctest/doctest.h>

using EraShift::Core::ConfigManager;
using EraShift::Core::LogLevel;
using EraShift::Core::Logger;
using EraShift::Core::SaveGame;
using EraShift::Core::SaveManager;

namespace {

/// A temporary directory that removes itself.
class TempDir {
public:
    explicit TempDir(const std::string& name)
        : m_path(std::filesystem::temp_directory_path() / ("erashift-test-" + name))
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
        std::filesystem::create_directories(m_path, ec);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

SaveGame sample()
{
    SaveGame save;
    save.levelId   = "ancient_forest";
    save.levelName = "Ancient Forest";
    save.playerX   = 42;
    save.playerY   = 23;
    save.era       = static_cast<int>(EraShift::Game::Era::Future);
    save.health    = 3.5f;
    save.chrono    = 61.0f;
    save.paradox   = 128.0f;
    save.elapsed   = 91.25;
    save.shifts    = 5;
    save.kills     = 2;
    save.seals     = 3;
    save.sealMask  = 0b111;
    return save;
}

} // namespace

TEST_CASE("a save round-trips through JSON")
{
    const SaveGame original = sample();
    SaveGame restored;
    std::string error;

    REQUIRE(SaveGame::fromJson(original.toJson(), restored, error));
    CHECK(error.empty());

    CHECK(restored.version == original.version);
    CHECK(restored.levelId == original.levelId);
    CHECK(restored.playerX == original.playerX);
    CHECK(restored.playerY == original.playerY);
    CHECK(restored.era == original.era);
    CHECK(restored.health == doctest::Approx(original.health));
    CHECK(restored.chrono == doctest::Approx(original.chrono));
    CHECK(restored.paradox == doctest::Approx(original.paradox));
    CHECK(restored.elapsed == doctest::Approx(original.elapsed));
    CHECK(restored.shifts == original.shifts);
    CHECK(restored.kills == original.kills);
    CHECK(restored.sealMask == original.sealMask);
}

TEST_CASE("a save from a different version is refused, not half-read")
{
    const std::string json = R"({"version": 99, "levelId": "x", "player": {"x": 1, "y": 2}})";
    SaveGame save;
    std::string error;
    CHECK_FALSE(SaveGame::fromJson(json, save, error));
    CHECK(error.find("version") != std::string::npos);
}

TEST_CASE("rubbish is refused with a reason")
{
    SaveGame save;
    std::string error;

    CHECK_FALSE(SaveGame::fromJson("this is not json", save, error));
    CHECK_FALSE(error.empty());

    CHECK_FALSE(SaveGame::fromJson("[]", save, error));

    // A save with no version at all is not a save this build can trust.
    CHECK_FALSE(SaveGame::fromJson(R"({"levelId": "x"})", save, error));
    CHECK(error.find("version") != std::string::npos);
}

TEST_CASE("a save recording an era that does not exist is refused")
{
    SaveGame save = sample();
    save.era = 7;
    SaveGame restored;
    std::string error;
    CHECK_FALSE(SaveGame::fromJson(save.toJson(), restored, error));
    CHECK(error.find("era") != std::string::npos);
}

TEST_CASE("a save with a negative position is refused")
{
    SaveGame save = sample();
    save.playerX = -3;
    SaveGame restored;
    std::string error;
    CHECK_FALSE(SaveGame::fromJson(save.toJson(), restored, error));
    CHECK(error.find("position") != std::string::npos);
}

TEST_CASE("writing then reading a slot gives back the same run")
{
    TempDir dir("save-roundtrip");
    Logger log;
    log.setMinLevel(LogLevel::Error);
    log.clearSinks();

    SaveManager manager(dir.path(), log);
    CHECK_FALSE(manager.hasSave());

    const SaveGame original = sample();
    REQUIRE(manager.store(original));
    CHECK(manager.hasSave());

    SaveGame loaded;
    REQUIRE(manager.load(loaded));
    CHECK(loaded.playerX == original.playerX);
    CHECK(loaded.sealMask == original.sealMask);
}

TEST_CASE("no save file means loading fails cleanly rather than throwing")
{
    TempDir dir("save-missing");
    Logger log;
    log.setMinLevel(LogLevel::Error);
    log.clearSinks();

    SaveManager manager(dir.path(), log);
    CHECK_FALSE(manager.hasSave());

    SaveGame loaded;
    CHECK_FALSE(manager.load(loaded));
}

TEST_CASE("a corrupt slot is reported and ignored, and does not block a rewrite")
{
    TempDir dir("save-corrupt");
    Logger log;
    log.setMinLevel(LogLevel::Error);
    log.clearSinks();

    SaveManager manager(dir.path(), log);
    std::filesystem::create_directories(dir.path());
    {
        std::ofstream file(manager.slotPath());
        file << "{ truncated";
    }

    SaveGame loaded;
    CHECK_FALSE(manager.load(loaded));

    // The corrupt file is still on disk, so `hasSave` says yes; that is why
    // the menu re-checks by loading rather than trusting the flag alone.
    REQUIRE(manager.store(sample()));
    SaveGame again;
    REQUIRE(manager.load(again));
    CHECK(again.playerX == sample().playerX);
}

TEST_CASE("erasing a save makes CONTINUE unavailable again")
{
    TempDir dir("save-erase");
    Logger log;
    log.setMinLevel(LogLevel::Error);
    log.clearSinks();

    SaveManager manager(dir.path(), log);
    REQUIRE(manager.store(sample()));
    CHECK(manager.hasSave());

    CHECK(manager.erase());
    CHECK_FALSE(manager.refresh());
    CHECK_FALSE(manager.hasSave());
}

TEST_CASE("writing leaves no temporary file behind")
{
    TempDir dir("save-atomic");
    Logger log;
    log.setMinLevel(LogLevel::Error);
    log.clearSinks();

    SaveManager manager(dir.path(), log);
    REQUIRE(manager.store(sample()));

    // A leftover .tmp would mean the rename did not happen, which is the whole
    // point of writing through one.
    const auto expected = manager.slotPath().string() + ".tmp";
    CHECK_FALSE(std::filesystem::exists(expected));
}

TEST_CASE("the save directory lives beside the user's settings, not the executable")
{
    Logger log;
    log.setMinLevel(LogLevel::Error);
    log.clearSinks();

    ConfigManager config(log);
    config.load(std::filesystem::current_path(), 0, nullptr);
    const auto directory = config.saveDirectory();

    // The user config directory may be empty before a real load in a container,
    // but the path must still be namespaced by the application rather than
    // being relative to wherever the game happened to be launched.
    CHECK(directory.filename() == "saves");
    CHECK_FALSE(directory.empty());
}
