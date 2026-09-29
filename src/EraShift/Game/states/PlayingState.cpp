#include "EraShift/Game/states/PlayingState.hpp"

#include "EraShift/Application/EventBus.hpp"
#include "EraShift/Core/SaveGame.hpp"
#include "EraShift/Game/states/PausedState.hpp"
#include "EraShift/Game/states/ResultState.hpp"
#include "EraShift/Graphics/BitmapFont.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace EraShift::Game {

using Core::GameState;
using StateContext = ::EraShift::StateContext;
using Graphics::BitmapFont;
using Graphics::BlendMode;
using Graphics::Camera2D;
using Graphics::Color;
using Graphics::FontStyle;
using Graphics::Rect;
using Graphics::Renderer2D;
using Graphics::TextAlign;
using Graphics::TextStyle;
using Graphics::TextVAlign;
using Graphics::UiScale;
using Graphics::Vec2;
using Input::Action;

namespace Palette = Graphics::Palette;

namespace {

/// How long a toast stays on screen.
constexpr float kToastLifetime = 2.2f;
/// Distance at which the interact prompt appears.
constexpr float kInteractRange = 96.0f;
/// Camera look-ahead in the direction of travel, in world units.
constexpr float kLookAhead = 56.0f;
constexpr float kCameraHalfLife = 0.085f;

constexpr float kTile = TileMap::kTileSize;

/// Era themes, in `Era` order. Each era owns a hue family so a single frame is
/// enough to tell the player where they are.
constexpr EraTheme kPastTheme{
    /*sky*/       {0x8A, 0xC8, 0xE8},
    /*skyLow*/    {0xD8, 0xE4, 0xD0},
    /*solid*/     {0x6B, 0x4E, 0x33},
    /*solidEdge*/ {0xF2, 0xC2, 0x6B},
    /*oneWay*/    {0xB9, 0xAE, 0x93},
    /*hazard*/    {0xE0, 0x5C, 0x4C},
    /*actor*/     {0xF2, 0xF4, 0xF8},
    /*accent*/    {0xF2, 0xC2, 0x6B},
};

constexpr EraTheme kPresentTheme{
    /*sky*/       {0x6B, 0x77, 0x86},
    /*skyLow*/    {0x9A, 0x9E, 0xA4},
    /*solid*/     {0x55, 0x59, 0x5E},
    /*solidEdge*/ {0xE0, 0x7A, 0x3C},
    /*oneWay*/    {0x8A, 0x87, 0x82},
    /*hazard*/    {0xE0, 0x5C, 0x4C},
    /*actor*/     {0xF2, 0xF4, 0xF8},
    /*accent*/    {0xE0, 0x7A, 0x3C},
};

constexpr EraTheme kFutureTheme{
    /*sky*/       {0x2A, 0x1E, 0x4A},
    /*skyLow*/    {0x7B, 0x5C, 0xC4},
    /*solid*/     {0x3A, 0x2C, 0x66},
    /*solidEdge*/ {0x53, 0xE0, 0xE8},
    /*oneWay*/    {0x59, 0x4B, 0x8C},
    /*hazard*/    {0xA0, 0x54, 0xC8},
    /*actor*/     {0xF2, 0xF4, 0xF8},
    /*accent*/    {0x53, 0xE0, 0xE8},
};

Rect fullArea(Renderer2D& renderer)
{
    return Rect{0.0f, 0.0f, static_cast<float>(renderer.camera().viewportWidth()),
                static_cast<float>(renderer.camera().viewportHeight())};
}

/// Interpolates between two themes during a shift.
EraTheme blendThemes(const EraTheme& from, const EraTheme& to, float t)
{
    EraTheme out;
    out.sky       = from.sky.lerpTo(to.sky, t);
    out.skyLow    = from.skyLow.lerpTo(to.skyLow, t);
    out.solid     = from.solid.lerpTo(to.solid, t);
    out.solidEdge = from.solidEdge.lerpTo(to.solidEdge, t);
    out.oneWay    = from.oneWay.lerpTo(to.oneWay, t);
    out.hazard    = from.hazard.lerpTo(to.hazard, t);
    out.actor     = from.actor.lerpTo(to.actor, t);
    out.accent    = from.accent.lerpTo(to.accent, t);
    return out;
}

/// Each enemy kind gets its own saturated body colour.
///
/// Deriving them from the era theme made every ground enemy the same white as
/// the player, which is the one thing an enemy must never be: it has to be
/// distinguishable from the thing it is chasing at a glance.
Color bodyColourFor(EnemyKind kind, const EraTheme& theme)
{
    switch (kind) {
        case EnemyKind::Sentinel: return Color{0xC8, 0x54, 0x4C};
        case EnemyKind::Wisp:     return Color{0x53, 0xE0, 0xE8};
        case EnemyKind::Warden:   return Color{0xE0, 0xA8, 0x3C};
    }
    static_cast<void>(theme);
    return Palette::Warning;
}

/// Fills a bar and returns the ratio actually drawn.
float drawBar(Renderer2D& renderer, const Rect& bar, float ratio, Color fill, Color back)
{
    const float clamped = Graphics::clampValue(ratio, 0.0f, 1.0f);
    renderer.drawRect(bar, back);
    if (clamped > 0.0f) {
        renderer.drawRect(Rect{bar.x, bar.y, bar.w * clamped, bar.h}, fill);
    }
    return clamped;
}

/// A row of pips, one per unit of a whole-number resource.
void drawPips(Renderer2D& renderer, const Vec2& origin, int filled, int total, float size,
              float gap, Color on, Color off)
{
    for (int i = 0; i < total; ++i) {
        const Rect pip{origin.x + static_cast<float>(i) * (size + gap), origin.y, size, size};
        renderer.drawRect(pip, i < filled ? on : off);
    }
}

} // namespace

