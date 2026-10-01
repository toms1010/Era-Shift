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
#include <vector>

namespace EraShift::Game {

class MainMenuState;

/// The level select, reached from the main menu.
///
/// Lists whatever is in the context's level directory, so adding a region is a
/// data change: drop a file in and it appears, with no menu edit and nothing to
/// keep in step with the directory.
class LevelSelectState final : public Core::IGameState {
public:
    explicit LevelSelectState(Core::GameState id = Core::GameState::LevelSelect);

    void onEnter(StateContext& ctx) override;
    void update(StateContext& ctx, double fixedDelta) override;
    void render(StateContext& ctx, double alpha) override;

    /// The region the player last chose, as a path relative to the level
    /// directory. Empty until something is picked.
    ///
    /// Static rather than per-instance because the *next* state is the one that
    /// needs it: `MainMenuState` builds a `LevelSelectState`, the player picks,
    /// and the `PlayingState` that replaces it has to be told which file. Passing
    /// it through a member would mean reading it off a state that is about to be
    /// destroyed.
    [[nodiscard]] static const std::filesystem::path& chosenLevel() noexcept;
    /// Clears the pending choice. Called once a run has consumed it, so a later
    /// NEW GAME does not silently replay the region from the previous visit.
    static void clearChosenLevel() noexcept;

private:
    void rebuild(StateContext& ctx);

    std::vector<Game::LevelEntry> m_levels;
    MenuList   m_menu;
    MenuStyles m_styles;
    float      m_time = 0.0f;
    Graphics::UiScale m_viewportScale{1.0f};
    bool       m_built = false;
};

/// Victory and defeat, with the run's numbers.
class ResultState final : public Core::IGameState {
public:
    ResultState(Core::GameState id, Game::Outcome outcome, const Game::RunStats& stats);

    /// Restarts the current region from a checkpoint instead of from the spawn.
    ///
    /// Set before the state is pushed. A defeat with no checkpoint offers only
    /// RETRY REGION, because offering RETRY CHECKPOINT and having it silently do
    /// the same thing as a full restart is a lie in the menu.
    void setRetryFromCheckpoint(bool enabled) noexcept { m_retryCheckpoint = enabled; }
    /// The level to load, so the results screen can name it. Empty is fine.
    void setLevelName(std::string name) { m_levelName = std::move(name); }
    /// The level's *id*, which is what the database is keyed on. The display name
    /// is not: a region can be renamed without invalidating its progress.
    void setLevelId(std::string id) { m_levelId = std::move(id); }
    [[nodiscard]] const std::string& levelId() const noexcept { return m_levelId; }
    [[nodiscard]] const std::string& levelName() const noexcept { return m_levelName; }
    /// Records the completion before the menu is drawn, for a victory.
    ///
    /// Called by the caller rather than done here, because unlocking is a
    /// database write and this state is otherwise read-only.
    void markCompleted(bool completed) noexcept { m_recorded = completed; }

    void onEnter(StateContext& ctx) override;
    void update(StateContext& ctx, double fixedDelta) override;
    void render(StateContext& ctx, double alpha) override;

    /// The option labels, in the order they are offered.
    ///
    /// Exposed so the set of options can be tested without a renderer: which
    /// options a screen offers *is* its behaviour, and it is the part most likely
    /// to regress when a new outcome is added.
    [[nodiscard]] std::vector<std::string> optionLabels() const;

private:
    void buildMenu(StateContext& ctx);

    Game::Outcome m_outcome = Game::Outcome::Defeat;
    Game::RunStats m_stats;
    MenuList       m_menu;
    MenuStyles     m_styles;
    float          m_time = 0.0f;
    Graphics::UiScale m_viewportScale{1.0f};
    bool  m_retryCheckpoint = false;
    bool  m_recorded = false;
    std::string m_levelName;
    std::string m_levelId;
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
