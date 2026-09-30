// Era Shift - 2D camera.
//
// Follows a target with exponential damping (frame-rate independent), respects
// world bounds, supports zoom and deterministic screen shake.

#include "EraShift/Graphics/Renderer2D.hpp"

#include <SDL3/SDL.h>

#include "EraShift/Graphics/Texture.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace EraShift::Graphics {

namespace {
constexpr float kMinZoom = 0.05f;
constexpr float kMaxZoom = 64.0f;
} // namespace

// ---------------------------------------------------------------------------
// Camera2D
// ---------------------------------------------------------------------------
void Camera2D::setViewport(int width, int height) noexcept
{
    m_viewportWidth  = std::max(width, 1);
    m_viewportHeight = std::max(height, 1);
}

Vec2 Camera2D::halfExtent() const noexcept
{
    const float zoom = std::clamp(m_zoom, kMinZoom, kMaxZoom);
    return {static_cast<float>(m_viewportWidth) / (2.0f * zoom),
            static_cast<float>(m_viewportHeight) / (2.0f * zoom)};
}

Rect Camera2D::viewportBounds() const noexcept
{
    const Vec2 half = halfExtent();
    return {m_position.x - half.x, m_position.y - half.y, half.x * 2.0f, half.y * 2.0f};
}

Vec2 Camera2D::screenToWorld(const Vec2& screen) const noexcept
{
    const float zoom = std::clamp(m_zoom, kMinZoom, kMaxZoom);
    return {m_position.x + (screen.x - static_cast<float>(m_viewportWidth) * 0.5f) / zoom,
            m_position.y + (screen.y - static_cast<float>(m_viewportHeight) * 0.5f) / zoom};
}

Vec2 Camera2D::worldToScreen(const Vec2& world) const noexcept
{
    const float zoom = std::clamp(m_zoom, kMinZoom, kMaxZoom);
    return {(world.x - m_position.x) * zoom + static_cast<float>(m_viewportWidth) * 0.5f,
            (world.y - m_position.y) * zoom + static_cast<float>(m_viewportHeight) * 0.5f};
}

Vec2 Camera2D::stablePosition() const noexcept
{
    return m_position + shakeOffset();
}

Vec2 Camera2D::shakeOffset() const noexcept
{
    if (m_shakeMagnitude <= 0.0f || m_shakeDuration <= 0.0f || m_shakeTimer <= 0.0f) {
        return {0.0f, 0.0f};
    }
    // Decaying sinusoid pair: deterministic, cheap, and avoids the harshness of
    // pure white noise.
    const float t = m_shakeSeed + static_cast<float>(m_shakeTimer) * 47.0f;
    const float decay = m_shakeTimer / m_shakeDuration;
    return {std::sin(t) * m_shakeMagnitude * decay,
            std::cos(t * 1.37f) * m_shakeMagnitude * decay};
}

void Camera2D::shake(float magnitude, double durationSeconds)
{
    if (magnitude <= 0.0f || durationSeconds <= 0.0) {
        return;
    }
    // Re-triggering a shake keeps the stronger of the two so a big hit is never
    // cancelled by a small one.
    m_shakeMagnitude = std::max(m_shakeMagnitude, magnitude);
    m_shakeDuration  = static_cast<float>(std::max(durationSeconds, static_cast<double>(m_shakeDuration)));
    m_shakeTimer     = m_shakeDuration;
    m_shakeSeed      = std::fmod(m_shakeSeed + magnitude * 0.731f, 6.2831853f);
}

void Camera2D::update(double deltaSeconds)
{
    if (m_shakeTimer > 0.0f) {
        m_shakeTimer -= static_cast<float>(deltaSeconds);
        if (m_shakeTimer <= 0.0f) {
            m_shakeTimer     = 0.0f;
            m_shakeMagnitude = 0.0f;
            m_shakeDuration  = 0.0f;
        }
    }
    clampToBounds();
}

