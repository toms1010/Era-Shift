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
using ::EraShift::StateContext;
using Graphics::BitmapFont;
using Graphics::BlendMode;
using Graphics::Color;
using Graphics::FontStyle;
using Graphics::Rect;
using Graphics::Renderer2D;
using Graphics::TextAlign;
using Graphics::TextStyle;
using Graphics::TextVAlign;
using Graphics::Vec2;
using Input::Action;

namespace Palette = Graphics::Palette;

namespace {

constexpr float kBackgroundScroll = 12.0f;

Rect fullArea(Renderer2D& renderer)
{
    return Rect{0.0f, 0.0f,
                static_cast<float>(renderer.camera().viewportWidth()),
                static_cast<float>(renderer.camera().viewportHeight())};
}

} // namespace

// ---------------------------------------------------------------------------
// MenuStyles
// ---------------------------------------------------------------------------
MenuStyles MenuStyles::make()
{
    MenuStyles s;

    // A large, bold title reads at a distance; the shadow separates it from the
    // animated backdrop behind it.
    s.title.pixelSize = 78;
    s.title.color     = Palette::TextPrimary;
    s.title.shadow    = Palette::FutureSky.scaled(0.45f);
    s.title.bold      = true;
    s.title.align     = TextAlign::Center;
    s.title.valign    = TextVAlign::Middle;

    s.heading.pixelSize = 24;
    s.heading.color     = Palette::TextPrimary;
    s.heading.align     = TextAlign::Center;
    s.heading.valign    = TextVAlign::Middle;
    s.heading.shadow    = Color{0, 0, 0, 0x90};

    s.entry.pixelSize       = 26;
    s.entry.color           = Palette::TextPrimary;
    s.entry.valign          = TextVAlign::Middle;
    s.entry.shadow          = Color{0, 0, 0, 0xB0};

    s.entrySelected         = s.entry;
    s.entrySelected.color   = Palette::Accent;
    s.entrySelected.bold    = true;

    s.entryDisabled         = s.entry;
    s.entryDisabled.color   = Palette::TextDim;
    s.entryDisabled.shadow  = Palette::Transparent;

    s.detail.pixelSize = 15;
    s.detail.color     = Palette::TextDim;
    s.detail.align     = TextAlign::Right;
    s.detail.valign    = TextVAlign::Middle;

    s.hint.pixelSize = 16;
    s.hint.color     = Palette::TextDim;
    s.hint.align     = TextAlign::Center;
    s.hint.valign    = TextVAlign::Middle;

    return s;
}

// ---------------------------------------------------------------------------
// MenuList
// ---------------------------------------------------------------------------
void MenuList::setItems(std::vector<MenuItem> items)
{
    m_items    = std::move(items);
    m_selected = 0;
    if (!m_items.empty() && !isSelectedEnabled()) {
        move(1);
    }
}

void MenuList::move(int delta)
{
    if (m_items.empty() || delta == 0) {
        return;
    }

    const int count = static_cast<int>(m_items.size());
    for (int step = 0; step < count; ++step) {
        m_selected = static_cast<std::size_t>(
            (static_cast<int>(m_selected) + delta % count + count) % count);
        if (isSelectedEnabled()) {
            return;
        }
    }
    // Nothing is enabled: leave the selection where it landed.
}

void MenuList::select(std::size_t index)
{
    if (index < m_items.size() && m_items[index].enabled) {
        m_selected = index;
    }
}

const MenuItem* MenuList::currentItem() const noexcept
{
    return m_items.empty() ? nullptr : &m_items[m_selected];
}

bool MenuList::isSelectedEnabled() const noexcept
{
    return m_selected < m_items.size() && m_items[m_selected].enabled;
}

float MenuList::rowHeight(const MenuStyles& styles) const
{
    // At least tall enough for the text plus breathing room, and never smaller
    // than the configured minimum.
    const float textHeight = styles.entry.pixelSize * 1.35f;
    return std::max(m_rowHeight, textHeight);
}

Rect MenuList::rowBounds(std::size_t index, const Rect& bounds, const MenuStyles& styles) const
{
    if (index >= m_items.size()) {
        return {};
    }
    const float stride = rowHeight(styles) + m_spacing;
    return Rect{bounds.x, bounds.y + static_cast<float>(index) * stride, bounds.w,
                rowHeight(styles)};
}

std::size_t MenuList::hitTest(const Rect& bounds, Vec2 point) const noexcept
{
    const float stride = m_rowHeight + m_spacing;
    if (stride <= 0.0f || !bounds.contains(point)) {
        return static_cast<std::size_t>(-1);
    }
    const auto index = static_cast<std::size_t>((point.y - bounds.y) / stride);
    if (index >= m_items.size()) {
        return static_cast<std::size_t>(-1);
    }
    return m_items[index].enabled ? index : static_cast<std::size_t>(-1);
}

