// Era Shift - saving and loading a run.
//
// The save format is plain JSON with an explicit version field, because the one
// thing a save system must never do is fail silently. A file written by a newer
// build is refused with a reason, not half-read.
//
// Saves live in the user's config directory rather than next to the executable,
// so they survive a reinstall and are not accidentally committed.

#pragma once

#include "EraShift/Core/Log.hpp"
#include "EraShift/Game/Era.hpp"
#include "EraShift/Game/World.hpp"

#include <filesystem>
#include <string>

namespace EraShift::Core {

/// Everything needed to resume a run.
struct SaveGame {
    /// Bumped whenever the meaning of a field changes. A file with a different
    /// version is refused rather than misinterpreted.
    static constexpr int kVersion = 1;

    int version = kVersion;
    std::string levelId;
    std::string levelName;

    int   playerX = 0;
    int   playerY = 0;
    int   era     = static_cast<int>(Game::Era::Present);
    float health  = 0.0f;
    float chrono  = 0.0f;
    float paradox = 0.0f;
    bool  dead    = false;

    double elapsed = 0.0;
    int shifts = 0;
    int kills  = 0;
    int seals  = 0;
    /// Index-bitmask of which seals were taken, so partial progress survives.
    int sealMask = 0;

    [[nodiscard]] std::string toJson(int indent = 2) const;
    /// @return false and fills `errorOut` when the document cannot be used.
    static bool fromJson(std::string_view json, SaveGame& out, std::string& errorOut);
};

/// Reads and writes the single continue slot.
class SaveManager {
public:
    /// `directory` is where save files live. It is created on first write.
    SaveManager(std::filesystem::path directory, Logger& log);

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return m_directory; }
    [[nodiscard]] std::filesystem::path slotPath() const;

    /// True when a save exists and parses. Used to enable CONTINUE, so it has
    /// to be cheap and has to not spam the log on every call.
    [[nodiscard]] bool hasSave() const noexcept { return m_hasSave; }
    /// Re-checks the slot on disk.
    bool refresh();

    bool load(SaveGame& out) const;
    bool store(const SaveGame& game) const;
    bool erase() const;

private:
    std::filesystem::path m_directory;
    Logger*               m_log = nullptr;
    bool                  m_hasSave = false;
};

} // namespace EraShift::Core