void Camera2D::clampToBounds()
{
    if (!m_hasBounds || m_bounds.isEmpty()) {
        return;
    }

    const Vec2 half = halfExtent();
    const float centreX = half.x * 2.0f >= m_bounds.w
                              ? m_bounds.center().x                       // viewport wider than world
                              : std::clamp(m_position.x, m_bounds.left() + half.x, m_bounds.right() - half.x);
    const float centreY = half.y * 2.0f >= m_bounds.h
                              ? m_bounds.center().y
                              : std::clamp(m_position.y, m_bounds.top() + half.y, m_bounds.bottom() - half.y);

    m_position.x = centreX;
    m_position.y = centreY;
}

// ---------------------------------------------------------------------------
// Renderer2D
// ---------------------------------------------------------------------------
Renderer2D::~Renderer2D()
{
    detach();
}

void Renderer2D::attach(Window& window)
{
    detach();
    m_window   = &window;
    m_renderer = window.renderer();
    if (m_renderer != nullptr) {
        m_camera.setViewport(window.logicalWidth(), window.logicalHeight());
        applyViewport();
    }
}

void Renderer2D::detach() noexcept
{
    m_renderer = nullptr;
    m_window   = nullptr;
    m_commands.clear();
    m_batchTextures.clear();
    m_clipStack.clear();
    m_batching = false;
    m_hasClip  = false;
}

void Renderer2D::applyViewport()
{
    if (m_renderer == nullptr) {
        return;
    }
    if (m_window != nullptr && m_window->logicalWidth() > 0) {
        SDL_SetRenderLogicalPresentation(m_renderer, m_window->logicalWidth(),
                                         m_window->logicalHeight(),
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    } else {
        SDL_SetRenderLogicalPresentation(m_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    }
}

void Renderer2D::beginFrame()
{
    m_drawCalls.reset();
    m_commands.clear();
    m_clipStack.clear();
    m_hasClip = false;
    m_blend   = BlendMode::None;
    if (m_renderer != nullptr) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    }
    if (m_window != nullptr) {
        m_camera.setViewport(m_window->logicalWidth(), m_window->logicalHeight());
    }
}

void Renderer2D::endFrame()
{
    if (m_batching) {
        endBatch();
    }
    if (m_renderer != nullptr) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    }
}

void Renderer2D::setBlendMode(BlendMode mode)
{
    if (m_blend == mode) {
        return;
    }
    if (m_batching) {
        DrawCommand cmd{};
        cmd.kind  = DrawCommand::Kind::SetBlend;
        cmd.blend = mode;
        m_commands.push_back(cmd);
        return;
    }
    m_blend = mode;
    if (m_renderer == nullptr) {
        return;
    }
    switch (mode) {
        case BlendMode::None:     SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE); break;
        case BlendMode::Alpha:    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND); break;
        case BlendMode::Additive: SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_ADD); break;
        case BlendMode::Multiply: SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_MUL); break;
    }
}

void Renderer2D::clear(Color color)
{
    if (m_renderer == nullptr) {
        return;
    }
    if (m_batching) {
        DrawCommand cmd{};
        cmd.kind  = DrawCommand::Kind::Clear;
        cmd.color = color;
        m_commands.push_back(cmd);
        return;
    }
    ++m_drawCalls;
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(m_renderer);
}

float Renderer2D::worldScale() const noexcept
{
    if (!m_cameraEnabled) {
        return 1.0f;
    }
    return std::clamp(m_camera.zoom(), kMinZoom, kMaxZoom);
}

Vec2 Renderer2D::toScreen(const Vec2& world) const noexcept
{
    if (!m_cameraEnabled) {
        return world;
    }
    const Camera2D& cam   = m_camera;
    const float zoom      = std::clamp(cam.zoom(), kMinZoom, kMaxZoom);
    const Vec2  centre    = cam.stablePosition();
    const float halfWidth  = static_cast<float>(cam.viewportWidth()) * 0.5f;
    const float halfHeight = static_cast<float>(cam.viewportHeight()) * 0.5f;

    // Both spaces have y growing downwards - SDL's logical presentation is
    // top-left origin - so this is a plain scale-and-offset, not a flip.
    return Vec2{(world.x - centre.x) * zoom + halfWidth,
                (world.y - centre.y) * zoom + halfHeight};
}

