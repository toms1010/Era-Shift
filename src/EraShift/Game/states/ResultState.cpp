#include "EraShift/Game/states/ResultState.hpp"

#include "EraShift/Core/ProgressDatabase.hpp"
#include "EraShift/Game/Level.hpp"
#include "EraShift/Game/states/MainMenuState.hpp"
#include "EraShift/Game/states/PausedState.hpp"
#include "EraShift/Game/states/PlayingState.hpp"
#include "EraShift/Graphics/BitmapFont.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

namespace EraShift::Game {

using Core::GameState;
using StateContext = ::EraShift::StateContext;
using Graphics::BitmapFont;
using Graphics::BlendMode;
using Graphics::Color;
using Graphics::FontStyle;
using Graphics::Rect;
using Graphics::Renderer2D;
using Graphics::TextAlign;
using Graphics::TextStyle;
using Graphics::TextVAlign;
using Graphics::UiScale;
namespace Palette = Graphics::Palette;
using Graphics::Vec2;
using Input::Action;

namespace {

Rect fullArea(Renderer2D& renderer)
{
    return Rect{0.0f, 0.0f, static_cast<float>(renderer.camera().viewportWidth()),
                static_cast<float>(renderer.camera().viewportHeight())};
}

/// "1:23" from a duration in seconds.
std::string formatTime(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const auto total = static_cast<int>(seconds);
    return std::to_string(total / 60) + ":" + std::string(total % 60 < 10 ? "0" : "") +
           std::to_string(total % 60);
}

/// Stagger for the options appearing one after another.
///
/// ~55ms apart: fast enough that the last row arrives while the player is still
/// reading the first, slow enough that it reads as a sequence rather than as
/// everything appearing at once.
constexpr float kRowStagger = 0.055f;
/// How many stat rows sit above the options, and the height of each.
///
/// Named rather than inlined because `update` needs the same number to find
/// where the options begin: a layout measured in one place and re-derived in
/// another is exactly the bug this pair of helpers exists to prevent.
constexpr std::size_t kStatRows = 6;

/// The top of the stats grid.
float optionsTop(const Rect&, const UiScale& scale) noexcept
{
    return scale.px(178.0f);
}

/// The strip of the screen the options panel is laid out in.
///
/// The options sit *below* the stats grid, not centred in the viewport, so this
/// is deliberately not `area`.
Rect optionsArea(const Rect& area, const UiScale& scale) noexcept
{
    const float gridBottom = optionsTop(area, scale) +
                             static_cast<float>(kStatRows) * scale.px(24.0f);
    const float y = gridBottom + scale.px(16.0f);
    // Never negative: a short window must not produce an inverted rectangle,
    // which would make every point "contained" and every click land somewhere.
    const float h = std::max(area.h - y, scale.px(80.0f));
    return Rect{area.x, y, area.w, h};
}

} // namespace

// ---------------------------------------------------------------------------
// ResultState
// ---------------------------------------------------------------------------
ResultState::ResultState(Core::GameState id, Game::Outcome outcome, const Game::RunStats& stats)
    : IGameState(id), m_outcome(outcome), m_stats(stats)
{
}

void ResultState::onEnter(StateContext& ctx)
{
    m_time         = 0.0f;
    m_viewportScale = uiScaleFor(ctx);
    m_styles       = MenuStyles::make(m_viewportScale);
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    buildMenu(ctx);
    ctx.log->info("Game", "run finished: {} with {} points", outcomeName(m_outcome),
                  m_stats.score());
}

std::vector<std::string> ResultState::optionLabels() const
{
    std::vector<std::string> labels;
    labels.reserve(m_menu.items().size());
    for (const MenuItem& item : m_menu.items()) {
        labels.push_back(item.label);
    }
    return labels;
}

void ResultState::buildMenu(StateContext& ctx)
{
    const bool won = m_outcome == Game::Outcome::Victory;

    // A defeat offers the checkpoint first when there is one, because that is
    // what the player wants nine times out of ten. It is offered *only* when a
    // checkpoint exists — the caller checks — so the row never appears and then
    // quietly restart the whole region.
    // LEVEL SELECT is on every outcome, including a victory. A player who clears a
    // region usually wants to pick the next one here, not navigate back through the
    // title screen to do it.
    if (won) {
        m_menu.setItems({
            {"PLAY AGAIN", "run the region again",       true},
            {"LEVEL SELECT", "choose another region",    true},
            {"MAIN MENU",  "back to the title screen",    true},
            {"QUIT",       "exit to desktop",             true},
        });
    } else if (m_retryCheckpoint) {
        m_menu.setItems({
            {"RETRY CHECKPOINT", "return to the last one",  true},
            {"RESTART REGION",   "start from the beginning", true},
            {"LEVEL SELECT",     "choose another region",    true},
            {"MAIN MENU",        "back to the title screen", true},
            {"QUIT",             "exit to desktop",           true},
        });
    } else {
        m_menu.setItems({
            {"RESTART REGION", "start from the beginning", true},
            {"LEVEL SELECT",   "choose another region",    true},
            {"MAIN MENU",      "back to the title screen", true},
            {"QUIT",           "exit to desktop",           true},
        });
    }
    static_cast<void>(ctx);
}

void ResultState::update(StateContext& ctx, double fixedDelta)
{
    m_time += static_cast<float>(fixedDelta);
    if (ctx.input == nullptr) {
        return;
    }

    const UiScale latest = uiScaleFor(ctx);
    if (latest.factor != m_viewportScale.factor) {
        m_viewportScale = latest;
        m_styles        = MenuStyles::make(latest);
        m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    }

    if (ctx.input->wasPressed(Action::Pause)) {
        ctx.states->popToRoot();
        return;
    }
    if (ctx.input->wasPressed(Action::MoveUp)) {
        m_menu.move(-1);
    }
    if (ctx.input->wasPressed(Action::MoveDown)) {
        m_menu.move(1);
    }

    // The panel has to be measured the same way `render` measures it. The
    // options sit *below* the stats grid rather than centred in the viewport, so
    // hit-testing a viewport-centred layout put every row in the wrong place: the
    // rows are drawn low on the screen and were clicked high on it.
    const Rect area  = fullArea(*ctx.renderer);
    const auto panel = layoutPanel(optionsArea(area, m_styles.scale), m_styles,
                                   m_menu.items().size(), "", "", *ctx.text);
    const auto hovered = m_menu.hitTest(panel.list, ctx.input->mousePosition(), m_styles);
    if (hovered != static_cast<std::size_t>(-1)) {
        m_menu.select(hovered);
    }

    const bool confirmed = ctx.input->wasPressed(Action::Interact) ||
                           ctx.input->wasPressed(Action::Jump) ||
                           (hovered != static_cast<std::size_t>(-1) &&
                            ctx.input->wasMousePressed(Input::MouseButton::Left));
    if (!confirmed) {
        return;
    }

    const MenuItem* item = m_menu.currentItem();
    if (item == nullptr) {
        return;
    }
    if (item->label == "RETRY CHECKPOINT") {
        auto retry = std::make_shared<PlayingState>();
        // Reads the checkpoint itself rather than being handed one, so the retry
        // and the last save of a checkpoint cannot disagree. A missing checkpoint
        // falls back to a plain restart, because "retry from a checkpoint that is
        // not there" has no meaning a player could act on.
        const bool restored =
            ctx.progress != nullptr && retry->applyCheckpoint(ctx.progress->readCheckpoint(levelId()));
        if (restored) {
            LevelSelectState::clearChosenLevel();
        }
        ctx.states->reset(std::move(retry));
    } else if (item->label == "RESTART REGION" || item->label == "PLAY AGAIN") {
        LevelSelectState::clearChosenLevel();
        ctx.states->reset(std::make_shared<PlayingState>());
    } else if (item->label == "LEVEL SELECT") {
        // Pushed, not reset: the results screen stays underneath, so BACK from the
        // region list returns to the statistics rather than dropping them.
        LevelSelectState::clearChosenLevel();
        ctx.states->push(std::make_shared<LevelSelectState>());
    } else if (item->label == "MAIN MENU") {
        ctx.states->popToRoot();
    } else if (item->label == "QUIT") {
        ctx.loop->requestStop();
    }
}

void ResultState::render(StateContext& ctx, double alpha)
{
    Renderer2D& renderer = *ctx.renderer;
    const Rect area = fullArea(renderer);
    static_cast<void>(alpha);

    const bool won      = m_outcome == Game::Outcome::Victory;
    const Color accent  = won ? Palette::Positive : Palette::Warning;

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);