void MenuList::render(Renderer2D& renderer, Graphics::TextRenderer& text, const Rect& bounds,
                      Vec2 mouse, const MenuStyles& styles) const
{
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const MenuItem& item = m_items[i];
        const Rect row = rowBounds(i, bounds, styles);
        if (row.isEmpty()) {
            continue;
        }

        const bool selected = (i == m_selected);
        const bool hovered  = row.contains(mouse) && item.enabled;

        Color fill = Palette::PanelFill;
        if (selected) {
            fill = Palette::PanelBorder.withAlpha(0x55);
        } else if (hovered) {
            fill = Palette::PanelBorder.withAlpha(0x28);
        }
        renderer.drawRect(row, fill);

        if (selected) {
            // The accent bar is a shape cue, not just a colour cue.
            renderer.drawRect(Rect{row.x, row.y, 4.0f, row.h}, Palette::Accent);
        }

        const TextStyle& style = !item.enabled  ? styles.entryDisabled
                                  : selected    ? styles.entrySelected
                                                : styles.entry;
        text.drawInRect(renderer, Rect{row.x + 18.0f, row.y, row.w - 36.0f, row.h}, item.label,
                        style);

        if (!item.detail.empty()) {
            text.drawInRect(renderer, Rect{row.x, row.y, row.w - 18.0f, row.h}, item.detail,
                            styles.detail);
        }
    }
}

// ---------------------------------------------------------------------------
// MainMenuState
// ---------------------------------------------------------------------------
MainMenuState::MainMenuState(GameState id)
    : IGameState(id)
{
}

void MainMenuState::onEnter(StateContext& ctx)
{
    m_time = 0.0f;

    m_styles = MenuStyles::make();
    m_menu.setMetrics(46.0f, 8.0f);
    m_menu.setItems({
        {"NEW GAME", "begin a new timeline", true},
        {"CONTINUE", "load the last save",   false},
        {"SETTINGS", "graphics, audio, input", true},
        {"CREDITS",  "about Era Shift",      true},
        {"QUIT",     "exit to desktop",      true},
    });

    ctx.log->info("Game", "entered MainMenu");
}

void MainMenuState::onExit(StateContext& ctx)
{
    ctx.log->info("Game", "left MainMenu");
}

void MainMenuState::update(StateContext& ctx, double fixedDelta)
{
    m_time += static_cast<float>(fixedDelta);

    if (ctx.input == nullptr || ctx.renderer == nullptr) {
        return;
    }

    if (ctx.input->wasPressed(Action::MoveUp)) {
        m_menu.move(-1);
    }
    if (ctx.input->wasPressed(Action::MoveDown)) {
        m_menu.move(1);
    }

    const Rect area    = fullArea(*ctx.renderer);
    const Rect bounds{area.center().x - 220.0f, area.h * 0.50f, 440.0f, 260.0f};

    const auto hovered = m_menu.hitTest(bounds, ctx.input->mousePosition());
    if (hovered != static_cast<std::size_t>(-1)) {
        m_menu.select(hovered);
    }

    const bool confirmed = ctx.input->wasPressed(Action::Interact) ||
                           ctx.input->wasPressed(Action::Jump) ||
                           (hovered != static_cast<std::size_t>(-1) &&
                            ctx.input->wasMousePressed(Input::MouseButton::Left));

    if (confirmed) {
        activateSelection(ctx);
    }
}

void MainMenuState::activateSelection(StateContext& ctx)
{
    const MenuItem* item = m_menu.currentItem();
    if (item == nullptr || !item->enabled) {
        return;
    }

    if (item->label == "NEW GAME") {
        ctx.states->switchTo(std::make_shared<PlayingState>());
    } else if (item->label == "SETTINGS") {
        ctx.states->push(std::make_shared<SettingsState>());
    } else if (item->label == "QUIT") {
        ctx.log->info("Game", "quit requested from the main menu");
        ctx.loop->requestStop();
    } else {
        ctx.log->debug("Game", "menu entry '{}' is not wired up yet", item->label);
    }
}