void Renderer2D::drawRect(const Rect& rect, Color color, bool filled, float thickness)
{
    if (m_renderer == nullptr || rect.isEmpty()) {
        return;
    }
    if (m_batching) {
        DrawCommand cmd{};
        cmd.kind  = filled ? DrawCommand::Kind::FillRect : DrawCommand::Kind::Line;
        cmd.dst   = rect;
        cmd.color = color;
        m_commands.push_back(cmd);
        return;
    }
    ++m_drawCalls;
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);

    if (m_cameraEnabled) {
        // A world rect becomes a screen rect. An axis-aligned rectangle stays
        // axis-aligned under this transform, so nothing more is needed than
        // repositioning and rescaling.
        const float zoom = worldScale();
        const Vec2  topLeft = toScreen(Vec2{rect.x, rect.y + rect.h});
        const SDL_FRect fr{topLeft.x, topLeft.y, rect.w * zoom, rect.h * zoom};
        if (fr.w <= 0.0f || fr.h <= 0.0f) {
            return;
        }
        if (filled) {
            SDL_RenderFillRect(m_renderer, &fr);
        } else {
            SDL_RenderRect(m_renderer, &fr);
        }
        static_cast<void>(thickness);
        return;
    }

    // Snap to whole pixels before filling. SDL's GPU renderer draws a fill as
    // a quad with alpha blending, so a rect whose edges land mid-pixel is
    // rasterised with partial coverage. Two abutting rects then both blend
    // into the shared pixel row and it comes out darker than either colour -
    // which shows up as a dark hairline across every gradient band, every
    // parallax ridge and every seam between two tiles.
    //
    // The *edges* are snapped and the size is derived from them, rather than
    // snapping the size as well. Rounding the size independently lets the edges
    // drift apart and leaves a one-pixel gap that the cleared framebuffer shows
    // through as a black line. Deriving it means abutting rects always share an
    // exact edge: no overlap to double-blend, no gap to see through.
    const float left   = std::round(rect.x);
    const float top    = std::round(rect.y);
    const float right  = std::round(rect.x + rect.w);
    const float bottom = std::round(rect.y + rect.h);

    const SDL_FRect fr{left, top, right - left, bottom - top};
    if (fr.w <= 0.0f || fr.h <= 0.0f) {
        return;
    }
    if (filled) {
        SDL_RenderFillRect(m_renderer, &fr);
    } else {
        SDL_RenderRect(m_renderer, &fr);
    }
    static_cast<void>(thickness);
}

void Renderer2D::drawRectOutline(const Rect& rect, Color color, float thickness)
{
    if (rect.isEmpty()) {
        return;
    }
    const float t = std::max(1.0f, thickness);
    drawRect(Rect{rect.x, rect.y, rect.w, t}, color);
    drawRect(Rect{rect.x, rect.bottom() - t, rect.w, t}, color);
    drawRect(Rect{rect.x, rect.y + t, t, std::max(0.0f, rect.h - t * 2.0f)}, color);
    drawRect(Rect{rect.right() - t, rect.y + t, t, std::max(0.0f, rect.h - t * 2.0f)}, color);
}

void Renderer2D::drawLine(const Vec2& a, const Vec2& b, Color color, float thickness)
{
    if (m_renderer == nullptr) {
        return;
    }
    if (m_batching) {
        DrawCommand cmd{};
        cmd.kind   = DrawCommand::Kind::Line;
        cmd.dst    = Rect{a.x, a.y, b.x - a.x, b.y - a.y};
        cmd.color  = color;
        m_commands.push_back(cmd);
        return;
    }
    ++m_drawCalls;
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    const Vec2 from = toScreen(a);
    const Vec2 to   = toScreen(b);
    SDL_RenderLine(m_renderer, from.x, from.y, to.x, to.y);
    static_cast<void>(thickness);
}

