// Era Shift - centralised resource management.
//
// Every asset in the game is loaded through ResourceManager exactly once and
// shared by handle. Internally, textures are reference counted (many sprites
// reference one texture), while non-GPU resources such as JSON documents are
// held as shared_ptr for the same reason.
//
// Loading is lazy: requesting a texture that has not been seen uploads it and
// returns a handle. A second request returns the existing handle without
// touching the disk. `preload` warms a manifest up front so the first frame of
// a level does not stall.

#pragma once

#include "EraShift/Core/Log.hpp"
#include "EraShift/Graphics/Texture.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;
struct TTF_Font;

namespace EraShift::Graphics {

class Window;

/// Handle to a cached texture.
///
/// A handle is a borrowed pointer plus a generation-independent id. It is cheap
/// to copy and safe to store in components. Contract: handles are valid until
/// `ResourceManager::unloadTexture` or `clear()` is called for that path, and
/// the manager must outlive every handle it issued (the engine guarantees this
/// by owning the manager for the whole session).
class TextureHandle {
public:
    TextureHandle() = default;
    explicit TextureHandle(const Texture* texture, std::uint32_t id) noexcept
        : m_texture(texture), m_id(id)
    {}

    [[nodiscard]] bool valid() const noexcept { return m_texture != nullptr; }
    [[nodiscard]] std::uint32_t id() const noexcept { return m_id; }
    /// A stable cache key, handy for profiling and for the debug overlay.
    [[nodiscard]] std::uint32_t pathId() const noexcept { return m_id; }

    [[nodiscard]] const Texture* get() const noexcept { return m_texture; }
    [[nodiscard]] const Texture& operator*() const;   ///< Throws std::logic_error when invalid
    [[nodiscard]] const Texture* operator->() const noexcept { return m_texture; }

    [[nodiscard]] bool operator==(const TextureHandle& o) const noexcept { return m_id == o.m_id; }
    [[nodiscard]] bool operator!=(const TextureHandle& o) const noexcept { return m_id != o.m_id; }

private:
    const Texture* m_texture = nullptr;
    std::uint32_t  m_id      = 0;
};

/// Cached JSON document. Immutable once loaded.
struct DataDocument {
    std::string name;
    std::string text;   ///< Raw JSON, parsed on demand by the caller.
};

/// A TTF font at a fixed pixel size.
class Font {
public:
    Font() = default;
    ~Font();

    Font(const Font&)            = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept { return m_font != nullptr; }
    [[nodiscard]] TTF_Font* handle() const noexcept { return m_font; }
    [[nodiscard]] int pixelSize() const noexcept { return m_pixelSize; }
    [[nodiscard]] float lineHeight() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }

private:
    friend class ResourceManager;

    TTF_Font*  m_font = nullptr;
    std::string m_name;
    int        m_pixelSize = 0;
};

class FontHandle {
public:
    FontHandle() = default;
    FontHandle(const Font* font, std::uint32_t id) noexcept : m_font(font), m_id(id) {}

    [[nodiscard]] bool valid() const noexcept { return m_font != nullptr; }
    [[nodiscard]] std::uint32_t id() const noexcept { return m_id; }
    [[nodiscard]] const Font* get() const noexcept { return m_font; }
    [[nodiscard]] const Font* operator->() const noexcept { return m_font; }

private:
    const Font* m_font = nullptr;
    std::uint32_t m_id = 0;
};

/// Statistics exposed by the debug overlay.
struct ResourceStats {
    std::size_t textures    = 0;
    std::size_t texturesLoaded = 0;
    std::size_t fonts       = 0;
    std::size_t documents   = 0;
    std::size_t loadFailures = 0;
    std::size_t cacheHits   = 0;
    std::size_t cacheMisses = 0;
    /// Total pixels held by resident textures, a proxy for VRAM use.
    std::uint64_t texturePixels = 0;
};

class ResourceManager {
public:
    ResourceManager(Core::Logger& log, Window& window, std::filesystem::path contentRoot);
    ~ResourceManager();

    ResourceManager(const ResourceManager&)            = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // --- textures -----------------------------------------------------------
    /// Returns a handle, loading the image on first use.
    /// @param relativePath Path relative to the content root, e.g.
    ///                     "assets/sprites/player/kael_idle.png".
    TextureHandle loadTexture(const std::string& relativePath);
    /// Same, but reports the failure reason.
    TextureHandle loadTexture(const std::string& relativePath, TextureError& errorOut);

    [[nodiscard]] const Texture* texture(std::uint32_t handleId) const noexcept;
    [[nodiscard]] const Texture& textureRef(std::uint32_t handleId) const;

    /// Releases a single texture. Other handles to the same path keep it alive.
    bool unloadTexture(const std::string& relativePath);

    // --- fonts --------------------------------------------------------------
    /// Loads a TTF at a pixel size. Both the path and the size form the key so
    /// UI at different scales does not fight over one texture.
    FontHandle loadFont(const std::string& relativePath, int pixelSize);
    [[nodiscard]] const Font* font(std::uint32_t handleId) const noexcept;

    // --- raw data -----------------------------------------------------------
    /// Loads a text or JSON document into memory. Used by the data-driven
    /// content pipeline (enemies, quests, dialogue).
    const DataDocument* loadDocument(const std::string& relativePath);
    [[nodiscard]] const DataDocument* document(const std::string& relativePath) const noexcept;

    // --- bulk operations ----------------------------------------------------
    /// Loads every path in a list, reporting a summary.
    struct LoadReport {
        std::size_t requested = 0;
        std::size_t loaded    = 0;
        std::size_t failed    = 0;
    };
    LoadReport preload(const std::vector<std::string>& relativePaths);

    /// Releases every asset. Called on shutdown and when changing content root.
    void clear();

    /// Resolves a game-relative path to an absolute one.
    [[nodiscard]] std::filesystem::path resolve(const std::string& relativePath) const;
    void setContentRoot(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& contentRoot() const noexcept { return m_contentRoot; }

    /// The renderer textures are created against. Text rasterisation needs
    /// it, and duplicating the pointer would be a second source of truth.
    [[nodiscard]] SDL_Renderer* renderer() const noexcept { return m_renderer; }
    [[nodiscard]] ResourceStats stats() const;

private:
    struct TextureEntry {
        std::shared_ptr<Texture> texture;
        std::string               path;
        std::size_t               refCount = 0;
    };

    Core::Logger*  m_log = nullptr;
    Window*        m_window = nullptr;
    SDL_Renderer*  m_renderer = nullptr;
    std::filesystem::path m_contentRoot;

    // Textures are stored in an id-indexed vector so `texture(id)` is an O(1)
    // lookup, with a path->id side table for the cache check.
    std::vector<TextureEntry>                          m_entries;
    std::unordered_map<std::string, std::uint32_t>    m_textureIds;   ///< path -> id
    std::vector<std::string>                           m_textureOrder; ///< stable iteration order
    std::uint32_t m_nextTextureId = 1;

    std::unordered_map<std::string, FontHandle> m_fonts;
    std::vector<std::unique_ptr<Font>>         m_fontStore;
    std::unordered_map<std::uint32_t, const Font*> m_fontById;
    std::uint32_t m_nextFontId = 1;

    std::unordered_map<std::string, DataDocument> m_documents;

    ResourceStats m_stats;
    bool          m_scaleModeApplied = false;
};

} // namespace EraShift::Graphics