const EraTheme& themeFor(Game::Era era)
{
    switch (era) {
        case Game::Era::Past:    return kPastTheme;
        case Game::Era::Present: return kPresentTheme;
        case Game::Era::Future:  return kFutureTheme;
    }
    return kPresentTheme;
}

// ---------------------------------------------------------------------------
// PlayingState
// ---------------------------------------------------------------------------
PlayingState::PlayingState(GameState id)
    : IGameState(id)
{
}

void PlayingState::applySave(const Core::SaveGame& save)
{
    m_pendingSave   = save;
    m_hasPendingSave = true;
}

UiScale PlayingState::currentUiScale(const StateContext& ctx) const
{
    return uiScaleFor(ctx);
}

void PlayingState::syncLevel(StateContext& ctx)
{
    // The data file is the source of truth. If it is missing or malformed the
    // built-in level is used instead, so a broken checkout still starts.
    const std::filesystem::path path = "data/levels/ancient_forest.json";
    Game::Level level;
    std::string error;

    if (std::filesystem::exists(path) && Game::loadLevelFromFile(path, level, error)) {
        m_level = std::move(level);
    } else {
        if (!error.empty()) {
            ctx.log->warn("Game", "using the built-in level: {}", error);
        } else {
            ctx.log->warn("Game", "no level at '{}', using the built-in one", path.string());
        }
        m_level = Game::builtInLevel();
    }

    m_world.load(m_level);
    m_ready = true;
}

void PlayingState::onEnter(StateContext& ctx)
{
    m_time        = 0.0f;
    m_hurtFlash   = 0.0f;
    m_shiftFlash  = 0.0f;
    m_toasts.clear();
    m_toastTimer  = 0.0f;

    syncLevel(ctx);

    const UiScale scale = uiScaleFor(ctx);
    m_labelStyle = Graphics::TextStyle{};
    m_labelStyle.pixelSize = scale.font(14.0f);
    m_labelStyle.color     = Palette::TextDim;
    m_labelStyle.shadow    = Color{0, 0, 0, 0xC0};

    m_valueStyle        = m_labelStyle;
    m_valueStyle.color  = Palette::TextPrimary;
    m_valueStyle.bold   = true;
    m_valueStyle.pixelSize = scale.font(16.0f);

    // Restoring a save is a reload followed by a state copy, so the world is
    // rebuilt and then told what the run looked like when it was written.
    if (m_hasPendingSave) {
        const Core::SaveGame& save = m_pendingSave;
        m_world.restart();
        const Game::TileMap& map = m_world.map();
        const Vec2 at{static_cast<float>(save.playerX) * kTile,
                      static_cast<float>(save.playerY) * kTile};
        m_world.mutablePlayer().restore(at, static_cast<Game::Era>(save.era), save.health,
                                        save.chrono);
        m_world.applySeals(save.sealMask);
        m_world.applyRunStats(save.elapsed, save.shifts, save.kills, save.paradox);
        resolvePenetration(map, m_world.mutablePlayer().body(), m_world.mutablePlayer().era());
        m_hasPendingSave = false;
        ctx.log->info("Game", "resumed '{}' at tile {} {}", save.levelName, save.playerX,
                      save.playerY);
    }

    m_currentPlayer  = m_world.player().body().position;
    m_previousPlayer = m_currentPlayer;

    Camera2D& camera = ctx.renderer->camera();
    camera.setZoom(1.0f);
    camera.setPosition(m_currentPlayer + Vec2{0.0f, -24.0f});
    camera.setBounds(m_world.map().bounds());
    camera.update(0.0);
    m_cameraPosition = camera.position();
    m_previousCamera = m_cameraPosition;

    ctx.log->info("Game", "entered Playing - '{}' ({}x{} tiles, {} enemies, {} seals)",
                  m_level.name, m_world.map().columns(), m_world.map().rows(),
                  m_world.enemyCount(), static_cast<int>(m_world.seals().size()));
}

void PlayingState::onResume(StateContext& ctx)
{
    // Simulated time must not jump forward across a pause.
    ctx.loop->resync(ctx.clock->seconds());
    ctx.log->debug("Game", "resumed Playing");
}

void PlayingState::onPause(StateContext& ctx)
{
    saveProgress(ctx);
    ctx.log->debug("Game", "paused Playing");
}

void PlayingState::onExit(StateContext& ctx)
{
    saveProgress(ctx);
    if (ctx.renderer != nullptr) {
        ctx.renderer->camera().clearBounds();
    }
    ctx.log->info("Game", "left Playing");
}

void PlayingState::saveProgress(StateContext& ctx) const
{
    if (ctx.config == nullptr || ctx.log == nullptr || m_world.outcome() != Game::Outcome::Running) {
        return;
    }
    Core::SaveGame save;
    captureSave(save);
    const Core::SaveManager manager(ctx.config->saveDirectory(), *ctx.log);
    manager.store(save);
}

void PlayingState::captureSave(Core::SaveGame& out) const
{
    const Game::Player& player = m_world.player();
    const Vec2 cell{static_cast<float>(m_world.map().cellX(player.body().position.x)),
                    static_cast<float>(m_world.map().cellY(player.body().position.y))};

    out = Core::SaveGame{};
    out.version   = Core::SaveGame::kVersion;
    out.levelId   = m_level.id;
    out.levelName = m_level.name;
    out.playerX   = static_cast<int>(cell.x);
    out.playerY   = static_cast<int>(cell.y);
    out.era       = static_cast<int>(player.era());
    out.health    = player.health();
    out.chrono    = player.chrono();
    out.paradox   = m_world.stats().paradox;
    out.dead      = !player.alive();

    out.elapsed  = m_world.stats().elapsed;
    out.shifts   = m_world.stats().shifts;
    out.kills    = m_world.stats().kills;
    out.seals    = m_world.stats().sealsTaken;
    out.sealMask = m_world.sealMask();
}

