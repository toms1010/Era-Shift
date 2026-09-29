#include "EraShift/Graphics/ResourceManager.hpp"

#include "EraShift/Graphics/Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace EraShift::Graphics {

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------
Font::~Font()
{
    if (m_font != nullptr) {
        TTF_CloseFont(m_font);
        m_font = nullptr;
    }
}

Font::Font(Font&& other) noexcept
    : m_font(std::exchange(other.m_font, nullptr)),
      m_name(std::move(other.m_name)),
      m_pixelSize(std::exchange(other.m_pixelSize, 0))
{
}

Font& Font::operator=(Font&& other) noexcept
{
    if (this != &other) {
        if (m_font != nullptr) {
            TTF_CloseFont(m_font);
        }
        m_font      = std::exchange(other.m_font, nullptr);
        m_name      = std::move(other.m_name);
        m_pixelSize = std::exchange(other.m_pixelSize, 0);
    }
    return *this;
}

float Font::lineHeight() const noexcept
{
    if (m_font == nullptr) {
        return 0.0f;
    }
    return static_cast<float>(TTF_GetFontHeight(m_font));
}

// ---------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------
const Texture& TextureHandle::operator*() const
{
    if (m_texture == nullptr) {
        throw std::logic_error("TextureHandle dereferenced while invalid");
    }
    return *m_texture;
}

// ---------------------------------------------------------------------------
// ResourceManager
// ---------------------------------------------------------------------------
ResourceManager::ResourceManager(Core::Logger& log, Window& window, std::filesystem::path contentRoot)
    : m_log(&log), m_window(&window), m_contentRoot(std::move(contentRoot))
{
    m_renderer = window.renderer();
}

ResourceManager::~ResourceManager()
{
    clear();
}

void ResourceManager::setContentRoot(std::filesystem::path root)
{
    if (root == m_contentRoot) {
        return;
    }
    clear();
    m_contentRoot = std::move(root);
    m_log->info("ResourceManager", "content root set to {}", m_contentRoot.string());
}

std::filesystem::path ResourceManager::resolve(const std::string& relativePath) const
{
    std::filesystem::path path(relativePath);
    if (path.is_absolute()) {
        return path;
    }
    return m_contentRoot / path;
}

TextureHandle ResourceManager::loadTexture(const std::string& relativePath)
{
    TextureError ignored = TextureError::None;
    return loadTexture(relativePath, ignored);
}

TextureHandle ResourceManager::loadTexture(const std::string& relativePath, TextureError& errorOut)
{
    errorOut = TextureError::None;

    const auto existing = m_textureIds.find(relativePath);
    if (existing != m_textureIds.end()) {
        ++m_stats.cacheHits;
        const std::uint32_t id = existing->second;
        m_entries[id - 1].refCount++;
        return TextureHandle{m_entries[id - 1].texture.get(), id};
    }

    ++m_stats.cacheMisses;

    if (m_renderer == nullptr && m_window != nullptr) {
        m_renderer = m_window->renderer();
    }

    auto texture = Texture::load(m_renderer, resolve(relativePath), *m_log, &errorOut);
    if (texture == nullptr) {
        ++m_stats.loadFailures;
        return TextureHandle{};
    }

    if (m_window != nullptr) {
        SDL_SetTextureScaleMode(texture->handle(),
                                m_window->scaleMode() == ScaleMode::Nearest ? SDL_SCALEMODE_NEAREST
                                                                         : SDL_SCALEMODE_LINEAR);
    }

    const std::uint32_t id = m_nextTextureId++;
    m_stats.texturePixels += static_cast<std::uint64_t>(texture->width()) *
                             static_cast<std::uint64_t>(texture->height());

    const int width  = texture->width();
    const int height = texture->height();

    m_entries.push_back(TextureEntry{std::move(texture), relativePath, 1});
    m_textureOrder.push_back(relativePath);
    m_textureIds.emplace(relativePath, id);
    ++m_stats.texturesLoaded;

    m_log->debug("ResourceManager", "loaded texture '{}' ({}x{})", relativePath, width, height);
    return TextureHandle{m_entries[id - 1].texture.get(), id};
}

const Texture* ResourceManager::texture(std::uint32_t handleId) const noexcept
{
    if (handleId == 0 || handleId > m_entries.size()) {
        return nullptr;
    }
    return m_entries[handleId - 1].texture.get();
}

const Texture& ResourceManager::textureRef(std::uint32_t handleId) const
{
    const Texture* result = texture(handleId);
    if (result == nullptr) {
        throw std::logic_error("invalid texture handle");
    }
    return *result;
}

