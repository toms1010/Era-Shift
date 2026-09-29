// Era Shift - window and rendering context.
//
// Window owns the SDL window and the SDL renderer and presents frames. All SDL
// types stay behind this boundary; the rest of the engine talks in terms of
// `Vec2`, `Rect` and `Color` from the utilities layer.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;

namespace EraShift::Graphics {

struct Vec2;
struct Rect;
struct Color;

/// Configuration for window creation. Values come from config/graphics.json.
struct WindowConfig {
    std::string title       = "Era Shift";
    int         width       = 1280;
    int         height      = 720;
    bool        resizable   = true;
    bool        fullscreen  = false;
    bool        vsync       = true;
    /// Allow the window to use more than the display scale (HiDPI crispness).
    bool        highDpi     = true;
    /// Logical render resolution. When non-zero the world is rendered at this
    /// size and scaled to the window, which keeps the UI identical on every
    /// display.
    int         logicalWidth  = 0;
    int         logicalHeight = 0;
};

/// Colour and blending modes for immediate-mode drawing.
enum class BlendMode {
    None,
    Alpha,
    Additive,
    Multiply,
};

enum class ScaleMode {
    Nearest,   ///< Crisp pixels, the default for pixel art.
    Linear,
};

/// A top-level window plus its 2D rendering context.
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    [[nodiscard]] bool create(const WindowConfig& config, std::string& errorOut);

    void destroy() noexcept;
    [[nodiscard]] bool isValid() const noexcept { return m_window != nullptr && m_renderer != nullptr; }

    /// Presents the current frame. Call once at the end of each frame.
    void present();

    void clear(const Color& color);
    /// Sets a scissor rectangle in logical coordinates; pass an empty rect to
    /// disable clipping.
    void setClipRect(const Rect* rect);
    void setScaleMode(ScaleMode mode);
    [[nodiscard]] ScaleMode scaleMode() const noexcept { return m_scaleMode; }

    void setTitle(const std::string& title);

    /// Turns vsync on or off at runtime. Safe to call before create().
    void setVsync(bool enabled);
    [[nodiscard]] const std::string& title() const noexcept { return m_config.title; }

    /// Restores the window from fullscreen.
    void setFullscreen(bool enabled);
    [[nodiscard]] bool isFullscreen() const noexcept { return m_config.fullscreen; }

    /// Applies a new logical resolution, re-creating the render target.
    bool applyLogicalSize(int width, int height, std::string& errorOut);

    void setLogicalSize(int width, int height) noexcept;
    [[nodiscard]] int  logicalWidth() const noexcept { return m_logicalWidth; }
    [[nodiscard]] int  logicalHeight() const noexcept { return m_logicalHeight; }

    /// Size of the drawable area in pixels.
    [[nodiscard]] int pixelWidth() const noexcept;
    [[nodiscard]] int pixelHeight() const noexcept;

    [[nodiscard]] SDL_Window*   handle() const noexcept { return m_window; }
    [[nodiscard]] SDL_Renderer* renderer() const noexcept { return m_renderer; }
    [[nodiscard]] const WindowConfig& config() const noexcept { return m_config; }

    /// Toggles that force the window into a borderless fullscreen desktop mode.
    [[nodiscard]] bool isFullscreenDesktop() const noexcept { return m_fullscreenDesktop; }
    void setFullscreenDesktop(bool enabled);

private:
    SDL_Window*   m_window  = nullptr;
    SDL_Renderer* m_renderer = nullptr;

    WindowConfig m_config;
    ScaleMode m_scaleMode = ScaleMode::Nearest;
    int  m_logicalWidth      = 0;
    int  m_logicalHeight     = 0;
    bool m_fullscreenDesktop = false;
};

/// Number of draw calls issued during the current frame. Sampled by the debug
/// overlay and asserted in tests.
class DrawCallCounter {
public:
    void reset() noexcept { m_count = 0; }
    void add(std::size_t n = 1) noexcept { m_count += n; }
    DrawCallCounter& operator++() noexcept { ++m_count; return *this; }
    DrawCallCounter& operator++(int) noexcept { ++m_count; return *this; }
    [[nodiscard]] std::size_t value() const noexcept { return m_count; }

private:
    std::size_t m_count = 0;
};

} // namespace EraShift::Graphics