void PlayingState::syncPrompt()
{
    // One line under the objective telling the player what the interact key does
    // right now. Showing it only when it is meaningful keeps the HUD quiet the
    // rest of the time.
    m_showInteractPrompt = false;
    m_interactPrompt.clear();

    const Game::Player& player = m_world.player();
    const Vec2 here = player.body().center();

    for (const Game::Seal& seal : m_world.seals()) {
        if (seal.collected) {
            continue;
        }
        const float dx = seal.position.x - here.x;
        const float dy = seal.position.y - here.y;
        if (dx * dx + dy * dy > seal.reach * seal.reach) {
            continue;
        }
        m_showInteractPrompt = true;
        m_interactPrompt = seal.era == m_world.era()
                                ? std::string("E  -  take the ") +
                                      std::string(eraName(seal.era)) + " seal"
                                : std::string("Q  -  shift to ") + std::string(eraName(seal.era)) +
                                      " to take its seal";
        return;
    }

    const Rect goal = m_world.goalBounds();
    if (!goal.isEmpty()) {
        const Rect grown{goal.x - 48.0f, goal.y - 48.0f, goal.w + 96.0f, goal.h + 96.0f};
        if (grown.intersects(player.body().rect())) {
            m_showInteractPrompt = true;
            m_interactPrompt = m_world.gateOpen() ? std::string("The gate is open")
                                                  : std::string("The gate is sealed  -  " +
                                                                std::to_string(m_world.seals().size() -
                                                                               m_world.sealsTaken()) +
                                                                " seals remain");
        }
    }
}