bool ResourceManager::unloadTexture(const std::string& relativePath)
{
    const auto it = m_textureIds.find(relativePath);
    if (it == m_textureIds.end()) {
        return false;
    }

    const std::uint32_t id = it->second;
    TextureEntry& entry = m_entries[id - 1];

    if (--entry.refCount > 0) {
        return true;
    }

    m_stats.texturePixels -= static_cast<std::uint64_t>(entry.texture->width()) *
                             static_cast<std::uint64_t>(entry.texture->height());
    m_stats.texturesLoaded--;

    entry.texture.reset();
    m_textureIds.erase(it);
    m_textureOrder.erase(std::remove(m_textureOrder.begin(), m_textureOrder.end(), relativePath),
                         m_textureOrder.end());

    m_log->debug("ResourceManager", "unloaded texture '{}'", relativePath);
    return true;
}

FontHandle ResourceManager::loadFont(const std::string& relativePath, int pixelSize)
{
    if (pixelSize <= 0) {
        pixelSize = 16;
    }

    const std::string key = relativePath + "@" + std::to_string(pixelSize);
    const auto existing = m_fonts.find(key);
    if (existing != m_fonts.end()) {
        ++m_stats.cacheHits;
        return existing->second;
    }
    ++m_stats.cacheMisses;

    const std::filesystem::path full = resolve(relativePath);
    TTF_Font* raw = TTF_OpenFont(full.string().c_str(), static_cast<float>(pixelSize));
    if (raw == nullptr) {
        m_log->error("ResourceManager", "failed to open font '{}': {}", full.string(), SDL_GetError());
        ++m_stats.loadFailures;
        return FontHandle{};
    }

    auto font      = std::make_unique<Font>();
    font->m_font   = raw;
    font->m_name   = relativePath;
    font->m_pixelSize = pixelSize;

    const Font* stable      = font.get();
    const std::uint32_t id = m_nextFontId++;

    m_fontStore.push_back(std::move(font));
    m_fontById.emplace(id, stable);
    m_fonts.emplace(key, FontHandle{stable, id});
    ++m_stats.fonts;

    m_log->debug("ResourceManager", "loaded font '{}' at {}px", relativePath, pixelSize);
    return FontHandle{stable, id};
}

const Font* ResourceManager::font(std::uint32_t handleId) const noexcept
{
    const auto it = m_fontById.find(handleId);
    return it == m_fontById.end() ? nullptr : it->second;
}

const DataDocument* ResourceManager::loadDocument(const std::string& relativePath)
{
    const auto existing = m_documents.find(relativePath);
    if (existing != m_documents.end()) {
        ++m_stats.cacheHits;
        return &existing->second;
    }
    ++m_stats.cacheMisses;

    const std::filesystem::path full = resolve(relativePath);
    std::ifstream file(full, std::ios::binary);
    if (!file.is_open()) {
        m_log->error("ResourceManager", "cannot open data file '{}'", full.string());
        ++m_stats.loadFailures;
        return nullptr;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    DataDocument doc;
    doc.name = relativePath;
    doc.text = std::move(buffer).str();

    auto [it, inserted] = m_documents.emplace(relativePath, std::move(doc));
    if (inserted) {
        ++m_stats.documents;
    }
    return &it->second;
}

const DataDocument* ResourceManager::document(const std::string& relativePath) const noexcept
{
    const auto it = m_documents.find(relativePath);
    return it == m_documents.end() ? nullptr : &it->second;
}

ResourceManager::LoadReport ResourceManager::preload(const std::vector<std::string>& relativePaths)
{
    LoadReport report{};
    report.requested = relativePaths.size();

    for (const auto& path : relativePaths) {
        if (loadTexture(path).valid()) {
            ++report.loaded;
        } else {
            ++report.failed;
        }
    }

    if (report.failed > 0) {
        m_log->warn("ResourceManager", "preload finished with {}/{} failures",
                    report.failed, report.requested);
    } else if (report.requested > 0) {
        m_log->info("ResourceManager", "preloaded {}/{} textures", report.loaded, report.requested);
    }
    return report;
}

void ResourceManager::clear()
{
    m_entries.clear();
    m_textureOrder.clear();
    m_textureIds.clear();
    m_fonts.clear();
    m_fontStore.clear();
    m_fontById.clear();
    m_documents.clear();
    m_nextTextureId = 1;
    m_nextFontId    = 1;
    m_stats         = ResourceStats{};
}

ResourceStats ResourceManager::stats() const
{
    ResourceStats snapshot = m_stats;
    snapshot.textures     = m_textureOrder.size();
    return snapshot;
}

} // namespace EraShift::Graphics
