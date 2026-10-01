// Era Shift - the in-game state.
//
// This state owns no gameplay. It reads the world simulation, turns input into
// the small `PlayerInput` / `WorldCommands` structs the simulation understands,
// and draws what the simulation reports. That split is what makes the game
// testable: everything with a rule in it lives in `Game::World`, and what is
// left here is presentation.
//
// Drawing the world means drawing three eras at once. `previousEra` and `era`
// are both drawn and cross-dissolved, which is what makes a shift read as the
// world changing rather than the palette being swapped under a still frame.

#pragma once

#include "EraShift/Core/GameState.hpp"
#include "EraShift/Core/ProgressDatabase.hpp"
#include "EraShift/Core/SaveGame.hpp"
#include "EraShift/Game/Animation.hpp"
#include "EraShift/Game/Dialogue.hpp"
#include "EraShift/Game/EraTheme.hpp"
#include "EraShift/Game/Feedback.hpp"
#include "EraShift/Game/Level.hpp"
#include "EraShift/Game/Tutorial.hpp"
#include "EraShift/Game/World.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"

#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace EraShift::Game {

using Graphics::Color;
using Graphics::UiScale;

class PausedState;

/// How far back the parallax layers sit, as a fraction of camera movement.
struct ParallaxLayer {
    float depth;
    Color  color;
    float amplitude;
};

class PlayingState final : public Core::IGameState {
public:
    explicit PlayingState(Core::GameState id = Core::GameState::Playing);

    /// Starts a fresh run, ignoring any save.
    void onEnter(StateContext& ctx) override;
    void onResume(StateContext& ctx) override;
    void onPause(StateContext& ctx) override;
    void onExit(StateContext& ctx) override;
    void update(StateContext& ctx, double fixedDelta) override;
    void render(StateContext& ctx, double alpha) override;

    /// Restores a saved run. Must be called before the state is pushed.
    void applySave(const Core::SaveGame& save);
    /// Restores a checkpoint from the progression database, instead of starting
    /// the region from its spawn.
    ///
    /// Returns false when there is no checkpoint for this level or no database, in
    /// which case the region starts normally — the caller does not need to
    /// distinguish "retry" from "restart", because both are playable.
    bool applyCheckpoint(const Core::Checkpoint& checkpoint);
    /// Clears a pending checkpoint restore. Called once it has been consumed, so a
    /// later RETRY REGION does not silently rewind again.
    void clearPendingCheckpoint() noexcept { m_hasPendingCheckpoint = false; }
    [[nodiscard]] bool hasPendingCheckpoint() const noexcept { return m_hasPendingCheckpoint; }
    [[nodiscard]] const std::string& levelId() const noexcept { return m_level.id; }
    [[nodiscard]] const std::string& levelName() const noexcept { return m_level.name; }
    /// Fills `out` with the current run so it can be written to disk.
    void captureSave(Core::SaveGame& out) const;

    [[nodiscard]] const Game::World& world() const noexcept { return m_world; }

private:
    void syncLevel(StateContext& ctx);
    void saveProgress(StateContext& ctx) const;
    void syncPrompt();

    void drawBackdrop(const StateContext& ctx, const Rect& area) const;
    void drawParallax(const StateContext& ctx, const Rect& area) const;
    void drawTiles(const StateContext& ctx, float blend) const;
    void drawPickups(const StateContext& ctx) const;
    /// The finish gate: a floating marker over a plinth, pulsing harder as the
    /// player closes on it.
    void drawFinishGate(const StateContext& ctx) const;
    /// The checkpoint volumes, dim while dormant and lit once the player has
    /// passed through one.
    void drawCheckpoints(const StateContext& ctx) const;
    void drawSeals(const StateContext& ctx, float blend) const;
    void drawEnemies(const StateContext& ctx) const;
    void drawPlayer(const StateContext& ctx, const Vec2& interpolated) const;
    /// Chooses the player's clip from the simulation's state and advances it.
    ///
    /// `shiftHeld` is the era-shift key's state for this frame. It is passed in
    /// rather than read from the context because the selection logic itself lives
    /// in `Game::playerClipFor`, next to the clip table, where the gameplay suite
    /// can assert on it.
    void updatePlayerAnimation(float dt, bool shiftHeld);
    /// Same, for every enemy, keyed on the enemy's stable id.
    void updateEnemyAnimations(float dt);
    /// The controller for an enemy, created on first sight. Pruned on death.
    [[nodiscard]] Game::AnimationController& enemyAnimation(std::uint32_t id);
    [[nodiscard]] const Game::Pose& enemyPose(const Enemy& enemy) const;
    void drawHud(const StateContext& ctx, const Rect& area) const;
    void drawToasts(const StateContext& ctx, const Rect& area) const;
    /// The tutorial lesson panel, top-left. Draws nothing when the tutorial is
    /// inactive, so the caller does not need to check.
    void drawTutorialPanel(const StateContext& ctx, const Rect& area) const;
    /// How many pixels at the bottom of the viewport `drawHud` claims.
    ///
    /// Exposed so the dialogue panel can be told what is already there instead of
    /// guessing. When this and the panel's own idea of the bottom disagreed, the
    /// frame landed on top of the hint row and the objective line.
    [[nodiscard]] static float hudBottomReserved(Graphics::UiScale scale) noexcept;