    // The backdrop is the outcome's own colour so the screen reads instantly
    // even from across the room.
    const Color backdrop = won ? Palette::PastSky.scaled(0.35f)
                               : Palette::FutureSky.scaled(0.75f);
    renderer.drawRect(area, backdrop);

    // A slow pulse on the accent, matching the main menu's bloom so the two
    // screens feel like the same game.
    const float glow = 0.5f + 0.5f * std::sin(static_cast<float>(m_time) * 1.6f);
    for (int i = 0; i < 20; ++i) {
        const float seed = static_cast<float>(i) * 0.6180339f;
        const float x = std::fmod(seed * area.w + static_cast<float>(m_time) * 10.0f, area.w);
        const float y = std::fmod(seed * area.h + area.h -
                                       static_cast<float>(m_time) * 6.0f,
                                   area.h);
        renderer.drawRect(Rect{x, y, 2.0f, 2.0f}, accent.withAlpha(0x60));
    }

    const UiScale scale = m_styles.scale;
    Graphics::TextRenderer& text = *ctx.text;

    // --- headline -----------------------------------------------------------
    // Fades in over about half a second, before the options start arriving. The
    // pause matters: a title and a menu appearing in the same frame is a wall, and
    // the player reads the wall rather than the title.
    const float titleIn = Graphics::clampValue(static_cast<float>(m_time) / 0.45f, 0.0f, 1.0f);
    const auto titleFade = [titleIn](Color colour) {
        return colour.withAlpha(
            static_cast<std::uint8_t>(static_cast<float>(colour.a) * titleIn));
    };
    const char* headlineText = won ? "THE GATE OPENS" : "TIMELINE COLLAPSED";

