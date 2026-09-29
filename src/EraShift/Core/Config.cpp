#include "EraShift/Core/Config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace EraShift::Core {
namespace {

using Json = nlohmann::json;

/// Flattens a JSON object into "section.key" -> scalar text.
/// Arrays are stored as their compact JSON text so structured values (control
/// bindings) survive a load/save round trip.
void flatten(const Json& node, const std::string& section, std::vector<std::pair<std::string, std::string>>& out)
{
    if (!node.is_object()) {
        return;
    }

    for (auto it = node.begin(); it != node.end(); ++it) {
        const std::string key = section.empty() ? it.key() : section + "." + it.key();
        const Json& value = it.value();

        if (value.is_object()) {
            flatten(value, key, out);
        } else if (value.is_array()) {
            out.emplace_back(key, value.dump());
        } else {
            out.emplace_back(key, value.is_string() ? ("\"" + it.value().get<std::string>() + "\"")
                                                    : it.value().dump());
        }
    }
}

std::string unquote(std::string_view raw)
{
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        std::string inner(raw.substr(1, raw.size() - 2));
        std::string out;
        out.reserve(inner.size());
        for (std::size_t i = 0; i < inner.size(); ++i) {
            if (inner[i] == '\\' && i + 1 < inner.size()) {
                ++i;
                switch (inner[i]) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    default: out.push_back(inner[i]); break;
                }
            } else {
                out.push_back(inner[i]);
            }
        }
        return out;
    }
    return std::string(raw);
}

std::string escape(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\t': out.append("\\t"); break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// ConfigStore
// ---------------------------------------------------------------------------
std::string ConfigStore::makeKey(std::string_view section, std::string_view key)
{
    std::string result;
    result.reserve(section.size() + key.size() + 1);
    result.append(section);
    if (!section.empty()) {
        result.push_back('.');
    }
    result.append(key);
    return result;
}

bool ConfigStore::mergeJson(std::string_view json, std::string_view origin, Logger& log)
{
    Json parsed = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        log.warn("Config", "ignoring malformed JSON from '{}'", origin);
        return false;
    }
    if (!parsed.is_object()) {
        log.warn("Config", "ignoring '{}': root must be an object", origin);
        return false;
    }

    std::vector<std::pair<std::string, std::string>> flat;
    flat.reserve(64);
    flatten(parsed, std::string{}, flat);

    for (auto& entry : flat) {
        const auto existing = m_index.find(entry.first);
        if (existing != m_index.end()) {
            m_values[existing->second].second = std::move(entry.second);
        } else {
            m_index.emplace(entry.first, m_values.size());
            m_values.push_back(std::move(entry));
        }
    }
    return true;
}

bool ConfigStore::loadFile(const std::filesystem::path& path, Logger& log)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return false; // absence is normal for optional layers
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        log.warn("Config", "cannot open '{}'", path.string());
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    log.debug("Config", "merging {}", path.string());
    return mergeJson(buffer.str(), path.string(), log);
}

std::string ConfigStore::toJson(int indent) const
{
    Json root = Json::object();

    for (const auto& [key, value] : m_values) {
        // Re-create the nested path, e.g. "graphics.display.fullscreen"
        // becomes root["graphics"]["display"]["fullscreen"].
        Json* target = &root;
        std::string leaf;
        std::size_t start = 0;
        while (true) {
            const std::size_t dot = key.find('.', start);
            const std::string part =
                (dot == std::string::npos) ? key.substr(start) : key.substr(start, dot - start);
            if (dot == std::string::npos) {
                leaf = part;
                break;
            }
            target = &(*target)[part];
            start = dot + 1;
        }

        if (value.size() >= 2 && value.front() == '"') {
            (*target)[leaf] = unquote(value);
        } else if (value == "true" || value == "false") {
            (*target)[leaf] = (value == "true");
        } else if (value == "null") {
            (*target)[leaf] = nullptr;
        } else if (std::isdigit(static_cast<unsigned char>(value.front())) != 0 ||
                   value == "-" ||
                   value.find_first_of(".eE") != std::string::npos) {
            // Integer when the value round-trips exactly, float otherwise.
            const long long asInt = std::strtoll(value.c_str(), nullptr, 10);
            const double asDouble  = std::strtod(value.c_str(), nullptr);
            (*target)[leaf] = (static_cast<double>(asInt) == asDouble) ? Json(asInt) : Json(asDouble);
        } else {
            (*target)[leaf] = value;
        }
    }

    return root.dump(indent);
}

bool ConfigStore::saveFile(const std::filesystem::path& path, Logger& log) const
{
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        log.error("Config", "cannot write '{}'", path.string());
        return false;
    }
    file << toJson() << '\n';
    return file.good();
}

void ConfigStore::clear() noexcept
{
    m_values.clear();
    m_index.clear();
}

std::optional<std::string> ConfigStore::findString(std::string_view section, std::string_view key) const
{
    const auto it = m_index.find(makeKey(section, key));
    if (it == m_index.end()) {
        return std::nullopt;
    }
    return unquote(m_values[it->second].second);
}