    [[nodiscard]] UiScale currentUiScale(const StateContext& ctx) const;

    Game::World      m_world;
    Game::Level      m_level;

    /// Everything the player sees and hears in response to the simulation. Owned
    /// here because this is the only state that draws the world, and a feedback
    /// system that outlived its state would have nothing to draw into.
    Game::FeedbackSystem m_feedback;

    /// The player's rig. Driven from the simulation's state rather than from the
    /// controller's own clock, so the picture cannot disagree with the physics.
    Game::AnimationController m_playerAnim;
    /// One controller per living enemy, keyed on `Enemy::id()` so an enemy keeps
    /// its animation when the vector it lives in is compacted.
    std::unordered_map<std::uint32_t, Game::AnimationController> m_enemyAnims;

    /// Positions from the previous fixed step, kept so rendering can
    /// interpolate and motion stays smooth on any refresh rate.
    Vec2 m_previousPlayer;
    Vec2 m_currentPlayer;
    Vec2 m_cameraPosition;
    Vec2 m_previousCamera;

    std::vector<std::string> m_toasts;
    float m_toastTimer = 0.0f;

    float m_time        = 0.0f;
    float m_hurtFlash   = 0.0f;
    float m_shiftFlash  = 0.0f;
    /// Counts down after the run ends, so the player sees what happened before
    /// the results screen replaces the world.
    float m_outcomeDelay = 0.0f;
    /// 0..0.72, how much the frame is darkened by a defeat. Rises while the death
    /// settles and is cleared on the next run, so the results screen arrives out
    /// of a darker picture rather than out of the same picture the player died in.
    float m_darken = 0.0f;
    bool  m_ready       = false;

    /// HUD typography, built once from the scale so the bars, the objective and
    /// the toasts cannot disagree about size.
    Graphics::TextStyle m_labelStyle;
    Graphics::TextStyle m_valueStyle;

    /// Contextual prompt under the objective, e.g. "E to take the Past seal".
    std::string m_interactPrompt;
    bool        m_showInteractPrompt = false;

    /// First-run lessons. Disabled entirely once the player has finished the
    /// tutorial, which is read from the progression database at load.
    Game::Tutorial m_tutorial;
    /// Seconds the current lesson has been on screen, used to fade it in and to
    /// stop a lesson flashing up for a single frame as the player passes through.
    float m_tutorialAge = 0.0f;
    /// The lesson to draw. A copy rather than a pointer so the panel cannot draw
    /// a half-updated tutorial during the same frame it advances.
    Game::TutorialLesson m_lesson{};
    /// Staged while `m_lesson` plays its exit, so the two never swap on one frame.
    Game::TutorialLesson m_nextLesson{};
    /// True while the satisfied lesson is fading out.
    bool m_tutorialLeaving = false;
    /// Opacity of the lesson panel right now: in, held, or out.
    [[nodiscard]] float tutorialFade() const noexcept;

    /// Records whatever the player just did, and advances the lesson if that was
    /// what it was asking for.
    void syncTutorial(StateContext& ctx, const Game::PlayerInput& input,
                      const Game::WorldCommands& commands);

    /// Writes a checkpoint when the player crosses a trigger volume.
    void syncCheckpoint(const StateContext& ctx);

    /// Records the run's end in the progression database and pushes the results
    /// screen, telling it whether a checkpoint retry is worth offering.
    ///
    /// A victory is written *before* the results screen appears. Doing it when the
    /// player leaves would lose the completion to anyone who closed the window on
    /// that screen, which is a real way for a game to lose a player's tenth level.
    void transitionToResult(StateContext& ctx);

    /// The checkpoint volume the player last wrote from, as its centre tile.
    ///
    /// Keyed on the volume, not on the player's tile. A trigger is a radius, so
    /// walking through one crosses several tiles, and keying on the player's tile
    /// wrote the same checkpoint once per tile — a database write every 200ms for
    /// as long as the player stood in it.
    std::pair<int, int> m_lastCheckpointVolume{-1, -1};

    /// The dialogue shown over the world. Level-defined, cleared on every entry, and
    /// owned by this state rather than the level so a region's text cannot outlive
    /// the run that loaded it.
    Game::DialoguePanel m_dialogue;
    /// Seconds the current line has been up, for the fade-out.
    float m_dialogueAge = 0.0f;

    /// Opens a dialogue line, replacing whatever was showing.
    void say(std::string speaker, std::string message, float seconds = 4.0f);
    /// Advances the dialogue timer and clears the panel when it expires.
    void updateDialogue(float dt);

    Core::SaveGame m_pendingSave;
    bool           m_hasPendingSave = false;

    /// A checkpoint to restore on entry, consumed by `syncLevel`.
    ///
    /// Held rather than applied immediately because the level has to be loaded
    /// first: `World::restoreCheckpoint` rebuilds the world, so there is nothing
    /// to rewind until `m_level` exists.
    Core::Checkpoint m_pendingCheckpoint;
    bool             m_hasPendingCheckpoint = false;
};

} // namespace EraShift::Game
