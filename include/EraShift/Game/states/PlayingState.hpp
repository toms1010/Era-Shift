// Era Shift - in-game state.
//
// Phase 1 scope: this is the "engine verification" scene. It exists to prove
// that the fixed-timestep loop, camera, input, pause menu and era visualisation
// all work together, and it will be replaced by the Ancient Forest vertical
// slice in Phase 2/3. Keeping it small and clearly labelled avoids the trap of
// a throwaway prototype quietly becoming the real player controller.

#pragma once

#include "EraShift/Core/GameState.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <cstdint>
#include <string>

namespace EraShift::Game {

/// The three eras the world can be observed in. Declared here in Phase 1 and
/// promoted to `EraShift::Era::Era` in Phase 3, when the timeline system needs
/// it for real.
enum class Era : std::uint8_t {
    Past = 0,
    Present = 1,
    Future = 2,
};

[[nodiscard]] std::string_view eraName(Era era) noexcept;

class PausedState;

class PlayingState final : public Core::IGameState {
public:
    explicit PlayingState(Core::GameState id = Core::GameState::Playing);

    void onEnter(StateContext& ctx) override;
    void onResume(StateContext& ctx) override;
    void onPause(StateContext& ctx) override;
    void onExit(StateContext& ctx) override;
    void update(StateContext& ctx, double fixedDelta) override;
    void render(StateContext& ctx, double alpha) override;

private:
    struct Body {
        Graphics::Vec2 position;
        Graphics::Vec2 velocity;
    };

    void updateEraShift(StateContext& ctx, double fixedDelta);
    void drawWorld(StateContext& ctx, const Graphics::Vec2& interpolated) const;
    void drawHud(StateContext& ctx, const Graphics::Rect& area) const;

    Body     m_body;
    Era      m_era          = Era::Present;
    float    m_eraBlend     = 1.0f;   ///< 1 = fully settled in the current era.
    float    m_chronoEnergy = 100.0f;
    float    m_maxEnergy    = 100.0f;
    float    m_facing       = 1.0f;
    float    m_time         = 0.0f;
    bool     m_grounded     = true;
    std::string m_statusLine;
};

} // namespace EraShift::Game
