#include "EraShift/Game/World.hpp"

#include <algorithm>
#include <cmath>

namespace EraShift::Game {

namespace {

/// Upper bound on the undrained event queue. A caller that polls every step
/// never comes close; this exists so one that does not cannot leak.
constexpr std::size_t kMaxEvents = 32;

/// Seconds a pickup stays gone before it returns.
constexpr float kPickupRespawn = 6.0f;
/// Chrono energy a cell restores.
constexpr float kChronoCellAmount = 30.0f;
/// Damage a hazard does, and how often it can re-trigger.
constexpr float kHazardDamage = 1.0f;
constexpr float kHazardCooldown = 0.7f;
/// Distance at which a seal can be taken by interacting.
///
/// Wide enough to take from the floor beneath it, which is how a player will
/// almost always approach one: the pillar is decoration and the interaction is
/// the seal.
constexpr float kSealReach = 76.0f;
/// Paradox awarded for each kind of event, shown on the results screen.
constexpr float kParadoxPerShift = 12.0f;

/// Seconds of freeze for each kind of impact. A graze should barely register; a
/// kill and a shift are the two moments the game is allowed to stop the world.
constexpr float kHitStopHit       = 0.045f;
constexpr float kHitStopKill      = 0.11f;
constexpr float kHitStopPlayerHit = 0.09f;
constexpr float kHitStopShift     = 0.16f;
constexpr float kHitStopSeal      = 0.20f;

/// Midpoint of the overlap between two rects, falling back to the midpoint of
/// their centres when they only just touch. `Rect` has no intersection helper
/// in this codebase, and adding one for a single call site would be the wrong
/// trade.
Vec2 midpointOfContact(const Rect& a, const Rect& b, const Vec2& centreA, const Vec2& centreB)
{
    const float left   = std::max(a.left(), b.left());
    const float right  = std::min(a.right(), b.right());
    const float top    = std::max(a.top(), b.top());
    const float bottom = std::min(a.bottom(), b.bottom());
    if (right > left && bottom > top) {
        return Vec2{(left + right) * 0.5f, (top + bottom) * 0.5f};
    }
    return (centreA + centreB) * 0.5f;
}
constexpr float kParadoxPerSeal  = 60.0f;
constexpr float kParadoxPerKill  = 25.0f;

Rect shrinkToCentre(const Rect& rect, float factor) noexcept
{
    const float w = rect.w * factor;
    const float h = rect.h * factor;
    return Rect{rect.center().x - w * 0.5f, rect.center().y - h * 0.5f, w, h};
}

} // namespace

std::string_view outcomeName(Outcome outcome) noexcept
{
    switch (outcome) {
        case Outcome::Running: return "Running";
        case Outcome::Victory: return "Victory";
        case Outcome::Defeat:  return "Defeat";
    }
    return "Unknown";
}

World::World() = default;

void World::emit(WorldEvent kind, std::string text)
{
    // Bounded so a caller that never drains the queue cannot grow it without
    // limit. Past the bound the oldest entries go, which is the right end to
    // lose: the newest event is the one a player needs to see.
    if (m_events.size() >= kMaxEvents) {
        m_events.erase(m_events.begin());
    }
    m_events.push_back(EventRecord{kind, std::move(text), m_player.body().center(), Vec2{1.0f, 0.0f}, 1.0f});
}

void World::emit(WorldEvent kind, Vec2 position, Vec2 direction, float strength, TileKind surface)
{
    if (m_events.size() >= kMaxEvents) {
        m_events.erase(m_events.begin());
    }
    EventRecord record;
    record.kind      = kind;
    record.position  = position;
    record.direction = direction;
    record.strength  = Graphics::clampValue(strength, 0.0f, 4.0f);
    record.surface   = surface;
    m_events.push_back(std::move(record));
}

std::vector<EventRecord> World::takeEvents()
{
    std::vector<EventRecord> out;
    out.swap(m_events);
    return out;
}

void World::load(const Level& level)
{
    m_level  = level;
    m_map    = level.buildMap();
    m_loaded = true;

    m_enemies.clear();
    m_pickups.clear();
    m_seals.clear();
    m_events.clear();

    m_previousEra  = Era::Present;
    m_outcome      = Outcome::Running;
    m_stats        = RunStats{};
    m_goalBounds   = Rect{};
    m_frameIndex   = 0.0f;
    m_damageFlash  = 0.0f;
    m_playerHurt   = false;
    m_swingResolved = false;

    for (const PlacedEntity& entity : m_level.entities) {
        const Vec2 at = m_level.worldPosition(entity);
        switch (entity.kind) {
            case EntityKind::PlayerSpawn:
                m_map.set(entity.x, entity.y, Tile::of(TileKind::Empty));
                break;

            case EntityKind::Enemy: {
                Enemy enemy;
                enemy.spawn(entity.enemy, at, entity.existsIn);
                enemy.setId(m_nextEnemyId++);
                m_enemies.push_back(enemy);
                break;
            }

            case EntityKind::Pickup: {
                Pickup pickup;
                pickup.kind = entity.pickup;
                pickup.body = Body{};
                pickup.body.size = Vec2{20.0f, 20.0f};
                pickup.body.position = at;
                m_pickups.push_back(pickup);
                break;
            }

            case EntityKind::Seal: {
                Seal seal;
                seal.position = at;
                seal.era      = entity.sealEra;
                seal.label    = entity.label.empty()
                                    ? (std::string(eraName(entity.sealEra)) + " SEAL")
                                    : entity.label;
                seal.reach    = kSealReach;
                m_seals.push_back(seal);
                break;
            }

            case EntityKind::Checkpoint:
                // Nothing to construct. The volume is queried from the level's
                // placement list when the player walks into it, so there is no
                // runtime state to keep and none to get out of step.
                break;
        }
    }

    // The goal marker is part of the tile grid rather than an entity, so it is
    // located by scanning for it. Levels without one are still playable; the
    // gate simply never opens.
    for (int y = 0; y < m_map.rows(); ++y) {
        for (int x = 0; x < m_map.columns(); ++x) {
            if (m_map.at(x, y).kind == TileKind::Goal) {
                m_goalBounds = m_map.cellRect(x, y);
            }
        }
    }

    restart();
}

void World::restart()
{
    // The placement list is the source of truth, so every runnable thing is
    // rebuilt from it rather than having its previous state carried over.
    const PlacedEntity* spawn = m_level.find(EntityKind::PlayerSpawn);
    const Vec2 at = spawn != nullptr ? m_level.worldPosition(*spawn) : Vec2{32.0f, 32.0f};

    m_player.reset(at, Era::Present);
    resolvePenetration(m_map, m_player.body(), m_player.era());

    std::vector<Enemy> rebuilt;
    rebuilt.reserve(m_level.entities.size());
    for (const PlacedEntity& entity : m_level.entities) {
        if (entity.kind != EntityKind::Enemy) {
            continue;
        }
        Enemy enemy;
        enemy.spawn(entity.enemy, m_level.worldPosition(entity), entity.existsIn);
        // Ids keep counting up across a restart rather than starting over, so a
        // stale presentation entry from the previous attempt can never be
        // mistaken for the same enemy.
        enemy.setId(m_nextEnemyId++);
        rebuilt.push_back(enemy);
    }
    m_enemies.swap(rebuilt);

    for (Pickup& pickup : m_pickups) {
        pickup.collected    = false;
        pickup.respawnTimer = 0.0f;
    }
    for (Seal& seal : m_seals) {
        seal.collected = false;
    }

    m_loaded        = true;
    m_previousEra   = Era::Present;
    m_outcome       = Outcome::Running;
    m_stats         = RunStats{};
    m_frameIndex    = 0.0f;
    m_damageFlash   = 0.0f;
    m_hazardCooldown = 0.0f;
    m_playerHurt    = false;
    m_swingResolved = false;
}

int World::sealsTaken() const noexcept
{
    int count = 0;
    for (const Seal& seal : m_seals) {
        if (seal.collected) {
            ++count;
        }
    }
    return count;
}

int World::sealMask() const noexcept
{
    int mask = 0;
    for (std::size_t i = 0; i < m_seals.size(); ++i) {
        if (m_seals[i].collected) {
            mask |= 1 << i;
        }
    }
    return mask;
}

void World::restoreCheckpoint(const Vec2& position, Era era, float health, float chrono,
                              int sealMask)
{
    // Rebuild everything first. A checkpoint restores *progress through the
    // level*, not the state of the fight the player was in when they last touched
    // one: enemies, pickups and seals go back to their initial state, and the
    // player resumes where they were.
    //
    // The alternative — rewinding the world to the moment the checkpoint was
    // taken — would need the enemy list, every enemy's HP and every pickup's
    // timer serialised into the save, and would produce a level where a checkpoint
    // taken before a fight replays the fight's opening rather than skipping it.
    // That is more faithful to a recording and much harder to explain.
    restart();

    // Clamped inside the map. A checkpoint written by a build with a different grid
    // size, or by hand, must not put the player outside the world.
    const int columns = m_map.columns();
    const int rows    = m_map.rows();
    const int tileX   = std::clamp(static_cast<int>(position.x / TileMap::kTileSize), 0,
                                   std::max(0, columns - 1));
    const int tileY   = std::clamp(static_cast<int>(position.y / TileMap::kTileSize), 0,
                                   std::max(0, rows - 1));
    const Vec2 safe{static_cast<float>(tileX) * TileMap::kTileSize,
                    static_cast<float>(tileY) * TileMap::kTileSize};

    m_player.restore(safe, era, health, chrono);

    // Nudge out of anything solid. A checkpoint taken mid-jump, or one whose tile
    // became solid because the player restored in a different era, must not drop
    // the player inside geometry — that is the one way a checkpoint can make a
    // level unwinnable.
    resolvePenetration(m_map, m_player.body(), m_player.era());

    applySeals(sealMask);

    // A restore is not a fresh attempt: the run counters continue, and the level
    // is running again rather than finished.
    m_outcome = Outcome::Running;
}

bool World::checkpointHere(int tileX, int tileY) const noexcept
{
    return m_level.checkpointHere(tileX, tileY);
}

void World::applySeals(int mask)
{
    for (std::size_t i = 0; i < m_seals.size(); ++i) {
        m_seals[i].collected = (mask & (1 << i)) != 0;
    }
    m_stats.sealsTaken = 0;
    for (const Seal& seal : m_seals) {
        if (seal.collected) {
            ++m_stats.sealsTaken;
        }
    }
}

void World::applyRunStats(double elapsed, int shifts, int kills, float paradox)
{
    m_stats.elapsed = std::max(0.0, elapsed);
    m_stats.shifts  = std::max(0, shifts);
    m_stats.kills   = std::max(0, kills);
    m_stats.paradox = std::max(0.0f, paradox);
}

bool World::gateOpen() const noexcept
{
    return !m_seals.empty() && sealsTaken() == static_cast<int>(m_seals.size());
}

std::vector<std::uint32_t> World::enemyIds() const
{
    std::vector<std::uint32_t> ids;
    ids.reserve(m_enemies.size());
    for (const Enemy& enemy : m_enemies) {
        ids.push_back(enemy.id());
    }
    return ids;
}

int World::enemyCount() const noexcept
{
    return static_cast<int>(m_enemies.size());
}

int World::activeEnemyCount() const noexcept
{
    int count = 0;
    for (const Enemy& enemy : m_enemies) {
        if (enemy.inCurrentEra() && enemy.state() != EnemyState::Dying) {
            ++count;
        }
    }
    return count;
}

float World::nearestSealDistance() const noexcept
{
    float best = -1.0f;
    const Vec2 here = m_player.body().center();
    for (const Seal& seal : m_seals) {
        if (seal.collected) {
            continue;
        }
        const float d = std::sqrt((seal.position.x - here.x) * (seal.position.x - here.x) +
                                  (seal.position.y - here.y) * (seal.position.y - here.y));
        if (best < 0.0f || d < best) {
            best = d;
        }
    }
    return best;
}

void World::update(const PlayerInput& input, const WorldCommands& commands, float dt)
{
    m_playerHurt  = false;
    m_damageFlash = std::max(0.0f, m_damageFlash - dt * 2.5f);

    // The event log is a queue, drained by the caller. Clearing it here would
    // mean anything that happened earlier in the step, or in an earlier step
    // that the caller had not yet polled, is silently discarded.

    if (!m_loaded || m_outcome != Outcome::Running || dt <= 0.0f || !std::isfinite(dt)) {
        return;
    }

    // Hit-stop. The simulation genuinely stops: physics, enemies, pickups and
    // combat all hold, so an enemy cannot land a hit during the freeze the
    // player's own blow caused. Only the presentation clock keeps running, so the
    // spark travels while the world it came from is still. Holding a direction
    // through a freeze is exactly what should feel heavy, so held input does not
    // cancel it.
    //
    // What does cancel it is a fresh *press*. A freeze that swallows a button
    // press is a lost input, and a lost input is felt as the game being
    // unresponsive rather than as a stylistic choice - so any step the player
    // pressed something on runs normally.
    const bool pressed = commands.shiftPressed || commands.interactPressed || input.jumpPressed ||
                         input.dashPressed || input.attackPressed;
    const bool frozen  = (m_hitStop > 0.0f) && !pressed;
    if (m_hitStop > 0.0f) {
        m_hitStop = std::max(0.0f, m_hitStop - dt);
    }

    if (frozen) {
        m_damageFlash = std::max(0.0f, m_damageFlash - dt * 2.5f);
        return;
    }

    // `elapsed` is the run timer, and it is incremented *after* the freeze check
    // on purpose. It measures how long the world actually experienced, so it
    // stays in step with how far everything moved. Counting frozen steps would
    // make the results screen disagree with the run it is describing: a player
    // who landed many heavy hits would read a longer time having seen less
    // happen.
    m_stats.elapsed += static_cast<double>(dt);
    ++m_frameIndex;

    // The shift runs on the same step as everything else, never on its own clock.
    updateEraShift(commands);

    const bool alive = m_player.update(m_map, input, dt);
    forwardPlayerEvents();
    if (!alive) {
        updateOutcome();
        return;
    }

    updateEnemies(dt, static_cast<float>(m_frameIndex));
    updateCombat(dt);
    updatePickups(dt);
    updateSealsAndGate(commands);
    removeDeadEnemies();
    updateOutcome();
}

void World::updateEraShift(const WorldCommands& commands)
{
    if (!commands.shiftPressed) {
        return;
    }

    const Era target = nextEra(m_player.era());
    if (!m_player.shiftTo(target, m_map)) {
        emit(WorldEvent::EraShiftFailed,
             m_player.chrono() < m_player.tuning().shiftCost ? "NOT ENOUGH CHRONO ENERGY"
                                                             : "CANNOT SHIFT");
        return;
    }

    // Recorded so the renderer can dissolve from the era being left rather than
    // cross-fading two arbitrary palettes.
    m_previousEra = m_player.era() == Era::Past      ? Era::Future
                    : m_player.era() == Era::Present ? Era::Past
                                                     : Era::Present;

    ++m_stats.shifts;
    m_stats.paradox += kParadoxPerShift;
    // The longest freeze in the game. A shift is the punctuation mark; if
    // anything is allowed to make the world stop, it is this.
    m_hitStop = std::max(m_hitStop, kHitStopShift);
    emit(WorldEvent::EraShifted,
         std::string("SHIFTED TO ") + std::string(eraName(target)));
}

void World::forwardPlayerEvents()
{
    const PlayerStepEvents& edges = m_player.stepEvents();
    const Vec2  centre = m_player.body().center();
    const float facing = m_player.facing();

    if (edges.jumped) {
        emit(WorldEvent::PlayerJumped, centre, Vec2{0.0f, -1.0f}, 1.0f);
    }
    if (edges.landed) {
        // Strength from the impact speed, so a small step is quiet and a drop
        // from the top of the level is not.
        const float strength = Graphics::clampValue(edges.landSpeed / 900.0f, 0.15f, 1.6f);
        emit(WorldEvent::PlayerLanded, centre, Vec2{0.0f, 1.0f}, strength);
    }
    if (edges.dashed) {
        emit(WorldEvent::PlayerDashed, centre, Vec2{facing, 0.0f}, 1.0f);
    }
    if (edges.attacked) {
        emit(WorldEvent::SwingStarted, centre, Vec2{facing, 0.0f}, 1.0f);
    }
    if (edges.swingConnected) {
        emit(WorldEvent::SwingActive, centre, Vec2{facing, 0.0f}, 1.0f);
    }
    if (edges.footstep) {
        emit(WorldEvent::PlayerFootstep, centre, Vec2{0.0f, 1.0f},
             Graphics::clampValue(std::fabs(m_player.body().velocity.x) / 250.0f, 0.2f, 1.4f),
             surfaceUnderfoot());
    }
}

TileKind World::surfaceUnderfoot() const noexcept
{
    // The tile the player's feet are on, not the one their centre is in: a player
    // straddling the edge of a bridge and a stone floor should read as the
    // surface they are standing on rather than whichever cell happens to contain
    // the middle of their body.
    const Vec2  centre = m_player.body().center();
    const float feet   = m_player.body().position.y + m_player.body().size.y;
    return m_map.at(m_map.cellX(centre.x), m_map.cellY(feet)).kind;
}

void World::updatePickups(float dt)
{
    const Rect player = m_player.body().rect();
    for (Pickup& pickup : m_pickups) {
        if (pickup.collected) {
            pickup.respawnTimer = std::max(0.0f, pickup.respawnTimer - dt);
            if (pickup.respawnTimer <= 0.0f) {
                pickup.collected = false;
            }
            continue;
        }
        if (!player.intersects(pickup.body.rect())) {
            continue;
        }

        pickup.collected    = true;
        pickup.respawnTimer = kPickupRespawn;

        if (pickup.kind == PickupKind::ChronoCell) {
            m_player.refillChrono();
            emit(WorldEvent::PickupTaken, "CHRONO CELL  +" +
                                              std::to_string(static_cast<int>(kChronoCellAmount)));
        } else {
            if (m_player.health() >= m_player.maxHealth()) {
                // Still consumed, but it reports honestly rather than implying
                // a heal that did not happen.
                emit(WorldEvent::PickupTaken, "ALREADY AT FULL HEALTH");
            } else {
                m_player.heal(1.0f);
                emit(WorldEvent::PickupTaken, "VITAL RESTORED");
            }
        }
    }
}

void World::updateCombat(float dt)
{
    const bool active = m_player.attacking();
    const Rect  box   = m_player.attackBox();

    if (!active) {
        m_swingResolved = false;
        return;
    }
    // One swing, one hit. Without this a swing that stays active across frames
    // would shred an enemy it is merely resting against.
    if (m_swingResolved) {
        return;
    }

    for (Enemy& enemy : m_enemies) {
        if (!enemy.inCurrentEra() || enemy.state() == EnemyState::Dying) {
            continue;
        }
        if (!enemy.body().rect().intersects(box)) {
            continue;
        }
        if (enemy.takeDamage(m_player.tuning().attackDamage, m_player.body().center())) {
            m_swingResolved = true;
            // The contact point is the intersection, not the enemy centre: the
            // spark belongs where the two things met.
            // The midpoint of the two centres, clipped to where they actually
            // meet, is the contact point: the spark belongs where the swing and
            // the target collided, not where either of them is centred.
            const Vec2 contact = midpointOfContact(enemy.body().rect(), box,
                                                   enemy.body().center(), m_player.body().center());
            const Vec2 away = (contact - m_player.body().center());
            const bool killed = enemy.state() == EnemyState::Dying;
            emit(killed ? WorldEvent::EnemyKilled : WorldEvent::EnemyHurt, contact,
                 away.isZero() ? Vec2{m_player.facing(), 0.0f} : away.normalized(),
                 killed ? 1.6f : 1.0f);
            if (killed) {
                ++m_stats.kills;
                m_stats.paradox += kParadoxPerKill;
                m_hitStop = std::max(m_hitStop, kHitStopKill);
            } else {
                m_hitStop = std::max(m_hitStop, kHitStopHit);
            }
            break;
        }
    }

    // Enemies that are mid-attack damage on contact. Hitting one during its
    // attack box is what makes the windup telegraph worth reading.
    const Rect playerRect = m_player.body().rect();
    for (const Enemy& enemy : m_enemies) {
        if (!enemy.inCurrentEra()) {
            continue;
        }
        const Rect hitBox = enemy.attackBox();
        if (hitBox.isEmpty() || !hitBox.intersects(playerRect)) {
            continue;
        }
        if (m_player.takeDamage(enemy.contactDamage(), enemy.body().center())) {
            ++m_stats.damageTaken;
            m_playerHurt  = true;
            m_damageFlash = 1.0f;
            m_hitStop = std::max(m_hitStop, kHitStopPlayerHit);
            emit(WorldEvent::PlayerHurt, "HIT BY " + std::string(enemyName(enemy.kind())));
        }
    }

    // Touching a walking enemy hurts too, at reduced damage, so contact is a
    // real threat rather than something to ignore.
    for (const Enemy& enemy : m_enemies) {
        if (!enemy.inCurrentEra() || enemy.dangerous()) {
            continue;
        }
        if (!shrinkToCentre(enemy.body().rect(), 0.8f).intersects(playerRect)) {
            continue;
        }
        if (m_player.takeDamage(enemy.contactDamage() * 0.5f, enemy.body().center())) {
            ++m_stats.damageTaken;
            m_playerHurt  = true;
            m_damageFlash = 1.0f;
            emit(WorldEvent::PlayerHurt, "BRUSHED AGAINST " +
                                            std::string(enemyName(enemy.kind())));
        }
        break;
    }

    // Hazards. The cooldown lives on the world rather than the player so a
    // player standing in one takes a hit per interval rather than every frame.
    if (m_map.rectOverHazard(playerRect, m_player.era())) {
        m_hazardCooldown -= dt;
        if (m_hazardCooldown <= 0.0f) {
            m_hazardCooldown = kHazardCooldown;
            if (m_player.takeDamage(kHazardDamage, playerRect.center())) {
                ++m_stats.damageTaken;
                m_playerHurt  = true;
                m_damageFlash = 1.0f;
                m_hitStop = std::max(m_hitStop, kHitStopPlayerHit);
                emit(WorldEvent::PlayerHurt, "HAZARD");
                emit(WorldEvent::HazardTick, playerRect.center(), Vec2{0.0f, -1.0f}, 1.0f);
            }
        }
    } else {
        m_hazardCooldown = 0.0f;
    }
}

void World::updateEnemies(float dt, float frameIndex)
{
    const Vec2 playerCentre = m_player.body().center();
    for (Enemy& enemy : m_enemies) {
        enemy.update(m_map, m_player.era(), playerCentre, dt, frameIndex);
    }
}

void World::removeDeadEnemies()
{
    m_enemies.erase(std::remove_if(m_enemies.begin(), m_enemies.end(),
                                   [](const Enemy& e) { return e.removed(); }),
                    m_enemies.end());
}

void World::updateSealsAndGate(const WorldCommands& commands)
{
    const Vec2 playerCentre = m_player.body().center();

    for (Seal& seal : m_seals) {
        if (seal.collected) {
            continue;
        }
        const float dx = seal.position.x - playerCentre.x;
        const float dy = seal.position.y - playerCentre.y;
        const bool inReach = commands.interactPressed &&
                             (dx * dx + dy * dy) <= seal.reach * seal.reach;
        if (!inReach) {
            continue;
        }

        // The seal only yields while the world is in its own era. That is the
        // one rule the whole level is built to teach.
        if (seal.era != m_player.era()) {
            emit(WorldEvent::SealWrongEra,
                 seal.label + "  -  SHIFT TO " + std::string(eraName(seal.era)));
            return;
        }

        seal.collected = true;
        ++m_stats.sealsTaken;
        m_stats.paradox += kParadoxPerSeal;
        m_hitStop = std::max(m_hitStop, kHitStopSeal);
        emit(WorldEvent::SealTaken, seal.label + " RECOVERED");
    }

    if (m_goalBounds.isEmpty()) {
        return;
    }
    if (!m_map.rectOverGoal(m_player.body().rect())) {
        return;
    }

    if (gateOpen()) {
        emit(WorldEvent::Victory, "THE GATE OPENS");
        return;
    }
    if (commands.interactPressed) {
        const int remaining = static_cast<int>(m_seals.size()) - sealsTaken();
        emit(WorldEvent::GateSealed,
             std::to_string(remaining) + (remaining == 1 ? " SEAL REMAINS" : " SEALS REMAIN"));
    }
}

void World::updateOutcome()
{
    if (m_outcome != Outcome::Running) {
        return;
    }

    if (!m_player.alive()) {
        m_outcome = Outcome::Defeat;
        emit(WorldEvent::PlayerDied, "TIMELINE COLLAPSED");
        return;
    }
    if (gateOpen() && !m_goalBounds.isEmpty() && m_map.rectOverGoal(m_player.body().rect())) {
        m_outcome = Outcome::Victory;
        emit(WorldEvent::Victory, "THE GATE OPENS");
    }
}

std::string World::objectiveText() const
{
    if (m_outcome == Outcome::Victory) {
        return "The Ancient Gate stands open";
    }
    if (m_outcome == Outcome::Defeat) {
        return "The timeline collapsed";
    }

    const int taken = sealsTaken();
    const int total = static_cast<int>(m_seals.size());

    std::string text = "Era seals  " + std::to_string(taken) + " / " + std::to_string(total);
    if (taken < total) {
        // Name the era of the nearest seal still outstanding. Telling the player
        // which button to press is the difference between a puzzle and a chore.
        const Vec2 here = m_player.body().center();
        float best     = -1.0f;
        Era  wanted    = Era::Past;
        for (const Seal& seal : m_seals) {
            if (seal.collected) {
                continue;
            }
            const float d = std::sqrt((seal.position.x - here.x) * (seal.position.x - here.x) +
                                      (seal.position.y - here.y) * (seal.position.y - here.y));
            if (best < 0.0f || d < best) {
                best   = d;
                wanted = seal.era;
            }
        }
        if (best >= 0.0f) {
            text += "   nearest needs " + std::string(eraName(wanted));
        }
    } else {
        text += "   the gate is open  -  reach it";
    }
    return text;
}

} // namespace EraShift::Game
