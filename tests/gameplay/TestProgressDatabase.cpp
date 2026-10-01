// Era Shift - tests for the progression database.
//
// The database is the only thing standing between a player and their unlocked
// levels, so these cover the failure paths as carefully as the happy one: a
// database that cannot be opened, a schema from a newer build, and a checkpoint
// written for a level that no longer exists.
//
// Every test uses its own temporary file. The real one is the player's.

#include "EraShift/Core/ProgressDatabase.hpp"
#include "EraShift/Core/Log.hpp"

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

// Aliases rather than `using namespace EraShift::Core`: this file also refers to
// `EraShift::Game::Era`, and a namespace-wide import of both would make the
// unqualified `Era` ambiguous.
using EraShift::Core::LevelRecord;
using EraShift::Core::ProgressDatabase;

namespace {

using EraShift::Core::Logger;

EraShift::Core::Logger& quietLog()
{
    static EraShift::Core::Logger logger;
    logger.setMinLevel(EraShift::Core::LogLevel::Off);
    return logger;
}

using EraShift::Core::Checkpoint;
using EraShift::Core::Progress;

/// A unique temporary database path, removed on destruction.
class TempDatabase {
public:
    explicit TempDatabase(const char* name)
    {
        static int counter = 0;
        m_path = std::filesystem::temp_directory_path() /
                 ("erashift_progress_" + std::string(name) + "_" +
                  std::to_string(counter++) + ".db");
        std::filesystem::remove(m_path);
    }
    ~TempDatabase()
    {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

} // namespace

TEST_CASE("a database opens, applies its schema and is idempotent")
{
    TempDatabase temp("schema");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.available());
    REQUIRE(db.initialise());
    // Applying the schema twice must not fail: `initialise` runs on every launch.
    CHECK(db.initialise());

    const Progress progress = db.loadProgress();
    CHECK(progress.highestLevel == 1);
    CHECK_FALSE(progress.tutorialComplete);
    CHECK(progress.levels.empty());
}

TEST_CASE("progress round trips")
{
    TempDatabase temp("progress");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());

    Progress written;
    written.currentLevelId    = "fallen_span";
    written.highestLevel     = 4;
    written.tutorialComplete = true;
    written.tutorialEnabled  = false;
    written.totalPlayTime    = 1234.5;
    REQUIRE(db.saveProgress(written));

    const Progress read = db.loadProgress();
    CHECK(read.currentLevelId == "fallen_span");
    CHECK(read.highestLevel == 4);
    CHECK(read.tutorialComplete);
    CHECK_FALSE(read.tutorialEnabled);
    CHECK(read.totalPlayTime == doctest::Approx(1234.5));
}

TEST_CASE("saving progress twice overwrites rather than appends")
{
    // The game_progress table is keyed on a single row; a second save must not
    // leave the first one behind.
    TempDatabase temp("overwrite");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());

    Progress first;
    first.currentLevelId = "awakening";
    REQUIRE(db.saveProgress(first));

    Progress second;
    second.currentLevelId = "convergence";
    second.highestLevel  = 10;
    REQUIRE(db.saveProgress(second));

    const Progress read = db.loadProgress();
    CHECK(read.currentLevelId == "convergence");
    CHECK(read.highestLevel == 10);
}

TEST_CASE("a level result records, and keeps the better time and score")
{
    TempDatabase temp("results");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());

    REQUIRE(db.recordLevelResult("awakening", "Awakening", true, 90.0, 1200, 3));
    REQUIRE(db.recordLevelResult("awakening", "Awakening", true, 140.0, 800, 3));
    REQUIRE(db.recordLevelResult("awakening", "Awakening", true, 70.0, 1500, 3));

    const Progress progress = db.loadProgress();
    REQUIRE(progress.levels.size() == 1);
    const LevelRecord& record = progress.levels.front();
    CHECK(record.levelId == "awakening");
    CHECK(record.name == "Awakening");
    CHECK(record.completed);
    CHECK(record.unlocked);
    CHECK(record.bestTime == doctest::Approx(70.0));
    CHECK(record.bestScore == 1500);
    // One row, three attempts: a replay must not multiply the level.
    CHECK(record.attempts == 3);
}

TEST_CASE("an incomplete attempt cannot un-complete a finished level")
{
    TempDatabase temp("regress");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());

    REQUIRE(db.recordLevelResult("awakening", "Awakening", true, 80.0, 1000, 3));
    REQUIRE(db.recordLevelResult("awakening", "Awakening", false, 95.0, 500, 1));

    const Progress progress = db.loadProgress();
    REQUIRE(progress.levels.size() == 1);
    CHECK(progress.levels.front().completed);
    CHECK(progress.levels.front().seals == 3);
}

TEST_CASE("a zero time does not wipe a real best")
{
    TempDatabase temp("zerotime");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());

    REQUIRE(db.recordLevelResult("awakening", "Awakening", true, 88.0, 1000, 3));
    REQUIRE(db.recordLevelResult("awakening", "Awakening", true, 0.0, 0, 0));

    const Progress progress = db.loadProgress();
    REQUIRE(progress.levels.size() == 1);
    CHECK(progress.levels.front().bestTime == doctest::Approx(88.0));
}

TEST_CASE("unlocking raises the highest level but never lowers it")
{
    TempDatabase temp("unlock");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());

    REQUIRE(db.unlockLevel("awakening", 1));
    REQUIRE(db.unlockLevel("ancient_bridge", 2));
    CHECK(db.loadProgress().highestLevel == 2);

    REQUIRE(db.unlockLevel("awakening", 1));
    CHECK(db.loadProgress().highestLevel == 2);
}