void MainMenuState::drawBackdrop(Renderer2D& renderer, const Rect& area, double time) const
{
    // Three bands of colour that stand in for the three eras. The bands drift at
    // different speeds, which reads as a parallax backdrop until real art
    // exists and gives the screen motion even on an empty project.
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(area, Palette::FutureSky);

    const float t = static_cast<float>(time);
    const float horizon = area.y + area.h * 0.55f;

    for (int band = 0; band < 3; ++band) {
        const float speed     = (1.0f + static_cast<float>(band) * 0.8f) * kBackgroundScroll;
        const float offset    = std::fmod(t * speed, area.w);
        const float thickness = area.h * (0.10f + 0.05f * static_cast<float>(band));

        Color color = Palette::FutureGround;
        float alpha = 0.35f + 0.15f * static_cast<float>(band);
        if (band == 1) {
            color = Palette::FutureStone;
        } else if (band == 2) {
            color = Palette::FutureAccent;
            alpha = 0.10f;
        }

        // Repeat the band across the width so the scroll is seamless.
        for (float x = -offset; x < area.w; x += area.w) {
            const Rect stripe{x, horizon + static_cast<float>(band) * area.h * 0.08f,
                              area.w, thickness};
            renderer.drawRect(stripe, color.withAlpha(static_cast<std::uint8_t>(alpha * 255.0f)));
        }
    }

    // A few drifting motes sell the "temporal" mood.
    for (int i = 0; i < 28; ++i) {
        const float seed = static_cast<float>(i) * 0.6180339f;
        const float x = std::fmod(seed * area.w + t * (8.0f + seed * 20.0f), area.w);
        const float y = std::fmod(seed * area.h - t * (5.0f + seed * 9.0f) + area.h, area.h);
        const float size = 1.0f + (seed > 0.5f ? 1.0f : 0.0f);
        renderer.drawRect(Rect{x, y, size, size}, Palette::FutureAccent.withAlpha(0x50));
    }

    renderer.setBlendMode(BlendMode::None);
}

void MainMenuState::render(StateContext& ctx, double alpha)
{
    Renderer2D& renderer = *ctx.renderer;
    const Rect area = fullArea(renderer);

    // The engine clears and presents; a state only draws.
    renderer.setCameraEnabled(false);
    drawBackdrop(renderer, area, static_cast<double>(m_time) + alpha);

    Graphics::TextRenderer& text = *ctx.text;

    // --- title ---------------------------------------------------------------
    const float titleY = area.h * 0.20f;
    const float glow   = 0.5f + 0.5f * std::sin(static_cast<float>(m_time) * 1.6f);

    // A pulsing bloom behind the title, then the title itself.
    {
        TextStyle bloom = m_styles.title;
        bloom.color = Palette::FutureAccent.withAlpha(
            static_cast<std::uint8_t>(40.0f + 60.0f * glow));
        bloom.shadow = Palette::Transparent;
        text.drawInRect(renderer, Rect{0.0f, titleY - 60.0f, area.w, 120.0f}, "ERA SHIFT", bloom);
    }
    text.drawInRect(renderer, Rect{0.0f, titleY - 60.0f, area.w, 120.0f}, "ERA SHIFT",
                    m_styles.title);

    text.drawInRect(renderer, Rect{0.0f, titleY + 34.0f, area.w, 26.0f},
                    "ONE WORLD - THREE ERAS - YOUR CHOICES PERSIST", m_styles.hint);
    text.drawInRect(renderer, Rect{0.0f, titleY + 58.0f, area.w, 26.0f}, "DEVELOPMENT BUILD",
                    [&] {
                        TextStyle s = m_styles.hint;
                        s.color = Palette::AccentWarm;
                        return s;
                    }());

    // --- menu ----------------------------------------------------------------
    const Rect bounds{area.center().x - 220.0f, area.h * 0.50f, 440.0f, 260.0f};
    m_menu.render(renderer, text, bounds,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles);

    // --- hints ---------------------------------------------------------------
    text.drawInRect(renderer, Rect{0.0f, area.bottom() - 52.0f, area.w, 22.0f},
                    "W/S or arrow keys to select    E to confirm", m_styles.hint);
    text.drawInRect(renderer, Rect{0.0f, area.bottom() - 30.0f, area.w, 22.0f},
                    "F3 debug overlay", m_styles.hint);

    // The debug overlay works in every state, including this one, because it is
    // the tool used to diagnose the main menu.
    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        renderer.setBlendMode(BlendMode::Alpha);
        ctx.overlay->render(renderer, text, *ctx.stats, *ctx.states, ctx.loop->stats());
        renderer.setBlendMode(BlendMode::None);
    }

    // The bitmap font stays available as a guaranteed fallback; if the TTF face
    // failed to load the title would otherwise be invisible.
    if (!text.ready()) {
        FontStyle fallback;
        fallback.scale = 6;
        BitmapFont::drawCentered(renderer, area.center().x, titleY, "ERA SHIFT",
                                 Palette::TextPrimary, fallback);
    }
}

} // namespace EraShift::Game
