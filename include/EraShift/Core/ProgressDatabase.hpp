// Era Shift - progression database.
//
// SQLite, entirely local. One file in the user's config directory holds the
// things a save file is bad at: which levels are unlocked, how each one went,
// and whether the tutorial has been seen. A single continue-slot JSON file
// cannot express "level 7 completed at 4:12 with 3 seals" alongside "level 8
// unlocked", and bolting those onto it would make every new field a change to
// the format.
//
// The split is deliberate:
//
//   SaveManager / continue.json   one slot, exact run state, rewritten often
//   ProgressDatabase              many rows, read and written rarely
//
// Both are versioned. A file the current build cannot read is reported with a
// reason and then ignored, never half-read, because the failure mode that
// matters is a player losing a run they had every right to keep.

#pragma once

#include "EraShift/Core/Log.hpp"
#include "EraShift/Game/Era.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct sqlite3;

namespace EraShift::Core {

/// How one region went, for the level-select screen.
struct LevelRecord {
    std::string levelId;
    std::string name;
    bool  unlocked   = false;
    bool  completed  = false;
    double bestTime  = 0.0;
    int    bestScore = 0;
    int    seals     = 0;   ///< Highest seal count seen in one attempt.
    int    attempts  = 0;
};

/// A point the player can be returned to after dying.
struct Checkpoint {
    std::string levelId;
    int   tileX       = 0;
    int   tileY       = 0;
    int   era         = static_cast<int>(Game::Era::Present);
    float health      = 0.0f;
    float chrono      = 0.0f;
    int   sealMask    = 0;
    double elapsed    = 0.0;
    int   shifts      = 0;
    int   kills       = 0;
    bool  valid       = false;
};

/// Aggregate progression, read once per session and written rarely.
struct Progress {
    std::string currentLevelId;
    /// Highest level index unlocked, 1-based. Level 1 is always unlocked.
    int  highestLevel     = 1;
    bool tutorialComplete = false;
    bool tutorialEnabled  = true;
    double totalPlayTime  = 0.0;
    std::vector<LevelRecord> levels;
};

/// Local SQLite-backed store for progression, unlocks and checkpoints.
///
/// Owns its `sqlite3*` and closes it in the destructor. Every public call is
/// safe on a database that could not be opened: the object stays usable, every
/// query reports failure, and `available()` is false. That is the same
/// philosophy as the rest of the engine — a missing optional thing must not stop
/// the game starting.
class ProgressDatabase {
public:
    /// Opens (and creates if absent) the database at `path`, applying the schema.
    ///
    /// Never throws. A failure to open leaves the object in the same state as a
    /// default-constructed one, with the reason logged and available from
    /// `lastError()`.
    /// A default-constructed instance holds no database; every call reports
    /// failure until `open` is called. `Engine` owns one of these and opens it
    /// during `initialise`, once the save directory is known.
    ProgressDatabase() = default;
    explicit ProgressDatabase(std::filesystem::path path, Logger& log);
    ~ProgressDatabase();

    ProgressDatabase(const ProgressDatabase&)            = delete;
    ProgressDatabase& operator=(const ProgressDatabase&) = delete;

    /// Opens (and creates if absent) the database at `path`, applying the schema.
    ///
    /// Separate from the constructor so `Engine` can own a default-constructed
    /// instance and open it during `initialise`. The class holds a `sqlite3*`, so
    /// assigning one into another would have to close the first handle; a named
    /// `open` says what it does instead.
    void open(std::filesystem::path path);
    /// Closes the handle. Safe to call when nothing is open.
    void close() noexcept;

    /// False when the database could not be opened. Every other call is a no-op
    /// returning a default when this is false.
    [[nodiscard]] bool available() const noexcept { return m_db != nullptr; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }
    /// Why the last failing operation failed. Empty when nothing has failed.
    [[nodiscard]] const std::string& lastError() const noexcept { return m_lastError; }

    /// Creates the schema if absent and records the running schema version.
    ///
    /// A database written by a newer build is left alone and reported, rather
    /// than migrated downwards: an older binary writing over a newer schema is
    /// how progress databases lose data.
    bool initialise();

    /// Reads progression and every level's record.
    [[nodiscard]] Progress loadProgress();
    /// Writes progression. Does not touch per-level rows.
    bool saveProgress(const Progress& progress);

    /// Records one region's result, unlocking the next on a completion.
    ///
    /// Best time and score keep the *better* of the two values seen, because a
    /// completion is not always an improvement and overwriting a good time with a
    /// worse one would be a small lie about the player's history.
    bool recordLevelResult(const std::string& levelId, const std::string& name,
                           bool completed, double time, int score, int seals);

    /// Marks a level unlocked without claiming it was played.
    bool unlockLevel(const std::string& levelId, int index);

    /// Clears everything. Used by NEW GAME.
    bool resetAll();

    /// Stores the current checkpoint for a level, replacing any previous one.
    bool writeCheckpoint(const Checkpoint& checkpoint);
    /// Reads a level's checkpoint. `out.valid` is false when there is none.
    [[nodiscard]] Checkpoint readCheckpoint(const std::string& levelId);
    bool clearCheckpoint(const std::string& levelId);

    /// Runs one or more statements, for tests that need to set up a state the
    /// public API cannot reach — a schema written by a newer build, most
    /// obviously. Public for that reason alone; nothing in the game calls it.
    ///
    /// Returns false and records the reason on failure, like every other call.
    bool execForTesting(const char* sql) { return exec(sql); }

    /// The schema version this build expects.
    static constexpr int kSchemaVersion = 1;

private:
    bool exec(const char* sql);
    bool prepareFailed(int rc, const char* what);

    std::filesystem::path m_path;
    Logger*     m_log = nullptr;
    sqlite3*    m_db  = nullptr;
    std::string m_lastError;
};

} // namespace EraShift::Core