#include "EraShift/Graphics/TextRenderer.hpp"

#include "EraShift/Graphics/Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace EraShift::Graphics {

namespace {

/// Upper bound on cached strings. UI text is a few dozen distinct strings;
/// this only exists so a pathological caller cannot grow the cache forever.
constexpr std::size_t kMaxCacheEntries = 512;

/// Converts an engine colour to the float form SDL_ttf wants.
SDL_Color toSDLColor(Color color) noexcept
{
    return SDL_Color{color.r, color.g, color.b, color.a};
}

bool isVisible(Color color) noexcept
{
    return color.a > 0;
}

} // namespace

TextRenderer::TextRenderer(ResourceManager& resources, Core::Logger& log)
    : m_resources(&resources), m_log(&log)
{
}

TextRenderer::~TextRenderer()
{
    clearCache();
}

bool TextRenderer::loadFont(const std::string& relativePath, int basePixelSize)
{
    clearCache();

    m_fontPath  = relativePath;
    m_baseFont  = m_resources->loadFont(relativePath, basePixelSize);
    if (!m_baseFont.valid()) {
        m_log->warn("TextRenderer", "no TTF face at '{}', text will use the bitmap fallback",
                    relativePath);
        return false;
    }

    // A second face at the bold weight keeps headings looking deliberate
    // instead of relying on a synthesised outline.
    m_boldFont = m_resources->loadFont(relativePath, basePixelSize);
    if (m_boldFont.valid()) {
        TTF_SetFontStyle(m_boldFont->handle(), TTF_STYLE_BOLD);
    }

    m_log->info("TextRenderer", "loaded '{}' at {}px", relativePath, basePixelSize);
    return true;
}

const Font* TextRenderer::resolveFont(const TextStyle& style) const
{
    if (style.bold && m_boldFont.valid()) {
        return m_boldFont.get();
    }
    return m_baseFont.get();
}

float TextRenderer::lineHeight(const TextStyle& style) const
{
    const Font* font = resolveFont(style);
    if (font == nullptr) {
        return static_cast<float>(style.pixelSize) + static_cast<float>(style.lineSpacing);
    }
    // Measure at the requested size rather than relying on the base size, so a
    // style can use any pixel height with a single loaded face.
    TTF_SetFontSize(font->handle(), static_cast<float>(style.pixelSize));
    return font->lineHeight() + static_cast<float>(style.lineSpacing);
}

Vec2 TextRenderer::measure(std::string_view text, const TextStyle& style) const
{
    const Font* font = resolveFont(style);
    if (font == nullptr || text.empty()) {
        return {0.0f, lineHeight(style)};
    }

    TTF_SetFontSize(font->handle(), static_cast<float>(style.pixelSize));

    int width  = 0;
    int height = 0;
    // SDL_ttf 3.2 takes an explicit byte count. Passing std::string::npos makes
    // it compute a size of -1 and memcpy with a negative length.
    const std::string owned(text);
    if (!TTF_GetStringSize(font->handle(), owned.c_str(), owned.size(), &width, &height)) {
        m_log->warn("TextRenderer", "could not measure '{}'", text);
        return {0.0f, lineHeight(style)};
    }
    return {static_cast<float>(width), static_cast<float>(height)};
}

void TextRenderer::clearCache()
{
    m_cache.clear();
    m_cacheOrder.clear();
    m_cacheHits   = 0;
    m_cacheMisses = 0;
}

TextRenderer::CacheStats TextRenderer::cacheStats() const
{
    CacheStats stats;
    stats.entries = m_cache.size();
    stats.hits    = m_cacheHits;
    stats.misses  = m_cacheMisses;
    for (const auto& [key, entry] : m_cache) {
        if (entry.texture != nullptr && entry.texture->valid()) {
            stats.bytes += static_cast<std::uint64_t>(entry.texture->width()) *
                           static_cast<std::uint64_t>(entry.texture->height()) * 4u;
        }
    }
    return stats;
}

TextRenderer::CachedText TextRenderer::rasterise(const std::string& text, const TextStyle& style)
{
    CachedText result;

    const Font* font = resolveFont(style);
    if (font == nullptr || text.empty()) {
        return result;
    }

    if (m_renderer == nullptr && m_resources != nullptr) {
        m_renderer = m_resources->renderer();
    }
    if (m_renderer == nullptr) {
        return result;
    }

    TTF_SetFontSize(font->handle(), static_cast<float>(style.pixelSize));

    SDL_Surface* surface = TTF_RenderText_Blended(font->handle(), text.c_str(), text.size(),
                                                  toSDLColor(style.color));
    if (surface == nullptr) {
        m_log->warn("TextRenderer", "failed to rasterise '{}': {}", text, SDL_GetError());
        return result;
    }

    // The size must be read before the surface is destroyed: touching
    // surface->w afterwards is a use-after-free, and SDL surfaces are heap
    // allocated, so the allocator noticed before the values were misused.
    const float width  = static_cast<float>(surface->w);
    const float height = static_cast<float>(surface->h);

    SDL_Texture* raw = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_DestroySurface(surface);

    if (raw == nullptr) {
        m_log->warn("TextRenderer", "texture upload failed for '{}': {}", text, SDL_GetError());
        return result;
    }

    result.texture = Texture::adopt(raw, "text:" + text);
    result.width   = width;
    result.height  = height;
    return result;
}