    {
        TextStyle bloom = m_styles.title;
        bloom.color   = accent.withAlpha(
            static_cast<std::uint8_t>((40.0f + 60.0f * glow) * titleIn));
        bloom.shadow  = Palette::Transparent;
        text.drawInRect(renderer, Rect{area.x, scale.px(70.0f), area.w, scale.px(90.0f)},
                        headlineText, bloom);
    }
    {
        TextStyle headline = m_styles.title;
        headline.color    = titleFade(Palette::TextPrimary);
        text.drawInRect(renderer, Rect{area.x, scale.px(70.0f), area.w, scale.px(90.0f)},
                        headlineText, headline);
    }

    // Which region this was. A results screen that does not say is a results screen
    // the player has to match against memory, and with eleven regions that is a
    // real ask.
    if (!m_levelName.empty()) {
        TextStyle subtitle = m_styles.tagline;
        subtitle.color     = titleFade(accent);
        text.drawInRect(renderer, Rect{area.x, scale.px(146.0f), area.w, scale.px(24.0f)},
                        m_levelName, subtitle);
    }

    // --- numbers ------------------------------------------------------------
    // Laid out as label/value pairs on one grid, so the figures line up into
    // columns instead of forming a ragged list of centred lines.
    TextStyle label = m_styles.detail;
    label.align     = TextAlign::Left;
    label.color     = Palette::TextDim;

    TextStyle value        = m_styles.detail;
    value.align           = TextAlign::Right;
    value.color           = Palette::TextPrimary;

    struct Row {
        const char* label;
        std::string value;
    };
    const std::vector<Row> rows = {
        {"TIME",         formatTime(m_stats.elapsed)},
        {"ERA SHIFTS",   std::to_string(m_stats.shifts)},
        {"ENEMIES FELLED", std::to_string(m_stats.kills)},
        {"SEALS RECOVERED", std::to_string(m_stats.sealsTaken) + " / 3"},
        {"PARADOX",      std::to_string(static_cast<int>(m_stats.paradox))},
        {"SCORE",        std::to_string(m_stats.score())},
    };

