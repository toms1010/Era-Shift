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
using Graphics::UiScale;
using Graphics::Vec2;
using Input::Action;

namespace Palette = Graphics::Palette;

namespace {

constexpr float kBackgroundScroll = 12.0f;

/// Small triangular marker used to bracket the selected menu entry.
void drawChevron(Renderer2D& renderer, float centerX, float centerY, float size, float direction,
                 Color color)
{
    renderer.drawTriangle(Vec2{centerX - direction * size * 0.5f, centerY - size},
                          Vec2{centerX - direction * size * 0.5f, centerY + size},
                          Vec2{centerX + direction * size * 0.5f, centerY},
                          color);
}

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
MenuStyles MenuStyles::make(UiScale scale)
{
    MenuStyles s;
    s.scale = scale;

    // --- title: the one place a large size is justified ----------------------
    // Bold plus a dark shadow keeps it legible over the moving backdrop.
    s.title.pixelSize = scale.font(78.0f);
    s.title.color     = Palette::TextPrimary;
    s.title.shadow    = Palette::FutureSky.scaled(0.35f);
    s.title.bold      = true;
    s.title.align     = TextAlign::Center;
    s.title.valign    = TextVAlign::Middle;

    s.tagline.pixelSize = scale.font(15.0f);
    s.tagline.color     = Palette::TextDim;
    s.tagline.align     = TextAlign::Center;
    s.tagline.valign    = TextVAlign::Middle;

    s.heading.pixelSize = scale.font(30.0f);
    s.heading.color     = Palette::TextPrimary;
    s.heading.align     = TextAlign::Center;
    s.heading.valign    = TextVAlign::Middle;
    s.heading.shadow    = Color{0, 0, 0, 0xC0};

    // --- entries -------------------------------------------------------------
    // Entries and their detail text are close in size on purpose. A 26px
    // label against 15px detail reads as a caption dropped onto a headline;
    // the hierarchy comes from weight and colour instead, which is both calmer
    // and more legible.
    s.entry.pixelSize = scale.font(21.0f);
    s.entry.color     = Palette::TextPrimary;
    s.entry.valign    = TextVAlign::Middle;
    s.entry.shadow    = Color{0, 0, 0, 0xD0};

    s.entrySelected       = s.entry;
    s.entrySelected.color = Palette::Accent;
    s.entrySelected.bold  = true;

    s.entryDisabled       = s.entry;
    s.entryDisabled.color = Palette::TextDim;
    s.entryDisabled.shadow = Palette::Transparent;

    s.detail.pixelSize = scale.font(16.0f);
    s.detail.color     = Palette::TextDim;
    s.detail.align     = TextAlign::Center;
    s.detail.valign    = TextVAlign::Middle;
    s.detail.shadow    = Color{0, 0, 0, 0xD0};

    s.hint.pixelSize = scale.font(15.0f);
    s.hint.color     = Palette::TextDim;
    s.hint.align     = TextAlign::Center;
    s.hint.valign    = TextVAlign::Middle;
    s.hint.shadow    = Color{0, 0, 0, 0xC0};

    s.rowHeight  = scale.px(46.0f);
    s.rowSpacing = scale.px(4.0f);
    s.listWidth  = scale.px(360.0f);
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
    // Always at least tall enough for the text plus breathing room, so a
    // custom font size can never clip its own row.
    const float textHeight = static_cast<float>(styles.entry.pixelSize) * 1.6f;
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

        // Unselected rows stay flat so the selection is unmistakable. Hover is a
        // subtle lift, not a second competing highlight.
        Color fill = Palette::Transparent;
        if (selected) {
            fill = Palette::PanelBorder.withAlpha(0x66);
        } else if (hovered) {
            fill = Palette::PanelBorder.withAlpha(0x22);
        }
        if (fill.a > 0) {
            renderer.drawRect(row, fill);
        }

        // A hairline between rows turns the panel into a list instead of a
        // slab of colour. Skipped on the last row so the panel has a clean base.
        if (i + 1 < m_items.size()) {
            renderer.drawRect(
                Rect{row.x + styles.scale.px(16.0f), row.bottom(), row.w - styles.scale.px(32.0f),
                     1.0f},
                Palette::PanelBorder.withAlpha(selected ? 0x50 : 0x28));
        }

        TextStyle style = !item.enabled ? styles.entryDisabled
                             : selected ? styles.entrySelected
                                        : styles.entry;
        // Centred to match the title, tagline and hints, and because short
        // labels pinned to the left of a wide panel look stranded.
        style.align = TextAlign::Center;

        // The selection is a full-width fill, accent-coloured bold text, and a
        // pair of chevrons bracketing the label. The chevrons keep the selection
        // readable without relying on colour alone, and are placed from the
        // measured label so they never sit on top of the text.
        if (selected) {
            const float labelWidth = text.measure(item.label, style).x;
            const float gap       = styles.scale.px(14.0f);
            const float size      = styles.scale.px(4.0f);
            const float offset    = labelWidth * 0.5f + gap + size * 0.5f;
            for (const float dir : {-1.0f, 1.0f}) {
                drawChevron(renderer, row.center().x + dir * offset, row.center().y, size, dir,
                            Palette::Accent);
            }
        }

        const float pad = styles.scale.px(20.0f);
        text.drawInRect(renderer, Rect{row.x + pad, row.y, row.w - pad * 2.0f, row.h},
                        item.label, style);

        // The detail is deliberately not drawn here. Right-aligning a caption
        // next to a short label leaves a wide, ragged gap; it is shown once,
        // centred, under the list for the selected entry instead.
    }
}

