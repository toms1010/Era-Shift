#include "EraShift/Game/states/PlayingState.hpp"

#include "EraShift/Core/Config.hpp"
#include "EraShift/Core/ProgressDatabase.hpp"
#include "EraShift/Game/EraTheme.hpp"

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
/// How long the finished world stays on screen before the results appear.
constexpr float kOutcomeDelay = 1.4f;

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

/// True when there is open space directly above a cell, so its top face is a
/// surface rather than the inside of a wall.
///
/// Without this every buried row is outlined as if it were a ledge, and a floor
/// six rows deep reads as six separate shelves.
bool exposedAbove(const Game::TileMap& map, int x, int y, Game::Era era)
{
    return !map.at(x, y - 1).blocksIn(era);
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

EraTheme blendThemes(const EraTheme& from, const EraTheme& to, float t)
{
    const float k = Graphics::clampValue(t, 0.0f, 1.0f);
    EraTheme out;
    out.sky       = from.sky.lerpTo(to.sky, k);
    out.skyLow    = from.skyLow.lerpTo(to.skyLow, k);
    out.solid     = from.solid.lerpTo(to.solid, k);
    out.solidEdge = from.solidEdge.lerpTo(to.solidEdge, k);
    out.oneWay    = from.oneWay.lerpTo(to.oneWay, k);
    out.hazard    = from.hazard.lerpTo(to.hazard, k);
    out.actor     = from.actor.lerpTo(to.actor, k);
    out.accent    = from.accent.lerpTo(to.accent, k);
    return out;
}

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
    // Three sources, in order of specificity: a region the player just picked
    // from the level select, an explicit `--level`, and finally the default.
    //
    // The picked region wins over `--level` because choosing a region in the
    // menu is a more recent instruction than a flag typed at launch, and a
    // player who picked one and then watched the game load a different one would
    // be right to call it a bug.
    const std::filesystem::path picked = LevelSelectState::chosenLevel();
    std::filesystem::path path;
    if (!picked.empty()) {
        path = ctx.levelDirectory.empty() ? picked : ctx.levelDirectory / picked;
        LevelSelectState::clearChosenLevel();
    } else if (!ctx.levelPath.empty()) {
        path = ctx.levelPath;
    } else {
        path = std::filesystem::path{"data/levels/ancient_forest.json"};
    }

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

    // A pending checkpoint is applied after the load, never instead of it: the
    // restore rebuilds the world, so it needs the level already in place. The
    // request is consumed either way, so a RETRY REGION after a RETRY CHECKPOINT
    // really does start from the spawn.
    if (m_hasPendingCheckpoint) {
        m_world.restoreCheckpoint(
            Vec2{static_cast<float>(m_pendingCheckpoint.tileX) * Game::TileMap::kTileSize,
                 static_cast<float>(m_pendingCheckpoint.tileY) * Game::TileMap::kTileSize},
            static_cast<Game::Era>(m_pendingCheckpoint.era), m_pendingCheckpoint.health,
            m_pendingCheckpoint.chrono, m_pendingCheckpoint.sealMask);
        m_world.applyRunStats(m_pendingCheckpoint.elapsed, m_pendingCheckpoint.shifts,
                              m_pendingCheckpoint.kills, 0.0f);
        m_pendingCheckpoint = Core::Checkpoint{};
        m_hasPendingCheckpoint = false;
    }

    m_ready = true;
}

bool PlayingState::applyCheckpoint(const Core::Checkpoint& checkpoint)
{
    if (!checkpoint.valid) {
        return false;
    }
    m_pendingCheckpoint   = checkpoint;
    m_hasPendingCheckpoint = true;
    return true;
}

