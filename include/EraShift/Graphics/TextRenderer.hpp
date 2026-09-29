// Era Shift - TrueType text rendering.
//
// The engine's built-in 5x7 bitmap font is a last-resort fallback so the game
// is never completely textless. The real UI uses a TTF face, which gives proper
// hinting, kerning, a full Unicode range and weights that actually look
// deliberate.
//
// Rasterising a string through TTF_RenderText_Blended and uploading a surface
// every frame is far too slow, so rendered strings are cached as GPU textures
// keyed by (font, pixel size, colour, text). UI text is drawn from a handful of
// distinct strings, so the cache stays small and the hit rate is very high.
//
// Threading: the cache is not thread safe, which matches the renderer. All
// calls are expected on the main thread.

#pragma once

#include "EraShift/Core/Log.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"
#include "EraShift/Graphics/ResourceManager.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace EraShift::Graphics {

/// Horizontal placement of a run of text.
enum class TextAlign {
    Left,
    Center,
    Right,
};

/// Vertical placement relative to the anchor point.
enum class TextVAlign {
    Top,
    Middle,
    Bottom,
};

/// Resolution-independent UI scaling.
///
/// Every size in the interface is expressed against a 1280x720 reference and
/// multiplied by this factor, so the HUD stays legible and correctly proportioned
/// from a 1024x600 laptop panel to a 4K display. Without it, hard-coded pixel
/// sizes either swamp a small window or disappear on a large one.
struct UiScale {
    /// Reference resolution the interface was designed against.
    static constexpr float kReferenceWidth  = 1280.0f;
    static constexpr float kReferenceHeight = 720.0f;

    /// Smallest and largest factor applied, so text never becomes unreadable or
    /// absurd on an extreme aspect ratio.
    static constexpr float kMin = 0.70f;
    static constexpr float kMax = 2.20f;

    float factor = 1.0f;

    [[nodiscard]] static UiScale forViewport(float width, float height) noexcept
    {
        // Fit the smaller axis: a short, wide window must not blow the type up.
        const float byWidth  = width / kReferenceWidth;
        const float byHeight = height / kReferenceHeight;
        const float fit      = (byWidth < byHeight) ? byWidth : byHeight;
        return UiScale{clampValue(fit, kMin, kMax)};
    }

    /// Converts a reference-resolution size into pixels.
    [[nodiscard]] float px(float referencePixels) const noexcept
    {
        return referencePixels * factor;
    }

    /// Converts to an integral pixel size, for TTF.
    [[nodiscard]] int font(float referencePixels) const noexcept
    {
        return std::max(1, static_cast<int>(px(referencePixels) + 0.5f));
    }
};

/// How a block of text is laid out inside a rectangle.
struct TextStyle {
    int       pixelSize = 20;
    Color     color     = Palette::TextPrimary;
    /// Draw a one-pixel offset shadow in this colour. Fully transparent
    /// disables it.
    Color     shadow    = Color{0, 0, 0, 0xC0};
    /// Extra spacing between lines, in pixels.
    int       lineSpacing = 4;
    /// Wrap to the width of the target rectangle. Ignored by the `draw*`
    /// overloads that take a single point.
    bool      wrap      = false;
    /// Maximum lines when wrapping. Text beyond the limit is cut off with an
    /// ellipsis so a long string can never overflow a panel.
    int       maxLines  = 0;   ///< 0 means unlimited.
    TextAlign align     = TextAlign::Left;
    TextVAlign valign   = TextVAlign::Top;
    /// Force the synthetic bold weight instead of loading a second face.
    bool      bold      = false;
};

/// Renders and caches TTF text.
class TextRenderer {
public:
    TextRenderer(ResourceManager& resources, Core::Logger& log);
    ~TextRenderer();

    TextRenderer(const TextRenderer&)            = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    /// Loads the UI face. Returns false if TTF is unavailable or the file is
    /// missing; the caller should then fall back to BitmapFont.
    bool loadFont(const std::string& relativePath, int basePixelSize = 20);

    [[nodiscard]] bool ready() const noexcept { return m_baseFont.valid(); }

    // --- measurement --------------------------------------------------------

    /// Size of a single line, excluding shadow padding.
    [[nodiscard]] Vec2 measure(std::string_view text, const TextStyle& style) const;

    /// Height of one line at this size, including line spacing.
    [[nodiscard]] float lineHeight(const TextStyle& style) const;

    // --- drawing ------------------------------------------------------------

    /// Draws one line with the anchor at `position`.
    void draw(Renderer2D& renderer, const Vec2& position, std::string_view text,
              const TextStyle& style);

    /// Draws one line aligned to `position` according to the style.
    void drawAligned(Renderer2D& renderer, const Vec2& position, std::string_view text,
                     const TextStyle& style);

    /// Draws text inside `area`, honouring wrapping and alignment. This is the
    /// workhorse for menus and dialogue.
    void drawInRect(Renderer2D& renderer, const Rect& area, std::string_view text,
                    const TextStyle& style);

    /// Splits `text` into lines that fit `maxWidth`, honouring the style's
    /// wrap setting. Exposed for tests and for laying out a paragraph before it
    /// is drawn.
    [[nodiscard]] std::vector<std::string> wrapText(std::string_view text,
                                                    const TextStyle& style, float maxWidth) const;

    // --- cache --------------------------------------------------------------

    /// Drops every cached texture. Call after a font reload or a resolution
    /// change that invalidates the rendered sizes.
    void clearCache();

    struct CacheStats {
        std::size_t entries  = 0;
        std::size_t hits     = 0;
        std::size_t misses   = 0;
        std::uint64_t bytes  = 0;
    };
    [[nodiscard]] CacheStats cacheStats() const;

private:
    struct CacheKey {
        std::string  text;
        std::uint32_t fontId;
        int          pixelSize;
        std::uint32_t color;

        bool operator==(const CacheKey& o) const noexcept
        {
            return pixelSize == o.pixelSize && color == o.color && fontId == o.fontId
                   && text == o.text;
        }
    };

    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& key) const noexcept
        {
            std::size_t seed = std::hash<std::uint32_t>{}(key.fontId);
            seed ^= std::hash<std::uint32_t>{}(key.pixelSize) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
            seed ^= std::hash<std::uint32_t>{}(key.color) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
            seed ^= std::hash<std::string>{}(key.text) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    struct CachedText {
        std::shared_ptr<Texture> texture;
        float width  = 0.0f;
        float height = 0.0f;
    };

    /// Rasterises `text` into a texture, or returns null on failure.
    [[nodiscard]] CachedText rasterise(const std::string& text, const TextStyle& style);

    [[nodiscard]] const Font* resolveFont(const TextStyle& style) const;

    ResourceManager* m_resources = nullptr;
    Core::Logger*    m_log       = nullptr;
    SDL_Renderer*    m_renderer  = nullptr;

    FontHandle m_baseFont;
    FontHandle m_boldFont;
    std::string m_fontPath;

    std::unordered_map<CacheKey, CachedText, CacheKeyHash> m_cache;
    /// Cache keys in insertion order, so eviction is not a full sort.
    std::vector<CacheKey> m_cacheOrder;
    std::size_t m_cacheHits   = 0;
    std::size_t m_cacheMisses = 0;
};

} // namespace EraShift::Graphics
