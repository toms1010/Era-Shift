// Era Shift - the world.
//
// `World` is the whole simulation: the tile grid, the player, the enemies, the
// pickups, the seals and the rules that connect them. It has no rendering and no
// knowledge of SDL, so the entire game can be stepped in a unit test.
//
// The rules it enforces, in the order they matter:
//
//   1. Shifting eras costs chrono energy and rewrites which tiles are solid,
//      which enemies exist, and what the seals respond to.
//   2. A swing only connects with enemies that exist in the current era. This
//      is what stops "swing at everything" being a universal answer and gives
//      the era a defensive use as well as a mobility one.
//   3. The gate stays shut until every era seal has been taken, and each seal
//      can only be taken while the world is in that seal's era. The level is
//      therefore a sequence of era switches, not a platforming corridor.
//   4. Touching the gate with all three seals wins. Running out of health, or
//      falling out of the world, loses.

#pragma once

#include "EraShift/Game/Era.hpp"
#include "EraShift/Game/Enemy.hpp"
#include "EraShift/Game/Level.hpp"
#include "EraShift/Game/Player.hpp"
#include "EraShift/Game/TileMap.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <string>
#include <vector>

namespace EraShift::Game {

using Graphics::Rect;
using Graphics::Vec2;

/// How a run ended.
enum class Outcome : std::uint8_t {
    Running,
    Victory,
    Defeat,
};

[[nodiscard]] std::string_view outcomeName(Outcome outcome) noexcept;

/// A collectable sitting in the level.
struct Pickup {
    Body       body;
    PickupKind kind = PickupKind::ChronoCell;
    bool       collected = false;
    /// Seconds until it comes back. Collectables respawn so a run cannot be
    /// made unwinnable by spending the only health in the level.
    float      respawnTimer = 0.0f;
};

/// A pillar that yields its seal only while the world is in its era.
struct Seal {
    Vec2  position;
    Era   era = Era::Past;
    std::string label;
    bool  collected = false;
    /// Distance at which interacting takes it.
    float reach = 52.0f;
};

/// Counters for the end-of-run summary.
struct RunStats {
    double elapsed   = 0.0;
    int    shifts    = 0;
    int    kills     = 0;
    int    sealsTaken = 0;
    int    damageTaken = 0;
    float  paradox   = 0.0f;

    /// Score used on the results screen. Weighted so that finishing the level
    /// is worth far more than farming, without making aggression worthless.
    [[nodiscard]] int score() const noexcept
    {
        return static_cast<int>(static_cast<float>(kills * 120 + sealsTaken * 750) +
                                paradox * 3.0f);
    }
};

/// One-shot things that happened this step, for the state to turn into toasts,
/// sound and particles.
enum class WorldEvent : std::uint8_t {
    EraShifted,
    EraShiftFailed,
    SealTaken,
    SealWrongEra,
    GateSealed,
    GateOpened,
    PickupTaken,
    EnemyKilled,
    PlayerHurt,
    PlayerDied,
    Victory,

    // --- presentation events -------------------------------------------------
    // The kinds above are things the player is told about in words. These are
    // things they are shown: they carry a position and no text, because a spark
    // at the point of contact says "that landed" better than a toast does, and a
    // toast for every footstep would be unreadable.
    PlayerJumped,
    PlayerLanded,
    PlayerDashed,
    SwingStarted,
    /// The hit window opened. Not the same as SwingStarted: a swing that whiffs
    /// still makes a sound, but only a swing that connects throws sparks.
    SwingActive,
    PlayerFootstep,
    EnemyHurt,
    HazardTick,
};

struct EventRecord {
    WorldEvent kind = WorldEvent::EraShifted;
    std::string text;
    /// Where it happened, in world space. Meaningless for the word-events, which
    /// default to the player's position.
    Vec2  position{};
    /// Which way the impact faced. Drives the spark cone and the camera punch.
    Vec2  direction{1.0f, 0.0f};
    /// 0..1, how hard it was. A graze and a full swing are the same event with
    /// different numbers, which is how one emitter covers both.
    float strength = 1.0f;
    /// What the player was standing on, for the events where the surface is the
    /// point. Carried on the event rather than looked up again by the feedback
    /// system, so presentation cannot disagree with the simulation about where
    /// the player actually is.
    TileKind surface = TileKind::Empty;
};

/// Non-player actions, kept apart from `PlayerInput` so the movement controller
/// stays a pure function of movement.
struct WorldCommands {
    bool shiftPressed    = false;   ///< Cycle to the next era.
    bool interactPressed = false;   ///< Take a seal or open the gate.
};

/// Owns and simulates everything in a level.
class World {
public:
    World();

