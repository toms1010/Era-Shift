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

/// Splits on newlines, keeping empty paragraphs so a blank line survives as one.
///
/// `std::getline` alone drops a trailing empty field, so "a\n\n" would come back as
/// one line instead of two and a deliberate blank line in a paragraph would vanish.
std::vector<std::string> splitLines(std::string_view text)
{
    std::vector<std::string> paragraphs;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            paragraphs.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    paragraphs.push_back(current);
    return paragraphs;
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

std::vector<std::string> wrapLines(std::string_view text, bool wrap, int maxLines, float maxWidth,
                                   WidthProbe widthOf, void* user)
{
    std::vector<std::string> lines;
    if (text.empty() || widthOf == nullptr) {
        return lines;
    }

    const auto width = [widthOf, user](const std::string& line) {
        return widthOf(line, user);
    };

    if (!wrap || maxWidth <= 0.0f) {
        // Explicit newlines are still honoured, so a caller can build a block
        // without enabling wrapping.
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

    // A token with no spaces in it — a path, a URL, a long number — is broken
    // rather than allowed to overflow. Character-based because there is nothing
    // else to do with it: breaking mid-word is ugly, and text crossing a panel
    // border is worse, because the caller cannot see it, cannot select it, and
    // cannot tell what it said.
    const auto breakToken = [&](const std::string& token, std::vector<std::string>& out) {
        std::string piece;
        for (const char c : token) {
            piece.push_back(c);
            // Tested on the piece *including* the new character, cut after
            // popping it: the previous piece was then the longest that fit.
            if (width(piece) > maxWidth && piece.size() > 1) {
                piece.pop_back();
                out.push_back(piece);
                piece.assign(1, c);
            }
        }
        if (!piece.empty()) {
            out.push_back(piece);
        }
    };

    std::string current;
    for (const std::string& paragraph : splitLines(text)) {
        if (paragraph.empty()) {
            // A blank line is a line. Flush whatever is pending so the blank lands
            // after it rather than replacing it.
            lines.push_back(current);
            current.clear();
            continue;
        }

        std::istringstream words(paragraph);
        std::string token;
        while (words >> token) {
            if (width(token) > maxWidth) {
                if (!current.empty()) {
                    lines.push_back(current);
                    current.clear();
                }
                breakToken(token, lines);
                continue;
            }

            const std::string candidate = current.empty() ? token : current + " " + token;
            if (width(candidate) <= maxWidth || current.empty()) {
                current = candidate;
            } else {
                lines.push_back(current);
                current = token;
            }
        }

        // Only flush a *populated* line. Pushing unconditionally appended an empty
        // line after every paragraph whose last word had been broken out already,
        // which put a phantom line under the block and made the count one too high
        // for every caller computing a height from it.
        if (!current.empty()) {
            lines.push_back(current);
        }
        current.clear();
    }

    if (maxLines > 0 && static_cast<int>(lines.size()) > maxLines) {
        lines.resize(static_cast<std::size_t>(maxLines));
        if (!lines.empty()) {
            // Trim the last visible line so the ellipsis itself fits. Without the
            // check, adding "..." to a line that exactly filled the width is how a
            // truncated line becomes the one line that overflows.
            std::string& last = lines.back();
            const std::string ellipsis = "...";
            while (!last.empty() && width(last + ellipsis) > maxWidth) {
                last.pop_back();
            }
            last += ellipsis;
        }
    }

    return lines;
}

std::vector<std::string> TextRenderer::wrapText(std::string_view text, const TextStyle& style,
                                                float maxWidth) const
{
    struct Probe {
        const TextRenderer* renderer;
        TextStyle style;
    } probe{this, style};

    return wrapLines(text, style.wrap, style.maxLines, maxWidth,
                     [](const std::string& line, void* user) -> float {
                         const auto* p = static_cast<Probe*>(user);
                         return p->renderer->measure(line, p->style).x;
                     },
                     &probe);
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