    const float rowH   = scale.px(24.0f);
    const float gridW  = std::min(scale.px(420.0f), area.w - scale.px(40.0f));
    const float gridX  = area.center().x - gridW * 0.5f;
    float y = optionsTop(area, scale);
    for (const Row& row : rows) {
        text.drawInRect(renderer, Rect{gridX, y, gridW * 0.55f, rowH}, row.label, label);
        text.drawInRect(renderer, Rect{gridX + gridW * 0.45f, y, gridW * 0.55f, rowH}, row.value,
                        value);
        y += rowH;
    }

    // --- options ------------------------------------------------------------
    // Rows arrive one after another. The panel itself fades with the first row, so
    // it appears to be drawn rather than to be revealed behind an invisible list.
    const float panelIn = Graphics::clampValue(static_cast<float>(m_time) / 0.30f, 0.0f, 1.0f);
    const PanelLayout panel =
        layoutPanel(optionsArea(area, scale), m_styles, m_menu.items().size(), "", "", text);
    renderer.drawRect(panel.panel,
                      Palette::PanelFill.withAlpha(static_cast<std::uint8_t>(
                          static_cast<float>(Palette::PanelFill.a) * panelIn)));
    renderer.drawRect(panel.panel, Palette::PanelBorder.withAlpha(
                                        static_cast<std::uint8_t>(0x90 * panelIn)));
    m_menu.ensureVisible(panel.visibleRows, m_styles);
    m_menu.render(renderer, *ctx.text, panel.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles,
                  panel.visibleRows, static_cast<float>(m_time), kRowStagger);

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        ctx.overlay->render(renderer, *ctx.text, *ctx.stats, *ctx.states, ctx.loop->stats());
    }

    renderer.setBlendMode(BlendMode::None);

    if (!ctx.text->ready()) {
        // The TTF face failed to load; the built-in font still has to say
        // something, or the screen is blank and looks like a crash.
        FontStyle fallback;
        fallback.scale = 4;
        const Vec2 centre{area.center().x, scale.px(110.0f)};
        BitmapFont::drawCentered(renderer, centre.x, centre.y,
                                 won ? "THE GATE OPENS" : "TIMELINE COLLAPSED",
                                 Palette::TextPrimary, fallback);
    }
}

// ---------------------------------------------------------------------------
// LevelSelectState
// ---------------------------------------------------------------------------
namespace {

/// The region the player picked, waiting to be read by the state that replaces
/// this one. A function rather than a member because the `PlayingState` built
/// on the far side of the transition needs it and this state is destroyed by
/// that transition.
std::filesystem::path g_chosenLevel;

} // namespace

const std::filesystem::path& LevelSelectState::chosenLevel() noexcept
{
    return g_chosenLevel;
}

void LevelSelectState::clearChosenLevel() noexcept
{
    g_chosenLevel.clear();
}

LevelSelectState::LevelSelectState(Core::GameState id)
    : IGameState(id)
{
}

void LevelSelectState::onEnter(StateContext& ctx)
{
    m_time = 0.0f;
    m_viewportScale = uiScaleFor(ctx);
    m_styles = MenuStyles::make(m_viewportScale);
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    rebuild(ctx);
    ctx.log->info("Game", "entered LevelSelect with {} region(s)", m_levels.size());
}