    /// Replaces the level and restarts the run.
    void load(const Level& level);

    /// Restarts the current level from its spawn, keeping the level data.
    void restart();

    /// Advances one fixed step.
    void update(const PlayerInput& input, const WorldCommands& commands, float dt);

    /// Effects the player just suffered, for the HUD flash and camera shake.
    /// Cleared at the start of every step.
    [[nodiscard]] float lastDamageFlash() const noexcept { return m_damageFlash; }
    [[nodiscard]] bool  playerHurtThisStep() const noexcept { return m_playerHurt; }

    /// Seconds of hit-stop remaining.
    ///
    /// Hit-stop is a real simulation pause, not a render trick: on a heavy hit
    /// the world holds still for a few frames. Implemented here rather than in
    /// the camera because it has to stop the *enemy*, or the player can land a
    /// blow, watch the world freeze, and still be hit by something mid-freeze.
    [[nodiscard]] float hitStopRemaining() const noexcept { return m_hitStop; }

    /// Drains the event log.
    ///
    /// A queue, not a snapshot: events survive until they are taken, so a
    /// caller that polls every step sees everything and a caller that polls
    /// late still sees what it missed. The world only reports what happened;
    /// what to show about it is the caller's decision.
    [[nodiscard]] std::vector<EventRecord> takeEvents();

    /// Events waiting to be taken.
    [[nodiscard]] std::size_t pendingEvents() const noexcept { return m_events.size(); }

    // --- queries ------------------------------------------------------------

    [[nodiscard]] const Level& level() const noexcept { return m_level; }

    /// False until `load` has been called. An unloaded world has no map and a
    /// player with no health, so simulating it would report a defeat the player
    /// never earned.
    [[nodiscard]] bool loaded() const noexcept { return m_loaded; }
    /// Direct access for restoring a save. The const overloads are what
    /// everything else should be using.
    [[nodiscard]] Player& mutablePlayer() noexcept { return m_player; }
    [[nodiscard]] const TileMap& map() const noexcept { return m_map; }
    [[nodiscard]] const Player& player() const noexcept { return m_player; }
    [[nodiscard]] const std::vector<Enemy>& enemies() const noexcept { return m_enemies; }
    [[nodiscard]] const std::vector<Pickup>& pickups() const noexcept { return m_pickups; }
    [[nodiscard]] const std::vector<Seal>& seals() const noexcept { return m_seals; }
    [[nodiscard]] const RunStats& stats() const noexcept { return m_stats; }

    [[nodiscard]] Outcome outcome() const noexcept { return m_outcome; }
    [[nodiscard]] Era era() const noexcept { return m_player.era(); }
    /// The era being shifted away from, valid while a transition is running.
    [[nodiscard]] Era previousEra() const noexcept { return m_previousEra; }
    /// 1 when fully settled in the current era, 0 at the instant of the shift.
    [[nodiscard]] float eraBlend() const noexcept { return m_player.eraBlend(); }

    [[nodiscard]] int sealsTaken() const noexcept;
    /// Bit `i` is set when seal `i` has been taken.
    [[nodiscard]] int sealMask() const noexcept;
    /// Restores which seals were taken, from a saved bitmask.
    void applySeals(int mask);

    /// Restarts the level and then rewinds the player to a checkpoint.
    ///
    /// The distinction from `restart()` is the whole point of a checkpoint:
    /// enemies and pickups are rebuilt from the placement list, but the player
    /// resumes where they were, with the seals they had already taken. Restarting
    /// from the spawn would make a checkpoint identical to retrying the region.
    ///
    /// The position is pushed out of any solid tile it happens to land in, so a
    /// checkpoint written while the player was mid-air or inside geometry comes
    /// back somewhere survivable rather than stuck.
    void restoreCheckpoint(const Vec2& position, Era era, float health, float chrono, int sealMask);

    /// Where a checkpoint should be written for this level, and whether one is
    /// warranted.
    ///
    /// A checkpoint is taken on reaching a trigger volume, never on a timer: a
    /// timed checkpoint writes during a fight, and a save written mid-combat is
    /// a save of a half-finished swing.
    [[nodiscard]] bool checkpointHere(int tileX, int tileY) const noexcept;
    /// Restores the run counters. Takes plain values rather than the save
    /// struct so the gameplay layer never has to know the save format.
    void applyRunStats(double elapsed, int shifts, int kills, float paradox);

