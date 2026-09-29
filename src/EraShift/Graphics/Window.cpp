#include "EraShift/Graphics/Window.hpp"

#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <SDL3/SDL.h>

#include <utility>

namespace EraShift::Graphics {

Window::~Window()
{
    destroy();
}

Window::Window(Window&& other) noexcept
    : m_window(std::exchange(other.m_window, nullptr)),
      m_renderer(std::exchange(other.m_renderer, nullptr)),
      m_config(std::move(other.m_config)),
      m_logicalWidth(std::exchange(other.m_logicalWidth, 0)),
      m_logicalHeight(std::exchange(other.m_logicalHeight, 0)),
      m_fullscreenDesktop(std::exchange(other.m_fullscreenDesktop, false))
{
}

Window& Window::operator=(Window&& other) noexcept
{
    if (this != &other) {
        destroy();
        m_window            = std::exchange(other.m_window, nullptr);
        m_renderer          = std::exchange(other.m_renderer, nullptr);
        m_config            = std::move(other.m_config);
        m_logicalWidth      = std::exchange(other.m_logicalWidth, 0);
        m_logicalHeight     = std::exchange(other.m_logicalHeight, 0);
        m_fullscreenDesktop = std::exchange(other.m_fullscreenDesktop, false);
    }
    return *this;
}

bool Window::create(const WindowConfig& config, std::string& errorOut)
{
    destroy();
    m_config = config;

    // No graphics-API flag: SDL picks whatever accelerated driver is available
    // (Vulkan, then OpenGL), which keeps the build portable across machines.
    SDL_WindowFlags flags = 0;
    if (m_config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (m_config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (m_config.highDpi) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }

    m_window = SDL_CreateWindow(m_config.title.c_str(),
                                std::max(m_config.width, 320),
                                std::max(m_config.height, 240),
                                flags);
    if (m_window == nullptr) {
        errorOut = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        return false;
    }

    // SDL 3.4 negotiates the driver automatically; vsync is a separate call.
    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (m_renderer == nullptr) {
        errorOut = std::string("SDL_CreateRenderer failed: ") + SDL_GetError();
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return false;
    }

    SDL_SetRenderVSync(m_renderer, m_config.vsync ? 1 : 0);

    // "Logical resolution" is the coordinate space every draw call works in.
    // With no explicit setting it is simply the window size, so the rest of the
    // engine never has to special-case an unset value (and never ends up with
    // a zero-sized viewport, which silently draws nothing).
    m_logicalWidth  = std::max(m_config.logicalWidth, 0);
    m_logicalHeight = std::max(m_config.logicalHeight, 0);
    if (m_logicalWidth <= 0 || m_logicalHeight <= 0) {
        m_logicalWidth  = std::max(m_config.width, 1);
        m_logicalHeight = std::max(m_config.height, 1);
        m_config.logicalWidth  = m_logicalWidth;
        m_config.logicalHeight = m_logicalHeight;
        SDL_SetRenderLogicalPresentation(m_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    } else {
        SDL_SetRenderLogicalPresentation(m_renderer, m_logicalWidth, m_logicalHeight,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }

    return true;
}

void Window::destroy() noexcept
{
    if (m_renderer != nullptr) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    m_logicalWidth  = 0;
    m_logicalHeight = 0;
}

void Window::present()
{
    if (m_renderer != nullptr) {
        SDL_RenderPresent(m_renderer);
    }
}

void Window::clear(const Color& color)
{
    if (m_renderer == nullptr) {
        return;
    }
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(m_renderer);
}

void Window::setClipRect(const Rect* rect)
{
    if (m_renderer == nullptr) {
        return;
    }
    if (rect == nullptr) {
        SDL_SetRenderClipRect(m_renderer, nullptr);
        return;
    }
    const SDL_Rect sdl{static_cast<int>(rect->x), static_cast<int>(rect->y),
                       static_cast<int>(rect->w), static_cast<int>(rect->h)};
    SDL_SetRenderClipRect(m_renderer, &sdl);
}

void Window::setScaleMode(ScaleMode mode)
{
    // SDL3 applies the scale mode per texture, not per renderer, so this only
    // records the preference; ResourceManager applies it as textures are loaded.
    m_scaleMode = mode;
}

void Window::setVsync(bool enabled)
{
    m_config.vsync = enabled;
    if (m_renderer != nullptr) {
        SDL_SetRenderVSync(m_renderer, enabled ? 1 : 0);
    }
}

void Window::setTitle(const std::string& title)
{
    m_config.title = title;
    if (m_window != nullptr) {
        SDL_SetWindowTitle(m_window, title.c_str());
    }
}

int Window::pixelWidth() const noexcept
{
    int w = 0;
    int h = 0;
    if (m_window != nullptr) {
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
    }
    return w;
}

int Window::pixelHeight() const noexcept
{
    int w = 0;
    int h = 0;
    if (m_window != nullptr) {
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
    }
    return h;
}

void Window::setFullscreen(bool enabled)
{
    m_config.fullscreen = enabled;
    if (m_window == nullptr) {
        return;
    }
    SDL_SetWindowFullscreen(m_window, enabled);
}

void Window::setFullscreenDesktop(bool enabled)
{
    m_fullscreenDesktop = enabled;
    if (m_window == nullptr) {
        return;
    }
    // A null mode requests the desktop's current mode, which is what
    // "borderless fullscreen" means. A non-null mode is an exclusive change
    // that we do not expose yet.
    SDL_SetWindowFullscreenMode(m_window, nullptr);
    SDL_SetWindowFullscreen(m_window, enabled);
    if (!enabled) {
        SDL_SetWindowSize(m_window, m_config.width, m_config.height);
    }
}

bool Window::applyLogicalSize(int width, int height, std::string& errorOut)
{
    if (m_renderer == nullptr) {
        errorOut = "no renderer";
        return false;
    }
    if (width <= 0 || height <= 0) {
        errorOut = "logical size must be positive";
        return false;
    }

    m_logicalWidth  = width;
    m_logicalHeight = height;
    m_config.logicalWidth  = width;
    m_config.logicalHeight = height;

    if (!SDL_SetRenderLogicalPresentation(m_renderer, width, height,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        errorOut = SDL_GetError();
        return false;
    }
    return true;
}

void Window::setLogicalSize(int width, int height) noexcept
{
    // Zero (or negative) means "track the window", which is also the default.
    m_logicalWidth  = std::max(width, 0);
    m_logicalHeight = std::max(height, 0);
    m_config.logicalWidth  = m_logicalWidth;
    m_config.logicalHeight = m_logicalHeight;

    if (m_renderer == nullptr) {
        return;
    }
    if (m_logicalWidth > 0 && m_logicalHeight > 0) {
        SDL_SetRenderLogicalPresentation(m_renderer, m_logicalWidth, m_logicalHeight,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    } else {
        m_logicalWidth  = std::max(m_config.width, 1);
        m_logicalHeight = std::max(m_config.height, 1);
        SDL_SetRenderLogicalPresentation(m_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    }
}

} // namespace EraShift::Graphics