bool ConfigStore::contains(std::string_view section, std::string_view key) const
{
    return m_index.find(makeKey(section, key)) != m_index.end();
}
bool ConfigStore::getBool(std::string_view section, std::string_view key, bool fallback) const
{
    const auto it = m_index.find(makeKey(section, key));
    if (it == m_index.end()) {
        return fallback;
    }
    // Accept both the bare form written by setBool() and a quoted one, so a
    // hand-edited config still works.
    const std::string raw = unquote(m_values[it->second].second);
    if (raw == "true" || raw == "1")  return true;
    if (raw == "false" || raw == "0") return false;
    return fallback;
}

int ConfigStore::getInt(std::string_view section, std::string_view key, int fallback) const
{
    const auto it = m_index.find(makeKey(section, key));
    if (it == m_index.end()) {
        return fallback;
    }
    // A value may be a bare number or a quoted one (setString, or a config file
    // written by hand), so unquote before parsing.
    const std::string text = unquote(m_values[it->second].second);
    if (text.empty()) {
        return fallback;
    }

    int value = 0;
    const auto* first = text.data();
    const auto* last  = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec == std::errc{} && result.ptr == last) {
        return value;
    }

    // Accept a whole-valued float such as "60.0" for user-friendliness.
    char* parseEnd = nullptr;
    const double asDouble = std::strtod(text.c_str(), &parseEnd);
    if (parseEnd != nullptr && *parseEnd == '\0' &&
        asDouble == static_cast<double>(static_cast<long long>(asDouble))) {
        return static_cast<int>(asDouble);
    }
    return fallback;
}

float ConfigStore::getFloat(std::string_view section, std::string_view key, float fallback) const
{
    const auto it = m_index.find(makeKey(section, key));
    if (it == m_index.end()) {
        return fallback;
    }
    const std::string text = unquote(m_values[it->second].second);
    if (text.empty()) {
        return fallback;
    }

    // Require the whole value to be numeric. A partial parse would silently
    // turn "0.1.0" into 0.1 and "12abc" into 12.
    char* parseEnd = nullptr;
    const double parsed = std::strtod(text.c_str(), &parseEnd);
    if (parseEnd == nullptr || *parseEnd != '\0') {
        return fallback;
    }
    return static_cast<float>(parsed);
}

std::string ConfigStore::getString(std::string_view section, std::string_view key, std::string_view fallback) const
{
    const auto it = m_index.find(makeKey(section, key));
    if (it == m_index.end()) {
        return std::string(fallback);
    }
    return unquote(m_values[it->second].second);
}

void ConfigStore::setString(std::string_view section, std::string_view key, std::string_view value)
{
    setRaw(section, key, "\"" + escape(std::string(value)) + "\"");
}

void ConfigStore::setRaw(std::string_view section, std::string_view key, std::string_view raw)
{
    const std::string full = makeKey(section, key);

    const auto existing = m_index.find(full);
    if (existing != m_index.end()) {
        m_values[existing->second].second = std::string(raw);
    } else {
        m_index.emplace(full, m_values.size());
        m_values.emplace_back(full, std::string(raw));
    }
}

void ConfigStore::setInt(std::string_view section, std::string_view key, int value)
{
    const std::string full = makeKey(section, key);
    const std::string raw  = std::to_string(value);
    const auto existing = m_index.find(full);
    if (existing != m_index.end()) {
        m_values[existing->second].second = raw;
    } else {
        m_index.emplace(full, m_values.size());
        m_values.emplace_back(full, raw);
    }
}

void ConfigStore::setFloat(std::string_view section, std::string_view key, float value)
{
    const std::string full = makeKey(section, key);
    std::string raw(32, '\0');
    const int written = std::snprintf(raw.data(), raw.size(), "%g", static_cast<double>(value));
    raw.resize(written > 0 ? static_cast<std::size_t>(written) : 0);

    const auto existing = m_index.find(full);
    if (existing != m_index.end()) {
        m_values[existing->second].second = raw;
    } else {
        m_index.emplace(full, m_values.size());
        m_values.emplace_back(full, raw);
    }
}

void ConfigStore::setBool(std::string_view section, std::string_view key, bool value)
{
    // Booleans are stored unquoted, matching the form mergeJson() produces, so
    // getBool() recognises them. Routing this through setString() would store
    // the *string* "true" and the setting would silently never take effect.
    setRaw(section, key, value ? "true" : "false");
}

