#include "EraShift/Game/Feedback.hpp"

#include "EraShift/Audio/AudioManager.hpp"
#include "EraShift/Audio/Synth.hpp"
#include "EraShift/Core/Log.hpp"
#include "EraShift/Game/World.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <algorithm>
#include <cmath>

namespace EraShift::Game {

using Audio::Sfx;
using Graphics::BlendMode;
using Graphics::Bounds;
using Graphics::Camera2D;
using Graphics::Color;
using Graphics::ParticleShape;
using Graphics::Rect;
using Graphics::Renderer2D;
using Graphics::Vec2;
using Input::Action;

namespace {

/// Tier thresholds, in paradox points. Chosen so a careful run stays in Calm,
/// ordinary play reaches Fractured, and Collapse means the player has been
/// shifting carelessly for a while.
constexpr float kTierStrained  = 18.0f;
constexpr float kTierFractured = 38.0f;
constexpr float kTierCollapse  = 62.0f;

/// How much paradox the screen is allowed to distort. Even at Collapse the world
/// is still readable, because a game you cannot see is not tense, it is broken.
constexpr float kMaxGlitch = 0.55f;

/// How a surface sounds underfoot.
///
/// The world has no material field - `TileKind` describes collision behaviour
/// per era, not what a floor is made of - so the surface is *derived* from the
/// kind. That is a deliberate limit: a separate material system would be a much
/// larger change to the level format for a cosmetic gain, and the kinds that
/// exist already separate the cases that matter audibly.
///
/// Loudness is part of the profile rather than a constant because a footstep on
/// sand should sit under the mix, and one on metal should cut through it.
struct SurfaceAudioProfile {
    Sfx   sfx;
    float volume;
    float pitch;
};

SurfaceAudioProfile surfaceProfile(TileKind kind) noexcept
{
    switch (kind) {
        // The ruins themselves: hard, bright, unremarkable.
        case TileKind::Solid:
        case TileKind::Goal:
            return {Sfx::FootstepStone, 0.22f, 1.00f};
        // Timber decking and the ruined span: a hollow, woody knock.
        case TileKind::Platform:
        case TileKind::Bridge:
            return {Sfx::FootstepStone, 0.20f, 0.78f};
        // Rotting masonry: duller than solid stone and quieter underfoot.
        case TileKind::Crumble:
            return {Sfx::FootstepGrass, 0.18f, 0.90f};
        // Grown crystal: bright, glassy, and the loudest thing you can stand on.
        case TileKind::Crystal:
            return {Sfx::FootstepCrystal, 0.24f, 1.05f};
        // Loose regolith: the quietest, dullest footstep there is.
        case TileKind::Hazard:
        case TileKind::Empty:
            return {Sfx::FootstepSand, 0.16f, 1.00f};
    }
    return {Sfx::FootstepStone, 0.20f, 1.0f};
}

float easeOut(float t) noexcept
{
    t = Graphics::clampValue(t, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse;
}

} // namespace

float tierThreshold(ParadoxTier tier) noexcept
{
    switch (tier) {
        case ParadoxTier::Calm:     return 0.0f;
        case ParadoxTier::Strained: return kTierStrained;
        case ParadoxTier::Fractured:return kTierFractured;
        case ParadoxTier::Collapse: return kTierCollapse;
    }
    return 0.0f;
}

ParadoxTier tierForParadox(float paradox) noexcept
{
    if (paradox >= kTierCollapse) {
        return ParadoxTier::Collapse;
    }
    if (paradox >= kTierFractured) {
        return ParadoxTier::Fractured;
    }
    if (paradox >= kTierStrained) {
        return ParadoxTier::Strained;
    }
    return ParadoxTier::Calm;
}

std::string_view toString(ParadoxTier tier) noexcept
{
    switch (tier) {
        case ParadoxTier::Calm:     return "STABLE";
        case ParadoxTier::Strained: return "STRAINED";
        case ParadoxTier::Fractured:return "FRACTURED";
        case ParadoxTier::Collapse: return "COLLAPSE";
    }
    return "?";
}

void FeedbackSystem::reset() noexcept
{
    m_particles.clear();
    m_glows.clear();
    m_glitchBars.clear();
    m_tension = 0.0f;
    m_tier = ParadoxTier::Calm;
    m_flash = 0.0f;
    m_charge = 0.0f;
    m_glitchTimer = 0.0f;
    m_hudPulse = 0.0f;
    m_ripple = ScreenEffect{};
}

void FeedbackSystem::triggerScreenEffect(ScreenEffect& effect, float duration, Color color,
                                         float strength)
{
    if (duration <= 0.0f) {
        return;
    }
    effect.progress = 0.0f;
    effect.duration = duration;
    effect.color = color;
    effect.strength = Graphics::clampValue(strength, 0.0f, 1.0f);
    effect.active = true;
}

void FeedbackSystem::emitGlow(float x, float y, float radius, Color color, float life, float peak)
{
    // Bounded, oldest-out. Glows are additive overlays, so a leak here is a
    // slowly brightening screen rather than a crash, which is still a bug worth
    // preventing by construction.
    constexpr std::size_t kMaxGlows = 48;
    if (m_glows.size() >= kMaxGlows) {
        m_glows.erase(m_glows.begin());
    }
    Glow glow;
    glow.x = x;
    glow.y = y;
    glow.radius = radius;
    glow.life = life;
    glow.maxLife = life;
    glow.peak = peak;
    glow.color = color;
    m_glows.push_back(glow);
}

void FeedbackSystem::consume(const std::vector<EventRecord>& events, const World& world,
                             StateContext& ctx)
{
    Audio::AudioManager* audio = ctx.audio;
    Camera2D* camera = (ctx.renderer != nullptr) ? &ctx.renderer->camera() : nullptr;

    const EraTheme theme = blendThemes(themeFor(world.previousEra()), themeFor(world.era()),
                                       Graphics::clampValue(world.eraBlend(), 0.0f, 1.0f));
    const Color accent = theme.accent;
    const Vec2  playerCentre = world.player().body().center();

    for (const EventRecord& event : events) {
        switch (event.kind) {
            case WorldEvent::EraShifted: {
                // The shift is the loudest thing in the game, and it is loud in
                // three channels at once: a charge, an impact, and the world's
                // own particles blowing outward.
                if (audio != nullptr) {
                    audio->play(Sfx::ShiftImpact, 1.0f);
                }
                m_particles.emitShiftBurst(event.position.x, event.position.y, accent);
                emitGlow(event.position.x, event.position.y, 220.0f, Graphics::Palette::White, 0.5f, 0.9f);
                emitGlow(event.position.x, event.position.y, 320.0f, accent, 0.9f, 0.6f);
                triggerScreenEffect(m_ripple, 0.7f, accent, 1.0f);
                m_flash = 1.0f;
                m_hudPulse = 1.0f;
                if (camera != nullptr) {
                    camera->punch(0.035f, 0.35);
                    camera->shake(9.0f, 0.3);
                }
                break;
            }

            case WorldEvent::EraShiftFailed:
                if (audio != nullptr) {
                    audio->play(Sfx::UiDeny, 0.7f);
                }
                break;

            case WorldEvent::SealTaken: {
                if (audio != nullptr) {
                    audio->play(Sfx::SealTaken, 1.0f);
                }
                m_particles.emitSeal(event.position.x, event.position.y, accent);
                emitGlow(event.position.x, event.position.y, 180.0f, accent, 1.1f, 0.85f);
                m_flash = std::max(m_flash, 0.55f);
                if (camera != nullptr) {
                    camera->punch(0.02f, 0.3);
                }
                break;
            }

            case WorldEvent::SealWrongEra:
                if (audio != nullptr) {
                    audio->play(Sfx::UiDeny, 0.55f, 0.8f);
                }
                break;

            case WorldEvent::GateOpened:
                if (audio != nullptr) {
                    audio->play(Sfx::GateOpened, 1.0f);
                }
                m_particles.emitShiftBurst(event.position.x, event.position.y, accent);
                emitGlow(event.position.x, event.position.y, 260.0f, accent, 1.4f, 0.8f);
                if (camera != nullptr) {
                    camera->punch(0.018f, 0.5);
                }
                break;

            case WorldEvent::PickupTaken: {
                if (audio != nullptr) {
                    const bool health = event.text.find("VITAL") != std::string::npos;
                    audio->play(health ? Sfx::PickupHealth : Sfx::PickupChrono, 0.85f);
                }
                m_particles.emitPickup(event.position.x, event.position.y, accent);
                emitGlow(event.position.x, event.position.y, 60.0f, accent, 0.4f, 0.5f);
                break;
            }

            case WorldEvent::EnemyKilled: {
                if (audio != nullptr) {
                    audio->play(Sfx::EnemyDie, 0.9f);
                }
                m_particles.emitDeath(event.position.x, event.position.y, accent);
                m_particles.emitImpact(event.position.x, event.position.y, event.direction.x,
                                       event.direction.y, Graphics::Palette::White, 1.4f, 20);
                emitGlow(event.position.x, event.position.y, 120.0f, accent, 0.6f, 0.7f);
                if (camera != nullptr) {
                    camera->punch(0.02f, 0.25);
                    camera->shake(6.0f, 0.2);
                }
                break;
            }

            case WorldEvent::PlayerHurt: {
                if (audio != nullptr) {
                    audio->play(Sfx::PlayerHurt, 0.95f);
                }
                m_particles.emitImpact(playerCentre.x, playerCentre.y, -event.direction.x,
                                       event.direction.y, Graphics::Palette::Warning, 1.2f, 16);
                emitGlow(playerCentre.x, playerCentre.y, 90.0f, Graphics::Palette::Warning, 0.45f, 0.8f);
                if (camera != nullptr) {
                    camera->punch(0.03f, 0.25);
                    camera->shake(9.0f, 0.24);
                }
                break;
            }

            case WorldEvent::PlayerDied:
                if (audio != nullptr) {
                    audio->play(Sfx::PlayerDie, 1.0f);
                }
                m_particles.emitDeath(playerCentre.x, playerCentre.y, Graphics::Palette::Warning);
                emitGlow(playerCentre.x, playerCentre.y, 200.0f, Graphics::Palette::Warning, 1.2f, 0.9f);
                if (camera != nullptr) {
                    camera->punch(0.04f, 0.6);
                    camera->shake(12.0f, 0.5);
                }
                m_flash = 1.0f;
                break;

            case WorldEvent::Victory:
                if (audio != nullptr) {
                    audio->play(Sfx::Victory, 1.0f);
                }
                m_particles.emitSeal(event.position.x, event.position.y, accent);
                m_flash = 1.0f;
                break;

            // --- presentation events -----------------------------------------
            case WorldEvent::PlayerJumped:
                if (audio != nullptr) {
                    audio->play(Sfx::Jump, 0.7f);
                }
                m_particles.emitFootfall(event.position.x, event.position.y + 18.0f, 0.6f, accent);
                break;

            case WorldEvent::PlayerLanded: {
                if (audio != nullptr) {
                    // Pitch and level follow the impact speed, so stepping off a
                    // ledge is quiet and dropping from height is not.
                    audio->play(Sfx::Land, Graphics::clampValue(event.strength * 0.8f, 0.2f, 1.0f),
                                Graphics::clampValue(1.2f - event.strength * 0.25f, 0.7f, 1.25f));
                }
                m_particles.emitFootfall(event.position.x, event.position.y + 20.0f,
                                         Graphics::clampValue(event.strength, 0.3f, 1.6f), accent);
                if (event.strength > 0.7f) {
                    emitGlow(event.position.x, event.position.y + 20.0f, 40.0f, accent, 0.3f,
                             event.strength * 0.4f);
                    if (camera != nullptr) {
                        camera->shake(2.5f * event.strength, 0.12);
                    }
                }
                break;
            }

            case WorldEvent::PlayerDashed:
                if (audio != nullptr) {
                    audio->play(Sfx::Dash, 0.8f);
                }
                m_particles.emitDashTrail(event.position.x, event.position.y, event.direction.x,
                                          accent);
                if (camera != nullptr) {
                    camera->punch(0.012f, 0.16);
                }
                break;

            case WorldEvent::SwingStarted:
                // The whoosh of the swing itself, before it can hurt anything.
                if (audio != nullptr) {
                    audio->play(Sfx::Attack, 0.65f);
                }
                break;

            case WorldEvent::SwingActive:
                // A thin arc along the hit box: this is the frame the player is
                // dangerous, and the picture says so.
                m_particles.emitImpact(event.position.x, event.position.y, event.direction.x,
                                       event.direction.y, accent, 0.35f, 4);
                break;

            case WorldEvent::EnemyHurt:
                if (audio != nullptr) {
                    audio->play(Sfx::HitEnemy, 0.9f);
                }
                m_particles.emitImpact(event.position.x, event.position.y, event.direction.x,
                                       event.direction.y, Graphics::Palette::White, 1.0f, 14);
                emitGlow(event.position.x, event.position.y, 46.0f, Graphics::Palette::White, 0.22f, 0.6f);
                if (camera != nullptr) {
                    camera->punch(0.012f, 0.14);
                }
                break;

            case WorldEvent::PlayerFootstep:
                if (audio != nullptr) {
                    // Speed still colours the step, but the surface decides which
                    // sound it is. Previously this was `Sfx::Land` with a pitch
                    // curve, which meant every surface in the level made the
                    // identical noise and the player had no way to hear what they
                    // were walking on.
                    const SurfaceAudioProfile profile = surfaceProfile(event.surface);
                    const float speedPitch =
                        1.0f - Graphics::clampValue(event.strength, 0.0f, 1.4f) * 0.12f;
                    audio->play(profile.sfx, profile.volume * event.strength,
                                Graphics::clampValue(profile.pitch * speedPitch, 0.5f, 2.0f));
                }
                m_particles.emitFootfall(event.position.x, event.position.y + 20.0f,
                                         event.strength * 0.35f, accent);
                break;

            case WorldEvent::HazardTick:
                if (audio != nullptr) {
                    audio->play(Sfx::Hazard, 0.8f);
                }
                m_particles.emitImpact(event.position.x, event.position.y, 0.0f, -1.0f,
                                       theme.hazard, 1.1f, 16);
                break;

            case WorldEvent::GateSealed:
                if (audio != nullptr) {
                    audio->play(Sfx::UiDeny, 0.6f);
                }
                break;
        }
    }
}

void FeedbackSystem::update(StateContext& ctx, double deltaSeconds, const World& world)
{
    const float dt = static_cast<float>(deltaSeconds);

    // --- paradox -> tension -------------------------------------------------
    // Tension is the paradox ratio, not the raw number, and it is eased so
    // crossing a tier boundary is a slide rather than a step.
    const float ratio = Graphics::clampValue(world.stats().paradox / 100.0f, 0.0f, 1.0f);
    m_tension += (ratio - m_tension) * std::min(1.0f, dt * 1.5f);
    m_tier = tierForParadox(world.stats().paradox);
    if (m_tier != m_shownTier) {
        m_shownTier = m_tier;
        m_hudPulse = 1.0f;
    }


    // --- particles ----------------------------------------------------------
    m_particles.update(dt);

    // Era weather over the visible area, so a room is never still even when
    // nothing is happening. Intensity rises with paradox: the air itself gets
    // less stable.
    m_playerCentre = world.player().body().center();
    m_eraAccent    = themeFor(world.era()).accent;
    if (ctx.renderer != nullptr) {
        const Rect view = ctx.renderer->camera().viewportBounds();
        m_ambientBounds = Bounds{view.left() - 32.0f, view.top() - 32.0f, view.right() + 32.0f,
                                 view.bottom() + 32.0f};
        m_particles.emitAmbient(world.era(), m_ambientBounds, dt, m_tension);
    }

    // The shift's charge aura. Driven from whether the player is actually
    // charging, so holding the key builds the aura and letting go lets it go.
    const bool charging = ctx.input != nullptr && ctx.input->isDown(Action::ShiftEra) &&
                          world.player().chrono() > 0.0f;
    m_charge += ((charging ? 1.0f : 0.0f) - m_charge) * std::min(1.0f, dt * 6.0f);
    if (m_charge > 0.05f) {
        m_particles.emitShiftWave(m_playerCentre.x, m_playerCentre.y, 1.0f - m_charge,
                                  m_eraAccent, Graphics::Palette::White, dt * m_charge);
    }

    // --- glows --------------------------------------------------------------
    for (Glow& glow : m_glows) {
        glow.life -= dt;
    }
    m_glows.erase(std::remove_if(m_glows.begin(), m_glows.end(),
                                 [](const Glow& g) { return g.life <= 0.0f; }),
                  m_glows.end());

    // --- screen effects -----------------------------------------------------
    m_flash    = std::max(0.0f, m_flash - dt * kFlashDecay);
    m_hudPulse = std::max(0.0f, m_hudPulse - dt * 2.0f);
    updateScreenEffect(m_ripple, dt);

    // --- glitch -------------------------------------------------------------
    // Bars are regenerated several times a second rather than every frame. A
    // glitch that changes every frame is noise; one that changes every 160ms is
    // an event, and the eye reads that as the picture tearing.
    m_glitchTimer -= dt;
    if (m_tension > 0.45f) {
        if (m_glitchTimer <= 0.0f) {
            m_glitchTimer = 0.16f - m_tension * 0.10f;
            m_glitchBars.clear();
            const int bars = static_cast<int>(m_tension * 4.0f);
            for (int i = 0; i < bars; ++i) {
                const float y = m_particles.nextRandom();
                const float h = 3.0f + std::fabs(m_particles.nextRandom()) * 14.0f;
                const float offset = m_particles.nextRandom() * 60.0f * m_tension;
                m_glitchBars.push_back(
                    Rect{offset, y * ctx.renderer->viewportRect().h, ctx.renderer->viewportRect().w, h});
            }
            if (m_tension > 0.7f && ctx.renderer != nullptr) {
                const Rect view = ctx.renderer->camera().viewportBounds();
                m_particles.emitGlitch(view.center().x, view.center().y, view.w,
                                       m_tension * kMaxGlitch);
            }
        }
    } else {
        m_glitchTimer = 0.0f;
        m_glitchBars.clear();
    }

    // --- audio --------------------------------------------------------------
    // The bed follows the threat, not the paradox: a nearby enemy means combat
    // music even at zero paradox, which is what makes a tense room sound tense
    // before anything has actually gone wrong.
    if (ctx.audio != nullptr) {
        ctx.audio->setTension(m_tension);
        float nearest = -1.0f;
        for (const Enemy& enemy : world.enemies()) {
            if (!enemy.inCurrentEra() || enemy.state() == EnemyState::Dying) {
                continue;
            }
            const float distance = (enemy.body().center() - m_playerCentre).length();
            if (nearest < 0.0f || distance < nearest) {
                nearest = distance;
            }
        }
        if (world.outcome() != Outcome::Running) {
            ctx.audio->setMusicState(Audio::MusicState::Still);
        } else if (nearest < 0.0f) {
            ctx.audio->setMusicState(Audio::MusicState::Exploring);
        } else if (nearest < 200.0f) {
            ctx.audio->setMusicState(Audio::MusicState::Combat);
        } else {
            ctx.audio->setMusicState(Audio::MusicState::Alert);
        }
        ctx.audio->setEra(world.era());
    }
}

void FeedbackSystem::updateScreenEffect(ScreenEffect& effect, double dt)
{
    if (!effect.active) {
        return;
    }
    effect.progress += static_cast<float>(dt) / std::max(0.001f, effect.duration);
    if (effect.progress >= 1.0f) {
        effect.active = false;
        effect.progress = 1.0f;
    }
}

void FeedbackSystem::drawWorld(StateContext& ctx) const
{
    if (ctx.renderer == nullptr) {
        return;
    }
    Renderer2D& renderer = *ctx.renderer;
    renderer.setCameraEnabled(true);

    drawGlows(ctx);
    drawShiftAura(ctx);

    // --- particles ----------------------------------------------------------
    // One pass per shape family, so the blend mode changes four times for the
    // whole system instead of once per particle. A few hundred blend-mode
    // changes is the difference between a busy screen and a slow one.
    static constexpr ParticleShape kPasses[4] = {
        ParticleShape::Filled, ParticleShape::Spark, ParticleShape::Ghost, ParticleShape::Ring};

    for (const ParticleShape wanted : kPasses) {
        // Sparks and glows are light, so they are added rather than mixed. Filled
        // debris and rings are geometry, so they blend normally.
        const bool additive = (wanted == ParticleShape::Spark) || (wanted == ParticleShape::Ghost);
        renderer.setBlendMode(additive ? BlendMode::Additive : BlendMode::Alpha);

        const Graphics::Particle* particles = m_particles.data();
        for (std::size_t i = 0; i < m_particles.size(); ++i) {
            const Graphics::Particle& p = particles[i];
            if (!p.alive || p.shape != wanted || p.life <= 0.0f) {
                continue;
            }
            // 1 at birth, 0 at death. Applied to the alpha, so a particle fades
            // out rather than shrinking into a single frame of nothing.
            const float life01 =
                Graphics::clampValue(p.life / std::max(0.001f, p.maxLife), 0.0f, 1.0f);
            const float size = p.size + (p.endSize - p.size) * (1.0f - life01);
            if (size <= 0.05f) {
                continue;
            }
            const Color color = p.color.lerpTo(p.endColor, 1.0f - life01);
            const std::uint8_t alpha = static_cast<std::uint8_t>(
                Graphics::clampValue(static_cast<float>(color.a) * life01, 0.0f, 255.0f));
            if (alpha == 0) {
                continue;
            }
            const Color shaded = color.withAlpha(alpha);

            if (p.shape == ParticleShape::Ring) {
                // A ring, faked with four rectangles, which is all a rectangle
                // renderer has. Drawn hollow so a shockwave reads as a wavefront.
                const float t = std::max(1.0f, size * 0.10f);
                const Rect box{p.x - size * 0.5f, p.y - size * 0.5f, size, size};
                renderer.drawRect(Rect{box.x, box.y, box.w, t}, shaded);
                renderer.drawRect(Rect{box.x, box.bottom() - t, box.w, t}, shaded);
                renderer.drawRect(Rect{box.x, box.y, t, box.h}, shaded);
                renderer.drawRect(Rect{box.right() - t, box.y, t, box.h}, shaded);
                continue;
            }

            if (p.shape == ParticleShape::Spark) {
                // Stretched along travel and rotated to match, so a spark looks
                // like it is going somewhere rather than like it is blinking.
                const float length = size * p.stretch;
                const Rect box{p.x - length * 0.5f, p.y - size * 0.5f, length, size};
                renderer.drawRectRotated(box, p.rotation, shaded);
                continue;
            }

            const Rect box{p.x - size * 0.5f, p.y - size * 0.5f, size, size};
            if (p.rotation != 0.0f) {
                renderer.drawRectRotated(box, p.rotation, shaded);
            } else {
                renderer.drawRect(box, shaded);
            }
        }
    }
    renderer.setBlendMode(BlendMode::None);
}

void FeedbackSystem::drawGlows(const StateContext& ctx) const
{
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Additive);
    renderer.setCameraEnabled(true);
    for (const Glow& glow : m_glows) {
        const float life01 = Graphics::clampValue(glow.life / std::max(0.001f, glow.maxLife), 0.0f, 1.0f);
        // Grows and fades: a hit's light spreads as it dies.
        const float radius = glow.radius * (1.4f - 0.4f * life01);
        const std::uint8_t alpha = static_cast<std::uint8_t>(
            Graphics::clampValue(glow.peak * life01 * 120.0f, 0.0f, 255.0f));
        if (alpha < 3 || radius <= 1.0f) {
            continue;
        }
        // Three nested squares at falling alpha. A radial gradient would be the
        // right tool; this is what a rectangle renderer can honestly fake.
        for (int layer = 0; layer < 3; ++layer) {
            const float scale = 1.0f - static_cast<float>(layer) * 0.28f;
            const float layerAlpha = static_cast<std::uint8_t>(alpha / (layer + 2));
            const Rect box{glow.x - radius * scale * 0.5f, glow.y - radius * scale * 0.5f,
                           radius * scale, radius * scale};
            renderer.drawRect(box, glow.color.withAlpha(layerAlpha));
        }
    }
    renderer.setBlendMode(BlendMode::None);
}

void FeedbackSystem::drawRipple(const StateContext& ctx) const
{
    if (!m_ripple.active || ctx.renderer == nullptr) {
        return;
    }
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Additive);
    renderer.setCameraEnabled(false);

