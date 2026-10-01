#include "EraShift/Core/ProgressDatabase.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <system_error>

namespace EraShift::Core {

namespace {

/// One statement per table, created only when absent.
///
/// `IF NOT EXISTS` throughout, so `initialise` is idempotent: opening an
/// existing database is not a migration and must not rewrite it.
constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS game_progress (
    id                INTEGER PRIMARY KEY CHECK (id = 1),
    current_level_id  TEXT    NOT NULL DEFAULT '',
    highest_level     INTEGER NOT NULL DEFAULT 1,
    tutorial_complete INTEGER NOT NULL DEFAULT 0,
    tutorial_enabled  INTEGER NOT NULL DEFAULT 1,
    total_play_time   REAL    NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS level_progress (
    level_id   TEXT    PRIMARY KEY,
    name       TEXT    NOT NULL DEFAULT '',
    unlocked   INTEGER NOT NULL DEFAULT 0,
    completed  INTEGER NOT NULL DEFAULT 0,
    best_time  REAL    NOT NULL DEFAULT 0,
    best_score INTEGER NOT NULL DEFAULT 0,
    seals      INTEGER NOT NULL DEFAULT 0,
    attempts   INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS checkpoints (
    level_id TEXT PRIMARY KEY,
    tile_x   INTEGER NOT NULL DEFAULT 0,
    tile_y   INTEGER NOT NULL DEFAULT 0,
    era      INTEGER NOT NULL DEFAULT 0,
    health   REAL    NOT NULL DEFAULT 0,
    chrono   REAL    NOT NULL DEFAULT 0,
    seal_mask INTEGER NOT NULL DEFAULT 0,
    elapsed  REAL    NOT NULL DEFAULT 0,
    shifts   INTEGER NOT NULL DEFAULT 0,
    kills    INTEGER NOT NULL DEFAULT 0
);
)sql";

std::string columnText(sqlite3_stmt* stmt, int column)
{
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

int clampLevelIndex(int index) noexcept
{
    // A level index is a count of levels, so anything below one is nonsense and
    // anything enormous is a corrupt row. Clamping rather than rejecting keeps a
    // damaged database usable instead of turning it into a second failure.
    return std::clamp(index, 1, 1000);
}

} // namespace

ProgressDatabase::ProgressDatabase(std::filesystem::path path, Logger& log)
{
    open(std::move(path));
    if (m_log == nullptr) {
        m_log = &log;
    }
}

void ProgressDatabase::open(std::filesystem::path path)
{
    // Re-opening closes the previous handle first, so a caller that re-points the
    // database does not leak the old one.
    close();

    m_path     = std::move(path);
    m_lastError.clear();

    std::error_code ec;
    const std::filesystem::path parent = m_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }

    const int rc = sqlite3_open_v2(m_path.string().c_str(), &m_db,
                                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        m_lastError = m_db != nullptr ? sqlite3_errmsg(m_db) : "sqlite3_open_v2 failed";
        if (m_log != nullptr) {
            m_log->warn("Progress", "cannot open '{}': {}", m_path.string(), m_lastError);
        }
        if (m_db != nullptr) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }
}

void ProgressDatabase::close() noexcept
{
    if (m_db != nullptr) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

ProgressDatabase::~ProgressDatabase()
{
    close();
}

bool ProgressDatabase::prepareFailed(int rc, const char* what)
{
    m_lastError = std::string(what) + ": " +
                  (m_db != nullptr ? sqlite3_errmsg(m_db) : sqlite3_errstr(rc));
    if (m_log != nullptr) {
        m_log->warn("Progress", "{}", m_lastError);
    }
    return false;
}

bool ProgressDatabase::exec(const char* sql)
{
    if (m_db == nullptr) {
        return false;
    }
    char* message = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        m_lastError = message != nullptr ? std::string(message) : "sqlite3_exec failed";
        if (m_log != nullptr) {
            m_log->warn("Progress", "statement failed: {}", m_lastError);
        }
        if (message != nullptr) {
            sqlite3_free(message);
        }
        return false;
    }
    if (message != nullptr) {
        sqlite3_free(message);
    }
    return true;
}

bool ProgressDatabase::initialise()
{
    if (m_db == nullptr) {
        return false;
    }
    if (!exec(kSchema)) {
        return false;
    }

    // Refuse to touch a database written by a newer build. Writing an older
    // schema over a newer one is how progress silently disappears.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT value FROM meta WHERE key = 'schema_version'", -1,
                           &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const int found = std::atoi(columnText(stmt, 0).c_str());
            if (found > kSchemaVersion) {
                m_lastError = "database schema is version " + std::to_string(found) +
                              ", this build understands " + std::to_string(kSchemaVersion);
                if (m_log != nullptr) {
                    m_log->warn("Progress", "{}", m_lastError);
                }
                sqlite3_finalize(stmt);
                sqlite3_close(m_db);
                m_db = nullptr;
                return false;
            }
        }
        sqlite3_finalize(stmt);
    }