void TextRenderer::draw(Renderer2D& renderer, const Vec2& position, std::string_view text,
                        const TextStyle& style)
{
    if (text.empty() || !style.color.a) {
        return;
    }

    const std::string key(text);
    const CacheKey lookup{key, style.bold ? m_boldFont.id() : m_baseFont.id(),
                          style.pixelSize, style.color.packed()};

    CachedText cached;
    const auto it = m_cache.find(lookup);
    if (it != m_cache.end()) {
        ++m_cacheHits;
        cached = it->second;
    } else {
        ++m_cacheMisses;
        cached = rasterise(key, style);
        if (cached.texture == nullptr) {
            return;
        }

        // Evict the oldest entry when full. Text UI cycles through a small set
        // of strings, so this almost never triggers.
        if (m_cacheOrder.size() >= kMaxCacheEntries) {
            m_cache.erase(m_cacheOrder.front());
            m_cacheOrder.erase(m_cacheOrder.begin());
        }
        m_cacheOrder.push_back(lookup);
        m_cache.emplace(lookup, cached);
    }

    const bool shadowed = isVisible(style.shadow) && style.shadow.a > 0;

    if (shadowed) {
        const Rect shadow{position.x + 1.0f, position.y + 1.0f, cached.width, cached.height};
        renderer.drawTexture(*cached.texture, shadow, {}, style.shadow);
    }

    const Rect destination{position.x, position.y, cached.width, cached.height};
    renderer.drawTexture(*cached.texture, destination, {}, style.color);
}

void TextRenderer::drawAligned(Renderer2D& renderer, const Vec2& position, std::string_view text,
                               const TextStyle& style)
{
    const Vec2 size = measure(text, style);
    float x = position.x;
    switch (style.align) {
        case TextAlign::Left:   x = position.x; break;
        case TextAlign::Center: x = position.x - size.x * 0.5f; break;
        case TextAlign::Right:  x = position.x - size.x;       break;
    }
    draw(renderer, Vec2{x, position.y}, text, style);
}

std::vector<std::string> TextRenderer::wrapText(std::string_view text, const TextStyle& style,
                                                float maxWidth) const
{
    std::vector<std::string> lines;

    if (text.empty()) {
        return lines;
    }
    if (!style.wrap || maxWidth <= 0.0f) {
        // Still honour explicit newlines so a caller can build a block.
        std::string current;
        for (const char c : text) {
            if (c == '\n') {
                lines.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        lines.push_back(current);
        return lines;
    }

    std::string current;
    std::istringstream stream{std::string(text)};
    std::string word;

    while (std::getline(stream, word, '\n')) {
        if (word.empty()) {
            lines.push_back(current);
            current.clear();
            continue;
        }

        std::istringstream words(word);
        std::string token;
        while (words >> token) {
            const std::string candidate = current.empty() ? token : current + " " + token;
            if (measure(candidate, style).x <= maxWidth || current.empty()) {
                current = candidate;
            } else {
                lines.push_back(current);
                current = token;
            }
        }
        lines.push_back(current);
        current.clear();
    }

    if (!current.empty()) {
        lines.push_back(current);
    }

    if (style.maxLines > 0 && static_cast<int>(lines.size()) > style.maxLines) {
        lines.resize(static_cast<std::size_t>(style.maxLines));
        if (!lines.empty()) {
            // Cut the last visible line short so the block cannot overflow.
            std::string& last = lines.back();
            while (measure(last + "...", style).x > maxWidth && !last.empty()) {
                last.pop_back();
            }
            last += "...";
        }
    }

    return lines;
}

void TextRenderer::drawInRect(Renderer2D& renderer, const Rect& area, std::string_view text,
                              const TextStyle& style)
{
    if (text.empty()) {
        return;
    }

    const std::vector<std::string> lines = wrapText(text, style, area.w);
    if (lines.empty()) {
        return;
    }

    const float lineHeightPx = lineHeight(style);
    const float blockHeight  = lineHeightPx * static_cast<float>(lines.size());

    float y = area.y;
    switch (style.valign) {
        case TextVAlign::Top:    y = area.y;                                  break;
        case TextVAlign::Middle: y = area.y + (area.h - blockHeight) * 0.5f; break;
        case TextVAlign::Bottom: y = area.bottom() - blockHeight;            break;
    }

    for (const std::string& line : lines) {
        float x = area.x;
        switch (style.align) {
            case TextAlign::Left:   x = area.x;                     break;
            case TextAlign::Center: x = area.x + (area.w - measure(line, style).x) * 0.5f; break;
            case TextAlign::Right:  x = area.right() - measure(line, style).x;              break;
        }
        draw(renderer, Vec2{x, y}, line, style);
        y += lineHeightPx;
    }
}

} // namespace EraShift::Graphics