void LevelSelectState::rebuild(StateContext& ctx)
{
    // The list is built from the directory every time this state is entered
    // rather than cached in `onEnter` alone: the directory is cheap to read and
    // the alternative is a select that shows a stale list after a player adds a
    // region mid-session.
    const std::filesystem::path directory =
        ctx.levelDirectory.empty() ? std::filesystem::path{"data/levels"} : ctx.levelDirectory;

    std::vector<std::filesystem::path> broken;
    m_levels = Game::listLevels(directory, &broken);
    for (const auto& path : broken) {
        // One unreadable file is worth a line, and worth *not* being fatal: the
        // remaining regions still have to be playable.
        ctx.log->warn("Game", "skipping unreadable level '{}'", path.string());
    }

    // Unlocked set, read once. Everything the level select shows is a function of
    // this plus the directory listing — never of the row order — so a level
    // cannot become selectable by being moved.
    Core::Progress progress;
    if (ctx.progress != nullptr) {
        progress = ctx.progress->loadProgress();
    }
    const int highest = std::max(1, progress.highestLevel);

    std::vector<Core::LevelRecord> completed;
    for (const Core::LevelRecord& record : progress.levels) {
        if (record.completed) {
            completed.push_back(record);
        }
    }
    const auto completedTime = [&completed](const std::string& id) -> const Core::LevelRecord* {
        const auto found = std::find_if(
            completed.begin(), completed.end(),
            [&id](const Core::LevelRecord& r) { return r.levelId == id; });
        return found != completed.end() ? &*found : nullptr;
    };

    std::vector<MenuItem> items;
    items.reserve(m_levels.size() + 1);
    for (std::size_t i = 0; i < m_levels.size(); ++i) {
        const Game::LevelEntry& level = m_levels[i];

        // Position in the progression is 1-based; a region with no `order` sorts
        // last and is never locked, because it cannot be "beaten" to reach.
        const int position = static_cast<int>(i) + 1;
        const bool ordered = level.order > 0;
        // A region is playable if the player has reached its position, or if the
                              // database has an explicit unlock for it (which is how level 1 is
                              // reachable before anything is completed).
                              const bool unlocked = !ordered || position <= highest;
        const Core::LevelRecord* record = completedTime(level.id);

        std::string detail;
        if (!unlocked) {
            detail = "locked";
        } else if (record != nullptr) {
            // Best time and score, when there is one to report.
            detail = "complete  -  " + formatTime(record->bestTime) + "  -  " +
                     std::to_string(record->bestScore);
        } else {
            detail = "begin here";
        }

        items.emplace_back(level.name, detail, level.id, unlocked);
    }

    // BACK is unconditional. A level select with no BACK is a trap, and an empty
    // directory is exactly the case where the player most needs a way out.
    items.emplace_back("BACK", "return to the title screen", "");

    // The selection lands on the first unlocked region rather than row 0, so a
    // player returning to a fresh build opens on something they can play.
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].enabled && items[i].label != "BACK") {
            m_menu.select(i);
            break;
        }
    }

    m_menu.setItems(std::move(items));
    m_built = true;
}

void LevelSelectState::update(StateContext& ctx, double fixedDelta)
{
    m_time += static_cast<float>(fixedDelta);
    if (ctx.input == nullptr || ctx.renderer == nullptr || ctx.text == nullptr) {
        return;
    }

    const UiScale latest = uiScaleFor(ctx);
    if (latest.factor != m_viewportScale.factor) {
        m_viewportScale = latest;
        m_styles = MenuStyles::make(latest);
        m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    }

    if (ctx.input->wasPressed(Action::Pause)) {
        ctx.states->pop();
        return;
    }
    if (ctx.input->wasPressed(Action::MoveUp)) {
        m_menu.move(-1);
    }
    if (ctx.input->wasPressed(Action::MoveDown)) {
        m_menu.move(1);
    }

    const Rect area = fullArea(*ctx.renderer);
    const PanelLayout layout = layoutPanel(area, m_styles, m_menu.items().size(), "SELECT REGION",
                                           "Esc to go back", *ctx.text);
    const auto hovered = m_menu.hitTest(layout.list, ctx.input->mousePosition(), m_styles);
    if (hovered != static_cast<std::size_t>(-1)) {
        m_menu.select(hovered);
    }

    const bool confirmed = ctx.input->wasPressed(Action::Interact) ||
                           ctx.input->wasPressed(Action::Jump) ||
                           (hovered != static_cast<std::size_t>(-1) &&
                            ctx.input->wasMousePressed(Input::MouseButton::Left));
    if (!confirmed) {
        return;
    }

    const MenuItem* item = m_menu.currentItem();
    if (item == nullptr) {
        return;
    }
    if (item->label == "BACK") {
        ctx.states->pop();
        return;
    }

    // The item's *value* is the level id, so the row and the entry it stands for
    // cannot drift apart: the menu is generated from the list, never matched
    // back to it by index or by re-reading the label.
    // Re-checked here, not trusted from the row. The row was built when the state
    // was entered; the player could have reset their progress from Settings in
    // between only by leaving this screen — but a stale `enabled` flag that starts
    // a locked region is exactly the kind of bug that reads as "the lock is broken".
    if (!item->enabled) {
        if (ctx.log != nullptr) {
            ctx.log->info("Game", "'{}' is locked", item->label);
        }
        return;
    }

    const std::string id = item->value;
    const auto found = std::find_if(m_levels.begin(), m_levels.end(),
                                    [&id](const Game::LevelEntry& e) { return e.id == id; });
    if (found == m_levels.end()) {
        ctx.log->warn("Game", "level '{}' vanished between listing and selection", id);
        return;
    }

    g_chosenLevel = found->file;
    ctx.log->info("Game", "region selected: {}", found->name);

    // Starting a region is a progression event, so the "current level" pointer
    // moves here rather than when the player leaves it. A player who picks level 7
    // and quits should come back to level 7, not to whatever they finished last.
    if (ctx.progress != nullptr) {
        Core::Progress progress = ctx.progress->loadProgress();
        progress.currentLevelId = found->id;
        ctx.progress->saveProgress(progress);
    }

    ctx.states->push(std::make_shared<PlayingState>());
}