void PlayingState::update(StateContext& ctx, double fixedDelta)
{
    const float dt = static_cast<float>(fixedDelta);
    m_time += dt;

    if (!m_ready || ctx.input == nullptr || ctx.renderer == nullptr) {
        return;
    }

    m_previousPlayer = m_currentPlayer;
    m_previousCamera = m_cameraPosition;

    // --- input -> simulation -----------------------------------------------
    Game::PlayerInput input;
    input.moveAxis      = ctx.input->axisHorizontal();
    input.jumpPressed   = ctx.input->wasPressed(Action::Jump);
    input.jumpHeld      = ctx.input->isDown(Action::Jump);
    input.dashPressed   = ctx.input->wasPressed(Action::Dash);
    input.attackPressed = ctx.input->wasPressed(Action::Attack) ||
                         ctx.input->wasPressed(Action::HeavyAttack);
    input.attackHeld    = ctx.input->isDown(Action::Attack);

    Game::WorldCommands commands;
    commands.shiftPressed    = ctx.input->wasPressed(Action::ShiftEra);
    commands.interactPressed = ctx.input->wasPressed(Action::Interact);

    // --- outcomes -----------------------------------------------------------
    if (m_world.outcome() != Game::Outcome::Running) {
        if (ctx.input->wasPressed(Action::Interact) || ctx.input->wasPressed(Action::Jump)) {
            ctx.states->push(std::make_shared<ResultState>(Core::GameState::GameOver,
                                                           m_world.outcome(),
                                                           m_world.stats()));
        }
        return;
    }

    m_world.update(input, commands, dt);
    m_currentPlayer = m_world.player().body().position;

    // --- events -> presentation --------------------------------------------
    for (const Game::EventRecord& event : m_world.takeEvents()) {
        m_toasts.push_back(event.text);
        m_toastTimer = kToastLifetime;

        switch (event.kind) {
            case Game::WorldEvent::EraShifted:
                m_shiftFlash = 1.0f;
                ctx.renderer->camera().shake(5.0f, 0.25);
                break;
            case Game::WorldEvent::EnemyKilled:
                ctx.renderer->camera().shake(4.0f, 0.18);
                break;
            case Game::WorldEvent::PlayerHurt:
                m_hurtFlash = 1.0f;
                ctx.renderer->camera().shake(7.0f, 0.22);
                break;
            default:
                break;
        }
    }

    m_hurtFlash  = std::max(0.0f, m_hurtFlash - dt * 2.2f);
    m_shiftFlash = std::max(0.0f, m_shiftFlash - dt * 2.0f);
    m_toastTimer -= dt;
    if (m_toastTimer <= 0.0f && !m_toasts.empty()) {
        m_toasts.erase(m_toasts.begin());
        m_toastTimer = kToastLifetime * 0.55f;
    }

    // --- camera -------------------------------------------------------------
    // Look-ahead in the direction of travel, damped frame-rate independently so
    // it behaves the same at 60 Hz and 144 Hz.
    const float look = m_world.player().facing() * kLookAhead;
    const Vec2 target = m_currentPlayer + Vec2{look, -24.0f};
    const float follow = Graphics::dampFactor(kCameraHalfLife, fixedDelta);
    m_cameraPosition   = Graphics::lerp(m_cameraPosition, target, follow);

    Camera2D& camera = ctx.renderer->camera();
    camera.setPosition(m_cameraPosition);
    camera.update(fixedDelta);

    syncPrompt();

    // --- state transitions --------------------------------------------------
    if (ctx.input->wasPressed(Action::Pause)) {
        ctx.states->push(std::make_shared<PausedState>());
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void PlayingState::drawBackdrop(const StateContext& ctx, const Rect& area) const
{
    Renderer2D& renderer = *ctx.renderer;
    const Game::World& world = m_world;

    // Everything below is translucent: the glow, the motes and the horizon all
    // depend on it. Without this the "motes" render as opaque orange squares
    // scattered across the sky.
    renderer.setBlendMode(BlendMode::Alpha);

    const EraTheme& from = themeFor(world.previousEra());
    const EraTheme& to   = themeFor(world.era());
    const EraTheme theme = blendThemes(from, to, Graphics::clampValue(world.eraBlend(), 0.0f, 1.0f));

    // A vertical gradient, drawn as bands. Twenty-four bands is enough to be
    // indistinguishable from a gradient and costs the same as three rects.
    //
    // The stride and the height must be the same value, or the bands do not
    // tile and the cleared framebuffer shows through the gaps as black lines.
    constexpr int kBands = 24;
    const float stride   = area.h / static_cast<float>(kBands);
    for (int i = 0; i < kBands; ++i) {
        const float t    = static_cast<float>(i) / static_cast<float>(kBands - 1);
        const Rect band{area.x, area.y + stride * static_cast<float>(i), area.w, stride};
        renderer.drawRect(band, theme.sky.lerpTo(theme.skyLow, t));
    }

    // A bright bloom around the horizon line, which is where the world is
    // brightest in all three eras and therefore anchors the eye.
    const float horizon = area.h * 0.62f;
    const float glow    = 0.5f + 0.5f * std::sin(static_cast<float>(m_time) * 0.6f);
    renderer.drawRect(Rect{area.x, horizon, area.w, area.h * 0.10f},
                     theme.accent.withAlpha(static_cast<std::uint8_t>(18.0f + 14.0f * glow)));
    renderer.drawRect(Rect{area.x, horizon, area.w, 2.0f},
                     theme.accent.withAlpha(0x50));

    // Drifting motes, shared with the main menu's language.
    for (int i = 0; i < 36; ++i) {
        const float seed = static_cast<float>(i) * 0.6180339f;
        const float x = std::fmod(seed * area.w + static_cast<float>(m_time) * (10.0f + seed * 24.0f),
                                  area.w);
        const float y = std::fmod(seed * area.h + area.h -
                                       static_cast<float>(m_time) * (6.0f + seed * 10.0f),
                                   area.h);
        renderer.drawRect(Rect{x, y, 1.0f + seed, 1.0f + seed}, theme.accent.withAlpha(0x45));
    }

    renderer.setBlendMode(BlendMode::None);
    static_cast<void>(ctx);
}

void PlayingState::drawParallax(const StateContext& ctx, const Rect& area) const
{
    Renderer2D& renderer = *ctx.renderer;
    const Camera2D& camera = renderer.camera();
    const EraTheme theme = blendThemes(themeFor(m_world.previousEra()), themeFor(m_world.era()),
                                       Graphics::clampValue(m_world.eraBlend(), 0.0f, 1.0f));

    // Three ridgelines at different depths. A parallax layer is just a shape
    // drawn at a fraction of the camera's travel, so the "depth" is a multiplier
    // on the camera offset rather than a special case in the renderer.
    struct Layer {
        float depth;
        float amplitude;
        float period;
        float baseline;
        std::uint8_t alpha;
    };
    // Amplitudes are large relative to the viewport on purpose: a ridge that
    // only rises a few pixels does not read as terrain, it reads as a slightly
    // different shade of background.
    constexpr Layer kLayers[] = {
        {0.12f, 130.0f, 1100.0f, 0.56f, 0x40},
        {0.28f, 170.0f, 760.0f, 0.66f, 0x60},
        {0.50f, 200.0f, 520.0f, 0.78f, 0x85},
    };

    renderer.setBlendMode(BlendMode::Alpha);
    for (const Layer& layer : kLayers) {
        const float offsetX = camera.position().x * (1.0f - layer.depth);
        const float baseline = area.h * layer.baseline;

        // A stepped ridge, drawn as one rect per step. Cheap, and at this scale
        // reads as terrain rather than as a waveform.
        const float step = layer.period * 0.25f;
        const float first = std::floor((offsetX - step) / step) * step;
        for (float x = first; x < offsetX + area.w + step; x += step) {
            const float worldX = x - offsetX;
            const float phase  = (x / layer.period) * 6.2831853f;
            const float top =
                baseline - layer.amplitude *
                               (0.5f + 0.5f * std::sin(phase) * std::sin(phase * 0.37f + 1.1f));
            renderer.drawRect(Rect{worldX, top, step + 1.0f, area.h}, theme.solid.withAlpha(layer.alpha));
        }
    }
    renderer.setBlendMode(BlendMode::None);
}

void PlayingState::drawTiles(const StateContext& ctx, float blend) const
{
    Renderer2D& renderer = *ctx.renderer;
    const Game::TileMap& map = m_world.map();
    const Camera2D& camera  = renderer.camera();

    const EraTheme from = blendThemes(themeFor(m_world.previousEra()),
                                      themeFor(m_world.era()),
                                      Graphics::clampValue(blend, 0.0f, 1.0f));
    const EraTheme to = themeFor(m_world.era());

    // Only the cells inside the view are touched. At 32px tiles over a 1280px
    // viewport that is 40 columns, whatever the level's real width.
    const Rect view = camera.viewportBounds();
    const int x0 = std::max(0, map.cellX(view.left()) - 1);
    const int y0 = std::max(0, map.cellY(view.top()) - 1);
    const int x1 = std::min(map.columns() - 1, map.cellX(view.right()) + 1);
    const int y1 = std::min(map.rows() - 1, map.cellY(view.bottom()) + 1);

    renderer.setBlendMode(BlendMode::Alpha);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Tile tile = map.at(x, y);
            const Rect cell = map.cellRect(x, y);

            switch (tile.kind) {
                case TileKind::Empty:
                    continue;

                case TileKind::Goal: {
                    // The gate pulses so it is findable across a wide level.
                    const float pulse =
                        0.55f + 0.45f * std::sin(static_cast<float>(m_time) * 3.0f);
                    renderer.drawRect(cell, to.accent.withAlpha(static_cast<std::uint8_t>(70 + 90 * pulse)));
                    renderer.drawRectOutline(cell, Palette::TextPrimary.withAlpha(0xC0), 3.0f);
                    continue;
                }

                case TileKind::Hazard:
                    // Hatches, not spikes: they read as dangerous without
                    // borrowing a colour that belongs to the HUD.
                    renderer.drawRect(cell, to.hazard.withAlpha(0xCC));
                    for (int i = 0; i < 3; ++i) {
                        const float t = static_cast<float>(i) / 3.0f;
                        renderer.drawLine({cell.x + cell.w * t, cell.bottom() - 3.0f},
                                          {cell.x + cell.w * (t + 0.22f), cell.y + 3.0f},
                                          Palette::TextPrimary.withAlpha(0x90), 2.0f);
                    }
                    continue;

                case TileKind::Platform:
                    renderer.drawRect(Rect{cell.x, cell.y, cell.w, 8.0f}, to.oneWay);
                    continue;

                default:
                    break;
            }

            // Era-dependent geometry is drawn twice, once per era, with the
            // outgoing era fading out. Tiles that exist in both eras end up
            // solid the whole time; tiles that exist in only one fade, which is
            // what makes a shift legible rather than a hard pop.
            const bool inFrom = tile.blocksIn(m_world.previousEra());
            const bool inTo   = tile.blocksIn(m_world.era());
            if (!inFrom && !inTo) {
                continue;
            }

            const float t = Graphics::clampValue(blend, 0.0f, 1.0f);
            if (inFrom && inTo) {
                renderer.drawRect(cell, from.solid);
                renderer.drawRect(Rect{cell.x, cell.y, cell.w, 3.0f}, from.solidEdge.withAlpha(0x90));
                // Subtle interior speckle so a large floor is not a flat slab.
                if (((x * 7 + y * 13) % 5) == 0) {
                    renderer.drawRect(Rect{cell.x + 6.0f, cell.y + 10.0f, cell.w - 12.0f, 4.0f},
                                      from.solid.scaled(1.25f));
                }
                continue;
            }

            if (inFrom) {
                const auto alpha = static_cast<std::uint8_t>(255.0f * (1.0f - t));
                renderer.drawRect(cell, from.solid.withAlpha(alpha));
                renderer.drawRect(Rect{cell.x, cell.y, cell.w, 3.0f},
                                  from.solidEdge.withAlpha(static_cast<std::uint8_t>(alpha * 0.6f)));
                continue;
            }

            const auto alpha = static_cast<std::uint8_t>(255.0f * t);
            renderer.drawRect(cell, to.solid.withAlpha(alpha));
            renderer.drawRect(Rect{cell.x, cell.y, cell.w, 3.0f},
                              to.solidEdge.withAlpha(static_cast<std::uint8_t>(alpha * 0.6f)));
        }
    }
    renderer.setBlendMode(BlendMode::None);
}

