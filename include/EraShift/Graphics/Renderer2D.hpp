// Era Shift - immediate-mode 2D renderer.
//
// Wraps SDL_Renderer with the operations the game actually needs: a camera
// transform, textured quads with source rectangles, solid fills, lines and
// nine-slice panels. Draw calls are counted so the debug overlay can show them,
// and every submission is batched by SDL's own texture batching.
//
// A `Renderer2D` never owns the window; it borrows `SDL_Renderer*` so it can be
// re-targeted (for example to render a minimap into a target texture).

#pragma once

#include "EraShift/Core/Time.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Graphics/Window.hpp"

#include <memory>
#include <string>
#include <vector>

struct SDL_Rect;

namespace EraShift::Graphics {

class Texture;
class Camera2D;

/// A single draw command, recorded when batching is enabled.
///
/// Batching lets UI code and particle systems queue geometry and flush it once,
/// avoiding per-quad state changes. Commands are plain data so they can be
/// replayed into a render target without re-running the emitting code.
struct DrawCommand {
    enum class Kind : std::uint8_t {
        Clear,
        FillRect,
        TexturedQuad,
        Line,
        SetBlend,
        SetCamera,
        PushClip,
        PopClip,
    };

    Kind        kind = Kind::FillRect;
    Rect        dst;
    Rect        src;
    Color       color;
    Color       tint;
    float       rotation  = 0.0f;
    bool        flipX     = false;
    bool        flipY     = false;
    bool        flag      = false;   ///< Kind-dependent boolean (SetCamera)
    BlendMode   blend     = BlendMode::None;
    /// Index into the texture table of the batch being built.
    std::size_t textureId = 0;
};

/// Camera plus viewport state applied to every draw call.
class Camera2D {
public:
    Camera2D() = default;

    void setViewport(int width, int height) noexcept;
    void setPosition(const Vec2& position) noexcept { m_position = position; }
    void setZoom(float zoom) noexcept { m_zoom = (zoom > 0.0f) ? zoom : 0.0001f; }

    /// Half-extent of the visible area in world units.
    [[nodiscard]] Vec2 halfExtent() const noexcept;
    /// World-space rectangle currently visible.
    [[nodiscard]] Rect viewportBounds() const noexcept;
    /// Converts a screen-space point to world space.
    [[nodiscard]] Vec2 screenToWorld(const Vec2& screen) const noexcept;
    /// Converts a world-space point to screen space.
    [[nodiscard]] Vec2 worldToScreen(const Vec2& world) const noexcept;

    void shake(float magnitude, double durationSeconds);

    void update(double deltaSeconds);

    void setBounds(const Rect& bounds) noexcept { m_bounds = bounds; m_hasBounds = !bounds.isEmpty(); }
    void clearBounds() noexcept { m_hasBounds = false; }

    /// Snaps the camera centre into the world bounds, accounting for view size.
    void clampToBounds();

    [[nodiscard]] const Vec2& position() const noexcept { return m_position; }
    [[nodiscard]] Vec2  stablePosition() const noexcept;
    [[nodiscard]] float zoom() const noexcept { return m_zoom; }
    [[nodiscard]] float shakeMagnitude() const noexcept { return m_shakeMagnitude; }
    [[nodiscard]] int   viewportWidth() const noexcept { return m_viewportWidth; }
    [[nodiscard]] int   viewportHeight() const noexcept { return m_viewportHeight; }
    [[nodiscard]] const Rect& bounds() const noexcept { return m_bounds; }

    /// Screen shake uses a deterministic low-frequency oscillation rather than
    /// random noise so a recorded replay looks identical every time.
    [[nodiscard]] Vec2 shakeOffset() const noexcept;

private:
    Vec2  m_position{0.0f, 0.0f};
    float m_zoom = 1.0f;
    int   m_viewportWidth  = 0;
    int   m_viewportHeight = 0;

    bool  m_hasBounds = false;
    Rect  m_bounds;

    float m_shakeMagnitude = 0.0f;
    float m_shakeTimer     = 0.0f;
    float m_shakeDuration  = 0.0f;
    float m_shakeSeed      = 0.0f;
};

class Renderer2D {
public:
    Renderer2D() = default;
    ~Renderer2D();

    Renderer2D(const Renderer2D&)            = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    void attach(Window& window);
    void detach() noexcept;