void Renderer2D::drawTriangle(const Vec2& a, const Vec2& b, const Vec2& c, Color color)
{
    if (m_renderer == nullptr) {
        return;
    }
    // A geometry submission cannot be expressed as a recorded command, so a
    // batch in progress has to be flushed rather than silently skipped: ending
    // the batch here keeps the recorded geometry in the right order.
    if (m_batching) {
        flushInternal();
    }
    ++m_drawCalls;
    const SDL_FColor tint{static_cast<float>(color.r) / 255.0f,
                          static_cast<float>(color.g) / 255.0f,
                          static_cast<float>(color.b) / 255.0f,
                          static_cast<float>(color.a) / 255.0f};
    const Vec2 pa = toScreen(a);
    const Vec2 pb = toScreen(b);
    const Vec2 pc = toScreen(c);
    const SDL_Vertex vertices[3] = {
        {{pa.x, pa.y}, tint, {0.0f, 0.0f}},
        {{pb.x, pb.y}, tint, {0.0f, 0.0f}},
        {{pc.x, pc.y}, tint, {0.0f, 0.0f}},
    };
    SDL_RenderGeometry(m_renderer, nullptr, vertices, 3, nullptr, 0);
}

void Renderer2D::drawTexture(const Texture& texture, const Rect& dst, const Rect& src,
                             Color tint, float rotation, bool flipX, bool flipY)
{
    if (m_renderer == nullptr || !texture.valid() || dst.isEmpty()) {
        return;
    }

    if (m_batching) {
        DrawCommand cmd{};
        cmd.kind      = DrawCommand::Kind::TexturedQuad;
        cmd.dst       = dst;
        cmd.src       = src;
        cmd.tint      = tint;
        cmd.rotation  = rotation;
        cmd.flipX     = flipX;
        cmd.flipY     = flipY;
        cmd.textureId = registerTexture(&texture);
        m_commands.push_back(cmd);
        return;
    }

    ++m_drawCalls;

    SDL_FRect dstRect{dst.x, dst.y, dst.w, dst.h};
    if (m_cameraEnabled) {
        const float zoom     = worldScale();
        const Vec2  topLeft  = toScreen(dst.position());
        dstRect = SDL_FRect{topLeft.x, topLeft.y, dst.w * zoom, dst.h * zoom};
    }
    SDL_FRect srcRect{src.x, src.y, src.w, src.h};
    if (srcRect.w <= 0.0f || srcRect.h <= 0.0f) {
        srcRect = SDL_FRect{0.0f, 0.0f, static_cast<float>(texture.width()),
                            static_cast<float>(texture.height())};
    }

    const SDL_FlipMode flip = static_cast<SDL_FlipMode>((flipX ? 1 : 0) | (flipY ? 2 : 0));

    if (rotation == 0.0f && flip == SDL_FLIP_NONE) {
        SDL_RenderTexture(m_renderer, texture.handle(), &srcRect, &dstRect);
    } else {
        SDL_RenderTextureRotated(m_renderer, texture.handle(), &srcRect, &dstRect,
                                 -rotation, nullptr, flip);
    }
}

void Renderer2D::drawTextureFit(const Texture& texture, const Rect& dst, Color tint, bool preserveAspect)
{
    if (!texture.valid() || dst.isEmpty()) {
        return;
    }

    Rect target = dst;
    if (preserveAspect && texture.width() > 0 && texture.height() > 0) {
        const float texAspect = static_cast<float>(texture.width()) / static_cast<float>(texture.height());
        const float dstAspect = dst.w / dst.h;
        if (dstAspect > texAspect) {
            target.w = dst.h * texAspect;
            target.x += (dst.w - target.w) * 0.5f;
        } else {
            target.h = dst.w / texAspect;
            target.y += (dst.h - target.h) * 0.5f;
        }
    }
    drawTexture(texture, target, {}, tint);
}