    const Rect area = ctx.renderer->viewportRect();
    const float eased = easeOut(m_ripple.progress);

    // Expanding rings from the centre. The distortion is a ring, not a wobble,
    // because a shift is a wavefront and a wobble is a mistake.
    for (int ring = 0; ring < 3; ++ring) {
        const float phase = Graphics::clampValue(eased * 1.4f - static_cast<float>(ring) * 0.18f, 0.0f, 1.0f);
        if (phase <= 0.0f || phase >= 1.0f) {
            continue;
        }
        const float width = area.w * 1.4f * phase;
        const float thickness = std::max(2.0f, 46.0f * (1.0f - phase));
        const std::uint8_t alpha = static_cast<std::uint8_t>(
            Graphics::clampValue((1.0f - phase) * m_ripple.strength * 130.0f, 0.0f, 255.0f));
        if (alpha < 3) {
            continue;
        }
        const Rect box{area.center().x - width * 0.5f, area.center().y - thickness * 0.5f,
                       width, thickness};
        const Color edge = m_ripple.color.withAlpha(alpha);
        renderer.drawRect(Rect{box.x, box.y, box.w, 2.0f}, edge);
        renderer.drawRect(Rect{box.x, box.bottom() - 2.0f, box.w, 2.0f}, edge);
        renderer.drawRect(Rect{box.x, box.y, 2.0f, box.h}, edge);
        renderer.drawRect(Rect{box.right() - 2.0f, box.y, 2.0f, box.h}, edge);
    }
    renderer.setBlendMode(BlendMode::None);
}

