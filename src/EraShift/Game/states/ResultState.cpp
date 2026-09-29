#include "EraShift/Game/states/ResultState.hpp"

#include "EraShift/Game/states/MainMenuState.hpp"
#include "EraShift/Game/states/PausedState.hpp"
#include "EraShift/Game/states/PlayingState.hpp"
#include "EraShift/Graphics/BitmapFont.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <algorithm>
#include <cmath>

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

void ResultState::buildMenu(StateContext& ctx)
{
    m_menu.setItems({
        {"PLAY AGAIN", "restart from the beginning", true},
        {"MAIN MENU",  "back to the title screen",    true},
        {"QUIT",       "exit to desktop",             true},
    });
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

    const Rect area  = fullArea(*ctx.renderer);
    const auto panel = layoutPanel(area, m_styles, m_menu.items().size(), "", "", *ctx.text);
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
    if (item->label == "PLAY AGAIN") {
        ctx.states->reset(std::make_shared<PlayingState>());
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
    {
        TextStyle bloom = m_styles.title;
        bloom.color   = accent.withAlpha(static_cast<std::uint8_t>(40.0f + 60.0f * glow));
        bloom.shadow  = Palette::Transparent;
        text.drawInRect(renderer, Rect{area.x, scale.px(70.0f), area.w, scale.px(90.0f)},
                        won ? "THE GATE OPENS" : "TIMELINE COLLAPSED", bloom);
    }
    {
        TextStyle headline = m_styles.title;
        headline.color    = Palette::TextPrimary;
        text.drawInRect(renderer, Rect{area.x, scale.px(70.0f), area.w, scale.px(90.0f)},
                        won ? "THE GATE OPENS" : "TIMELINE COLLAPSED", headline);
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
    float y            = scale.px(178.0f);
    for (const Row& row : rows) {
        text.drawInRect(renderer, Rect{gridX, y, gridW * 0.55f, rowH}, row.label, label);
        text.drawInRect(renderer, Rect{gridX + gridW * 0.45f, y, gridW * 0.55f, rowH}, row.value,
                        value);
        y += rowH;
    }

    // --- options ------------------------------------------------------------
    const PanelLayout panel =
        layoutPanel(Rect{area.x, y + scale.px(16.0f), area.w, area.h - y - scale.px(16.0f)},
                    m_styles, m_menu.items().size(), "", "", text);
    renderer.drawRect(panel.panel, Palette::PanelFill);
    renderer.drawRect(panel.panel, Palette::PanelBorder.withAlpha(0x90));
    m_menu.render(renderer, *ctx.text, panel.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles);

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
    m_menu.render(renderer, text, panel.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles);

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