void PlayingState::drawPickups(const StateContext& ctx) const
{
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.setCameraEnabled(true);

    for (const Game::Pickup& pickup : m_world.pickups()) {
        if (pickup.collected) {
            continue;
        }
        const Rect r = pickup.body.rect();
        // Bobbing and a pulse make the collectables visible against a busy
        // floor without needing a sprite.
        const float bob = std::sin(static_cast<float>(m_time) * 3.0f + r.x * 0.05f) * 3.0f;
        const Rect body{r.x, r.y + bob, r.w, r.h};

        if (pickup.kind == Game::PickupKind::ChronoCell) {
            const float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(m_time) * 4.0f);
            renderer.drawRect(body, Palette::EnergyFill.withAlpha(static_cast<std::uint8_t>(140 + 90 * pulse)));
            renderer.drawRectOutline(body, Palette::White.withAlpha(0xB0), 2.0f);
            renderer.drawRect(Rect{body.center().x - 2.0f, body.center().y - 6.0f, 4.0f, 12.0f},
                              Palette::White);
        } else {
            renderer.drawRect(body, Palette::HealthFill.withAlpha(0xE0));
            renderer.drawRect(Rect{body.center().x - 8.0f, body.center().y - 2.0f, 16.0f, 4.0f},
                              Palette::White);
            renderer.drawRect(Rect{body.center().x - 2.0f, body.center().y - 8.0f, 4.0f, 16.0f},
                              Palette::White);
        }
    }
    renderer.setBlendMode(BlendMode::None);
}

void PlayingState::drawSeals(const StateContext& ctx, float blend) const
{
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.setCameraEnabled(true);

    const Vec2 playerCentre = m_world.player().body().center();

    for (const Game::Seal& seal : m_world.seals()) {
        if (seal.collected) {
            continue;
        }
        const EraTheme sealTheme = themeFor(seal.era);
        const float sway = std::sin(static_cast<float>(m_time) * 2.0f + seal.position.x * 0.01f) * 4.0f;

        const float h = 72.0f;
        const Rect pillar{seal.position.x - 14.0f, seal.position.y - h + sway, 28.0f, h};

        // A seal that is not in the current era is dim and hollow, so the
        // player can see that it exists and needs a shift rather than walking
        // up to it and getting a "wrong era" message.
        const bool here = seal.era == m_world.era();
        const auto alpha =
            here ? std::uint8_t{0xFF}
                 : static_cast<std::uint8_t>(0xFF * (0.35f + 0.30f * (1.0f - blend)));

        renderer.drawRect(pillar, sealTheme.solid.withAlpha(alpha));
        renderer.drawRectOutline(pillar, sealTheme.accent.withAlpha(alpha), 2.0f);

        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_time) * 3.4f);
        const Rect core{pillar.center().x - 8.0f, pillar.y + 12.0f, 16.0f, 16.0f};
        renderer.drawRect(core, sealTheme.accent.withAlpha(
                                   static_cast<std::uint8_t>((0x60 + 0xA0 * pulse) * alpha / 255.0f)));
        renderer.drawRectOutline(core, Palette::TextPrimary.withAlpha(alpha), 1.0f);

        // Prompt when the player is close enough to interact.
        const float distance = std::sqrt((seal.position.x - playerCentre.x) *
                                             (seal.position.x - playerCentre.x) +
                                         (seal.position.y - playerCentre.y) *
                                             (seal.position.y - playerCentre.y));
        if (distance <= seal.reach) {
            Graphics::TextRenderer& text = *ctx.text;
            TextStyle prompt;
            prompt.pixelSize = 15;
            prompt.color     = here ? Palette::TextPrimary : Palette::TextDim;
            prompt.align     = TextAlign::Center;
            prompt.shadow    = Color{0, 0, 0, 0xC0};
            const std::string label =
                here ? std::string("E  -  ") + seal.label
                     : std::string("SHIFT TO ") + std::string(eraName(seal.era));
            const Rect where{pillar.center().x - 110.0f, pillar.y - 34.0f, 220.0f, 20.0f};
            text.drawInRect(renderer, where, label, prompt);
        }
    }
    renderer.setBlendMode(BlendMode::None);
}