void FeedbackSystem::drawVignette(const StateContext& ctx) const
{
    if (ctx.renderer == nullptr || m_tension < 0.05f) {
        return;
    }
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.setCameraEnabled(false);

    const Rect area = ctx.renderer->viewportRect();
    // Paradox darkens the edges. At Collapse the middle is still clear, because
    // a game you cannot see is not tense, it is broken.
    const float depth = m_tension * m_tension;
    const int bands = 6;
    for (int band = 0; band < bands; ++band) {
        const float t = static_cast<float>(band) / static_cast<float>(bands);
        const float width = area.w * 0.09f * (1.0f - t);
        const std::uint8_t alpha = static_cast<std::uint8_t>(
            Graphics::clampValue(depth * 46.0f * (0.35f + t * 0.9f), 0.0f, 190.0f));
        if (alpha < 2 || width <= 0.5f) {
            continue;
        }
        const Color wash = Graphics::Palette::ParadoxBack.withAlpha(alpha);
        renderer.drawRect(Rect{area.x, area.y, width, area.h}, wash);
        renderer.drawRect(Rect{area.right() - width, area.y, width, area.h}, wash);
        renderer.drawRect(Rect{area.x, area.y, area.w, width * 0.6f}, wash);
        renderer.drawRect(Rect{area.x, area.bottom() - width * 0.6f, area.w, width * 0.6f}, wash);
    }
    renderer.setBlendMode(BlendMode::None);
}