    /// How many seals the finish line needs, resolved.
    ///
    /// `Level::requiresSeals` when the file states it, otherwise every seal
    /// placed. Resolved once at load rather than read per frame, because "how many
    /// seals does this level need" is a question about the level and answering it
    /// from the live seal list would let a level that places two seals quietly ask
    /// for one.
    [[nodiscard]] int requiredSeals() const noexcept { return m_requiredSeals; }

    /// Whether the finish line will open for the seals collected so far.
    ///
    /// Requires a positive requirement: a level with no seals and no stated
    /// requirement has nothing to collect, and calling that "open" would complete
    /// the level by walking into it.
    [[nodiscard]] bool gateOpen() const noexcept;

    /// How many more seals the finish line needs, or 0 when it is open.
    [[nodiscard]] int sealsRemaining() const noexcept;

    /// Whether the player is standing in the finish marker right now.
    ///
    /// Separate from the outcome because the two are asked at different times and
    /// for different reasons: the HUD asks "is the player at the gate" every frame
    /// to decide whether to prompt, while the outcome is decided once. Asking the
    /// question through one predicate is what stops the prompt and the completion
    /// rule from disagreeing about where the gate is.
    [[nodiscard]] bool atFinish() const noexcept;

    /// The finish marker as the player approaches it: 0 far away, 1 on top of it.
    ///
    /// Drives the marker brightening and pulsing harder as the player closes in, so
    /// the gate announces itself before it is reached rather than only when it is
    /// already underfoot.
    [[nodiscard]] float finishProximity() const noexcept;
    /// Rect of the goal marker, empty when the level has none.
    [[nodiscard]] Rect goalBounds() const noexcept { return m_goalBounds; }

    [[nodiscard]] int enemyCount() const noexcept;
    /// Ids currently in play, for a caller keeping per-enemy state.
    [[nodiscard]] std::vector<std::uint32_t> enemyIds() const;
    /// Enemies that exist and are awake in the current era.
    [[nodiscard]] int activeEnemyCount() const noexcept;

    /// The line the HUD shows as the current objective.
    [[nodiscard]] std::string objectiveText() const;

    /// Distance from the player to the nearest uncollected seal, or -1.
    [[nodiscard]] float nearestSealDistance() const noexcept;

private:
    void emit(WorldEvent kind, std::string text);
    void emit(WorldEvent kind, Vec2 position, Vec2 direction, float strength,
              TileKind surface = TileKind::Empty);
    /// The tile under the player's feet, for the events that care what they are
    /// standing on.
    [[nodiscard]] TileKind surfaceUnderfoot() const noexcept;
    void updateEraShift(const WorldCommands& commands);
    /// Turns the player's one-step edges into world events, so the presentation
    /// layer has a single event stream to read rather than two to correlate.
    void forwardPlayerEvents();
    void updateSealsAndGate(const WorldCommands& commands);
    void updatePickups(float dt);
    void updateCombat(float dt);
    void updateEnemies(float dt, float frameIndex);
    void removeDeadEnemies();
    void updateOutcome();

    Level              m_level;
    /// Resolved from `Level::requiresSeals` at load; see `requiredSeals`.
    int                m_requiredSeals = 0;
    TileMap            m_map;
    Player             m_player;
    std::vector<Enemy> m_enemies;
    std::vector<Pickup> m_pickups;
    std::vector<Seal>   m_seals;

    bool    m_loaded = false;
    Era     m_previousEra = Era::Present;
    Outcome m_outcome     = Outcome::Running;
    RunStats m_stats;
    Rect     m_goalBounds;

    std::vector<EventRecord> m_events;
    float m_frameIndex = 0.0f;
    float m_damageFlash = 0.0f;
    /// Counts down before a hazard can bite again, so standing in one is a
    /// punishment rather than an instant kill.
    float m_hazardCooldown = 0.0f;
    bool  m_playerHurt = false;
    /// Set while a single swing is live so it cannot hit two enemies.
    bool  m_swingResolved = false;

    /// Source of enemy identities. Never resets, so an id is unique for the whole
    /// session rather than just the current attempt.
    std::uint32_t m_nextEnemyId = 1;

    float m_hitStop = 0.0f;
    /// Distance walked since the last footfall, mirrored from the player so the
    /// world's footstep events stay in step with the animation's.
    float m_strideDistance = 0.0f;
};

} // namespace EraShift::Game