void PlayingState::drawEnemies(const StateContext& ctx) const
{
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.setCameraEnabled(true);

    const EraTheme theme = blendThemes(themeFor(m_world.previousEra()), themeFor(m_world.era()),
                                       Graphics::clampValue(m_world.eraBlend(), 0.0f, 1.0f));

    for (const Game::Enemy& enemy : m_world.enemies()) {
        const bool present = enemy.inCurrentEra();

        // A sleeping enemy is drawn as a faint outline in its own era's colour.
        // Showing it is deliberate: the player needs to know it is there in
        // order to want to shift to deal with it.
        if (!present) {
            const Color ghost = bodyColourFor(enemy.kind(), themeFor(enemy.era())).withAlpha(0x35);
            renderer.drawRectOutline(enemy.body().rect(), ghost, 2.0f);
            continue;
        }

        const Rect r = enemy.body().rect();
        const bool dying = enemy.state() == Game::EnemyState::Dying;
        const float alpha = dying ? 0.5f : 1.0f;

        Color bodyColour = bodyColourFor(enemy.kind(), theme);
        if (enemy.hurt()) {
            bodyColour = Palette::White;
        }
        if (enemy.state() == Game::EnemyState::Windup) {
            // Telegraph: the enemy swells before it commits, so a hit during
            // the windup reads as the player's reward for reading the tell.
            const float wind = Graphics::clampValue(enemy.stateProgress(), 0.0f, 1.0f);
            bodyColour = bodyColour.lerpTo(Palette::Warning, 0.25f + 0.55f * wind);
        }

        const Rect drawable{r.x, r.y + (dying ? 8.0f : 0.0f), r.w, r.h};
        renderer.drawRect(drawable, bodyColour.withAlpha(static_cast<std::uint8_t>(255.0f * alpha)));
        renderer.drawRectOutline(drawable, theme.accent.withAlpha(0xC0), 2.0f);

        // Facing indicator: an eye that moves to the side it is looking at.
        const float eyeX = enemy.facing() > 0.0f ? drawable.right() - 10.0f : drawable.x + 4.0f;
        renderer.drawRect(Rect{eyeX, drawable.y + 7.0f, 6.0f, 6.0f}, Palette::Black);

        // Health above the enemy once it has been hurt, so a long fight is
        // legible without opening anything.
        if (enemy.health() < enemy.maxHealth() && !dying) {
            const Rect bar{drawable.x, drawable.y - 9.0f, drawable.w, 4.0f};
            drawBar(renderer, bar, enemy.health() / enemy.maxHealth(),
                    Palette::Warning.withAlpha(0xE0), Palette::ParadoxBack.withAlpha(0xC0));
        }
    }
    renderer.setBlendMode(BlendMode::None);
}

void PlayingState::drawPlayer(const StateContext& ctx, const Vec2& interpolated) const
{
    Renderer2D& renderer = *ctx.renderer;
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.setCameraEnabled(true);

    const Game::Player& player = m_world.player();
    const EraTheme theme = blendThemes(themeFor(m_world.previousEra()), themeFor(m_world.era()),
                                       Graphics::clampValue(m_world.eraBlend(), 0.0f, 1.0f));

    const Vec2 delta = interpolated - m_previousPlayer;
    const float squash = Graphics::clampValue(1.0f - delta.y * 0.0016f, 0.92f, 1.08f);

    const Rect body{interpolated.x, interpolated.y, player.body().size.x,
                    player.body().size.y * squash};

    // Contact shadow. Without it the player floats and the jump arc is much
    // harder to read.
    const float shadowY = static_cast<float>(m_world.map().rows()) * kTile;
    renderer.drawRect(Rect{body.center().x - body.w * 0.45f, shadowY - 4.0f, body.w * 0.9f, 4.0f},
                      Palette::Black.withAlpha(0x40));

    // Invulnerability is shown by flickering, which reads instantly and needs
    // no extra UI explaining it.
    const bool flicker = player.invulnerable() &&
                         (static_cast<int>(m_time * 22.0f) % 2 == 0);

    if (!flicker) {
        renderer.drawRect(body, theme.actor);
        renderer.drawRectOutline(body, theme.accent, 2.0f);

        // A visor that looks the way the player is facing.
        const float facing = player.facing();
        renderer.drawRect(Rect{body.center().x + (facing > 0.0f ? 2.0f : -10.0f), body.y + 8.0f,
                               8.0f, 5.0f},
                          theme.accent);
    }

    // The swing itself, drawn as an arc of the hit box so the reach is visible.
    const Rect swing = player.attackBox();
    if (!swing.isEmpty()) {
        renderer.drawRect(swing, theme.accent.withAlpha(0x33));
        renderer.drawRectOutline(swing, theme.accent.withAlpha(0xCC), 2.0f);
    }

    // Dash trail: echoes of the body trailing behind the direction of travel,
    // fading out. Reads as speed without a particle system.
    if (player.dashing()) {
        const float facing = player.facing();
        for (int i = 1; i <= 3; ++i) {
            const float trailX = body.x - facing * static_cast<float>(i) * 12.0f;
            renderer.drawRectOutline(
                Rect{trailX, body.y, body.w, body.h},
                theme.accent.withAlpha(static_cast<std::uint8_t>(0x50 - i * 0x10)), 2.0f);
        }
    }

    renderer.setBlendMode(BlendMode::None);
}

