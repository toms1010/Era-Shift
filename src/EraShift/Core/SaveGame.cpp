#include "EraShift/Core/SaveGame.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

namespace EraShift::Core {

namespace {

using Json = nlohmann::json;

/// File name of the single continue slot.
constexpr const char* kSlotFile = "continue.json";

std::string readFileText(const std::filesystem::path& path, std::error_code& ec)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) {
        ec = std::make_error_code(std::errc::io_error);
        return {};
    }
    return buffer.str();
}

/// Writes through a temporary file and renames into place.
///
/// A save is the one file where a partial write is worse than no write: a
/// truncated file parses as invalid and the run is lost either way, but a
/// rename is atomic, so the previous save survives a crash mid-write.
bool writeFileAtomic(const std::filesystem::path& path, const std::string& contents,
                     std::error_code& ec)
{
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            ec = std::make_error_code(std::errc::permission_denied);
            return false;
        }
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        file.flush();
        if (!file) {
            ec = std::make_error_code(std::errc::io_error);
            file.close();
            std::filesystem::remove(temporary, ec);
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return !ec;
}

} // namespace

std::string SaveGame::toJson(int indent) const
{
    Json document;
    document["version"]   = version;
    document["levelId"]   = levelId;
    document["levelName"] = levelName;
    document["player"]    = Json{{"x", playerX}, {"y", playerY}};
    document["era"]       = era;
    document["health"]    = health;
    document["chrono"]    = chrono;
    document["paradox"]   = paradox;
    document["dead"]      = dead;

    Json run;
    run["elapsed"]  = elapsed;
    run["shifts"]   = shifts;
    run["kills"]    = kills;
    run["seals"]    = seals;
    run["sealMask"] = sealMask;
    document["run"] = std::move(run);

    return document.dump(indent);
}

bool SaveGame::fromJson(std::string_view json, SaveGame& out, std::string& errorOut)
{
    Json document;
    try {
        document = Json::parse(json);
    } catch (const std::exception& ex) {
        errorOut = std::string("invalid JSON: ") + ex.what();
        return false;
    }

    if (!document.is_object()) {
        errorOut = "save document must be an object";
        return false;
    }

    const int version = document.value("version", 0);
    if (version != kVersion) {
        errorOut = "save is version " + std::to_string(version) + ", this build reads version " +
                   std::to_string(kVersion);
        return false;
    }

    SaveGame save;
    save.version   = version;
    save.levelId   = document.value("levelId", std::string{});
    save.levelName = document.value("levelName", save.levelId);

    if (document.contains("player") && document.at("player").is_object()) {
        const Json& player = document.at("player");
        save.playerX = player.value("x", 0);
        save.playerY = player.value("y", 0);
    }

    save.era     = document.value("era", static_cast<int>(Game::Era::Present));
    save.health  = document.value("health", 0.0f);
    save.chrono  = document.value("chrono", 0.0f);
    save.paradox = document.value("paradox", 0.0f);
    save.dead    = document.value("dead", false);

    if (document.contains("run") && document.at("run").is_object()) {
        const Json& run = document.at("run");
        save.elapsed  = run.value("elapsed", 0.0);
        save.shifts   = run.value("shifts", 0);
        save.kills    = run.value("kills", 0);
        save.seals    = run.value("seals", 0);
        save.sealMask = run.value("sealMask", 0);
    }

    // An era outside the three valid values means the file is not ours.
    if (save.era < 0 || save.era >= Game::kEraCount) {
        errorOut = "save records era " + std::to_string(save.era) + ", which does not exist";
        return false;
    }
    if (save.playerX < 0 || save.playerY < 0) {
        errorOut = "save records a negative player position";
        return false;
    }

    out = save;
    return true;
}

SaveManager::SaveManager(std::filesystem::path directory, Logger& log)
    : m_directory(std::move(directory)), m_log(&log)
{
    refresh();
}

std::filesystem::path SaveManager::slotPath() const
{
    return m_directory / kSlotFile;
}

bool SaveManager::refresh()
{
    std::error_code ec;
    m_hasSave = std::filesystem::is_regular_file(slotPath(), ec) && !ec;
    return m_hasSave;
}

bool SaveManager::load(SaveGame& out) const
{
    const std::filesystem::path path = slotPath();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return false;
    }

    std::error_code readError;
    const std::string json = readFileText(path, readError);
    if (readError) {
        if (m_log != nullptr) {
            m_log->warn("Save", "cannot read '{}': {}", path.string(), readError.message());
        }
        return false;
    }

    std::string error;
    if (!SaveGame::fromJson(json, out, error)) {
        if (m_log != nullptr) {
            m_log->warn("Save", "ignoring '{}': {}", path.string(), error);
        }
        return false;
    }
    return true;
}

bool SaveManager::store(const SaveGame& game) const
{
    std::error_code ec;
    std::filesystem::create_directories(m_directory, ec);
    if (ec) {
        if (m_log != nullptr) {
            m_log->error("Save", "cannot create '{}': {}", m_directory.string(), ec.message());
        }
        return false;
    }

    const std::filesystem::path path = slotPath();
    if (!writeFileAtomic(path, game.toJson(), ec) || ec) {
        if (m_log != nullptr) {
            m_log->error("Save", "cannot write '{}': {}", path.string(), ec.message());
        }
        return false;
    }
    if (m_log != nullptr) {
        m_log->info("Save", "wrote '{}'", path.string());
    }
    return true;
}

bool SaveManager::erase() const
{
    std::error_code ec;
    std::filesystem::remove(slotPath(), ec);
    return !ec;
}

} // namespace EraShift::Core
