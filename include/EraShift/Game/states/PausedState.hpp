// Era Shift - pause and settings overlays.

#pragma once

#include "EraShift/Core/GameState.hpp"
#include "EraShift/Game/states/MainMenuState.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <string>
#include <vector>

namespace EraShift::Game {

/// Pause menu. Pushed on top of the active gameplay state, which stops
/// simulating without any special-casing in the state below.
class PausedState final : public Core::IGameState {
public:
    explicit PausedState(Core::GameState id = Core::GameState::Paused);

    void onEnter(StateContext& ctx) override;
    void update(StateContext& ctx, double fixedDelta) override;
    void render(StateContext& ctx, double alpha) override;

private:
    MenuList   m_menu;
    MenuStyles m_styles;
    float      m_time = 0.0f;
};

/// Settings screen with live-adjustable options. Applies immediately so the
/// player can see the effect while the menu is open.
class SettingsState final : public Core::IGameState {
public:
    explicit SettingsState(Core::GameState id = Core::GameState::Settings);

    void onEnter(StateContext& ctx) override;
    void onExit(StateContext& ctx) override;
    void update(StateContext& ctx, double fixedDelta) override;
    void render(StateContext& ctx, double alpha) override;

private:
    /// Which page of options is showing.
    enum class Page { General, Graphics, Audio, Controls };

    void buildItems(StateContext& ctx);
    void adjust(StateContext& ctx, int direction);
    void setPage(Page page, StateContext& ctx);

    MenuList   m_menu;
    MenuStyles m_styles;
    Page       m_page = Page::General;
    float      m_time = 0.0f;

    // Cached option values, written back into the config store on change.
    int   m_fullscreen  = 0;
    int   m_vsync       = 1;
    int   m_showFps     = 0;
    int   m_masterVolume = 80;
    int   m_musicVolume  = 70;
    int   m_sfxVolume    = 90;
    int   m_uiScale      = 100;
};

} // namespace EraShift::Game