void Renderer2D::drawNineSlice(const Texture& texture, const Rect& dst, float corner, Color tint)
{
    if (!texture.valid() || dst.isEmpty() || corner <= 0.0f) {
        drawTexture(texture, dst, {}, tint);
        return;
    }

    const float cw = std::min(corner, static_cast<float>(texture.width()) * 0.5f);
    const float ch = std::min(corner, static_cast<float>(texture.height()) * 0.5f);
    const float tw = static_cast<float>(texture.width());
    const float th = static_cast<float>(texture.height());

    const float midW = std::max(dst.w - cw * 2.0f, 0.0f);
    const float midH = std::max(dst.h - ch * 2.0f, 0.0f);
    const float srcMidW = std::max(tw - cw * 2.0f, 0.0f);
    const float srcMidH = std::max(th - ch * 2.0f, 0.0f);

    struct Cell { float sx, sy, sw, sh, dx, dy, dw, dh; };
    const Cell cells[9] = {
        {0,        0,        cw,     ch,     dst.x,                dst.y,                cw,     ch},
        {cw,       0,        srcMidW, ch,     dst.x + cw,           dst.y,                midW,   ch},
        {tw - cw,  0,        cw,     ch,     dst.x + cw + midW,     dst.y,                cw,     ch},
        {0,        ch,       cw,     srcMidH, dst.x,               dst.y + ch,           cw,     midH},
        {cw,       ch,       srcMidW, srcMidH, dst.x + cw,         dst.y + ch,           midW,   midH},
        {tw - cw,  ch,       cw,     srcMidH, dst.x + cw + midW,   dst.y + ch,           cw,     midH},
        {0,        th - ch,  cw,     ch,     dst.x,                dst.y + ch + midH,    cw,     ch},
        {cw,       th - ch,  srcMidW, ch,     dst.x + cw,           dst.y + ch + midH,    midW,   ch},
        {tw - cw,  th - ch,  cw,     ch,     dst.x + cw + midW,     dst.y + ch + midH,    cw,     ch},
    };

    for (const Cell& cell : cells) {
        if (cell.dw <= 0.0f || cell.dh <= 0.0f) {
            continue;
        }
        drawTexture(texture,
                    Rect{cell.dx, cell.dy, cell.dw, cell.dh},
                    Rect{cell.sx, cell.sy, cell.sw, cell.sh},
                    tint);
    }
}

void Renderer2D::drawCircle(const Vec2& center, float radius, Color color, int segments)
{
    if (m_renderer == nullptr || radius <= 0.0f) {
        return;
    }
    segments = std::clamp(segments, 3, 127);
    if (m_batching) {
        flushInternal();
    }
    ++m_drawCalls;

    // One triangle fan: a centre vertex followed by the rim, submitted as a
    // single geometry call so a circle costs one driver round trip.
    const SDL_FColor tint{static_cast<float>(color.r) / 255.0f,
                          static_cast<float>(color.g) / 255.0f,
                          static_cast<float>(color.b) / 255.0f,
                          static_cast<float>(color.a) / 255.0f};

    const Vec2 screenCentre = toScreen(center);
    const float screenRadius = radius * worldScale();

    std::array<SDL_Vertex, 128> ring{};
    ring[0] = SDL_Vertex{{screenCentre.x, screenCentre.y}, tint, {0.0f, 0.0f}};
    for (int i = 0; i < segments; ++i) {
        const float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 6.2831853f;
        ring[static_cast<std::size_t>(i) + 1] =
            SDL_Vertex{{screenCentre.x + std::cos(angle) * screenRadius,
                        screenCentre.y + std::sin(angle) * screenRadius}, tint, {0.0f, 0.0f}};
    }
    SDL_RenderGeometry(m_renderer, nullptr, ring.data(), segments + 1, nullptr, 0);
}

namespace {

/// Snaps a float rectangle to whole pixels so the clip is exact and the
/// intersection of nested clips never leaves a seam.
Rect toPixelRect(const Rect& r)
{
    return Rect{std::floor(r.x), std::floor(r.y), std::ceil(r.w), std::ceil(r.h)};
}

SDL_Rect toSDLRect(const Rect& r)
{
    return SDL_Rect{static_cast<int>(r.x), static_cast<int>(r.y),
                    static_cast<int>(r.w), static_cast<int>(r.h)};
}

} // namespace

