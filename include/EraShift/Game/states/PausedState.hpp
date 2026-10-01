// Era Shift - pause and settings overlays.

#pragma once

#include "EraShift/Core/GameState.hpp"
#include "EraShift/Game/states/MainMenuState.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"
#include "EraShift/Graphics/Math.hpp"

#include "EraShift/Input/InputManager.hpp"

#include <string>
#include <vector>

namespace EraShift::Game {

// `uiScaleFor`, `layoutPanel`, `MenuStyles` and `MenuList` all come from
// MainMenuState.hpp, which is where the shared interface layout lives.

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
    Graphics::UiScale m_viewportScale{1.0f};

    /// Rebuilds the styles when the viewport or the UI-scale preference changes.
    void syncScale(StateContext& ctx);
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
    enum class Page { General, Graphics, Game, Controls };

    /// What the controls page is waiting for. Idle otherwise.
    enum class Rebind {
        None,
        Waiting,
        Cancelled,
    };

    void buildItems(StateContext& ctx);
    /// The controls page's rows, built from the live bindings.
    [[nodiscard]] std::vector<MenuItem> controlItems(StateContext& ctx) const;
    void adjust(StateContext& ctx, int direction);
    void setPage(Page page, StateContext& ctx);
    /// True when this menu label is an adjustable option rather than a page link
    /// or a back button.
    [[nodiscard]] bool isAdjustable(const std::string& label) const;

    /// Handles the controls page: showing the bindings, and capturing a new
    /// one when the player asks for it.
    void updateRebinding(StateContext& ctx);
    /// Writes a new binding back into the controls config table, so it survives
    /// a restart.
    void persistBinding(StateContext& ctx, Input::Action action,
                        const Input::InputBinding& binding);
    /// Actions offered on the controls page, in the order they are listed.
    [[nodiscard]] static const std::vector<Input::Action>& controlActions();
    /// The heading for the current page.
    ///
    /// Shared with `update`, which hit tests the rows and so has to measure the
    /// same panel `render` draws.
    [[nodiscard]] const char* pageTitle() const noexcept;
    /// The hint line for the current page. Shares `pageTitle`'s reason.
    [[nodiscard]] const char* pageFooter() const noexcept;
    /// Locks every region again and forgets the tutorial's completion.
    ///
    /// A separate method rather than an `adjust` branch because it is destructive
    /// and must not be reachable by holding a key: left/right steps a value, this
    /// wipes progress.
    void resetProgress(StateContext& ctx);

    MenuList   m_menu;
    MenuStyles m_styles;
    Page       m_page = Page::General;
    float      m_time = 0.0f;
    Graphics::UiScale m_viewportScale{1.0f};

    void syncScale(StateContext& ctx);

    // Cached option values, written back into the config store on change.
    int   m_fullscreen  = 0;
    int   m_vsync       = 1;
    int   m_showFps     = 0;
    int   m_uiScale      = 100;
    /// Whether the first-run tutorial is shown. Recorded in the progression
    /// database, so it survives a New Game the way a settings.json value would not.
    int   m_tutorialEnabled = 1;

    /// True while the destructive reset is waiting for a second confirmation.
    ///
    /// Reset rather than a dialog, because a modal for one yes/no question is
    /// more machinery than the question deserves — and this way the confirmation
    /// is the row itself, so a player cannot confirm a dialog they did not read.
    bool  m_confirmReset = false;

    Rebind m_rebind = Rebind::None;
    Input::Action m_rebindTarget = Input::Action::Count;
};

} // namespace EraShift::Game