UiScale MainMenuState::currentUiScale(const StateContext& ctx) const
{
    if (ctx.renderer == nullptr) {
        return m_viewportScale;
    }
    return UiScale::forViewport(static_cast<float>(ctx.renderer->camera().viewportWidth()),
                                static_cast<float>(ctx.renderer->camera().viewportHeight()));
}

Rect MainMenuState::titleBlock(const Rect& area) const
{
    // Title, tagline and build label form one block, anchored a little above
    // centre so the menu below it feels like the main event.
    const float height = m_styles.scale.px(132.0f);
    return Rect{area.x, area.y + area.h * 0.15f, area.w, height};
}

Rect MainMenuState::menuPanel(const Rect& area) const
{
    // The panel is sized from the list itself, then centred in the space below
    // the title, so it stays put whatever the number of entries.
    const float padding   = m_styles.scale.px(14.0f);
    const float width     = m_styles.listWidth + padding * 2.0f;
    const float rowStride = m_menu.rowHeight(m_styles) + m_styles.scale.px(4.0f);
    const float listHeight = static_cast<float>(m_menu.items().size()) * rowStride;
    const float height     = listHeight + padding * 2.0f;

    const float top = titleBlock(area).bottom() + m_styles.scale.px(34.0f);
    return Rect{area.center().x - width * 0.5f, top, width, height};
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

    // Styles depend on the viewport, so they are rebuilt on entry and whenever
    // the window is resized. Row metrics come from the style too, so the two
    // can never disagree.
    m_styles = MenuStyles::make(currentUiScale(ctx));
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
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

    // Resizing changes the UI scale, so the styles and the list metrics are
    // rebuilt when it moves. Doing this here rather than in render() keeps
    // layout and drawing in agreement.
    const UiScale latest = currentUiScale(ctx);
    if (latest.factor != m_viewportScale.factor) {
        m_viewportScale = latest;
        m_styles        = MenuStyles::make(latest);
        m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    }

    if (ctx.input->wasPressed(Action::MoveUp)) {
        m_menu.move(-1);
    }
    if (ctx.input->wasPressed(Action::MoveDown)) {
        m_menu.move(1);
    }

    const Rect area   = fullArea(*ctx.renderer);
    const Rect panel  = menuPanel(area);
    const Rect bounds{panel.x, panel.y, panel.w, panel.h};

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
    // Kept below the menu panel so the horizon never runs through the text.
    const float horizon = area.y + area.h * 0.78f;

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
    const UiScale  scale  = currentUiScale(ctx);
    const float    titleY = area.h * 0.15f;
    const float    glow   = 0.5f + 0.5f * std::sin(static_cast<float>(m_time) * 1.6f);

    // --- title ---------------------------------------------------------------
    // A pulsing bloom behind the title, then the title itself. The bloom is
    // drawn in the same glyphs, so it reads as a glow rather than a shadow.
    {
        TextStyle bloom = m_styles.title;
        bloom.color   = Palette::FutureAccent.withAlpha(
            static_cast<std::uint8_t>(40.0f + 60.0f * glow));
        bloom.shadow  = Palette::Transparent;
        text.drawInRect(renderer, Rect{area.x, titleY, area.w, scale.px(104.0f)}, "ERA SHIFT", bloom);
    }
    text.drawInRect(renderer, Rect{area.x, titleY, area.w, scale.px(104.0f)}, "ERA SHIFT",
                    m_styles.title);

    // Tagline and build label, stacked tightly under the title so they read as
    // one block rather than three floating lines.
    const float taglineY = titleY + scale.px(56.0f);
    text.drawInRect(renderer, Rect{area.x, taglineY, area.w, scale.px(20.0f)},
                    "ONE WORLD - THREE ERAS - YOUR CHOICES PERSIST", m_styles.tagline);

    {
        TextStyle build = m_styles.tagline;
        build.color = Palette::AccentWarm;
        build.pixelSize = scale.font(13.0f);
        text.drawInRect(renderer, Rect{area.x, taglineY + scale.px(24.0f), area.w, scale.px(18.0f)},
                        "DEVELOPMENT BUILD", build);
    }

    // --- menu panel ----------------------------------------------------------
    // The list sits on a panel. Without it the animated horizon runs straight
    // through the entries, which reads as a rendering artefact rather than a
    // deliberate horizon.
    const Rect panel = menuPanel(area);
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(panel, Palette::PanelFill);
    renderer.drawRect(panel, Palette::PanelBorder.withAlpha(0x90));

    const float panelPad = scale.px(14.0f);
    const Rect list{panel.x + panelPad, panel.y + panelPad, panel.w - panelPad * 2.0f,
                    panel.h - panelPad * 2.0f};
    m_menu.render(renderer, text, list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles);

    // --- description of the highlighted entry --------------------------------
    if (const MenuItem* selected = m_menu.currentItem()) {
        if (!selected->detail.empty()) {
            TextStyle detail = m_styles.detail;
            detail.align  = TextAlign::Center;
            detail.valign = TextVAlign::Middle;
            detail.color  = Palette::TextDim;
            text.drawInRect(renderer,
                            Rect{panel.x, panel.bottom() + scale.px(8.0f), panel.w, scale.px(22.0f)},
                            selected->detail, detail);
        }
    }

    // --- hints ---------------------------------------------------------------
    const float hintY = area.bottom() - scale.px(44.0f);
    text.drawInRect(renderer, Rect{area.x, hintY, area.w, scale.px(18.0f)},
                    "W/S or arrow keys to select      E to confirm", m_styles.hint);
    text.drawInRect(renderer, Rect{area.x, hintY + scale.px(20.0f), area.w, scale.px(18.0f)},
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
        BitmapFont::drawCentered(renderer, area.center().x, titleY + scale.px(45.0f), "ERA SHIFT",
                                 Palette::TextPrimary, fallback);
    }
}

} // namespace EraShift::Game
