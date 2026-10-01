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
#include "EraShift/Core/SaveGame.hpp"
#include "EraShift/Game/Animation.hpp"
#include "EraShift/Game/EraTheme.hpp"
#include "EraShift/Game/Feedback.hpp"
#include "EraShift/Game/Level.hpp"
#include "EraShift/Game/World.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"

#include <string>
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
    bool  m_ready       = false;

    /// HUD typography, built once from the scale so the bars, the objective and
    /// the toasts cannot disagree about size.
    Graphics::TextStyle m_labelStyle;
    Graphics::TextStyle m_valueStyle;

    /// Contextual prompt under the objective, e.g. "E to take the Past seal".
    std::string m_interactPrompt;
    bool        m_showInteractPrompt = false;

    Core::SaveGame m_pendingSave;
    bool           m_hasPendingSave = false;
};

} // namespace EraShift::Game
