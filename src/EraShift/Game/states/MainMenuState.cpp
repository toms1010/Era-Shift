#include "EraShift/Game/states/MainMenuState.hpp"

#include "EraShift/Core/SaveGame.hpp"
#include "EraShift/Game/states/PausedState.hpp"
#include "EraShift/Game/states/ResultState.hpp"
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

constexpr const char* kTitle      = "ERA SHIFT";
constexpr const char* kTagline    = "ONE WORLD - THREE ERAS - YOUR CHOICES PERSIST";
constexpr const char* kHintRow1   = "W/S or arrow keys to select      E to confirm";
constexpr const char* kHintRow2   = "F3 debug overlay";

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

/// Height a single line of text occupies, in pixels.
float lineHeightOf(Graphics::TextRenderer& text, std::string_view content,
                   const Graphics::TextStyle& style)
{
    if (content.empty()) {
        return 0.0f;
    }
    return text.measure(content, style).y;
}

} // namespace

// ---------------------------------------------------------------------------
// Shared interface scale
// ---------------------------------------------------------------------------
Graphics::UiScale uiScaleFor(const StateContext& ctx)
{
    // With no renderer there is no viewport to fit, so the neutral scale is the
    // only defensible answer.
    if (ctx.renderer == nullptr) {
        return Graphics::UiScale{1.0f};
    }

    // The player's own preference is a percentage on top of the automatic fit.
    // Reading it here (rather than only when Settings changes it) means the
    // adjustment is picked up the moment it is written.
    const float preference = ctx.config != nullptr
                                 ? static_cast<float>(
                                       ctx.config->store().getInt("graphics", "uiScale", 100)) / 100.0f
                                 : 1.0f;

    return Graphics::UiScale::forViewport(
        static_cast<float>(ctx.renderer->camera().viewportWidth()),
        static_cast<float>(ctx.renderer->camera().viewportHeight()), preference);
}

// ---------------------------------------------------------------------------
// Shared panel layout
// ---------------------------------------------------------------------------
PanelLayout layoutPanel(const Rect& area, const MenuStyles& styles, std::size_t rowCount,
                       std::string_view headingText, std::string_view footerText,
                       Graphics::TextRenderer& text)
{
    PanelLayout layout;
    const UiScale scale = styles.scale;

    const float padding  = scale.px(20.0f);
    const float headingH = lineHeightOf(text, headingText, styles.heading);
    const float footerH  = lineHeightOf(text, footerText, styles.hint);
    // Gaps are reference-space and scale with the interface.
    const float headingGap = headingH > 0.0f ? scale.px(18.0f) : 0.0f;
    const float footerGap  = footerH > 0.0f ? scale.px(18.0f) : 0.0f;

    const float rowStride = styles.rowHeight + styles.rowSpacing;
    const float listH     = static_cast<float>(rowCount) * rowStride;

    // The panel is as wide as the widest thing it contains, never narrower than
    // the shared list width, and never wider than the space available.
    const float contentW = std::max({styles.listWidth, text.measure(headingText, styles.heading).x,
                                     text.measure(footerText, styles.hint).x});
    const float availableW = std::max(area.w - scale.px(24.0f), scale.px(120.0f));
    const float width      = std::min(contentW + padding * 2.0f, availableW);

    const float height = padding + headingH + headingGap + listH + footerGap + footerH + padding;
    const float maxH   = std::max(area.h - scale.px(16.0f), scale.px(80.0f));
    const float clampedH = std::min(height, maxH);

    layout.panel = Rect{area.center().x - width * 0.5f, area.center().y - clampedH * 0.5f, width,
                        clampedH};

    float y = layout.panel.y + padding;
    if (headingH > 0.0f) {
        layout.heading = Rect{layout.panel.x, y, layout.panel.w, headingH};
        y += headingH + headingGap;
    }
    layout.list = Rect{layout.panel.x + padding, y, layout.panel.w - padding * 2.0f, listH};
    if (footerH > 0.0f) {
        // Anchored to the bottom of the panel so the gap above it absorbs any
        // shortfall between the requested and the clamped height.
        layout.footer = Rect{layout.panel.x, layout.panel.bottom() - padding - footerH,
                             layout.panel.w, footerH};
    }
    return layout;
}

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

float MenuList::rowHeight(const MenuStyles& styles) const noexcept
{
    // Always at least tall enough for the text plus breathing room, so a
    // custom font size can never clip its own row.
    const float textHeight = static_cast<float>(styles.entry.pixelSize) * 1.6f;
    return std::max(m_rowHeight, textHeight);
}