void PlayingState::drawHud(const StateContext& ctx, const Rect& area) const
{
    Renderer2D& renderer = *ctx.renderer;
    Graphics::TextRenderer& text = *ctx.text;
    const Game::Player& player = m_world.player();

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);

    const UiScale scale = currentUiScale(ctx);
    const TextStyle& label = m_labelStyle;
    const float pad = scale.px(18.0f);

    // --- damage and shift vignettes ----------------------------------------
    // A red edge pulse on damage and an era-coloured wash on a shift. Both are
    // full-screen alpha blits, so they cost one rect each.
    if (m_hurtFlash > 0.0f) {
        const auto a = static_cast<std::uint8_t>(120.0f * m_hurtFlash);
        renderer.drawRect(area, Palette::HealthFill.withAlpha(a));
    }
    if (m_shiftFlash > 0.0f) {
        const EraTheme theme = themeFor(m_world.era());
        const auto a = static_cast<std::uint8_t>(70.0f * m_shiftFlash);
        renderer.drawRect(area, theme.accent.withAlpha(a));
    }
    if (player.health() <= 1.0f && player.alive()) {
        // Low health throbs steadily so the state is never ambiguous.
        const float throb = 0.5f + 0.5f * std::sin(static_cast<float>(m_time) * 4.0f);
        renderer.drawRect(area, Palette::HealthFill.withAlpha(static_cast<std::uint8_t>(28 + 26 * throb)));
    }

    // The HUD is laid out as one left column and one right column, stacked in
    // order and never sharing a row. Laying these out by eye is how the era
    // badge ends up printed on top of the health pips.
    const float pipSize    = scale.px(20.0f);
    const float barW       = std::max(scale.px(190.0f), pipSize * 5.0f + scale.px(4.0f));
    const float gap        = scale.px(8.0f);
    float leftY            = pad;

    // --- health: one pip per unit ------------------------------------------
    const int totalHealth = std::max(1, static_cast<int>(player.maxHealth()));
    const int filled      = static_cast<int>(player.health());
    drawPips(renderer, Vec2{pad, leftY}, filled, totalHealth, pipSize, scale.px(5.0f),
             Palette::HealthFill, Palette::HealthBack);
    {
        TextStyle healthLabel = label;
        healthLabel.align     = TextAlign::Right;
        text.drawAligned(renderer, Vec2{pad + barW, leftY + pipSize * 0.5f - scale.px(7.0f)},
                         std::to_string(filled) + " / " + std::to_string(totalHealth),
                         healthLabel);
    }
    leftY += pipSize + gap;

    // --- chrono energy ------------------------------------------------------
    // Below one shift's worth the bar turns warm, so the player knows the shift
    // key is about to stop working before they press it and wonder why.
    const Rect energy{pad, leftY, barW, scale.px(12.0f)};
    const float energyRatio = player.chrono() / std::max(1.0f, player.maxChrono());
    const bool lowEnergy     = player.chrono() < player.tuning().shiftCost;
    drawBar(renderer, energy, energyRatio,
            lowEnergy ? Palette::AccentWarm : Palette::EnergyFill, Palette::EnergyBack);
    text.drawInRect(renderer, Rect{energy.right() + scale.px(8.0f), energy.y - scale.px(3.0f),
                                   scale.px(120.0f), scale.px(18.0f)},
                    "CHRONO", label);
    leftY = energy.bottom() + gap * 0.5f;

    // --- paradox ------------------------------------------------------------
    const Rect paradox{pad, leftY, barW, scale.px(7.0f)};
    constexpr float kParadoxMax = 600.0f;
    drawBar(renderer, paradox, m_world.stats().paradox / kParadoxMax, Palette::ParadoxFill,
            Palette::ParadoxBack);
    leftY = paradox.bottom() + gap;

    // --- era badge ----------------------------------------------------------
    // The single most important thing on screen: which era the world is in, and
    // how far through the transition it is.
    {
        TextStyle eraStyle = m_valueStyle;
        eraStyle.align     = TextAlign::Center;
        eraStyle.valign    = TextVAlign::Middle;
        const std::string name(eraName(m_world.era()));
        const Vec2 measured = text.measure(name, eraStyle);
        const Rect badge{pad, leftY, measured.x + scale.px(28.0f), scale.px(26.0f)};
        renderer.drawRect(badge, Palette::PanelFill);
        renderer.drawRect(Rect{badge.x, badge.y, scale.px(4.0f), badge.h},
                          themeFor(m_world.era()).accent);
        text.drawInRect(renderer, Rect{badge.x + scale.px(9.0f), badge.y, badge.w, badge.h},
                        name, eraStyle);
        text.drawInRect(renderer, Rect{badge.right() + scale.px(10.0f), badge.y,
                                       scale.px(140.0f), badge.h},
                        "Q  SHIFT ERA", label);
        leftY = badge.bottom() + scale.px(4.0f);

        // A bar under the badge while the world is dissolving. It also
        // communicates that a shift is not instantaneous.
        const float blend = m_world.eraBlend();
        if (blend < 1.0f) {
            const Rect settle{badge.x + scale.px(4.0f), badge.bottom() + scale.px(2.0f),
                              badge.w - scale.px(8.0f), scale.px(3.0f)};
            drawBar(renderer, settle, blend, themeFor(m_world.era()).accent,
                    Palette::EnergyBack.withAlpha(0xC0));
        }
    }

    // --- seals: top-right, one icon per era ----------------------------------
    {
        const std::size_t sealCount = m_world.seals().size();
        const float sealSize = scale.px(18.0f);
        const float sealGap  = scale.px(6.0f);
        const float blockW   = sealCount > 0
                                   ? sealSize * static_cast<float>(sealCount) +
                                         sealGap * (static_cast<float>(sealCount) - 1.0f)
                                   : 0.0f;
        float x = area.right() - pad - blockW;
        const float y = pad;

        for (const Game::Seal& seal : m_world.seals()) {
            const EraTheme theme = themeFor(seal.era);
            const Rect box{x, y, sealSize, sealSize};
            renderer.drawRect(box, seal.collected ? theme.accent : Palette::PanelFill);
            renderer.drawRectOutline(box, theme.accent.withAlpha(seal.collected ? 0xFF : 0x88), 2.0f);
            if (seal.collected) {
                // A tick as well as the colour change, so the state does not
                // depend on being able to tell the hues apart.
                renderer.drawLine({box.x + sealSize * 0.26f, box.center().y},
                                  {box.center().x, box.bottom() - sealSize * 0.26f},
                                  Palette::Black, 2.0f);
                renderer.drawLine({box.center().x, box.bottom() - sealSize * 0.26f},
                                  {box.right() - sealSize * 0.26f, box.y + sealSize * 0.26f},
                                  Palette::Black, 2.0f);
            }
            x += sealSize + sealGap;
        }
        if (sealCount > 0) {
            TextStyle sealLabel = label;
            sealLabel.align     = TextAlign::Right;
            text.drawAligned(renderer, Vec2{area.right() - pad, y + sealSize + scale.px(4.0f)},
                             "ERA SEALS", sealLabel);
        }
    }

    // --- bottom stack: prompt, hint, objective -------------------------------
    // Built upwards from the bottom edge so adding a line never overlaps the
    // one above it.
    float bottom = area.bottom() - pad * 0.5f;

    {
        TextStyle counter = label;
        counter.align     = TextAlign::Right;
        text.drawAligned(renderer, Vec2{area.right() - pad, bottom - scale.px(14.0f)},
                         std::to_string(m_world.activeEnemyCount()) + " / " +
                             std::to_string(m_world.enemyCount()) + " HOSTILE",
                         counter);
    }

    TextStyle hint = label;
    hint.align     = TextAlign::Center;
    const Rect hintRect{area.x, bottom - scale.px(18.0f), area.w, scale.px(18.0f)};
    text.drawInRect(renderer, hintRect, "A / D move     SPACE jump     SHIFT dash     E attack     Q era", hint);

    TextStyle objective = m_valueStyle;
    objective.align     = TextAlign::Center;
    text.drawInRect(renderer, Rect{area.x, hintRect.y - scale.px(24.0f), area.w, scale.px(20.0f)},
                    m_world.objectiveText(), objective);

    if (m_showInteractPrompt) {
        TextStyle prompt = m_valueStyle;
        prompt.align     = TextAlign::Center;
        prompt.color     = Palette::AccentWarm;
        text.drawInRect(renderer,
                        Rect{area.x, hintRect.y - scale.px(46.0f), area.w, scale.px(18.0f)},
                        m_interactPrompt, prompt);
    }

    renderer.setBlendMode(BlendMode::None);

    if (!text.ready()) {
        // Keep something legible on screen if the TTF face never loaded.
        FontStyle fallback;
        fallback.scale = 2;
        BitmapFont::drawCentered(renderer, area.center().x, area.bottom() - scale.px(30.0f),
                                 m_world.objectiveText(), Palette::TextPrimary, fallback);
    }
}

