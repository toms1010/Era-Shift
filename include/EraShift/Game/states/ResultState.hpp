// Era Shift - end-of-run screen.
//
// One state for both endings. The difference is a headline, a colour and which
// options are offered; making them two states would duplicate the whole layout
// to change three strings.

#pragma once

#include "EraShift/Core/GameState.hpp"
#include "EraShift/Game/states/MainMenuState.hpp"
#include "EraShift/Game/World.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"

#include <string>

namespace EraShift::Game {

class MainMenuState;

/// Victory and defeat, with the run's numbers.
class ResultState final : public Core::IGameState {
public:
    ResultState(Core::GameState id, Game::Outcome outcome, const Game::RunStats& stats);

    void onEnter(StateContext& ctx) override;
    void update(StateContext& ctx, double fixedDelta) override;
    void render(StateContext& ctx, double alpha) override;

private:
    void buildMenu(StateContext& ctx);

    Game::Outcome m_outcome = Game::Outcome::Defeat;
    Game::RunStats m_stats;
    MenuList       m_menu;
    MenuStyles     m_styles;
    float          m_time = 0.0f;
    Graphics::UiScale m_viewportScale{1.0f};
};

/// The credits screen. Reached from the main menu.
class CreditsState final : public Core::IGameState {
public:
    explicit CreditsState(Core::GameState id = Core::GameState::Credits);

    void onEnter(StateContext& ctx) override;
    void update(StateContext& ctx, double fixedDelta) override;
    void render(StateContext& ctx, double alpha) override;

private:
    float          m_time = 0.0f;
    Graphics::UiScale m_viewportScale{1.0f};
    MenuStyles     m_styles;
    MenuList       m_menu;
    bool           m_built = false;
};

} // namespace EraShift::Game