Rect MenuList::rowBounds(std::size_t index, const Rect& bounds, const MenuStyles& styles) const noexcept
{
    if (index >= m_items.size()) {
        return {};
    }
    const float stride = rowHeight(styles) + m_spacing;
    return Rect{bounds.x, bounds.y + static_cast<float>(index) * stride, bounds.w,
                rowHeight(styles)};
}

std::size_t MenuList::hitTest(const Rect& bounds, Vec2 point, const MenuStyles& styles) const noexcept
{
    // Must use the same derived row height the renderer uses, otherwise a large
    // font pushes the drawn rows down and the click lands on the wrong entry.
    const float stride = rowHeight(styles) + m_spacing;
    if (stride <= 0.0f || !bounds.contains(point)) {
        return static_cast<std::size_t>(-1);
    }
    // The pointer can sit in the spacing below the last row; `floor` of the
    // stride would then report an index inside the list, so the index is
    // checked against the row rectangle as well.
    const auto index = static_cast<std::size_t>((point.y - bounds.y) / stride);
    if (index >= m_items.size() || !rowBounds(index, bounds, styles).contains(point)) {
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

// ---------------------------------------------------------------------------
// MainMenuState
// ---------------------------------------------------------------------------
MainMenuState::MainMenuState(GameState id)
    : IGameState(id)
{
}

void MainMenuState::buildMenu(StateContext& ctx)
{
    // CONTINUE is only offered when there is something to continue. Enabling it
    // unconditionally and failing on activation is worse than not offering it.
    if (ctx.config != nullptr) {
        const Core::SaveManager manager(ctx.config->saveDirectory(), *ctx.log);
        m_hasSave = manager.hasSave();
    } else {
        m_hasSave = false;
    }

    m_menu.setItems({
        {"NEW GAME", "begin a new timeline",   true},
        {"CONTINUE", m_hasSave ? "resume where you left off" : "no saved run", m_hasSave},
        {"SETTINGS", "graphics, audio, input", true},
        {"CREDITS",  "about Era Shift",        true},
        {"QUIT",     "exit to desktop",        true},
    });
}

void MainMenuState::onEnter(StateContext& ctx)
{
    m_time = 0.0f;

    // Styles depend on the viewport, so they are rebuilt on entry and whenever
    // the window is resized. Row metrics come from the style too, so the two
    // can never disagree.
    m_viewportScale = uiScaleFor(ctx);
    m_styles        = MenuStyles::make(m_viewportScale);
    m_buildStyle    = m_styles.tagline;
    m_buildStyle.color     = Palette::AccentWarm;
    m_buildStyle.pixelSize = m_styles.scale.font(13.0f);
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    buildMenu(ctx);

    ctx.log->info("Game", "entered MainMenu");
}

void MainMenuState::onExit(StateContext& ctx)
{
    ctx.log->info("Game", "left MainMenu");
}

void MainMenuState::syncScale(StateContext& ctx)
{
    // Resizing changes the UI scale, so the styles and the list metrics are
    // rebuilt when it moves. Doing this here rather than in render() keeps
    // layout and drawing in agreement.
    const UiScale latest = uiScaleFor(ctx);
    if (latest.factor == m_viewportScale.factor) {
        return;
    }
    m_viewportScale = latest;
    m_styles        = MenuStyles::make(latest);
    m_buildStyle    = m_styles.tagline;
    m_buildStyle.color     = Palette::AccentWarm;
    m_buildStyle.pixelSize = latest.font(13.0f);
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
}

Rect MainMenuState::listBounds(const StateContext& ctx) const
{
    if (ctx.renderer == nullptr) {
        return Rect{};
    }
    return computeLayout(ctx).list;
}

void MainMenuState::update(StateContext& ctx, double fixedDelta)
{
    m_time += static_cast<float>(fixedDelta);

    if (ctx.input == nullptr || ctx.renderer == nullptr) {
        return;
    }

    syncScale(ctx);

    if (ctx.input->wasPressed(Action::MoveUp)) {
        m_menu.move(-1);
    }
    if (ctx.input->wasPressed(Action::MoveDown)) {
        m_menu.move(1);
    }

    const Rect list = computeLayout(ctx).list;
    const auto hovered = m_menu.hitTest(list, ctx.input->mousePosition(), m_styles);
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
        return;
    }

    if (item->label == "CONTINUE") {
        Core::SaveGame save;
        if (ctx.config == nullptr) {
            return;
        }
        const Core::SaveManager manager(ctx.config->saveDirectory(), *ctx.log);
        if (!manager.load(save)) {
            m_hasSave = false;
            buildMenu(ctx);
            ctx.log->warn("Game", "CONTINUE selected but the save could not be read");
            return;
        }
        auto state = std::make_shared<PlayingState>();
        state->applySave(save);
        ctx.states->switchTo(state);
        return;
    }

    if (item->label == "SETTINGS") {
        ctx.states->push(std::make_shared<SettingsState>());
        return;
    }

    if (item->label == "CREDITS") {
        ctx.states->push(std::make_shared<CreditsState>());
        return;
    }

    if (item->label == "QUIT") {
        ctx.log->info("Game", "quit requested from the main menu");
        ctx.loop->requestStop();
        return;
    }

    ctx.log->debug("Game", "menu entry '{}' is not wired up", item->label);
}

MainMenuState::Layout MainMenuState::computeLayout(const StateContext& ctx) const
{
    Layout layout;
    const UiScale scale = m_styles.scale;
    const Graphics::TextRenderer& text = *ctx.text;

    // Everything is measured, then stacked, then the whole group is centred.
    // Hard-coded offsets between a 78px title and a 15px tagline cannot stay
    // correct across every type size and window size, and did not: the tagline
    // sat inside the title's descenders.
    const float titleH  = text.measure(kTitle, m_styles.title).y;
    const float tagH    = text.measure(kTagline, m_styles.tagline).y;
    const float buildH  = text.measure(ctx.buildLabel, m_buildStyle).y;

    const float titleToTag = scale.px(14.0f);
    const float tagToBuild = scale.px(6.0f);
    const float titleToMenu = scale.px(40.0f);
    const float menuToDetail = scale.px(10.0f);

    const float headH = titleH + titleToTag + tagH + tagToBuild + buildH;

    // The detail caption is only present for an entry that has one.
    const MenuItem* selected = m_menu.currentItem();
    const float detailH = (selected != nullptr && !selected->detail.empty())
                              ? text.measure(selected->detail, m_styles.detail).y
                              : 0.0f;
    const float detailGap = detailH > 0.0f ? menuToDetail : 0.0f;

    // The hints are pinned near the bottom of the screen, so they are not part
    // of the centred group.
    const Rect area = fullArea(*ctx.renderer);
    const float hintH = scale.px(36.0f);
    layout.hints = Rect{area.x, area.bottom() - hintH, area.w, hintH};

    const float rowStride = m_menu.rowHeight(m_styles) + m_styles.rowSpacing;
    const std::size_t rows = m_menu.items().size();
    const float listH = rows > 0 ? static_cast<float>(rows) * rowStride - m_styles.rowSpacing
                                 : 0.0f;

    // Panel padding is shared with layoutPanel() so the main menu and the pause
    // menu have the same inset.
    const float padding = scale.px(20.0f);
    const float panelH  = listH + padding * 2.0f;

    // Never taller than the space above the hints, or the block would slide
    // under them on a short window.
    const float usableH = std::max(layout.hints.y - area.y, scale.px(80.0f));
    layout.totalHeight =
        std::min(headH + titleToMenu + panelH + detailGap + detailH, usableH - scale.px(16.0f));

    // Centred in the space above the hints, then nudged up a little so the
    // menu rather than the title is the optical centre.
    float y = area.y + (usableH - layout.totalHeight) * 0.44f;
    y = std::max(y, area.y + scale.px(8.0f));

    layout.title    = Rect{area.x, y, area.w, titleH};
    y += titleH + titleToTag;
    layout.tagline  = Rect{area.x, y, area.w, tagH};
    y += tagH + tagToBuild;
    layout.build    = Rect{area.x, y, area.w, buildH};
    y += buildH + titleToMenu;

    const float panelW = std::min(m_styles.listWidth + padding * 2.0f,
                                  area.w - scale.px(24.0f));
    layout.panel = Rect{area.center().x - panelW * 0.5f, y, panelW, panelH};
    layout.list  = Rect{layout.panel.x + padding, y + padding, panelW - padding * 2.0f, listH};
    y += panelH + detailGap;

    if (detailH > 0.0f) {
        layout.detail = Rect{layout.panel.x, y, layout.panel.w, detailH};
    }
    return layout;
}

void MainMenuState::drawTitleBlock(const StateContext& ctx, const Layout& layout) const
{
    Renderer2D& renderer = *ctx.renderer;
    Graphics::TextRenderer& text = *ctx.text;
    const float glow = 0.5f + 0.5f * std::sin(static_cast<float>(m_time) * 1.6f);

    // A pulsing bloom drawn in the same glyphs reads as a glow; a blurred copy
    // would need a second texture and look the same.
    {
        TextStyle bloom = m_styles.title;
        bloom.color  = Palette::FutureAccent.withAlpha(
            static_cast<std::uint8_t>(40.0f + 60.0f * glow));
        bloom.shadow = Palette::Transparent;
        text.drawInRect(renderer, layout.title, kTitle, bloom);
    }
    text.drawInRect(renderer, layout.title, kTitle, m_styles.title);

    text.drawInRect(renderer, layout.tagline, kTagline, m_styles.tagline);
    text.drawInRect(renderer, layout.build, ctx.buildLabel, m_buildStyle);
}

void MainMenuState::drawBackdrop(Renderer2D& renderer, const Rect& area, double time) const
{
    // Three bands of colour that stand in for the three eras. The bands drift at
    // different speeds, which reads as a parallax backdrop until real art
    // exists and gives the screen motion even on an empty project.
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(area, Palette::FutureSky);

    const float t       = static_cast<float>(time);
    const float horizon = area.y + area.h * 0.78f;
    const float spacing = area.h * 0.06f;
    const float depth   = area.h * 0.20f;

    for (int band = 0; band < 3; ++band) {
        const float speed  = (1.0f + static_cast<float>(band) * 0.8f) * kBackgroundScroll;
        const float offset = std::fmod(t * speed, area.w);

        Color color = Palette::FutureGround;
        float peak  = 0.34f + 0.14f * static_cast<float>(band);
        if (band == 1) {
            color = Palette::FutureStone;
        } else if (band == 2) {
            color = Palette::FutureAccent;
            peak   = 0.09f;
        }

        const Rect stripe{0.0f, horizon + static_cast<float>(band) * spacing, area.w, depth};

        // The band's alpha ramps from nothing to its peak instead of starting
        // at full strength. A hard top edge cuts a visible line across the
        // screen, which reads as a rendering fault rather than a horizon.
        constexpr int kStrips = 10;
        for (int strip = 0; strip < kStrips; ++strip) {
            const float f = static_cast<float>(strip) / static_cast<float>(kStrips);
            const float fade = f * f * (3.0f - 2.0f * f);   // smoothstep
            const Rect layer{stripe.x, stripe.y + stripe.h * f, stripe.w,
                             stripe.h / static_cast<float>(kStrips) + 1.0f};
            renderer.drawRect(layer, color.withAlpha(static_cast<std::uint8_t>(peak * fade * 255.0f)));
        }

        // Repeat the band across the width so the scroll is seamless.
        for (float x = -offset; x < area.w; x += area.w) {
            for (int strip = 0; strip < kStrips; ++strip) {
                const float f = static_cast<float>(strip) / static_cast<float>(kStrips);
                const float fade = f * f * (3.0f - 2.0f * f);
                const Rect layer{x, stripe.y + stripe.h * f, area.w,
                                 stripe.h / static_cast<float>(kStrips) + 1.0f};
                renderer.drawRect(
                    layer, color.withAlpha(static_cast<std::uint8_t>(peak * fade * 255.0f)));
            }
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
    Graphics::TextRenderer& text = *ctx.text;
    const Rect area = fullArea(renderer);

    // The engine clears and presents; a state only draws.
    renderer.setCameraEnabled(false);
    drawBackdrop(renderer, area, static_cast<double>(m_time) + alpha);

    const Layout layout = computeLayout(ctx);
    drawTitleBlock(ctx, layout);

    // --- menu panel ----------------------------------------------------------
    // The list sits on a panel. Without it the animated horizon runs straight
    // through the entries, which reads as a rendering artefact rather than a
    // deliberate horizon.
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(layout.panel, Palette::PanelFill);
    renderer.drawRect(layout.panel, Palette::PanelBorder.withAlpha(0x90));
    renderer.setBlendMode(BlendMode::None);

    m_menu.render(renderer, text, layout.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles);

    // --- description of the highlighted entry --------------------------------
    if (!layout.detail.isEmpty()) {
        if (const MenuItem* selected = m_menu.currentItem()) {
            text.drawInRect(renderer, layout.detail, selected->detail, m_styles.detail);
        }
    }

    // --- hints ---------------------------------------------------------------
    text.drawInRect(renderer, Rect{layout.hints.x, layout.hints.y, layout.hints.w,
                                   layout.hints.h * 0.5f},
                    kHintRow1, m_styles.hint);
    text.drawInRect(renderer, Rect{layout.hints.x, layout.hints.y + layout.hints.h * 0.5f,
                                   layout.hints.w, layout.hints.h * 0.5f},
                    kHintRow2, m_styles.hint);

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
        BitmapFont::drawCentered(renderer, area.center().x, layout.title.center().y, kTitle,
                                 Palette::TextPrimary, fallback);
    }
}

} // namespace EraShift::Game