void FeedbackSystem::drawGlitch(const StateContext& ctx) const
{
    if (m_glitchBars.empty() || ctx.renderer == nullptr) {
        return;
    }
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Additive);
    renderer.setCameraEnabled(false);
    for (const Rect& bar : m_glitchBars) {
        // Bars are in screen space, so they are offset rather than translated:
        // the picture tears sideways, it does not slide.
        // Capped low on purpose. These are additive bands across the middle of
        // the screen, which is where the objective line and the toasts live, so
        // the ceiling is set by legibility rather than by how much looks
        // dramatic.
        const std::uint8_t alpha = static_cast<std::uint8_t>(
            Graphics::clampValue(m_tension * 44.0f, 0.0f, 76.0f));
        if (alpha < 3) {
            continue;
        }
        const Color tone = Color::fromFloats(0.5f, 1.0f, 1.0f, static_cast<float>(alpha) / 255.0f);
        renderer.drawRect(bar, tone);
    }
    renderer.setBlendMode(BlendMode::None);
}

void FeedbackSystem::drawFlash(const StateContext& ctx) const
{
    if (m_flash < 0.01f || ctx.renderer == nullptr) {
        return;
    }
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.setCameraEnabled(false);
    const Rect area = ctx.renderer->viewportRect();
    const std::uint8_t alpha =
        static_cast<std::uint8_t>(Graphics::clampValue(m_flash * 150.0f, 0.0f, 200.0f));
    renderer.drawRect(area, Graphics::Palette::White.withAlpha(alpha));
    renderer.setBlendMode(BlendMode::None);
}