TEST_CASE("checkpoints round trip and are replaced rather than duplicated")
{
    TempDatabase temp("checkpoint");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());

    Checkpoint written;
    written.levelId  = "split_meadow";
    written.tileX    = 42;
    written.tileY    = 18;
    written.era      = static_cast<int>(EraShift::Game::Era::Future);
    written.health   = 2.5f;
    written.chrono   = 40.0f;
    written.sealMask = 0b101;
    written.elapsed  = 61.25;
    written.shifts   = 7;
    written.kills    = 3;
    written.valid    = true;
    REQUIRE(db.writeCheckpoint(written));

    Checkpoint read = db.readCheckpoint("split_meadow");
    CHECK(read.valid);
    CHECK(read.tileX == 42);
    CHECK(read.tileY == 18);
    CHECK(read.era == written.era);
    CHECK(read.health == doctest::Approx(2.5f));
    CHECK(read.chrono == doctest::Approx(40.0f));
    CHECK(read.sealMask == 0b101);
    CHECK(read.shifts == 7);
    CHECK(read.kills == 3);

    // A second write at the same spot replaces: one checkpoint per level.
    written.tileX = 60;
    written.health = 1.0f;
    REQUIRE(db.writeCheckpoint(written));
    const Checkpoint moved = db.readCheckpoint("split_meadow");
    CHECK(moved.tileX == 60);
    CHECK(moved.health == doctest::Approx(1.0f));

    REQUIRE(db.clearCheckpoint("split_meadow"));
    CHECK_FALSE(db.readCheckpoint("split_meadow").valid);
}

TEST_CASE("a checkpoint for a level that does not exist reads as invalid")
{
    TempDatabase temp("nocp");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());
    CHECK_FALSE(db.readCheckpoint("never_played").valid);
}

TEST_CASE("a new game clears progress but keeps the schema")
{
    TempDatabase temp("reset");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());

    REQUIRE(db.recordLevelResult("awakening", "Awakening", true, 60.0, 900, 3));
    REQUIRE(db.unlockLevel("ancient_bridge", 2));
    Progress saved;
    saved.currentLevelId = "awakening";
    saved.highestLevel  = 2;
    saved.tutorialComplete = true;
    REQUIRE(db.saveProgress(saved));

    REQUIRE(db.resetAll());

    const Progress progress = db.loadProgress();
    CHECK(progress.levels.empty());
    CHECK(progress.currentLevelId.empty());
    CHECK(progress.highestLevel == 1);
    CHECK_FALSE(progress.tutorialComplete);

    // The database is still usable afterwards, which is what "keeps the schema"
    // has to mean in practice.
    CHECK(db.recordLevelResult("awakening", "Awakening", true, 10.0, 100, 3));
}

TEST_CASE("a corrupt database file is refused, not crashed on")
{
    TempDatabase temp("corrupt");
    {
        std::ofstream file(temp.path(), std::ios::binary);
        file << "this is definitely not a SQLite database, not even close";
    }

    ProgressDatabase db(temp.path(), quietLog());
    if (db.available()) {
        // SQLite will happily open a file it cannot read a schema from. What must
        // hold is that initialise fails cleanly and every call is then a no-op.
        CHECK_FALSE(db.initialise());
    }
    // Either it refused to open, or it opened and refused the schema. Both leave
    // a usable object.
    const Progress progress = db.loadProgress();
    CHECK(progress.highestLevel == 1);
    CHECK_FALSE(db.writeCheckpoint(Checkpoint{}));
}

TEST_CASE("an unwritable path leaves the object usable and reporting")
{
    // A directory where a file should be: opening must fail rather than crash.
    const auto dir = std::filesystem::temp_directory_path() / "erashift_progress_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "nested");

    ProgressDatabase db(dir, quietLog());
    CHECK_FALSE(db.available());
    CHECK_FALSE(db.initialise());
    CHECK(db.lastError().empty() == false);

    // Every accessor is safe on a closed database.
    CHECK(db.loadProgress().levels.empty());
    CHECK(db.readCheckpoint("x").valid == false);
    CHECK_FALSE(db.saveProgress(Progress{}));
    CHECK_FALSE(db.recordLevelResult("a", "A", true, 1.0, 1, 3));
    CHECK_FALSE(db.unlockLevel("a", 1));
    CHECK_FALSE(db.resetAll());
    CHECK_FALSE(db.clearCheckpoint("a"));

    std::filesystem::remove_all(dir);
}

TEST_CASE("a database from a newer build is closed rather than written to")
{
    // The important direction: an older binary must not overwrite a newer schema.
    TempDatabase temp("newer");
    {
        ProgressDatabase db(temp.path(), quietLog());
        REQUIRE(db.initialise());
    }
    {
        // Rewrite the stored version to something this build does not know.
        ProgressDatabase db(temp.path(), quietLog());
        REQUIRE(db.available());
        REQUIRE(db.execForTesting("UPDATE meta SET value = '99' WHERE key = 'schema_version'"));
    }

    ProgressDatabase db(temp.path(), quietLog());
    CHECK(db.available());          // it opens...
    CHECK_FALSE(db.initialise());   // ...but refuses to touch the schema
    CHECK_FALSE(db.lastError().empty());
    CHECK_FALSE(db.available());    // and closes itself rather than half-using it
}

TEST_CASE("an absurd level index is clamped rather than stored")
{
    TempDatabase temp("clamp");
    ProgressDatabase db(temp.path(), quietLog());
    REQUIRE(db.initialise());

    Progress progress;
    progress.highestLevel = -50;
    REQUIRE(db.saveProgress(progress));
    CHECK(db.loadProgress().highestLevel == 1);

    REQUIRE(db.unlockLevel("x", 999999));
    CHECK(db.loadProgress().highestLevel == 1000);
}