void LevelSelectState::render(StateContext& ctx, double alpha)
{
    Renderer2D& renderer = *ctx.renderer;
    Graphics::TextRenderer& text = *ctx.text;
    const Rect area = fullArea(renderer);
    static_cast<void>(alpha);

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);

    // The same backdrop as the main menu, so the select reads as part of it
    // rather than as a separate screen the player has to learn.
    renderer.drawRect(area, Palette::FutureSky.scaled(0.55f));
    renderer.drawRect(area, Color{0, 0, 0, 0x90});

    const PanelLayout layout = layoutPanel(area, m_styles, m_menu.items().size(), "SELECT REGION",
                                           "Esc to go back", text);
    renderer.drawRect(layout.panel, Palette::PanelFill);
    renderer.drawRectOutline(layout.panel, Palette::PanelBorder, 2.0f);
    text.drawInRect(renderer, layout.heading, "SELECT REGION", m_styles.heading);

    m_menu.ensureVisible(layout.visibleRows, m_styles);
    m_menu.render(renderer, text, layout.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles,
                  layout.visibleRows, static_cast<float>(m_time));
    text.drawInRect(renderer, layout.footer, "Esc to go back", m_styles.hint);

    // An empty directory is a real state, not an error to hide: the player is
    // told why the list is blank and given the way back out.
    if (m_built && m_levels.empty()) {
        const char* message = "No regions found in data/levels";
        text.drawInRect(renderer, Rect{layout.panel.x, layout.panel.center().y,
                                       layout.panel.w, m_styles.rowHeight},
                        message, m_styles.detail);
    }

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        ctx.overlay->render(renderer, text, *ctx.stats, *ctx.states, ctx.loop->stats());
    }

    renderer.setBlendMode(BlendMode::None);

    if (!text.ready()) {
        FontStyle fallback;
        fallback.scale = 5;
        BitmapFont::drawCentered(renderer, area.center().x, area.center().y, "REGIONS",
                                 Palette::TextPrimary, fallback);
    }
}

// ---------------------------------------------------------------------------
// CreditsState
// ---------------------------------------------------------------------------
CreditsState::CreditsState(Core::GameState id)
    : IGameState(id)
{
}

void CreditsState::onEnter(StateContext& ctx)
{
    m_time          = 0.0f;
    m_viewportScale = uiScaleFor(ctx);
    m_styles        = MenuStyles::make(m_viewportScale);
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    m_menu.setItems({{"BACK", "return to the title screen", true}});
    m_built = true;
    ctx.log->info("Game", "entered Credits");
}