void PlayingState::onEnter(StateContext& ctx)
{
    m_time        = 0.0f;
    m_hurtFlash   = 0.0f;
    m_shiftFlash  = 0.0f;
    m_outcomeDelay = kOutcomeDelay;
    m_toasts.clear();
    m_toastTimer  = 0.0f;

    // Presentation state is per-run, not per-process: a restart must not inherit
    // the previous attempt's particles or enemy animations, or the new run opens
    // with debris falling that nothing in it caused.
    m_feedback.reset();
    m_enemyAnims.clear();
    m_playerAnim.reset();
    m_lastCheckpointVolume = {-1, -1};

    // The tutorial is seeded from the progression database rather than always
    // starting fresh: a player who has finished it must never be shown it again,
    // and a player who turned it off must never be shown it at all. Both are
    // recorded states, not a session flag, which is why they are read from here.
    {
        bool enabled  = true;
        bool complete = false;
        if (ctx.progress != nullptr) {
            const Core::Progress progress = ctx.progress->loadProgress();
            enabled  = progress.tutorialEnabled;
            complete = progress.tutorialComplete;
        } else {
            // No database: teach it. Showing the tutorial is the lower-risk
            // failure — a returning player sees hints they do not need, rather
            // than a first-time player being given none.
            complete = false;
        }
        m_tutorial.restore(enabled, complete);
        m_lesson     = m_tutorial.lesson();
        m_tutorialAge = 0.0f;
    }

    // Cleared per run, not per region: dialogue is state, and a line from the
    // previous attempt must not still be on screen when this one starts.
    m_dialogue.clear();
    m_dialogueAge = 0.0f;

    // A region's own opening line, when it declares one. Placed entities carry a
    // `label`, and a seal's label reads as something the world would say, so the
    // first seal of a region introduces it rather than appearing silently.
    if (const Game::PlacedEntity* spawn = m_level.find(Game::EntityKind::PlayerSpawn);
        spawn != nullptr && !spawn->label.empty()) {
        say("ERA SHIFT", "You wake in the " + m_level.name + ".", 4.5f);
    }

    // `graphics.maxParticles` is a real budget, applied here rather than left as
    // a decoration. A slider that looks live and does nothing is worse than no
    // slider, and this is the one number a player might genuinely want to raise
    // on a machine that can afford it.
    if (ctx.config != nullptr && ctx.log != nullptr) {
        const int budget =
            ctx.config->store().clampInt("graphics", "maxParticles",
                                         static_cast<int>(m_feedback.particleCapacity()), 64,
                                         8192, *ctx.log);
        m_feedback.setParticleCapacity(static_cast<std::size_t>(budget));
    }

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

void PlayingState::transitionToResult(StateContext& ctx)
{
    const bool won = m_world.outcome() == Game::Outcome::Victory;

    auto results = std::make_shared<ResultState>(Core::GameState::GameOver, m_world.outcome(),
                                                 m_world.stats());
    results->setLevelName(m_level.name);
    results->setLevelId(m_level.id);

    if (won) {
        // A victory is a progression event, so it is recorded before the results
        // screen appears rather than when the player leaves it: closing the window
        // on the results screen would otherwise lose the completion entirely.
        results->markCompleted(true);
        if (ctx.progress != nullptr) {
            const std::filesystem::path directory =
                ctx.levelDirectory.empty() ? std::filesystem::path{"data/levels"}
                                           : ctx.levelDirectory;
            const std::vector<LevelEntry> levels = listLevels(directory);

            // The region finished, which *defines* its position in the
            // progression. Looking it up in the list rather than counting rows in
            // the database means a region added to `data/` later still unlocks
            // its successor correctly.
            int position = 0;
            for (std::size_t i = 0; i < levels.size(); ++i) {
                if (levels[i].id == m_level.id) {
                    position = static_cast<int>(i);
                    break;
                }
            }

            if (ctx.progress->recordLevelResult(m_level.id, m_level.name, true,
                                                m_world.stats().elapsed,
                                                m_world.stats().score(),
                                                m_world.sealsTaken())) {
                // Unlocking the *next* region is what makes this a sequence
                // rather than ten independent levels.
                if (position + 1 < static_cast<int>(levels.size())) {
                    const LevelEntry& next = levels[static_cast<std::size_t>(position) + 1];
                    ctx.progress->unlockLevel(next.id, position + 2);
                }
            }

            Core::Progress updated = ctx.progress->loadProgress();
            updated.currentLevelId = m_level.id;
            updated.highestLevel = std::max(
                updated.highestLevel,
                std::min(position + 2, static_cast<int>(levels.size())));
            updated.tutorialComplete = updated.tutorialComplete || m_tutorial.complete();
            updated.totalPlayTime += m_world.stats().elapsed;
            ctx.progress->saveProgress(updated);

            if (ctx.log != nullptr) {
                ctx.log->info("Game", "recorded '{}' complete", m_level.name);
            }
        }
    } else {
        // A defeat may offer a checkpoint retry, but only if one exists for this
        // level. Offering the option and having it quietly restart the whole region
        // would be a lie in the menu.
        results->setRetryFromCheckpoint(
            ctx.progress != nullptr && ctx.progress->readCheckpoint(m_level.id).valid);
    }

    ctx.states->push(std::move(results));
}

void PlayingState::syncTutorial(StateContext& ctx, const Game::PlayerInput& input,
                                const Game::WorldCommands& commands)
{
    static_cast<void>(ctx);

    if (!m_tutorial.active()) {
        return;
    }

    // Each lesson names the action that satisfies it. Only the lesson currently
    // being taught can advance, so pressing space during the movement lesson does
    // not skip ahead — `Tutorial::perform` ignores a mismatched action.
    const Game::TutorialStep current = m_tutorial.step();

    bool matched = false;
    switch (current) {
        case Game::TutorialStep::Move:
            // Any real horizontal input counts. Deliberately not "moved N tiles":
            // a lesson that waits for a distance teaches nothing the player can
            // feel, and a player who wiggles the stick should not be held at it.
            matched = input.moveAxis > 0.1f || input.moveAxis < -0.1f;
            break;

        case Game::TutorialStep::Jump:
            matched = input.jumpPressed;
            break;

        case Game::TutorialStep::Shift:
            matched = commands.shiftPressed;
            break;

        case Game::TutorialStep::Attack:
            matched = input.attackPressed;
            break;

        case Game::TutorialStep::Dash:
            matched = input.dashPressed;
            break;

        case Game::TutorialStep::Interact:
            matched = commands.interactPressed;
            break;

        case Game::TutorialStep::Enemy:
        case Game::TutorialStep::Seal:
        case Game::TutorialStep::Gate:
            // Not key-driven. These are taught by arriving, and the caller tells
            // the tutorial so by calling `skip` at the right moment.
            return;

        default:
            return;
    }

    if (matched && m_tutorial.perform(current)) {
        m_lesson     = m_tutorial.lesson();
        m_tutorialAge = 0.0f;
        if (ctx.log != nullptr) {
            ctx.log->debug("Game", "tutorial: {}", m_tutorial.step() == Game::TutorialStep::Complete
                                             ? std::string("complete")
                                             : std::string(m_lesson.title));
        }
    }
}

void PlayingState::say(std::string speaker, std::string message, float seconds)
{
    // Replaced wholesale. The panel keeps a title and a hint from the previous
    // line otherwise, which is how a character's name survives into the next
    // conversation.
    m_dialogue.clear();
    m_dialogue.show(std::move(speaker), std::move(message), seconds);
    m_dialogueAge = 0.0f;
}

void PlayingState::updateDialogue(float dt)
{
    if (!m_dialogue.open()) {
        return;
    }
    m_dialogueAge += dt;
    // Zero or negative means "until something clears it", which is how a line
    // that must be read rather than glanced at is expressed.
    if (m_dialogue.duration > 0.0f && m_dialogueAge >= m_dialogue.duration) {
        m_dialogue.clear();
    }
}

void PlayingState::syncCheckpoint(const StateContext& ctx)
{
    if (ctx.progress == nullptr || !m_ready) {
        return;
    }

    const Vec2 centre = m_world.player().body().center();
    const int tileX   = static_cast<int>(centre.x / Game::TileMap::kTileSize);
    const int tileY   = static_cast<int>(centre.y / Game::TileMap::kTileSize);

    if (!m_world.checkpointHere(tileX, tileY)) {
        return;
    }

    // Once per *volume*, not once per tile. The trigger is a radius, and a player
    // walking through one crosses four or five tiles — keying on the tile wrote a
    // row five times over a second of walking, which is a database write per
    // 200ms in the middle of ordinary movement.
    //
    // The volume is identified by its centre, not by the player: `checkpointHere`
    // answers "is there one here", so the identity has to come from the level.
    int volumeX = -1;
    int volumeY = -1;
    for (const Game::PlacedEntity& entity : m_level.entities) {
        if (entity.kind != Game::EntityKind::Checkpoint) {
            continue;
        }
        if (std::abs(entity.x - tileX) <= Game::Level::kCheckpointReach &&
            std::abs(entity.y - tileY) <= Game::Level::kCheckpointReach) {
            volumeX = entity.x;
            volumeY = entity.y;
            break;
        }
    }
    if (volumeX < 0) {
        return;
    }
    if (m_lastCheckpointVolume == std::make_pair(volumeX, volumeY)) {
        return;
    }
    m_lastCheckpointVolume = {volumeX, volumeY};

    Core::Checkpoint checkpoint;
    checkpoint.levelId  = m_level.id;
    checkpoint.tileX    = tileX;
    checkpoint.tileY    = tileY;
    checkpoint.era      = static_cast<int>(m_world.era());
    checkpoint.health   = m_world.player().health();
    checkpoint.chrono   = m_world.player().chrono();
    checkpoint.sealMask = m_world.sealMask();
    checkpoint.elapsed  = m_world.stats().elapsed;
    checkpoint.shifts   = m_world.stats().shifts;
    checkpoint.kills    = m_world.stats().kills;
    checkpoint.valid    = true;

    if (ctx.progress->writeCheckpoint(checkpoint) && ctx.log != nullptr) {
        ctx.log->info("Game", "checkpoint at {},{} ({})", tileX, tileY, m_level.name);
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

    // Whether the shift key is *down*, for the charge animation. Held rather than
    // pressed, and deliberately not "can afford a shift": the charge pose is
    // translucent, so deriving it from anything other than the key being held
    // leaves the player permanently see-through and never showing a walk cycle.
    const bool shiftHeld = ctx.input->isDown(Action::ShiftEra);

    // --- tutorial & checkpoints ---------------------------------------------
    // Before the simulation, so a lesson is satisfied by the press that caused
    // this step rather than the one after it.
    syncTutorial(ctx, input, commands);

    // The lesson panel's fade-in runs on the presentation clock, not the
    // simulation's, so a player standing in hit-stop does not see the prompt
    // freeze half-transparent.
    if (m_tutorial.active()) {
        m_tutorialAge += dt;
    }

    // --- outcomes -----------------------------------------------------------
    if (m_world.outcome() != Game::Outcome::Running) {
        // The world keeps rendering behind the results screen, so hold it for a
        // moment: cutting straight to a panel the instant the player dies takes
        // away the last frame of what killed them. A keypress skips the wait.
        //
        // The *animation* keeps running through that hold, which is the point of
        // holding at all. Returning here before the presentation updates froze the
        // player's rig one frame into the death clip for the whole 1.4 seconds, so
        // the last thing the player ever saw of their own run was a single frame of
        // it. Same arrangement as hit-stop: the simulation stops, the picture
        // finishes what it started.
        m_feedback.update(ctx, fixedDelta, m_world);
        updatePlayerAnimation(dt, shiftHeld);
        updateEnemyAnimations(dt);

        m_outcomeDelay -= dt;
        const bool confirmed = ctx.input->wasPressed(Action::Interact) ||
                               ctx.input->wasPressed(Action::Jump);
        if (confirmed || m_outcomeDelay <= 0.0f) {
            transitionToResult(ctx);
        }
        return;
    }
    m_outcomeDelay = kOutcomeDelay;

    m_world.update(input, commands, dt);
    m_currentPlayer = m_world.player().body().position;

    // --- events -> presentation ---------------------------------------------
    // The feedback system owns every reaction to an event. This loop only keeps
    // the toasts, because a toast is this state's business: it is text on this
    // screen, and no other state draws toasts.
    const std::vector<Game::EventRecord> events = m_world.takeEvents();
    for (const Game::EventRecord& event : events) {
        if (event.text.empty()) {
            continue;
        }
        m_toasts.push_back(event.text);
        m_toastTimer = kToastLifetime;
    }
    m_feedback.consume(events, m_world, ctx);

    // Runs on the full step even when the world is in hit-stop. That is the
    // point of hit-stop: the world is frozen and the spark that caused it keeps
    // travelling.
    m_feedback.update(ctx, fixedDelta, m_world);

    m_hurtFlash  = std::max(0.0f, m_hurtFlash - dt * 2.2f);
    m_shiftFlash = std::max(0.0f, m_shiftFlash - dt * 2.0f);
    m_toastTimer -= dt;
    if (m_toastTimer <= 0.0f && !m_toasts.empty()) {
        m_toasts.erase(m_toasts.begin());
        m_toastTimer = kToastLifetime * 0.55f;
    }

    // --- animation -----------------------------------------------------------
    updatePlayerAnimation(dt, shiftHeld);
    updateEnemyAnimations(dt);

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
    syncCheckpoint(ctx);
    updateDialogue(dt);

    // --- state transitions --------------------------------------------------
    if (ctx.input->wasPressed(Action::Pause)) {
        ctx.states->push(std::make_shared<PausedState>());
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
namespace {

/// Applies a rig pose to a rectangle: squash about the feet, then lean about
/// the centre.
///
/// Order matters and is not negotiable. Squashing about the *bottom* edge keeps
/// the character standing on the floor while it compresses; squashing about the
/// centre makes it sink into the ground. The lean is applied afterwards about
/// the result, because a lean about the original centre would slide the feet out
/// from under the body.
Rect poseRect(const Rect& base, const Game::Pose& pose, float facing)
{
    const Vec2 feet{base.center().x, base.bottom()};
    const float height = base.h * pose.scaleY;
    const float width  = base.w * pose.scaleX * (1.0f - pose.limbSpread * 0.12f);

    Rect out{0.0f, feet.y - height, width, height};
    out.x = feet.x - width * 0.5f + pose.offsetX * facing;

    if (pose.lean != 0.0f) {
        // A rectangle has no rotation, so a lean is faked by shearing the top
        // edge: the body leans and the feet stay put. At the angles a character
        // actually uses, the difference from a real rotation is invisible, and
        // it costs one subtraction instead of a quad.
        const float radians = pose.lean * 0.0174533f;
        out.x += std::sin(radians) * height;
    }
    return out;
}

} // namespace

void PlayingState::updatePlayerAnimation(float dt, bool shiftHeld)
{
    const Game::Player& player = m_world.player();
    const AnimId wanted = Game::playerClipFor(player, shiftHeld);

    // A reaction is forced. `play` refuses to cut a short action short, which is
    // right for two actions competing for the same gesture - a swing must not be
    // visually restarted by mashing - and wrong for a reaction, which has to be
    // seen or the game has failed to tell the player they were hit. Dying is the
    // same argument with more behind it: the death clip is the last thing anyone
    // sees of that run, and it must not queue behind a swing.
    const bool reaction = (wanted == AnimId::Hurt || wanted == AnimId::Death);
    m_playerAnim.play(wanted, reaction);

    // A swing is one clip covering all three simulation phases, so its time is
    // set from the simulation rather than advanced. Everything else free-runs on
    // its own clock, because nothing about it is load-bearing.
    if (wanted == AnimId::Attack) {
        m_playerAnim.setTime(player.attackProgress() * Game::clipFor(AnimId::Attack).duration);
    } else {
        m_playerAnim.update(dt);
    }

    // Footsteps come from the simulation, which counts them by ground covered
    // rather than by time. The clip's own footstep markers would be on a clock,
    // and a clock-tied footstep slides at low speed and scrabbles at high.
    if (player.stepEvents().footstep) {
        m_feedback.particles().emitFootfall(player.body().center().x, player.body().center().y,
                                            0.4f, themeFor(m_world.era()).accent);
    }
}

void PlayingState::updateEnemyAnimations(float dt)
{
    std::unordered_map<std::uint32_t, Game::AnimationController> live;
    live.reserve(m_world.enemies().size());

    for (const Game::Enemy& enemy : m_world.enemies()) {
        auto [it, inserted] = m_enemyAnims.try_emplace(enemy.id());
        if (inserted) {
            it->second.play(AnimId::EIdle, true);
        }
        // Only enemies in the current era animate; a sleeping one is drawn as an
        // outline, and a running walk cycle behind an outline would be odd.
        if (enemy.inCurrentEra()) {
            // A hurt enemy is forced for the same reason the player's is: an
            // enemy that is visibly struck and visibly unchanged reads as a miss,
            // which is the single most damaging thing this game can get wrong
            // about its own combat.
            const AnimId wanted = Game::enemyClipFor(enemy);
            it->second.play(wanted, wanted == AnimId::EHurt || wanted == AnimId::EDie);
            it->second.update(dt);
        }
        live.emplace(enemy.id(), it->second);
    }

    // Rebuild from `live` so controllers for removed enemies are freed. A run
    // with twelve enemies does not keep twelve controllers alive after it ends.
    m_enemyAnims = std::move(live);
}

Game::AnimationController& PlayingState::enemyAnimation(std::uint32_t id)
{
    auto [it, inserted] = m_enemyAnims.try_emplace(id);
    if (inserted) {
        it->second.play(AnimId::EIdle, true);
    }
    return it->second;
}

const Game::Pose& PlayingState::enemyPose(const Game::Enemy& enemy) const
{
    const auto it = m_enemyAnims.find(enemy.id());
    if (it == m_enemyAnims.end()) {
        static const Game::Pose neutral{};
        return neutral;
    }
    return it->second.pose();
}

void PlayingState::drawBackdrop(const StateContext& ctx, const Rect& area) const
{
    Renderer2D& renderer = *ctx.renderer;
    const Game::World& world = m_world;

    // Screen space.
    renderer.setCameraEnabled(false);

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

    // Screen space: the layers are positioned from the camera by hand, so the
    // transform must be off or they are drawn twice over.
    renderer.setCameraEnabled(false);
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
    //
    // The baselines sit high enough that the ridges form a band of distant
    // landscape behind the play area. Pushed lower they fill the bottom of the
    // frame with the same value as the tiles in front of them, and the level
    // stops being legible as a silhouette.
    constexpr Layer kLayers[] = {
        {0.12f, 110.0f, 1100.0f, 0.40f, 0x38},
        {0.26f, 150.0f, 760.0f, 0.48f, 0x50},
        {0.46f, 180.0f, 520.0f, 0.56f, 0x66},
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

    // World space. Every pass states its own camera and blend mode rather than
    // inheriting whatever the previous one happened to leave: relying on that
    // is how the tile layer ends up drawn in screen coordinates.
    renderer.setCameraEnabled(true);

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

    // Solid tiles are merged into horizontal runs before being drawn.
    //
    // A level is overwhelmingly floor, and a floor drawn cell by cell is ~900
    // draw calls for what is really a handful of rectangles. Merging only
    // *identical* cells (same era membership, same kind class, same exposure)
    // keeps every visual difference intact while collapsing the bulk of it.
    const Era era = m_world.era();
    const Era eraFrom = m_world.previousEra();

    for (int y = y0; y <= y1; ++y) {
        int x = x0;
        while (x <= x1) {
            const Tile tile = map.at(x, y);
            const bool hazard = tile.isHazardIn(era);
            const bool goal   = tile.kind == TileKind::Goal;
            const bool oneWay = tile.isOneWayIn(era) && tile.blocksIn(era);
            const bool solid  = !hazard && !goal && !oneWay && tile.blocksIn(era);

            if (tile.empty() || (!hazard && !goal && !oneWay && !solid)) {
                ++x;
                continue;
            }

            // How far does an identical run extend? Exposure has to match too,
            // or the merged block would draw a lit top edge across cells that
            // are buried.
            const bool exposed = solid && exposedAbove(map, x, y, era);
            const bool inFrom  = solid && tile.blocksIn(eraFrom);

            int run = 1;
            while (x + run <= x1) {
                const Tile next = map.at(x + run, y);
                if (next.empty() || next.isHazardIn(era) != hazard ||
                    (next.kind == TileKind::Goal) != goal ||
                    (next.isOneWayIn(era) && next.blocksIn(era)) != oneWay ||
                    next.blocksIn(era) != solid ||
                    (solid && exposedAbove(map, x + run, y, era) != exposed) ||
                    (solid && next.blocksIn(eraFrom) != inFrom)) {
                    break;
                }
                ++run;
            }

            const float left  = static_cast<float>(x) * kTile;
            const float top   = static_cast<float>(y) * kTile;
            const Rect   block{left, top, kTile * static_cast<float>(run), kTile};

            if (goal) {
                // The gate pulses so it is findable across a wide level.
                const float pulse =
                    0.55f + 0.45f * std::sin(static_cast<float>(m_time) * 3.0f);
                renderer.drawRect(block,
                                  to.accent.withAlpha(static_cast<std::uint8_t>(70 + 90 * pulse)));
                renderer.drawRectOutline(block, Palette::TextPrimary.withAlpha(0xC0), 3.0f);
            } else if (hazard) {
                // Hatches, not spikes: they read as dangerous without borrowing
                // a colour that belongs to the HUD.
                renderer.drawRect(block, to.hazard.withAlpha(0xCC));
                for (int i = 0; i < run * 3; ++i) {
                    const float t = static_cast<float>(i) / 3.0f;
                    renderer.drawLine({block.x + block.w * t, block.bottom() - 3.0f},
                                      {block.x + block.w * (t + 0.22f / static_cast<float>(run)),
                                       block.y + 3.0f},
                                      Palette::TextPrimary.withAlpha(0x90), 2.0f);
                }
            } else if (oneWay) {
                renderer.drawRect(Rect{block.x, block.y, block.w, 8.0f}, to.oneWay);
            } else {
                const float t = Graphics::clampValue(blend, 0.0f, 1.0f);
                // A tile present in both eras stays fully opaque the whole
                // time; one present in only one fades in or out with the shift.
                const Color body =
                    inFrom ? (tile.blocksIn(era)
                                  ? from.solid
                                  : from.solid.withAlpha(
                                        static_cast<std::uint8_t>(255.0f * (1.0f - t))))
                           : to.solid.withAlpha(static_cast<std::uint8_t>(255.0f * t));
                renderer.drawRect(block, body);

                if (exposed) {
                    const auto edgeAlpha = static_cast<std::uint8_t>(0xA0 * (body.a / 255.0f));
                    renderer.drawRect(
                        Rect{block.x, block.y, block.w, 3.0f},
                        (inFrom ? from.solidEdge : to.solidEdge).withAlpha(edgeAlpha));
                    // A lip just under the surface, so the top of a wall reads as
                    // an edge rather than a change of colour.
                    renderer.drawRect(Rect{block.x, block.y + 3.0f, block.w, 4.0f},
                                      body.scaled(1.18f));
                }

                // Interior speckle so a large floor is not a flat slab. Applied
                // per cell so the texture does not change with the run length.
                if (solid) {
                    for (int i = 0; i < run; ++i) {
                        if ((((x + i) * 7 + y * 13) % 5) != 0) {
                            continue;
                        }
                        renderer.drawRect(Rect{left + kTile * static_cast<float>(i) + 6.0f,
                                               top + 10.0f, kTile - 12.0f, 4.0f},
                                          body.scaled(1.25f));
                    }
                }
            }

            x += run;
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

    // The interaction prompt lives in the HUD, once, for the nearest seal.
    // Drawing it here as well put two prompts on screen at once and, with two
    // seals close together, printed them on top of each other.
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

        const Game::Pose& pose = enemyPose(enemy);
        const Rect drawable = poseRect(r, pose, enemy.facing());
        renderer.drawRect(drawable, bodyColour.withAlpha(static_cast<std::uint8_t>(255.0f * alpha)));
        renderer.drawRectOutline(drawable, theme.accent.withAlpha(0xC0), 2.0f);

        // Facing indicator: an eye that moves to the side it is looking at.
        const float eyeX = enemy.facing() > 0.0f ? drawable.right() - 10.0f : drawable.x + 4.0f;
        renderer.drawRect(Rect{eyeX, drawable.y + 7.0f, 6.0f, 6.0f}, Palette::Black);

        // A windup ring: the telegraph made literal. It appears exactly when the
        // enemy commits and grows as the commit completes, so the moment it turns
        // dangerous is the moment the ring is full.
        if (enemy.state() == Game::EnemyState::Windup) {
            const float wind = Graphics::clampValue(enemy.stateProgress(), 0.0f, 1.0f);
            const float radius = 14.0f + 16.0f * wind;
            const Color warn = Palette::Warning.withAlpha(
                static_cast<std::uint8_t>(0x60 + 0x90 * wind));
            const Rect ring{drawable.center().x - radius, drawable.center().y - radius,
                            radius * 2.0f, radius * 2.0f};
            renderer.drawRectOutline(ring, warn, 1.0f + wind);
        }

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
    const Game::Pose& pose = m_playerAnim.pose();
    const float facing = player.facing();

    // The animation supplies the squash, so the old velocity-derived one is gone.
    // Two competing squash terms is one too many, and the one driven by speed is
    // the one that made a fast fall look like a small hop.
    const Rect body = poseRect(Rect{interpolated.x, interpolated.y, player.body().size.x,
                                    player.body().size.y},
                               pose, facing);

    // Contact shadow. Without it the player floats and the jump arc is much
    // harder to read. It is drawn at the interpolated position so it slides with
    // the body rather than snapping a step behind it.
    const float shadowY = static_cast<float>(m_world.map().rows()) * kTile;
    renderer.drawRect(Rect{body.center().x - body.w * 0.45f, shadowY - 4.0f, body.w * 0.9f, 4.0f},
                      Palette::Black.withAlpha(0x40));

    // Invulnerability is shown by flickering, which reads instantly and needs no
    // extra UI explaining it.
    const bool flicker =
        player.invulnerable() && (static_cast<int>(m_time * 22.0f) % 2 == 0);

    if (!flicker) {
        const std::uint8_t alpha =
            static_cast<std::uint8_t>(Graphics::clampValue(pose.alpha, 0.0f, 1.0f) * 255.0f);

        // --- legs ---------------------------------------------------------
        // The rig is four rectangles: two legs, a body, a leading arm. The legs
        // swing on `limbSwing` and the arm extends with `armExtension`, which is
        // what makes a windup read as a windup before the hitbox opens.
        const float swing = pose.limbSwing;
        const float legH = body.h * 0.30f;
        const float legW = std::max(3.0f, body.w * 0.24f);
        const Color limb = theme.actor.scaled(0.72f);
        renderer.drawRect(Rect{body.x + body.w * 0.18f - swing * 4.0f, body.bottom() - legH,
                               legW, legH},
                          limb.withAlpha(alpha));
        renderer.drawRect(Rect{body.x + body.w * 0.82f - body.w * 0.24f + swing * 4.0f,
                               body.bottom() - legH, legW, legH},
                          limb.withAlpha(alpha));

        renderer.drawRect(body, theme.actor.withAlpha(alpha));
        renderer.drawRectOutline(body, theme.accent.withAlpha(alpha), 2.0f);

        // A visor that looks the way the player is facing, and which leads the
        // lean so the head still points where the character is going.
        const float visorX = body.center().x + (facing > 0.0f ? 2.0f : -10.0f);
        renderer.drawRect(Rect{visorX, body.y + body.h * 0.18f, 8.0f, 5.0f},
                          theme.accent.withAlpha(alpha));

        // The leading arm, which is the one that reaches out during a swing.
        const float armLen = body.h * 0.34f * (1.0f + pose.armExtension * 0.9f);
        const float armX = body.center().x + (facing > 0.0f
                                                  ? body.w * 0.35f + armLen * 0.5f * facing
                                                  : -body.w * 0.35f + armLen * 0.5f * facing);
        renderer.drawRect(Rect{armX - armLen * 0.5f, body.y + body.h * 0.32f, armLen,
                               std::max(3.0f, body.w * 0.2f)},
                          theme.accent.withAlpha(alpha));
    }

    // The swing itself, drawn as an arc of the hit box so the reach is visible.
    // Only while the hit window is genuinely open: the simulation decides, and
    // the picture agrees with it frame for frame.
    const Rect swing = player.attackBox();
    if (!swing.isEmpty()) {
        // Fades across the window rather than blinking, so the player can see how
        // much of the swing is left.
        const float window = Graphics::clampValue(player.attackProgress(), 0.0f, 1.0f);
        renderer.drawRect(swing, theme.accent.withAlpha(static_cast<std::uint8_t>(0x33 * window)));
        renderer.drawRectOutline(swing, theme.accent.withAlpha(0xCC), 2.0f);
    }

    // Dash trail: echoes of the body trailing behind the direction of travel,
    // fading out. Reads as speed without needing a particle.
    if (player.dashing()) {
        for (int i = 1; i <= 3; ++i) {
            const float trailX = body.x - facing * static_cast<float>(i) * 12.0f;
            renderer.drawRectOutline(Rect{trailX, body.y, body.w, body.h},
                                     theme.accent.withAlpha(static_cast<std::uint8_t>(0x50 - i * 0x10)),
                                     2.0f);
        }
    }

    renderer.setBlendMode(BlendMode::None);
}

float PlayingState::hudBottomReserved(Graphics::UiScale scale) noexcept
{
    // The tallest thing `drawHud` puts in the bottom stack is the interaction
    // prompt, which sits `kPromptLift` above the hint row. Measured from the same
    // numbers `drawHud` uses rather than restated, so the two cannot drift — a
    // dialog panel drawn over the objective line is invisible until somebody
    // reports it.
    constexpr float kPad        = 18.0f;
    constexpr float kHintHeight = 18.0f;
    constexpr float kPromptLift = 46.0f;
    return scale.px(kPad * 0.5f + kHintHeight + kPromptLift);
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

    // --- scrims -------------------------------------------------------------
    // The three eras have wildly different brightnesses - a sunlit Past sky and
    // a near-black Future one - and the HUD is the same dim grey in all of
    // them. A soft gradient top and bottom keeps the text legible over any of
    // them without putting a hard-edged box on screen.
    {
        constexpr int kStrips = 12;
        const float topH = scale.px(112.0f);
        for (int i = 0; i < kStrips; ++i) {
            const float f = 1.0f - static_cast<float>(i) / static_cast<float>(kStrips);
            const Rect strip{area.x, area.y + topH * static_cast<float>(i) /
                                               static_cast<float>(kStrips),
                             area.w, topH / static_cast<float>(kStrips) + 1.0f};
            renderer.drawRect(strip, Color{0, 0, 0, static_cast<std::uint8_t>(f * 150.0f)});
        }

        const float bottomH = scale.px(84.0f);
        for (int i = 0; i < kStrips; ++i) {
            const float f = static_cast<float>(i) / static_cast<float>(kStrips - 1);
            const Rect strip{area.x, area.bottom() - bottomH * static_cast<float>(i + 1),
                             area.w, bottomH / static_cast<float>(kStrips) + 1.0f};
            renderer.drawRect(strip, Color{0, 0, 0, static_cast<std::uint8_t>(f * f * 170.0f)});
        }
    }

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
    // A tiered gauge rather than a plain bar. The tiers are where the game
    // actually changes - the screen distorts, the weather thickens - so a
    // number the player has to interpret is the wrong thing to show them. The
    // ticks say "you are approaching something", which is actionable, and the
    // label says what you are currently in.
    const Rect paradox{pad, leftY, barW, scale.px(7.0f)};
    constexpr float kParadoxMax = 600.0f;
    const float paradoxFill = Graphics::clampValue(m_world.stats().paradox / kParadoxMax, 0.0f, 1.0f);
    drawBar(renderer, paradox, paradoxFill, Palette::ParadoxFill, Palette::ParadoxBack);

    // Tier boundaries, drawn as gaps in the fill rather than as marks above it,
    // so they read at a glance and cost nothing.
    for (Game::ParadoxTier tier : {Game::ParadoxTier::Strained, Game::ParadoxTier::Fractured,
                                    Game::ParadoxTier::Collapse}) {
        const float t = Game::tierThreshold(tier) / kParadoxMax;
        const float x = paradox.x + paradox.w * Graphics::clampValue(t, 0.0f, 1.0f);
        renderer.drawRect(Rect{x - scale.px(1.0f), paradox.y - scale.px(2.0f), scale.px(2.0f),
                              paradox.h + scale.px(4.0f)},
                          Palette::ParadoxBack.withAlpha(0xFF));
    }
    // The tier's own colour, brightening as the tier does. At Collapse the bar
    // is the same hue as the vignette, which ties the two together visually.
    const Game::ParadoxTier tier = m_feedback.tier();
    const Color tierColour = (tier == Game::ParadoxTier::Collapse)  ? Palette::Warning
                             : (tier == Game::ParadoxTier::Fractured) ? Palette::ParadoxFill
                             : (tier == Game::ParadoxTier::Strained)  ? Palette::ParadoxFill.scaled(0.8f)
                                                                     : Palette::ParadoxFill.scaled(0.6f);
    if (paradoxFill > 0.0f) {
        renderer.drawRect(Rect{paradox.x, paradox.y, paradox.w * paradoxFill, paradox.h},
                          tierColour.withAlpha(0xFF));
    }
    {
        TextStyle tierStyle = label;
        tierStyle.align     = TextAlign::Right;
        // The label pulses as the tier changes, so crossing a boundary is felt
        // and not just noticed. Driven by the tier crossing rather than by the
        // screen flash: a flash also fires on damage and on pickups, and a label
        // that pulses when you collect a health cell is lying about why.
        const float pulse = 0.65f + 0.35f * m_feedback.tierPulse();
        tierStyle.color = tierColour.scaled(0.6f + 0.4f * pulse);
        text.drawAligned(renderer, Vec2{pad + barW, leftY + paradox.h * 0.5f - scale.px(7.0f)},
                         std::string("PARADOX  ") + std::string(Game::toString(tier)), tierStyle);
    }
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

    drawTutorialPanel(ctx, area);

    // The dialogue sits above the tutorial panel: a line being spoken is the more
    // urgent of the two, and the tutorial is a hint the player can re-see.
    drawDialoguePanel(ctx, m_dialogue, m_currentPlayer, area.h, currentUiScale(ctx),
                       hudBottomReserved(currentUiScale(ctx)));

    renderer.setBlendMode(BlendMode::None);

    if (!text.ready()) {
        // Keep something legible on screen if the TTF face never loaded.
        FontStyle fallback;
        fallback.scale = 2;
        BitmapFont::drawCentered(renderer, area.center().x, area.bottom() - scale.px(30.0f),
                                 m_world.objectiveText(), Palette::TextPrimary, fallback);
    }
}

void PlayingState::drawTutorialPanel(const StateContext& ctx, const Rect& area) const
{
    if (!m_tutorial.active() || m_lesson.title == nullptr || m_lesson.title[0] == '\0') {
        return;
    }
    if (ctx.renderer == nullptr || ctx.text == nullptr) {
        return;
    }

    Renderer2D& renderer = *ctx.renderer;
    Graphics::TextRenderer& text = *ctx.text;
    const UiScale scale = currentUiScale(ctx);

    // Fades in over its first third of a second. A panel that appears at full
    // opacity on a single frame reads as a pop-up the game threw at the player;
    // one that is already 40% faded reads as arriving.
    const float fade = Graphics::clampValue(m_tutorialAge / 0.30f, 0.0f, 1.0f);

    // Top-left, below the toasts, so it never covers the player or the objective.
    const float panelW = std::min(scale.px(300.0f), area.w - scale.px(24.0f));
    const float panelH = scale.px(62.0f);
    const Rect panel{area.x + scale.px(12.0f), area.y + scale.px(12.0f), panelW, panelH};

    const auto faded = [fade](Color colour) {
        return colour.withAlpha(static_cast<std::uint8_t>(
            static_cast<float>(colour.a) * (0.4f + 0.6f * fade)));
    };

    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(panel, faded(Palette::PanelFill));
    renderer.drawRectOutline(panel, faded(Palette::PanelBorder), 1.0f);

    TextStyle title = m_labelStyle;
    title.color     = faded(Palette::Accent);
    title.bold      = true;
    text.drawInRect(renderer,
                    Rect{panel.x + scale.px(10.0f), panel.y + scale.px(6.0f),
                         panel.w - scale.px(20.0f), scale.px(16.0f)},
                    m_lesson.title, title);

    // Wrapped and clipped to two lines, so a long instruction cannot spill out of
    // the panel and over the world.
    TextStyle body = m_labelStyle;
    body.color     = faded(Palette::TextPrimary);
    body.wrap      = true;
    body.maxLines  = 2;
    text.drawInRect(renderer,
                    Rect{panel.x + scale.px(10.0f), panel.y + scale.px(24.0f),
                         panel.w - scale.px(20.0f), scale.px(28.0f)},
                    m_lesson.body, body);

    if (m_lesson.key != nullptr && m_lesson.key[0] != '\0') {
        TextStyle key = m_labelStyle;
        key.color     = faded(Palette::TextDim);
        key.align     = TextAlign::Right;
        text.drawInRect(renderer,
                        Rect{panel.x, panel.bottom() - scale.px(15.0f), panel.w - scale.px(10.0f),
                             scale.px(12.0f)},
                        m_lesson.key, key);
    }
    renderer.setBlendMode(BlendMode::None);
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
    drawParallax(ctx, area);
    drawTiles(ctx, blend);
    drawSeals(ctx, blend);
    drawPickups(ctx);
    drawEnemies(ctx);
    drawPlayer(ctx, interpolated);

    // Effects that live in the world sit over the actors but under the HUD, so
    // an explosion cannot obscure the health the player needs to read.
    m_feedback.drawWorld(ctx);

    drawHud(ctx, area);

    // Atmosphere under the text. Paradox is meant to make the *world* hard to
    // look at, not the objective line - a player who cannot read what to do next
    // is not under pressure, they are stuck.
    m_feedback.drawUnderlay(ctx);

    // A shift's flash goes over everything: it is light, not decoration, and it
    // is meant to wash the whole frame.
    m_feedback.drawOverlay(ctx);

    // Toasts last, so the newest thing that happened is the clearest thing on
    // screen no matter what the run looks like underneath it.
    drawToasts(ctx, area);

    if (ctx.stats != nullptr) {
        // The overlay's Entities row is the simulation's to report: only the
        // state knows what is alive, and a counter owned by the engine would
        // have to be told, which is exactly this.
        ctx.stats->setEntityCount(1 + m_world.enemies().size() + m_world.pickups().size() +
                                  m_world.seals().size());
    }

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        renderer.setBlendMode(BlendMode::Alpha);
        ctx.overlay->render(renderer, *ctx.text, *ctx.stats, *ctx.states, ctx.loop->stats());
        renderer.setBlendMode(BlendMode::None);
    }

    static_cast<void>(ctx.text);
}

} // namespace EraShift::Game
