#include "EraShift/Graphics/Texture.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <utility>

namespace EraShift::Graphics {

std::string_view describe(TextureError error)
{
    switch (error) {
        case TextureError::None:                return "ok";
        case TextureError::FileNotFound:        return "file not found";
        case TextureError::DecodeFailed:        return "decode failed";
        case TextureError::NotAnImage:          return "unsupported image format";
        case TextureError::OutOfMemory:         return "out of memory";
        case TextureError::RendererUnavailable: return "no renderer";
    }
    return "unknown error";
}

Texture::Texture(SDL_Texture* texture, std::string name)
    : m_texture(texture), m_name(std::move(name))
{
    if (m_texture != nullptr) {
        float width = 0.0f;
        float height = 0.0f;
        SDL_GetTextureSize(m_texture, &width, &height);
        m_width  = static_cast<int>(width + 0.5f);
        m_height = static_cast<int>(height + 0.5f);
    }
}

Texture::~Texture()
{
    release();
}

Texture::Texture(Texture&& other) noexcept
    : m_texture(std::exchange(other.m_texture, nullptr)),
      m_name(std::move(other.m_name)),
      m_width(std::exchange(other.m_width, 0)),
      m_height(std::exchange(other.m_height, 0))
{
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other) {
        release();
        m_texture = std::exchange(other.m_texture, nullptr);
        m_name    = std::move(other.m_name);
        m_width   = std::exchange(other.m_width, 0);
        m_height  = std::exchange(other.m_height, 0);
    }
    return *this;
}

void Texture::release() noexcept
{
    if (m_texture != nullptr) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
    m_width  = 0;
    m_height = 0;
}

std::shared_ptr<Texture> Texture::adopt(SDL_Texture* texture, std::string debugName)
{
    if (texture == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<Texture>(new Texture(texture, std::move(debugName)));
}

std::shared_ptr<Texture> Texture::load(SDL_Renderer* renderer, const std::filesystem::path& path,
                                        Core::Logger& log, TextureError* error)
{
    const auto fail = [&](TextureError reason) -> std::shared_ptr<Texture> {
        if (error != nullptr) {
            *error = reason;
        }
        log.error("Texture", "failed to load '{}': {}", path.string(), describe(reason));
        return nullptr;
    };

    if (renderer == nullptr) {
        return fail(TextureError::RendererUnavailable);
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return fail(TextureError::FileNotFound);
    }

    // SDL_image 3.4 has no separate error string; SDL_GetError() covers it.
    SDL_Surface* surface = IMG_Load(path.string().c_str());
    if (surface == nullptr) {
        const char* reason = SDL_GetError();
        if (reason != nullptr &&
            std::string_view(reason).find("not a known file format") != std::string_view::npos) {
            return fail(TextureError::NotAnImage);
        }
        log.debug("Texture", "SDL_image reported: {}", reason != nullptr ? reason : "unknown");
        return fail(TextureError::DecodeFailed);
    }

    SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (raw == nullptr) {
        return fail(TextureError::OutOfMemory);
    }

    if (error != nullptr) {
        *error = TextureError::None;
    }
    return std::shared_ptr<Texture>(new Texture(raw, path.string()));
}

void Texture::setAlphaMod(std::uint8_t alpha) noexcept
{
    if (m_texture != nullptr) {
        SDL_SetTextureAlphaMod(m_texture, alpha);
    }
}

void Texture::setColorMod(Color color) noexcept
{
    if (m_texture != nullptr) {
        SDL_SetTextureColorMod(m_texture, color.r, color.g, color.b);
    }
}

void Texture::resetModulation() noexcept
{
    if (m_texture != nullptr) {
        SDL_SetTextureAlphaMod(m_texture, 255);
        SDL_SetTextureColorMod(m_texture, 255, 255, 255);
    }
}

} // namespace EraShift::Graphics
