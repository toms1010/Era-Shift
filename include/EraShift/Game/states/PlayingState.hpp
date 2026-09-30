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
#include "EraShift/Game/Level.hpp"
#include "EraShift/Game/World.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"

#include <string>
#include <vector>

namespace EraShift::Game {

using Graphics::Color;
using Graphics::UiScale;

class PausedState;

/// Presentation values for one era. The gameplay layer decides *what* the world
/// is; this decides how it looks.
struct EraTheme {
    Color sky;
    Color skyLow;
    Color solid;
    Color solidEdge;
    Color oneWay;
    Color hazard;
    Color actor;
    Color accent;
};

/// The three era themes, in `Era` order.
[[nodiscard]] const EraTheme& themeFor(Game::Era era);

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
    void drawHud(const StateContext& ctx, const Rect& area) const;
    void drawToasts(const StateContext& ctx, const Rect& area) const;

    [[nodiscard]] UiScale currentUiScale(const StateContext& ctx) const;

    Game::World      m_world;
    Game::Level      m_level;

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