std::vector<std::string> ConfigStore::keys(std::string_view section) const
{
    const std::string prefix = section.empty() ? std::string{} : std::string(section) + ".";

    std::vector<std::string> result;
    for (const auto& [key, value] : m_values) {
        static_cast<void>(value);
        if (prefix.empty()) {
            if (key.find('.') == std::string::npos) {
                result.push_back(key);
            }
        } else if (key.rfind(prefix, 0) == 0) {
            result.push_back(key.substr(prefix.size()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

int ConfigStore::clampInt(std::string_view section, std::string_view key, int fallback,
                          int min, int max, Logger& log) const
{
    const int value = getInt(section, key, fallback);
    if (value < min || value > max) {
        const int clamped = std::clamp(value, min, max);
        log.warn("Config", "{}.{} = {} is outside [{}, {}], using {}", section, key, value, min, max, clamped);
        return clamped;
    }
    return value;
}

float ConfigStore::clampFloat(std::string_view section, std::string_view key, float fallback,
                              float min, float max, Logger& log) const
{
    const float value = getFloat(section, key, fallback);
    if (value < min || value > max) {
        const float clamped = std::clamp(value, min, max);
        log.warn("Config", "{}.{} = {} is outside [{}, {}], using {}", section, key, value, min, max, clamped);
        return clamped;
    }
    return value;
}

std::unordered_map<std::string, std::string> ConfigStore::dump() const
{
    std::unordered_map<std::string, std::string> result;
    result.reserve(m_values.size());
    for (const auto& [key, value] : m_values) {
        result.emplace(key, value);
    }
    return result;
}

// ---------------------------------------------------------------------------
// ConfigManager
// ---------------------------------------------------------------------------
std::filesystem::path ConfigManager::defaultUserConfigDir()
{
    std::error_code ec;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / "EraShift";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "EraShift";
    }
    return std::filesystem::current_path(ec) / "config";
}

std::filesystem::path ConfigManager::saveDirectory() const
{
    return m_userConfigDir / "saves";
}

std::filesystem::path executableDirectory(const char* argv0)
{
    if (argv0 == nullptr || argv0[0] == '\0') {
        return {};
    }

    std::error_code ec;

    // Absolute path?
    std::filesystem::path path(argv0);
    if (path.is_absolute()) {
        return path.parent_path();
    }

    // Resolve through PATH so `EraShift` started from a shell works.
    if (path.has_parent_path()) {
        const auto resolved = std::filesystem::absolute(path, ec);
        if (!ec) {
            return resolved.parent_path();
        }
        return path.parent_path();
    }

    if (const char* pathEnv = std::getenv("PATH"); pathEnv != nullptr) {
        std::stringstream streams(pathEnv);
        std::string dir;
        while (std::getline(streams, dir, ':')) {
            if (dir.empty()) {
                continue;
            }
            const std::filesystem::path candidate = std::filesystem::path(dir) / argv0;
            if (std::filesystem::exists(candidate, ec) && !ec) {
                return std::filesystem::absolute(candidate, ec).parent_path();
            }
        }
    }

    const auto resolved = std::filesystem::absolute(path, ec);
    return ec ? std::filesystem::path{} : resolved.parent_path();
}

void ConfigManager::load(const std::filesystem::path& projectRoot, int argc, char* const argv[])
{
    const std::filesystem::path exeDir = executableDirectory(argc > 0 ? argv[0] : nullptr);
    m_userConfigDir = defaultUserConfigDir();
    m_contentRoot   = projectRoot;

    // A build tree layout puts assets next to the binary; an installed layout
    // puts them under share/EraShift. Check both before giving up.
    if (!exeDir.empty()) {
        const std::filesystem::path besideExe = exeDir / "assets";
        const std::filesystem::path shareDir   = exeDir / ".." / "share" / "EraShift" / "assets";
        std::error_code ec;
        if (std::filesystem::exists(besideExe, ec) && !ec) {
            m_contentRoot = exeDir;
        } else if (std::filesystem::exists(shareDir, ec) && !ec) {
            m_contentRoot = shareDir.parent_path().parent_path().parent_path().parent_path();
        }
    }

    m_searchPaths.clear();
    m_searchPaths.push_back(projectRoot / "config");
    if (!exeDir.empty()) {
        m_searchPaths.push_back(exeDir / "config");
    }
    m_searchPaths.push_back(m_userConfigDir);
    m_searchPaths.push_back(exeDir / ".." / "share" / "EraShift" / "config");

    m_log->debug("Config", "content root: {}", m_contentRoot.string());
    m_log->debug("Config", "user config: {}", m_userConfigDir.string());

    for (const auto& dir : m_searchPaths) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || ec) {
            continue;
        }
        // Deterministic order within a directory.
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            m_store.loadFile(file, *m_log);
        }
    }

    // Command line overrides, applied last: --set graphics.vsync=false
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) != "--set" || i + 1 >= argc) {
            continue;
        }
        const std::string assignment = argv[++i];
        const auto eq = assignment.find('=');
        if (eq == std::string::npos) {
            m_log->warn("Config", "ignoring malformed --set '{}'", assignment);
            continue;
        }
        const std::string dotted = assignment.substr(0, eq);
        const std::string value  = assignment.substr(eq + 1);
        m_store.setString(std::string_view{}, dotted, value);
        m_log->info("Config", "override {} = {}", dotted, value);
    }
}

bool ConfigManager::saveUserConfig(Logger& log) const
{
    return m_store.saveFile(m_userConfigDir / "settings.json", log);
}

} // namespace EraShift::Core