void CreditsState::update(StateContext& ctx, double fixedDelta)
{
    m_time += static_cast<float>(fixedDelta);
    if (ctx.input == nullptr || ctx.renderer == nullptr) {
        return;
    }

    const UiScale latest = uiScaleFor(ctx);
    if (latest.factor != m_viewportScale.factor) {
        m_viewportScale = latest;
        m_styles        = MenuStyles::make(latest);
        m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    }

    // Escape always goes back, from anywhere on the screen.
    if (ctx.input->wasPressed(Action::Pause) || ctx.input->wasPressed(Action::Interact) ||
        ctx.input->wasPressed(Action::Jump)) {
        ctx.states->pop();
        return;
    }

    const Rect area  = fullArea(*ctx.renderer);
    const auto panel = layoutPanel(area, m_styles, m_menu.items().size(), "", "", *ctx.text);
    const auto hovered = m_menu.hitTest(panel.list, ctx.input->mousePosition(), m_styles);
    if (hovered != static_cast<std::size_t>(-1) && ctx.input->wasMousePressed(Input::MouseButton::Left)) {
        ctx.states->pop();
    }
}

void CreditsState::render(StateContext& ctx, double alpha)
{
    Renderer2D& renderer = *ctx.renderer;
    const Rect area = fullArea(renderer);
    static_cast<void>(alpha);

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(area, Palette::FutureSky);

    Graphics::TextRenderer& text = *ctx.text;
    const UiScale scale = m_styles.scale;

    TextStyle heading = m_styles.title;
    heading.pixelSize = scale.font(44.0f);
    heading.shadow    = Palette::FutureAccent.withAlpha(0x40);
    text.drawInRect(renderer, Rect{area.x, scale.px(56.0f), area.w, scale.px(56.0f)}, "CREDITS",
                    heading);

    // A short column of centred lines. Static rather than scrolling: there is
    // not enough text to need it, and a scroll the player has to wait for is
    // worse than a page they can read at once.
    struct Line {
        const char* text;
        int size;
        bool    bright;
    };
    static constexpr Line kLines[] = {
        {"ERA SHIFT", 26, true},
        {"", 8, false},
        {"One world. Three eras. Your choices persist.", 16, false},
        {"", 8, false},
        {"ENGINE", 14, true},
        {"Hand-rolled SDL3 renderer, fixed-timestep loop", 14, false},
        {"Headless-testable core with no SDL dependency", 14, false},
        {"", 8, false},
        {"GAMEPLAY", 14, true},
        {"Era-gated tile layers, actors and objectives", 14, false},
        {"Swept AABB physics with coyote time and jump buffering", 14, false},
        {"", 8, false},
        {"TYPEFACE", 14, true},
        {"Space Grotesk, SIL Open Font License 1.1", 14, false},
        {"", 8, false},
        {"Built from scratch in C++20.", 16, true},
    };

    const float lineH = scale.px(26.0f);
    float y           = scale.px(140.0f);
    for (const Line& line : kLines) {
        if (line.text[0] != '\0') {
            TextStyle style = m_styles.detail;
            style.pixelSize = scale.font(static_cast<float>(line.size));
            style.align     = TextAlign::Center;
            style.color     = line.bright ? Palette::TextPrimary : Palette::TextDim;
            style.bold      = line.bright;
            text.drawInRect(renderer, Rect{area.x, y, area.w, lineH}, line.text, style);
        }
        y += lineH * 0.75f;
    }

    const PanelLayout panel =
        layoutPanel(area, m_styles, m_built ? m_menu.items().size() : 0, "", "", text);
    renderer.drawRect(panel.panel, Palette::PanelFill);
    renderer.drawRect(panel.panel, Palette::PanelBorder.withAlpha(0x90));
    m_menu.ensureVisible(panel.visibleRows, m_styles);
    m_menu.render(renderer, text, panel.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles,
                  panel.visibleRows, static_cast<float>(m_time), kRowStagger);

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        ctx.overlay->render(renderer, text, *ctx.stats, *ctx.states, ctx.loop->stats());
    }

    renderer.setBlendMode(BlendMode::None);

    if (!text.ready()) {
        FontStyle fallback;
        fallback.scale = 5;
        BitmapFont::drawCentered(renderer, area.center().x, scale.px(80.0f), "CREDITS",
                                 Palette::TextPrimary, fallback);
    }
}

} // namespace EraShift::Game