void PlayingState::drawToasts(const StateContext& ctx, const Rect& area) const
{
    if (m_toasts.empty()) {
        return;
    }
    Renderer2D& renderer = *ctx.renderer;
    Graphics::TextRenderer& text = *ctx.text;
    const UiScale scale = currentUiScale(ctx);

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);

    TextStyle style = m_valueStyle;
    style.align     = TextAlign::Center;
    style.color     = Palette::AccentWarm;

    float y = area.h * 0.30f;
    // Newest at the bottom, oldest fading above, so the stack reads as a log.
    for (std::size_t i = 0; i < m_toasts.size(); ++i) {
        const float age = static_cast<float>(i) / static_cast<float>(m_toasts.size());
        const auto alpha = static_cast<std::uint8_t>(40.0f + 215.0f * age);
        TextStyle faded  = style;
        faded.color      = Palette::AccentWarm.withAlpha(alpha);
        text.drawInRect(renderer, Rect{area.x, y, area.w, scale.px(22.0f)}, m_toasts[i], faded);
        y += scale.px(22.0f);
    }

    renderer.setBlendMode(BlendMode::None);
}

void PlayingState::render(StateContext& ctx, double alpha)
{
    if (!m_ready || ctx.renderer == nullptr) {
        return;
    }
    Renderer2D& renderer = *ctx.renderer;
    renderer.beginFrame();

    const Rect area = fullArea(renderer);

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::None);

    // Interpolating between the last two fixed steps keeps movement smooth on
    // displays whose refresh rate is not a multiple of the simulation rate.
    const float t = Graphics::clampValue(static_cast<float>(alpha), 0.0f, 1.0f);
    const Vec2 interpolated = Graphics::lerp(m_previousPlayer, m_currentPlayer, t);
    const float blend       = Graphics::clampValue(m_world.eraBlend(), 0.0f, 1.0f);

    drawBackdrop(ctx, area);
    if (!std::getenv("ES_NOPARALLAX")) drawParallax(ctx, area);
    if (!std::getenv("ES_NOTILES")) drawTiles(ctx, blend);
    drawSeals(ctx, blend);
    drawPickups(ctx);
    drawEnemies(ctx);
    drawPlayer(ctx, interpolated);

    drawHud(ctx, area);
    drawToasts(ctx, area);

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        renderer.setBlendMode(BlendMode::Alpha);
        ctx.overlay->render(renderer, *ctx.text, *ctx.stats, *ctx.states, ctx.loop->stats());
        renderer.setBlendMode(BlendMode::None);
    }

    static_cast<void>(ctx.text);
}

} // namespace EraShift::Game