void FeedbackSystem::drawShiftAura(const StateContext& ctx) const
{
    if (m_charge < 0.04f || ctx.renderer == nullptr) {
        return;
    }
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Additive);
    renderer.setCameraEnabled(true);

    const Vec2  centre = m_playerCentre;
    const Color accent = m_eraAccent;
    // Three rings that contract as the charge builds, which reads as something
    // being gathered in rather than something being emitted.
    for (int ring = 0; ring < 3; ++ring) {
        const float phase = Graphics::clampValue(m_charge * 1.3f - static_cast<float>(ring) * 0.22f,
                                                 0.0f, 1.0f);
        if (phase <= 0.0f) {
            continue;
        }
        const float radius = 96.0f * (1.4f - phase);
        const float thickness = 2.5f;
        const std::uint8_t alpha = static_cast<std::uint8_t>(
            Graphics::clampValue(phase * 200.0f * m_charge, 0.0f, 255.0f));
        if (alpha < 3) {
            continue;
        }
        const Rect box{centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f};
        const Color edge = accent.withAlpha(alpha);
        renderer.drawRect(Rect{box.x, box.y, box.w, thickness}, edge);
        renderer.drawRect(Rect{box.x, box.bottom() - thickness, box.w, thickness}, edge);
        renderer.drawRect(Rect{box.x, box.y, thickness, box.h}, edge);
        renderer.drawRect(Rect{box.right() - thickness, box.y, thickness, box.h}, edge);
    }
    renderer.setBlendMode(BlendMode::None);
}

void FeedbackSystem::drawUnderlay(StateContext& ctx) const
{
    if (ctx.renderer == nullptr) {
        return;
    }
    drawVignette(ctx);
    drawGlitch(ctx);
}

void FeedbackSystem::drawOverlay(StateContext& ctx) const
{
    if (ctx.renderer == nullptr) {
        return;
    }
    drawFlash(ctx);
}

} // namespace EraShift::Game
