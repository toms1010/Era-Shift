// Era Shift - texture ownership and loading.
//
// `Texture` is a move-only RAII wrapper around `SDL_Texture`. Resources are
// acquired through ResourceManager, which keeps one instance per asset path so
// nothing is uploaded twice.

#pragma once

#include "EraShift/Core/Log.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <filesystem>
#include <memory>
#include <string>

struct SDL_Texture;
struct SDL_Renderer;

namespace EraShift::Graphics {

/// Reason a texture could not be created.
enum class TextureError {
    None,
    FileNotFound,
    DecodeFailed,
    NotAnImage,
    OutOfMemory,
    RendererUnavailable,
};

[[nodiscard]] std::string_view describe(TextureError error);

/// A GPU texture with move semantics and automatic cleanup.
class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    /// Loads an image through SDL3_image. Logs and leaves the texture invalid on
    /// failure; `error` receives the reason.
    static std::shared_ptr<Texture> load(SDL_Renderer* renderer, const std::filesystem::path& path,
                                         Core::Logger& log, TextureError* error = nullptr);

    /// Wraps an existing SDL_Texture. Ownership transfers to this object.
    static std::shared_ptr<Texture> adopt(SDL_Texture* texture, std::string debugName);

    [[nodiscard]] bool valid() const noexcept { return m_texture != nullptr; }
    explicit operator bool() const noexcept { return valid(); }

    [[nodiscard]] SDL_Texture* handle() const noexcept { return m_texture; }
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] Vec2 size() const noexcept { return {static_cast<float>(m_width), static_cast<float>(m_height)}; }

    void setAlphaMod(std::uint8_t alpha) noexcept;
    void setColorMod(Color color) noexcept;
    void resetModulation() noexcept;

    void release() noexcept;

private:
    explicit Texture(SDL_Texture* texture, std::string name);

    SDL_Texture* m_texture = nullptr;
    std::string  m_name;
    int          m_width   = 0;
    int          m_height  = 0;
};

} // namespace EraShift::Graphics