    [[nodiscard]] bool isValid() const noexcept { return m_renderer != nullptr; }
    [[nodiscard]] SDL_Renderer* handle() const noexcept { return m_renderer; }
    [[nodiscard]] const DrawCallCounter& drawCalls() const noexcept { return m_drawCalls; }

    /// Resets per-frame counters. Called automatically by `beginFrame()`.
    void beginFrame();
    void endFrame();

    // --- camera -------------------------------------------------------------
    [[nodiscard]] Camera2D& camera() noexcept { return m_camera; }
    [[nodiscard]] const Camera2D& camera() const noexcept { return m_camera; }
    /// Detaches the camera so subsequent draws are in screen space (UI).
    void setCameraEnabled(bool enabled) noexcept { m_cameraEnabled = enabled; }
    [[nodiscard]] bool cameraEnabled() const noexcept { return m_cameraEnabled; }

    /// The transform every world-space draw call goes through.
    ///
    /// Exposed because the game has to place things in world coordinates that
    /// it measures itself (the camera bounds, the reachability of a ledge), and
    /// having two definitions of "world to screen" is how a world-space UI
    /// element ends up in the wrong place.
    ///
    /// With the camera detached this is the identity, so UI code can use it
    /// unconditionally.
    [[nodiscard]] Vec2 toScreen(const Vec2& world) const noexcept;
    /// The uniform scale factor currently applied to world geometry.
    [[nodiscard]] float worldScale() const noexcept;

    // --- drawing ------------------------------------------------------------
    void clear(Color color);

    void drawRect(const Rect& rect, Color color, bool filled = true, float thickness = 1.0f);
    void drawRectOutline(const Rect& rect, Color color, float thickness = 1.0f);
    void drawLine(const Vec2& a, const Vec2& b, Color color, float thickness = 1.0f);
    void drawTriangle(const Vec2& a, const Vec2& b, const Vec2& c, Color color);

    /// Draws a texture into a world-space rectangle. `src` selects a sub-rect of
    /// the texture; an empty `src` uses the whole texture.
    void drawTexture(const Texture& texture, const Rect& dst, const Rect& src = {},
                     Color tint = Palette::White, float rotation = 0.0f,
                     bool flipX = false, bool flipY = false);

    /// Draws a texture keeping its aspect ratio, centred in `dst`.
    void drawTextureFit(const Texture& texture, const Rect& dst, Color tint = Palette::White,
                        bool preserveAspect = true);

    /// Nine-slice panel for UI frames. `corner` is the size of the untouched
    /// corner regions in source pixels.
    void drawNineSlice(const Texture& texture, const Rect& dst, float corner, Color tint = Palette::White);

    /// Draws an axis-aligned filled circle using a triangle fan approximation.
    void drawCircle(const Vec2& center, float radius, Color color, int segments = 24);

    void setBlendMode(BlendMode mode);
    [[nodiscard]] BlendMode blendMode() const noexcept { return m_blend; }

    /// Sets a scissor rectangle in screen space. Pass nullptr to disable.
    void setClipRect(const Rect* screenRect);

    // --- batching -----------------------------------------------------------
    /// Starts recording draw commands. Textures must be registered with
    /// `registerTexture` so the batch can re-bind them during a flush.
    void beginBatch();
    void endBatch();
    [[nodiscard]] bool isBatching() const noexcept { return m_batching; }
    [[nodiscard]] std::size_t pendingCommands() const noexcept { return m_commands.size(); }

    /// Interns a texture so it can be referenced from a DrawCommand.
    [[nodiscard]] std::size_t registerTexture(const Texture* texture);
    [[nodiscard]] const Texture* textureFor(std::size_t id) const;
    void clearBatchTextures();

private:
    void flushInternal();
    void applyViewport();
    void pushClipInternal(const Rect& screenRect);

    SDL_Renderer*     m_renderer = nullptr;
    Window*          m_window   = nullptr;
    DrawCallCounter  m_drawCalls;
    Camera2D         m_camera;
    BlendMode        m_blend = BlendMode::None;
    bool             m_cameraEnabled = true;
    bool             m_batching     = false;

    std::vector<DrawCommand> m_commands;
    std::vector<const Texture*> m_batchTextures;
    std::vector<Rect> m_clipStack;
    bool m_hasClip = false;
};

} // namespace EraShift::Graphics
