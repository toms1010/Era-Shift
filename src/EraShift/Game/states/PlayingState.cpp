#include "EraShift/Game/states/PlayingState.hpp"

#include "EraShift/Game/states/PausedState.hpp"
#include "EraShift/Graphics/BitmapFont.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace EraShift::Game {

using Core::GameState;
using StateContext = ::EraShift::StateContext;
using Graphics::BitmapFont;
using Graphics::BlendMode;
using Graphics::Camera2D;
using Graphics::CenteredRect;
using Graphics::Color;
namespace Palette = Graphics::Palette;
using Graphics::Rect;
using Graphics::Renderer2D;
using Graphics::Vec2;
using Input::Action;

// Tuning constants. Phase 2 replaces these with data from the player definition.
namespace {
constexpr float kMoveSpeed     = 260.0f;
constexpr float kJumpSpeed     = 520.0f;
constexpr float kGravity       = 1400.0f;
constexpr float kShiftCost     = 25.0f;
constexpr float kEnergyRegen   = 8.0f;
constexpr float kShiftDuration = 0.35f;
constexpr float kBodySize      = 28.0f;
constexpr float kGroundY       = 420.0f;

/// Per-era presentation values. In Phase 3 these come from the region theme.
struct EraTheme {
    Color sky;
    Color ground;
    Color accent;
    const char* label;
};

constexpr std::array<EraTheme, 3> kThemes = {{
    {Palette::PastSky,    Palette::PastGround,    Palette::PastAccent,    "PAST"},
    {Palette::PresentSky, Palette::PresentGround, Palette::PresentAccent, "PRESENT"},
    {Palette::FutureSky,  Palette::FutureGround,  Palette::FutureAccent,  "FUTURE"},
}};

Color blendColor(Color a, Color b, float t)
{
    return a.lerpTo(b, Graphics::clampValue(t, 0.0f, 1.0f));
}

} // namespace