    // A single-row table needs its row to exist before anything can update it.
    const std::string versionInsert =
        "INSERT OR IGNORE INTO meta (key, value) VALUES ('schema_version', '" +
        std::to_string(kSchemaVersion) + "')";
    if (!exec(versionInsert.c_str())) {
        return false;
    }
    return exec("INSERT OR IGNORE INTO game_progress (id) VALUES (1)");
}

Progress ProgressDatabase::loadProgress()
{
    Progress progress;
    if (m_db == nullptr) {
        return progress;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            m_db,
            "SELECT current_level_id, highest_level, tutorial_complete, tutorial_enabled,"
            " total_play_time FROM game_progress WHERE id = 1",
            -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            progress.currentLevelId    = columnText(stmt, 0);
            progress.highestLevel     = clampLevelIndex(sqlite3_column_int(stmt, 1));
            progress.tutorialComplete = sqlite3_column_int(stmt, 2) != 0;
            progress.tutorialEnabled  = sqlite3_column_int(stmt, 3) != 0;
            progress.totalPlayTime    = sqlite3_column_double(stmt, 4);
        }
        sqlite3_finalize(stmt);
    }

    stmt = nullptr;
    if (sqlite3_prepare_v2(
            m_db,
            "SELECT level_id, name, unlocked, completed, best_time, best_score, seals,"
            " attempts FROM level_progress ORDER BY level_id",
            -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            LevelRecord record;
            record.levelId   = columnText(stmt, 0);
            record.name      = columnText(stmt, 1);
            record.unlocked  = sqlite3_column_int(stmt, 2) != 0;
            record.completed = sqlite3_column_int(stmt, 3) != 0;
            record.bestTime  = sqlite3_column_double(stmt, 4);
            record.bestScore = sqlite3_column_int(stmt, 5);
            record.seals     = sqlite3_column_int(stmt, 6);
            record.attempts  = sqlite3_column_int(stmt, 7);
            progress.levels.push_back(std::move(record));
        }
        sqlite3_finalize(stmt);
    }
    return progress;
}

bool ProgressDatabase::saveProgress(const Progress& progress)
{
    if (m_db == nullptr) {
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            m_db,
            "INSERT INTO game_progress (id, current_level_id, highest_level, tutorial_complete,"
            " tutorial_enabled, total_play_time) VALUES (1, ?, ?, ?, ?, ?)"
            " ON CONFLICT(id) DO UPDATE SET current_level_id = excluded.current_level_id,"
            " highest_level = excluded.highest_level,"
            " tutorial_complete = excluded.tutorial_complete,"
            " tutorial_enabled = excluded.tutorial_enabled,"
            " total_play_time = excluded.total_play_time",
            -1, &stmt, nullptr) != SQLITE_OK) {
        return prepareFailed(sqlite3_errcode(m_db), "saveProgress prepare");
    }

    sqlite3_bind_text(stmt, 1, progress.currentLevelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, clampLevelIndex(progress.highestLevel));
    sqlite3_bind_int(stmt, 3, progress.tutorialComplete ? 1 : 0);
    sqlite3_bind_int(stmt, 4, progress.tutorialEnabled ? 1 : 0);
    sqlite3_bind_double(stmt, 5, progress.totalPlayTime);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return prepareFailed(rc, "saveProgress step");
    }
    return true;
}

bool ProgressDatabase::recordLevelResult(const std::string& levelId, const std::string& name,
                                         bool completed, double time, int score, int seals)
{
    if (m_db == nullptr) {
        return false;
    }

    // Keep the better of the old and new values rather than overwriting. A player
    // replaying a level to farm a seal should not lose the time they set.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            m_db,
            "INSERT INTO level_progress (level_id, name, unlocked, completed, best_time,"
            " best_score, seals, attempts) VALUES (?, ?, 1, ?, ?, ?, ?, 1)"
            " ON CONFLICT(level_id) DO UPDATE SET"
            "   completed = MAX(level_progress.completed, excluded.completed),"
            "   best_time = CASE WHEN level_progress.best_time <= 0 THEN excluded.best_time"
            "                     WHEN excluded.best_time <= 0 THEN level_progress.best_time"
            "                     ELSE MIN(level_progress.best_time, excluded.best_time) END,"
            "   best_score = MAX(level_progress.best_score, excluded.best_score),"
            "   seals = MAX(level_progress.seals, excluded.seals),"
            "   attempts = level_progress.attempts + 1,"
            "   name = CASE WHEN excluded.name = '' THEN level_progress.name ELSE excluded.name END",
            -1, &stmt, nullptr) != SQLITE_OK) {
        return prepareFailed(sqlite3_errcode(m_db), "recordLevelResult prepare");
    }

    sqlite3_bind_text(stmt, 1, levelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, completed ? 1 : 0);
    sqlite3_bind_double(stmt, 4, time);
    sqlite3_bind_int(stmt, 5, score);
    sqlite3_bind_int(stmt, 6, seals);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return prepareFailed(rc, "recordLevelResult step");
    }
    return true;
}