void Renderer2D::pushClipInternal(const Rect& screenRect)
{
    if (m_renderer == nullptr) {
        return;
    }

    Rect pixelRect = toPixelRect(screenRect);
    if (m_hasClip) {
        // Intersect with the enclosing clip so nested regions behave.
        const Rect& current = m_clipStack.back();
        const float left   = std::max(pixelRect.left(), current.left());
        const float top    = std::max(pixelRect.top(), current.top());
        const float right  = std::min(pixelRect.right(), current.right());
        const float bottom = std::min(pixelRect.bottom(), current.bottom());
        pixelRect = Rect{left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
    }

    m_clipStack.push_back(pixelRect);
    const SDL_Rect sdl = toSDLRect(pixelRect);
    SDL_SetRenderClipRect(m_renderer, &sdl);
    m_hasClip = true;
}

void Renderer2D::setClipRect(const Rect* screenRect)
{
    if (m_renderer == nullptr) {
        return;
    }
    if (screenRect == nullptr || screenRect->isEmpty()) {
        SDL_SetRenderClipRect(m_renderer, nullptr);
        m_clipStack.clear();
        m_hasClip = false;
        return;
    }
    pushClipInternal(*screenRect);
}

// ---------------------------------------------------------------------------
// Batching
// ---------------------------------------------------------------------------
void Renderer2D::beginBatch()
{
    m_batching    = true;
    m_commands.clear();
    m_batchTextures.clear();
}

void Renderer2D::endBatch()
{
    if (!m_batching) {
        return;
    }
    flushInternal();
    m_batching = false;
}

std::size_t Renderer2D::registerTexture(const Texture* texture)
{
    if (texture == nullptr) {
        return 0;
    }
    const auto it = std::find(m_batchTextures.begin(), m_batchTextures.end(), texture);
    if (it != m_batchTextures.end()) {
        return static_cast<std::size_t>(std::distance(m_batchTextures.begin(), it));
    }
    m_batchTextures.push_back(texture);
    return m_batchTextures.size() - 1;
}

const Texture* Renderer2D::textureFor(std::size_t id) const
{
    return id < m_batchTextures.size() ? m_batchTextures[id] : nullptr;
}

void Renderer2D::clearBatchTextures()
{
    m_batchTextures.clear();
}

void Renderer2D::flushInternal()
{
    if (m_renderer == nullptr || m_commands.empty()) {
        m_commands.clear();
        return;
    }

    // Replay the recorded commands. Blend and clip changes are applied in
    // order; geometry is re-submitted through the same public entry points so
    // there is only one implementation of camera transform and counting.
    const bool savedBatching  = m_batching;
    const bool savedCamera   = m_cameraEnabled;
    const BlendMode savedBlend = m_blend;

    m_batching = false;

    for (const DrawCommand& cmd : m_commands) {
        switch (cmd.kind) {
            case DrawCommand::Kind::Clear:
                clear(cmd.color);
                break;
            case DrawCommand::Kind::SetBlend:
                setBlendMode(cmd.blend);
                break;
            case DrawCommand::Kind::PushClip:
                pushClipInternal(cmd.dst);
                break;
            case DrawCommand::Kind::PopClip:
                if (!m_clipStack.empty()) {
                    m_clipStack.pop_back();
                    if (m_clipStack.empty()) {
                        SDL_SetRenderClipRect(m_renderer, nullptr);
                        m_hasClip = false;
                    } else {
                        const SDL_Rect sdl = toSDLRect(m_clipStack.back());
                        SDL_SetRenderClipRect(m_renderer, &sdl);
                    }
                }
                break;
            case DrawCommand::Kind::FillRect:
                drawRect(cmd.dst, cmd.color, true);
                break;
            case DrawCommand::Kind::Line:
                drawLine(cmd.dst.position(),
                         Vec2{cmd.dst.x + cmd.dst.w, cmd.dst.y + cmd.dst.h},
                         cmd.color);
                break;
            case DrawCommand::Kind::TexturedQuad: {
                const Texture* texture = textureFor(cmd.textureId);
                if (texture != nullptr && texture->valid()) {
                    drawTexture(*texture, cmd.dst, cmd.src, cmd.tint, cmd.rotation,
                                cmd.flipX, cmd.flipY);
                }
                break;
            }
            case DrawCommand::Kind::SetCamera:
                m_cameraEnabled = cmd.flag;
                break;
        }
    }

    m_commands.clear();
    m_batching     = savedBatching;
    m_cameraEnabled = savedCamera;
    m_blend        = savedBlend;
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
}

} // namespace EraShift::Graphics
