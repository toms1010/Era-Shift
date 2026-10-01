#include "EraShift/Game/Level.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <sstream>

namespace EraShift::Game {

namespace {

using Json = nlohmann::json;

/// Tiles that exist in every era are written once; the per-era rows only need
/// to carry what actually differs.
Tile tileFromChar(char c) noexcept
{
    switch (c) {
        case '#': return Tile::of(TileKind::Solid);
        case '=': return Tile::of(TileKind::Platform);
        case 'c': return Tile::of(TileKind::Crumble);
        case 'b': return Tile::of(TileKind::Bridge);
        case 'x': return Tile::of(TileKind::Crystal);
        case '^': return Tile::of(TileKind::Hazard);
        case 'G': return Tile::of(TileKind::Goal);
        case '.':
        case ' ': return Tile::of(TileKind::Empty);
        default:  return Tile::of(TileKind::Empty);
    }
}

/// Parses an era list such as "Past,Present" or "All".
EraMask maskFromString(std::string_view text) noexcept
{
    if (text.empty()) {
        return kEraMaskAll;
    }
    if (text == "All") {
        return kEraMaskAll;
    }

    EraMask mask = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string_view token =
            text.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                               : comma - start);
        Era era = Era::Present;
        if (!token.empty() && parseEra(token, era)) {
            mask |= eraBit(era);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return mask == 0 ? kEraMaskAll : mask;
}

bool nameToKind(std::string_view name, EntityKind& out) noexcept
{
    if (name == "player") { out = EntityKind::PlayerSpawn; return true; }
    if (name == "enemy")  { out = EntityKind::Enemy;       return true; }
    if (name == "pickup") { out = EntityKind::Pickup;      return true; }
    if (name == "seal")   { out = EntityKind::Seal;        return true; }
    return false;
}

} // namespace

bool parseTileRow(std::string_view row, std::vector<Tile>& out)
{
    if (row.empty()) {
        return false;
    }
    out.clear();
    out.reserve(row.size());
    for (const char c : row) {
        out.push_back(tileFromChar(c));
    }
    return true;
}

TileMap Level::buildMap() const
{
    const auto widest = [](std::size_t a, std::size_t b) { return a > b ? a : b; };
    std::size_t columns = 0;
    std::size_t rows    = 0;
    for (const std::vector<std::string>* layer : {&pastRows, &presentRows, &futureRows}) {
        rows    = widest(rows, layer->size());
        for (const std::string& row : *layer) {
            columns = widest(columns, row.size());
        }
    }

    TileMap map;
    if (columns == 0 || rows == 0) {
        return map;
    }
    map.resize(static_cast<int>(columns), static_cast<int>(rows));

    const std::vector<std::string>* layers[kEraCount] = {&pastRows, &presentRows, &futureRows};

    for (std::size_t y = 0; y < rows; ++y) {
        for (std::size_t x = 0; x < columns; ++x) {
            Tile combined;

            for (int e = 0; e < kEraCount; ++e) {
                const std::vector<std::string>& layer = *layers[e];
                if (y >= layer.size() || x >= layer[y].size()) {
                    continue;
                }
                const Tile one = tileFromChar(layer[y][x]);
                const Era era  = static_cast<Era>(e);

                // Each layer describes *that era's* world, so a property is only
                // copied across for the era it appears in. Taking the tile's
                // masks wholesale would make a hazard written into the Present
                // layer a hazard in the Past as well, painting spikes on solid
                // floor.
                if (one.blocksIn(era)) {
                    combined.solidIn |= eraBit(era);
                }
                if (one.isHazardIn(era)) {
                    combined.hazardIn |= eraBit(era);
                }
                if (one.isOneWayIn(era)) {
                    combined.oneWayIn |= eraBit(era);
                }

                // The displayed kind is the most structural of the three, so a
                // cell that is solid everywhere is not drawn as if it were only
                // solid in one era.
                const auto rank = [](TileKind kind) {
                    switch (kind) {
                        case TileKind::Empty:  return 0;
                        case TileKind::Goal:   return 1;
                        case TileKind::Crumble:
                        case TileKind::Bridge:
                        case TileKind::Crystal: return 2;
                        case TileKind::Hazard: return 3;
                        case TileKind::Platform: return 4;
                        case TileKind::Solid:  return 5;
                    }
                    return 0;
                };
                if (rank(one.kind) > rank(combined.kind)) {
                    combined.kind = one.kind;
                }
            }

            map.set(static_cast<int>(x), static_cast<int>(y), combined);
        }
    }
    return map;
}

Vec2 Level::worldPosition(const PlacedEntity& entity) const noexcept
{
    return Vec2{static_cast<float>(entity.x) * TileMap::kTileSize,
                static_cast<float>(entity.y) * TileMap::kTileSize};
}

const PlacedEntity* Level::find(EntityKind kind) const noexcept
{
    for (const PlacedEntity& entity : entities) {
        if (entity.kind == kind) {
            return &entity;
        }
    }
    return nullptr;
}

bool loadLevelFromJson(std::string_view json, Level& out, std::string& errorOut)
{
    Json document;
    try {
        document = Json::parse(json);
    } catch (const std::exception& ex) {
        errorOut = std::string("invalid JSON: ") + ex.what();
        return false;
    }

    if (!document.is_object()) {
        errorOut = "level document must be an object";
        return false;
    }

    Level level;
    level.id   = document.value("id", std::string{"unknown"});
    level.name = document.value("name", level.id);

    const auto readRows = [&](const char* key, std::vector<std::string>& target) -> bool {
        if (!document.contains(key)) {
            return true;
        }
        const Json& rows = document.at(key);
        if (!rows.is_array()) {
            errorOut = std::string("'") + key + "' must be an array of strings";
            return false;
        }
        for (const Json& row : rows) {
            if (!row.is_string()) {
                errorOut = std::string("'") + key + "' must contain only strings";
                return false;
            }
            target.push_back(row.get<std::string>());
        }
        return true;
    };

    if (!readRows("past", level.pastRows) || !readRows("present", level.presentRows) ||
        !readRows("future", level.futureRows)) {
        return false;
    }

    if (level.presentRows.empty() && level.pastRows.empty() && level.futureRows.empty()) {
        errorOut = "level has no tile rows";
        return false;
    }

    if (document.contains("entities")) {
        const Json& entities = document.at("entities");
        if (!entities.is_array()) {
            errorOut = "'entities' must be an array";
            return false;
        }
        for (const Json& item : entities) {
            if (!item.is_object()) {
                errorOut = "each entity must be an object";
                return false;
            }
            PlacedEntity entity;
            if (!nameToKind(item.value("type", std::string{}), entity.kind)) {
                // Unknown types are skipped: a level written for a later build
                // still loads rather than refusing to start.
                continue;
            }
            entity.x = item.value("x", 0);
            entity.y = item.value("y", 0);
            entity.label = item.value("label", std::string{});

            if (entity.kind == EntityKind::Enemy) {
                if (!parseEnemyKind(item.value("kind", std::string{"Sentinel"}), entity.enemy)) {
                    errorOut = "unknown enemy kind '" + item.value("kind", std::string{}) + "'";
                    return false;
                }
                entity.existsIn = maskFromString(item.value("eras", std::string{"A"}));
            } else if (entity.kind == EntityKind::Seal) {
                if (!parseEra(item.value("era", std::string{"Past"}), entity.sealEra)) {
                    errorOut = "unknown seal era '" + item.value("era", std::string{}) + "'";
                    return false;
                }
            } else if (entity.kind == EntityKind::Pickup) {
                entity.pickup = item.value("kind", std::string{"chrono"}) == "health"
                                    ? PickupKind::Health
                                    : PickupKind::ChronoCell;
            }
            level.entities.push_back(entity);
        }
    }

    // A level without a spawn point would drop the player into nowhere, so the
    // absence is reported rather than papered over.
    if (level.find(EntityKind::PlayerSpawn) == nullptr) {
        errorOut = "level has no player spawn";
        return false;
    }

    out = std::move(level);
    return true;
}

bool loadLevelFromFile(const std::filesystem::path& path, Level& out, std::string& errorOut)
{
    std::ifstream file(path);
    if (!file) {
        errorOut = "cannot open '" + path.string() + "'";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadLevelFromJson(buffer.str(), out, errorOut);
}

std::vector<LevelEntry> listLevels(const std::filesystem::path& directory,
                                   std::vector<std::filesystem::path>* brokenOut)
{
    std::vector<LevelEntry> entries;

    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        // No directory is not a failure. A shipped build with no `data/` shows an
        // empty level list, which is honest; returning an error here would make
        // every caller treat "no extra levels" as "something is broken".
        return entries;
    }

    // Sorted so the list order does not depend on the filesystem's. An
    // unsorted directory listing is the kind of thing that looks fine until a
    // player gets a different order on a different machine.
    std::vector<std::filesystem::path> files;
    for (const auto& item : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (!item.is_regular_file() || item.path().extension() != ".json") {
            continue;
        }
        files.push_back(item.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        Level level;
        std::string error;
        if (!loadLevelFromFile(file, level, error)) {
            // Skipped, not fatal: one unreadable file must not make the whole
            // level select unopenable.
            if (brokenOut != nullptr) {
                brokenOut->push_back(file);
            }
            continue;
        }
        LevelEntry entry;
        entry.file = file.filename();
        entry.id   = file.stem().string();
        // The file's own `name` wins, because that is what the author wrote for
        // players. The stem is only the fallback for a file that omits it.
        entry.name = level.name.empty() ? entry.id : level.name;
        entries.push_back(std::move(entry));
    }
    return entries;
}

Level builtInLevel()
{
    // A small, complete, hand-authored level. It exists so the game always has
    // something to play and so the world can be tested without touching the
    // filesystem. `data/levels/ancient_forest.json` is the real region.
    Level level;
    level.id   = "fallback_ruins";
    level.name = "Fallback Ruins";

    level.pastRows = {
        "................................................................",
        "................................................................",
        "...............................................................",
        "...............................................................",
        "..............==......................==......===.............",
        "................................................................",
        "......................................................xx.......",
        "................................................................",
        "............^^^.......................^^^.............^^.........",
        "................................................................",
        "................................................................",
        ".....==.........................................................",
        "................................................................",
        "................................................................",
        "...........................................................G....",
        "################################################################",
        "################################################################",
    };

    level.presentRows = {
        "................................................................",
        "................................................................",
        "...............................................................",
        "...............................................................",
        "..............==......................==......===.............",
        "................................................................",
        "......................................................xx.......",
        "................................................................",
        "............^^^.......................^^^.............^^.........",
        "................................................................",
        ".......................bbbbbbbbbbbbbbbbbbbbbbbbbbbb..........",
        ".....==.........................................................",
        "................................................................",
        "................................................................",
        "...........................................................G....",
        "###########bbbbbbb##########bbbbbbbbbb########bbbbbbbb########",
        "################################################################",
    };

    level.futureRows = {
        "................................................................",
        "................................................................",
        "...............................................................",
        "...............................................................",
        "..............==......................==......===.............",
        ".....................................................xxxxxx....",
        "......................................................xx.......",
        ".............................................................",
        "............^^^.......................^^^.............^^.........",
        ".......................xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.",
        ".....==.........................................................",
        "................................................................",
        "................................................................",
        "...........................................................G....",
        "##########xxxxxxxxxx#########xxxxxxxxxxxxx#########xxxxxxxxxx#",
        "################################################################",
        "################################################################",
    };

    PlacedEntity spawn;
    spawn.kind = EntityKind::PlayerSpawn;
    spawn.x    = 2;
    spawn.y    = 12;
    level.entities.push_back(spawn);

    PlacedEntity chrono;
    chrono.kind   = EntityKind::Pickup;
    chrono.pickup = PickupKind::ChronoCell;
    chrono.x      = 10;
    chrono.y      = 12;
    level.entities.push_back(chrono);

    chrono.x = 24;
    chrono.y = 12;
    level.entities.push_back(chrono);

    chrono.x = 40;
    chrono.y = 12;
    level.entities.push_back(chrono);

    chrono.x = 52;
    chrono.y = 12;
    level.entities.push_back(chrono);

    PlacedEntity medkit;
    medkit.kind   = EntityKind::Pickup;
    medkit.pickup = PickupKind::Health;
    medkit.x      = 33;
    medkit.y      = 8;
    level.entities.push_back(medkit);

    PlacedEntity seal;
    seal.kind    = EntityKind::Seal;
    seal.sealEra = Era::Past;
    seal.x       = 8;
    seal.y       = 13;
    seal.label   = "PAST SEAL";
    level.entities.push_back(seal);

    seal.sealEra = Era::Present;
    seal.x       = 20;
    seal.y       = 12;
    seal.label   = "PRESENT SEAL";
    level.entities.push_back(seal);

    seal.sealEra = Era::Future;
    seal.x       = 46;
    seal.y       = 8;
    seal.label   = "FUTURE SEAL";
    level.entities.push_back(seal);

    PlacedEntity enemy;
    enemy.kind = EntityKind::Enemy;
    enemy.enemy = EnemyKind::Sentinel;
    enemy.x     = 16;
    enemy.y     = 13;
    level.entities.push_back(enemy);

    enemy.x = 30;
    enemy.y = 13;
    level.entities.push_back(enemy);

    enemy.enemy = EnemyKind::Warden;
    enemy.x     = 36;
    enemy.y     = 13;
    enemy.existsIn = kEraBitPast;
    level.entities.push_back(enemy);

    enemy.enemy = EnemyKind::Wisp;
    enemy.x     = 44;
    enemy.y     = 6;
    enemy.existsIn = kEraBitFuture;
    level.entities.push_back(enemy);

    return level;
}

} // namespace EraShift::Game