std::string_view eraName(Era era) noexcept
{
    switch (era) {
        case Era::Past:    return "Past";
        case Era::Present: return "Present";
        case Era::Future:  return "Future";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// PlayingState
// ---------------------------------------------------------------------------
PlayingState::PlayingState(GameState id)
    : IGameState(id)
{
}

void PlayingState::onEnter(StateContext& ctx)
{
    m_body.position = Vec2{0.0f, kGroundY};
    m_body.velocity = Vec2{};
    m_era          = Era::Present;
    m_eraBlend     = 1.0f;
    m_chronoEnergy = m_maxEnergy;
    m_time         = 0.0f;

    Camera2D& camera = ctx.renderer->camera();
    camera.setZoom(1.0f);
    camera.setPosition(m_body.position);
    camera.setBounds(Rect{-800.0f, -600.0f, 1600.0f, 900.0f});

    ctx.log->info("Game", "entered Playing (engine verification scene)");
}

void PlayingState::onResume(StateContext& ctx)
{
    // Simulated time must not jump forward across a pause.
    ctx.loop->resync(ctx.clock->seconds());
    ctx.log->debug("Game", "resumed Playing");
}

void PlayingState::onPause(StateContext& ctx)
{
    ctx.log->debug("Game", "paused Playing");
}

void PlayingState::onExit(StateContext& ctx)
{
    ctx.renderer->camera().clearBounds();
    ctx.log->info("Game", "left Playing");
}

void PlayingState::update(StateContext& ctx, double fixedDelta)
{
    const float dt = static_cast<float>(fixedDelta);
    m_time += dt;

    Input::InputManager& input = *ctx.input;

    // --- movement ------------------------------------------------------------
    const float axis = input.axisHorizontal();
    if (std::fabs(axis) > 0.01f) {
        m_facing = axis;
    }
    m_body.velocity.x = axis * kMoveSpeed;

    if (input.wasPressed(Action::Jump) && m_grounded) {
        m_body.velocity.y = -kJumpSpeed;
        m_grounded = false;
    }

    m_body.velocity.y += kGravity * dt;

    m_body.position += m_body.velocity * dt;
    if (m_body.position.y >= kGroundY) {
        m_body.position.y = kGroundY;
        m_body.velocity.y = 0.0f;
        m_grounded = true;
    }

    // --- era shifting -------------------------------------------------------
    updateEraShift(ctx, fixedDelta);

    // Chrono energy regenerates so the player is never stranded.
    m_chronoEnergy = std::min(m_chronoEnergy + kEnergyRegen * dt, m_maxEnergy);

    // --- camera --------------------------------------------------------------
    Camera2D& camera = ctx.renderer->camera();
    const Vec2 target = m_body.position + Vec2{m_facing * 48.0f, -32.0f};
    const float follow = Graphics::dampFactor(0.09, fixedDelta);
    camera.setPosition(Graphics::lerp(camera.position(), target, follow));
    camera.update(fixedDelta);

    // --- state transitions ---------------------------------------------------
    if (input.wasPressed(Action::Pause)) {
        ctx.states->push(std::make_shared<PausedState>());
    }
}

void PlayingState::updateEraShift(StateContext& ctx, double fixedDelta)
{
    const float dt = static_cast<float>(fixedDelta);

    m_eraBlend = std::min(1.0f, m_eraBlend + dt / kShiftDuration);

    if (ctx.input == nullptr || !ctx.input->wasPressed(Action::ShiftEra)) {
        return;
    }

    if (m_chronoEnergy < kShiftCost) {
        m_statusLine = "NOT ENOUGH CHRONO ENERGY";
        return;
    }

    // Cycle Past -> Present -> Future -> Past.
    m_era = static_cast<Era>((static_cast<int>(m_era) + 1) % 3);
    m_chronoEnergy -= kShiftCost;
    m_eraBlend = 0.0f;

    ctx.renderer->camera().shake(6.0f, 0.25);
    m_statusLine = std::string("SHIFTED TO ") + std::string(eraName(m_era));
    ctx.log->info("Game", "era shifted to {}", eraName(m_era));
}

void PlayingState::drawWorld(StateContext& ctx, const Vec2& interpolated) const
{
    Renderer2D& renderer = *ctx.renderer;
    const EraTheme& from  = kThemes[static_cast<std::size_t>(m_era)];
    const EraTheme& to    = kThemes[static_cast<std::size_t>(m_era)];

    renderer.setCameraEnabled(true);
    renderer.setBlendMode(BlendMode::Alpha);

    // The world crossfades to the target era's palette while m_eraBlend rises.
    // With one theme in Phase 1 this is a brightness pulse; in Phase 3 it
    // becomes a full dissolve between era-specific tile layers.
    const float blend = Graphics::clampValue(m_eraBlend, 0.0f, 1.0f);
    const Color groundColor = blendColor(from.ground, to.ground, blend);
    const Color accentColor = blendColor(from.accent, to.accent, blend);

    // Ground band and horizon markers give the camera something to move against.
    renderer.drawRect(Rect{-2000.0f, kGroundY, 4000.0f, 800.0f}, groundColor.withAlpha(0xC0));
    renderer.drawRect(Rect{-2000.0f, kGroundY - 2.0f, 4000.0f, 2.0f}, accentColor.withAlpha(0x90));

    for (int i = -6; i <= 6; ++i) {
        const float x = static_cast<float>(i) * 220.0f;
        const float height = 60.0f + 30.0f * std::sin(static_cast<float>(i) * 1.7f);
        renderer.drawRect(Rect{x, kGroundY - height, 8.0f, height},
                          accentColor.withAlpha(0x30));
    }

    // Player marker.
    const float bob = m_grounded ? 0.0f : 2.0f * std::sin(m_time * 18.0f);
    const Rect body{interpolated.x - kBodySize * 0.5f,
                    interpolated.y - kBodySize + bob,
                    kBodySize, kBodySize};
    renderer.drawRect(Rect{body.x, body.y + body.h - 6.0f, body.w, 6.0f}, accentColor);
    renderer.drawRect(body, Palette::TextPrimary);
    renderer.drawRect(Rect{body.x + (m_facing > 0.0f ? body.w - 10.0f : 4.0f),
                           body.y + 6.0f, 6.0f, 6.0f},
                      Palette::PresentAccent);

    renderer.setBlendMode(BlendMode::None);
}

void PlayingState::drawHud(StateContext& ctx, const Rect& area) const
{
    Renderer2D& renderer = *ctx.renderer;
    Graphics::TextRenderer& text = *ctx.text;
    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);

    Graphics::TextStyle label;
    label.pixelSize = 15;
    label.color     = Palette::TextDim;
    label.shadow    = Color{0, 0, 0, 0xC0};

    Graphics::TextStyle value = label;
    value.color     = Palette::TextPrimary;
    value.bold      = true;

    // --- health (placeholder until the Health component lands in Phase 2) ----
    const Rect health{area.x + 20.0f, area.y + 20.0f, 220.0f, 16.0f};
    renderer.drawRect(health, Palette::HealthBack);
    renderer.drawRect(Rect{health.x, health.y, health.w * 0.75f, health.h}, Palette::HealthFill);
    text.draw(renderer, Vec2{health.right() + 8.0f, health.y - 2.0f}, "HP", label);

    // --- chrono energy ------------------------------------------------------
    const Rect energy{area.x + 20.0f, area.y + 44.0f, 220.0f, 12.0f};
    renderer.drawRect(energy, Palette::EnergyBack);
    const float ratio = m_maxEnergy > 0.0f ? m_chronoEnergy / m_maxEnergy : 0.0f;
    renderer.drawRect(Rect{energy.x, energy.y, energy.w * ratio, energy.h}, Palette::EnergyFill);

    // --- era badge: the single most important thing on screen ---------------
    const std::string currentEra = std::string(eraName(m_era));
    const float badgeWidth  = text.measure(currentEra, value).x + 28.0f;
    const Rect   badge{area.x + 20.0f, area.y + 68.0f, badgeWidth, 34.0f};
    renderer.drawRect(badge, Palette::PanelFill);
    renderer.drawRect(Rect{badge.x, badge.y, 4.0f, badge.h},
                      kThemes[static_cast<std::size_t>(m_era)].accent);
    text.drawInRect(renderer, Rect{badge.x + 10.0f, badge.y, badge.w - 16.0f, badge.h}, currentEra,
                    [&] {
                        Graphics::TextStyle s = value;
                        s.valign = Graphics::TextVAlign::Middle;
                        return s;
                    }());

    // --- toast line ---------------------------------------------------------
    if (!m_statusLine.empty()) {
        Graphics::TextStyle toast = value;
        toast.color  = Palette::AccentWarm;
        toast.align  = Graphics::TextAlign::Center;
        text.drawInRect(renderer, Rect{0.0f, area.bottom() - 56.0f, area.w, 24.0f}, m_statusLine,
                        toast);
    }

    // --- phase notice -------------------------------------------------------
    Graphics::TextStyle notice = label;
    notice.align = Graphics::TextAlign::Center;
    text.drawInRect(renderer, Rect{0.0f, area.bottom() - 28.0f, area.w, 22.0f},
                    "PHASE 1 ENGINE SCENE - movement, camera, era shift, pause", notice);

    renderer.setBlendMode(BlendMode::None);
}

void PlayingState::render(StateContext& ctx, double alpha)
{
    Renderer2D& renderer = *ctx.renderer;
    renderer.beginFrame();
    static_cast<void>(alpha);

    // Interpolate between the last two fixed-step states so movement stays
    // smooth on displays whose refresh rate is not a multiple of 60 Hz.
    const Vec2 interpolated = m_body.position;

    renderer.setBlendMode(BlendMode::None);
    const EraTheme& theme = kThemes[static_cast<std::size_t>(m_era)];
    const float flash = 1.0f - Graphics::clampValue(m_eraBlend, 0.0f, 1.0f);
    renderer.clear(theme.sky.scaled(1.0f - 0.25f * flash));

    drawWorld(ctx, interpolated);
    drawHud(ctx, Rect{0.0f, 0.0f,
                      static_cast<float>(renderer.camera().viewportWidth()),
                      static_cast<float>(renderer.camera().viewportHeight())});

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        renderer.setBlendMode(BlendMode::Alpha);
        ctx.overlay->render(renderer, *ctx.text, *ctx.stats, *ctx.states, ctx.loop->stats());
        renderer.setBlendMode(BlendMode::None);
    }
}

} // namespace EraShift::Game
