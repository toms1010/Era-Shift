// Era Shift - configuration system.
//
// Configuration is layered, lowest precedence first:
//
//   1. Built-in defaults (compiled into the executable)
//   2. User config directory   (~/.config/EraShift/*.json)
//   3. Project config directory (./config/*.json, installed alongside assets)
//   4. Command line overrides  (--set section.key=value)
//
// Every read is typed and bounds checked; a malformed file is reported through
// the logger and skipped rather than aborting start-up.

#pragma once

#include "EraShift/Core/Log.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace EraShift::Core {

/// A parsed JSON document flattened to "section.key" -> scalar.
///
/// Flattening keeps lookups allocation-free and makes precedence merging
/// trivial, while the value keeps its original JSON type.
class ConfigStore {
public:
    ConfigStore() = default;

    /// Applies a JSON document, merging it over the current contents.
    /// @return false and logs the reason when the document is not valid JSON.
    bool mergeJson(std::string_view json, std::string_view origin, Logger& log);

    /// Loads and merges a file. Missing files are not an error.
    bool loadFile(const std::filesystem::path& path, Logger& log);

    /// Serialises the store back to JSON, grouped by section.
    [[nodiscard]] std::string toJson(int indent = 2) const;

    /// Writes the store to disk, creating parent directories.
    bool saveFile(const std::filesystem::path& path, Logger& log) const;

    void clear() noexcept;
    [[nodiscard]] bool empty() const noexcept { return m_values.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_values.size(); }

    // --- typed access -------------------------------------------------------
    // `section` may be empty for root-level keys.

    [[nodiscard]] bool        getBool  (std::string_view section, std::string_view key, bool fallback) const;
    [[nodiscard]] int         getInt   (std::string_view section, std::string_view key, int fallback) const;
    [[nodiscard]] float       getFloat (std::string_view section, std::string_view key, float fallback) const;
    [[nodiscard]] std::string getString(std::string_view section, std::string_view key, std::string_view fallback) const;
    [[nodiscard]] std::optional<std::string> findString(std::string_view section, std::string_view key) const;
    [[nodiscard]] bool contains(std::string_view section, std::string_view key) const;

    void setBool  (std::string_view section, std::string_view key, bool value);
    void setInt   (std::string_view section, std::string_view key, int value);
    void setFloat (std::string_view section, std::string_view key, float value);
    void setString(std::string_view section, std::string_view key, std::string_view value);

    /// Returns every key inside a section, sorted. Empty when absent.
    [[nodiscard]] std::vector<std::string> keys(std::string_view section) const;

    /// Restricts a value to [min, max], logging when it had to be clamped.
    [[nodiscard]] int clampInt(std::string_view section, std::string_view key, int fallback,
                               int min, int max, Logger& log) const;

    [[nodiscard]] float clampFloat(std::string_view section, std::string_view key, float fallback,
                                   float min, float max, Logger& log) const;

    /// All values as "section.key" -> raw scalar text. Used by the debug overlay.
    [[nodiscard]] std::unordered_map<std::string, std::string> dump() const;

private:
    [[nodiscard]] static std::string makeKey(std::string_view section, std::string_view key);

    /// Stores a value in its raw JSON form (numbers and booleans unquoted,
    /// strings quoted). All setters funnel through here.
    void setRaw(std::string_view section, std::string_view key, std::string_view raw);

    // Insertion-ordered storage keeps toJson() output stable across runs.
    std::vector<std::pair<std::string, std::string>> m_values;
    std::unordered_map<std::string, std::size_t>    m_index;
};

/// Locates the config directories and applies the layering rules.
class ConfigManager {
public:
    explicit ConfigManager(Logger& log) noexcept : m_log(&log) {}

    /// Builds the full layered configuration. `executableDir` and `projectRoot`
    /// are discovered from argv[0] and the working directory respectively.
    void load(const std::filesystem::path& projectRoot, int argc, char* const argv[]);

    /// Directories searched, in precedence order (highest last).
    [[nodiscard]] const std::vector<std::filesystem::path>& searchPaths() const noexcept { return m_searchPaths; }

    /// Writes the current settings back to the user config directory.
    bool saveUserConfig(Logger& log) const;

    [[nodiscard]] const ConfigStore& store() const noexcept { return m_store; }
    [[nodiscard]] ConfigStore& store() noexcept { return m_store; }
    [[nodiscard]] const std::filesystem::path& userConfigDir() const noexcept { return m_userConfigDir; }

    /// Directory that assets and data are loaded from.
    [[nodiscard]] const std::filesystem::path& contentRoot() const noexcept { return m_contentRoot; }

    static std::filesystem::path defaultUserConfigDir();

private:
    Logger* m_log = nullptr;
    ConfigStore m_store;
    std::vector<std::filesystem::path> m_searchPaths;
    std::filesystem::path m_userConfigDir;
    std::filesystem::path m_contentRoot;
};

/// Extracts the directory containing the running executable, or "" if unknown.
[[nodiscard]] std::filesystem::path executableDirectory(const char* argv0);

} // namespace EraShift::Core