bool ProgressDatabase::unlockLevel(const std::string& levelId, int index)
{
    if (m_db == nullptr) {
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            m_db,
            "INSERT INTO level_progress (level_id, unlocked) VALUES (?, 1)"
            " ON CONFLICT(level_id) DO UPDATE SET unlocked = 1",
            -1, &stmt, nullptr) != SQLITE_OK) {
        return prepareFailed(sqlite3_errcode(m_db), "unlockLevel prepare");
    }
    sqlite3_bind_text(stmt, 1, levelId.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return prepareFailed(rc, "unlockLevel step");
    }

    // The index is kept in the aggregate row too, so "what is unlocked" is one
    // number rather than a query the level select has to run every time it draws.
    sqlite3_stmt* progress = nullptr;
    if (sqlite3_prepare_v2(
            m_db,
            "UPDATE game_progress SET highest_level = MAX(highest_level, ?) WHERE id = 1",
            -1, &progress, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(progress, 1, clampLevelIndex(index));
        const int prc = sqlite3_step(progress);
        sqlite3_finalize(progress);
        if (prc != SQLITE_DONE) {
            return prepareFailed(prc, "unlockLevel progress step");
        }
    }
    return true;
}

bool ProgressDatabase::resetAll()
{
    if (m_db == nullptr) {
        return false;
    }
    // The schema itself survives; only the player's data goes.
    return exec("DELETE FROM level_progress;"
                "DELETE FROM checkpoints;"
                "UPDATE game_progress SET current_level_id = '', highest_level = 1,"
                " tutorial_complete = 0, total_play_time = 0 WHERE id = 1;");
}

bool ProgressDatabase::writeCheckpoint(const Checkpoint& checkpoint)
{
    if (m_db == nullptr) {
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            m_db,
            "INSERT INTO checkpoints (level_id, tile_x, tile_y, era, health, chrono, seal_mask,"
            " elapsed, shifts, kills) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(level_id) DO UPDATE SET tile_x = excluded.tile_x,"
            " tile_y = excluded.tile_y, era = excluded.era, health = excluded.health,"
            " chrono = excluded.chrono, seal_mask = excluded.seal_mask,"
            " elapsed = excluded.elapsed, shifts = excluded.shifts, kills = excluded.kills",
            -1, &stmt, nullptr) != SQLITE_OK) {
        return prepareFailed(sqlite3_errcode(m_db), "writeCheckpoint prepare");
    }

    sqlite3_bind_text(stmt, 1, checkpoint.levelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, checkpoint.tileX);
    sqlite3_bind_int(stmt, 3, checkpoint.tileY);
    sqlite3_bind_int(stmt, 4, checkpoint.era);
    sqlite3_bind_double(stmt, 5, checkpoint.health);
    sqlite3_bind_double(stmt, 6, checkpoint.chrono);
    sqlite3_bind_int(stmt, 7, checkpoint.sealMask);
    sqlite3_bind_double(stmt, 8, checkpoint.elapsed);
    sqlite3_bind_int(stmt, 9, checkpoint.shifts);
    sqlite3_bind_int(stmt, 10, checkpoint.kills);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return prepareFailed(rc, "writeCheckpoint step");
    }
    return true;
}

Checkpoint ProgressDatabase::readCheckpoint(const std::string& levelId)
{
    Checkpoint out;
    if (m_db == nullptr) {
        return out;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            m_db,
            "SELECT tile_x, tile_y, era, health, chrono, seal_mask, elapsed, shifts, kills"
            " FROM checkpoints WHERE level_id = ?",
            -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_text(stmt, 1, levelId.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.levelId  = levelId;
        out.tileX    = sqlite3_column_int(stmt, 0);
        out.tileY    = sqlite3_column_int(stmt, 1);
        out.era      = sqlite3_column_int(stmt, 2);
        out.health   = static_cast<float>(sqlite3_column_double(stmt, 3));
        out.chrono   = static_cast<float>(sqlite3_column_double(stmt, 4));
        out.sealMask = sqlite3_column_int(stmt, 5);
        out.elapsed  = sqlite3_column_double(stmt, 6);
        out.shifts   = sqlite3_column_int(stmt, 7);
        out.kills    = sqlite3_column_int(stmt, 8);
        out.valid    = true;
    }
    sqlite3_finalize(stmt);
    return out;
}

bool ProgressDatabase::clearCheckpoint(const std::string& levelId)
{
    if (m_db == nullptr) {
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM checkpoints WHERE level_id = ?", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return prepareFailed(sqlite3_errcode(m_db), "clearCheckpoint prepare");
    }
    sqlite3_bind_text(stmt, 1, levelId.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return prepareFailed(rc, "clearCheckpoint step");
    }
    return true;
}

} // namespace EraShift::Core