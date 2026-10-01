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

    // The panel is never taller than the space available, so on a short window the
    // list has to shrink with it — clamping the *panel* alone left the rows
    // spilling out of the bottom, which at 640x360 happened on every page with more
    // than six rows and on the twelve-row controls page at any size.
    //
    // Row height is a floor because a row shorter than its own text hides the text,
    // so past this point the correct trade is a cropped list, not unreadable rows.
    // The caller scrolls; `MenuList` reports what does not fit.
    const float fixed = padding + headingH + headingGap + footerGap + footerH + padding;
    const float maxH  = std::max(area.h - scale.px(16.0f), scale.px(80.0f));
    const float listBudget = std::max(rowStride, maxH - fixed);
    const float clampedListH = std::min(listH, listBudget);
    const float clampedH = std::min(fixed + clampedListH, maxH);
    const bool  listClipped = clampedListH < listH - 0.5f;

    layout.panel = Rect{area.center().x - width * 0.5f, area.center().y - clampedH * 0.5f, width,
                        clampedH};

    float y = layout.panel.y + padding;
    if (headingH > 0.0f) {
        layout.heading = Rect{layout.panel.x, y, layout.panel.w, headingH};
        y += headingH + headingGap;
    }
    layout.list = Rect{layout.panel.x + padding, y, layout.panel.w - padding * 2.0f,
                        clampedListH};
    // Rows below the fold. A caller that scrolls uses this to decide whether to; a
    // caller that does not at least knows the list is taller than the frame, which
    // is the difference between "the last row is cut off" and "the last row is
    // somewhere else entirely".
    layout.visibleRows = static_cast<std::size_t>(
        std::floor(clampedListH / std::max(1.0f, rowStride)));
    if (footerH > 0.0f) {
        // Anchored to the bottom of the panel so the gap above it absorbs any
        // shortfall between the requested and the clamped height.
        layout.footer = Rect{layout.panel.x, layout.panel.bottom() - padding - footerH,
                             layout.panel.w, footerH};
    }
    layout.clipped = listClipped;
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

float MenuList::rowStride(const MenuStyles& styles) const noexcept
{
    return rowHeight(styles) + m_spacing;
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

std::size_t MenuList::rowCountFor(const Rect& bounds, const MenuStyles& styles) const noexcept
{
    if (m_items.empty()) {
        return 0;
    }
    const float stride = rowHeight(styles) + m_spacing;
    if (stride <= 0.0f) {
        return 0;
    }
    // Rows that fit fully inside the frame. A row only half-visible at the bottom
    // edge is not drawn, so it must not be clickable either.
    const auto fitted = static_cast<std::size_t>((bounds.h + m_spacing) / stride);
    return std::min(fitted, m_items.size() - m_scroll);
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
    const auto slot = static_cast<std::size_t>((point.y - bounds.y) / stride);

    // `slot` is a position on screen, not an entry. The render pass draws entry
    // `m_scroll + slot` in that slot, so hit testing has to apply the same offset
    // or a scrolled list selects a different row than the one under the cursor.
    if (slot >= rowCountFor(bounds, styles)) {
        return static_cast<std::size_t>(-1);
    }
    const std::size_t index = m_scroll + slot;
    if (index >= m_items.size() || !rowBounds(slot, bounds, styles).contains(point)) {
        return static_cast<std::size_t>(-1);
    }
    return m_items[index].enabled ? index : static_cast<std::size_t>(-1);
}

std::size_t MenuList::visibleRowCount(const Rect& bounds, const MenuStyles& styles) const noexcept
{
    const std::size_t fitted = rowCountFor(bounds, styles);
    // With no window measured, nothing is clipped and every row is on screen.
    return fitted == 0 && !m_items.empty() && m_scroll == 0 ? m_items.size() : fitted;
}

void MenuList::ensureVisible(std::size_t visibleRows, const MenuStyles&) noexcept
{
    if (visibleRows == 0 || m_items.size() <= visibleRows) {
        m_scroll = 0;
        return;
    }
    // At least one row of context beside the selection, so scrolling down does not
    // leave the player with a lone highlighted row and no idea which way is "more".
    const std::size_t margin = visibleRows > 3 ? 1u : 0u;

    if (m_selected < m_scroll + margin) {
        m_scroll = (m_selected > margin) ? m_selected - margin : 0u;
    }
    if (m_selected >= m_scroll + visibleRows - margin) {
        m_scroll = m_selected + margin + 1 - visibleRows;
    }

    // The arithmetic above can overshoot at either end; clamp rather than trust it.
    const std::size_t maxScroll = m_items.size() - visibleRows;
    if (m_scroll > maxScroll) {
        m_scroll = maxScroll;
    }
}

void MenuList::render(Renderer2D& renderer, Graphics::TextRenderer& text, const Rect& bounds,
                      Vec2 mouse, const MenuStyles& styles, std::size_t visibleRows,
                      float elapsed, float revealStagger) const
{
    const std::size_t first = m_scroll;
    // Zero means "no limit", for a caller that has not measured the frame.
    const std::size_t count =
        visibleRows > 0 ? std::min(visibleRows, m_items.size() - std::min(first, m_items.size()))
                        : m_items.size() - std::min(first, m_items.size());

    // One pulse for the selected row, on the caller's clock. Slow and shallow: this
    // is a focus indicator that should never pull the eye away from the value being
    // read, and a fast pulse in a list the player is scanning is worse than none.
    const float pulse = 0.5f + 0.5f * std::sin(elapsed * 3.4f);

    // How long one row takes to arrive, and how far it rises. Named here because a
    // caller passing a stagger needs to know the duration it is staggering across.
    constexpr float kRevealDuration = 0.22f;
    constexpr float kRevealRise = 10.0f;

    for (std::size_t slot = 0; slot < count; ++slot) {
        const std::size_t i = first + slot;
        const MenuItem& item = m_items[i];
        Rect row = rowBounds(slot, bounds, styles);
        if (row.isEmpty()) {
            continue;
        }

        // Staggered arrival. A row not yet due is not drawn at all rather than drawn
        // transparent: half-opacity text over a panel reads as a rendering fault.
        if (revealStagger > 0.0f) {
            const float t = (elapsed - static_cast<float>(slot) * revealStagger) /
                             kRevealDuration;
            if (t <= 0.0f) {
                continue;
            }
            const float eased = t >= 1.0f ? 1.0f : 1.0f - (1.0f - t) * (1.0f - t);
            row.y -= styles.scale.px(kRevealRise) * (1.0f - eased);
        }

        const bool selected = (i == m_selected);
        const bool hovered  = row.contains(mouse) && item.enabled;

        // Unselected rows stay flat so the selection is unmistakable. Hover is a
        // subtle lift, not a second competing highlight.
        Color fill = Palette::Transparent;
        if (selected) {
            // The fill breathes between 0x50 and 0x7A. Narrow on purpose: a pulse
            // wide enough to notice while reading is a distraction, and this row is
            // often a value the player is in the middle of comparing.
            const auto base = static_cast<std::uint8_t>(0x50 + 0x2A * pulse);
            fill = Palette::PanelBorder.withAlpha(base);
        } else if (hovered) {
            fill = Palette::PanelBorder.withAlpha(0x22);
        }
        if (fill.a > 0) {
            renderer.drawRect(row, fill);
        }

        // An accent bar on the selected row's leading edge. The chevrons already
        // say "selected" in the middle of the row; this says it from the edge, which
        // is what makes a long list scannable without reading every label.
        if (selected) {
            renderer.drawRect(
                Rect{row.x, row.y + styles.scale.px(4.0f), styles.scale.px(3.0f),
                     row.h - styles.scale.px(8.0f)},
                Palette::Accent);
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

        // How much of the row the label owns. With a value present the label is
        // centred in what is left, not in the row, so everything measured from
        // the label has to use the same box or the selection ends up bracketing
        // empty space.
        const float pad    = styles.scale.px(20.0f);
        const float innerX = row.x + pad;
        const float innerW = row.w - pad * 2.0f;
        float valueWidth   = 0.0f;
        if (!item.value.empty()) {
            TextStyle valueStyle = style;
            valueWidth = text.measure(item.value, valueStyle).x + styles.scale.px(16.0f);
        }
        const float labelBoxW = innerW - valueWidth;
        const float labelCentre = innerX + labelBoxW * 0.5f;

        // The selection is a full-width fill, accent-coloured bold text, and a
        // pair of chevrons bracketing the label. The chevrons keep the selection
        // readable without relying on colour alone, and are placed from the
        // measured label so they never sit on top of the text.
        if (selected) {
            const float labelWidth = text.measure(item.label, style).x;
            const float gap       = styles.scale.px(14.0f);
            const float size      = styles.scale.px(4.0f);
            const float offset    = labelWidth * 0.5f + gap + size * 0.5f;
            // The chevrons narrow with the pulse, so the selection "breathes"
            // rather than blinking. A hard on/off would flicker against the row
            // fill's own pulse and read as a flicker rather than as focus.
            const float reach = size * (0.75f + 0.25f * pulse);
            for (const float dir : {-1.0f, 1.0f}) {
                drawChevron(renderer, labelCentre + dir * offset, row.center().y, reach, dir,
                            Palette::Accent);
            }
        }

        if (item.value.empty()) {
            text.drawInRect(renderer, Rect{innerX, row.y, innerW, row.h}, item.label, style);
        } else {
            // The label is centred in what is left once the value's width is
            // reserved, so a row reads as two balanced columns rather than the
            // value shoved to the edge and the label stranded on the other side.
            TextStyle valueStyle = style;
            valueStyle.color     = selected ? Palette::TextPrimary : Palette::TextDim;
            valueStyle.bold      = false;
            valueStyle.shadow    = style.shadow;

            text.drawInRect(renderer, Rect{innerX, row.y, labelBoxW, row.h}, item.label, style);
            text.drawInRect(renderer, Rect{innerX + labelBoxW, row.y, valueWidth, row.h},
                            item.value, valueStyle);
        }

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
    m_continueName.clear();
    if (ctx.config != nullptr) {
        const Core::SaveManager manager(ctx.config->saveDirectory(), *ctx.log);
        m_hasSave = manager.hasSave();
        if (m_hasSave) {
            Core::SaveGame save;
            if (manager.load(save) && !save.levelName.empty()) {
                m_continueName = save.levelName;
            }
        }
    } else {
        m_hasSave = false;
    }

    // LEVEL SELECT is offered only once something has been completed. Before that
    // there is exactly one unlocked region, so the screen would be a list of nine
    // locks — which is a worse first impression than not offering it.
    m_hasProgress = false;
    if (ctx.progress != nullptr) {
        const Core::Progress progress = ctx.progress->loadProgress();
        m_hasProgress = progress.highestLevel > 1 || progress.tutorialComplete;
        if (m_hasProgress) {
            const auto levels = listLevels(ctx.levelDirectory.empty()
                                               ? std::filesystem::path{"data/levels"}
                                               : ctx.levelDirectory);
            const int unlocked = std::clamp(progress.highestLevel, 1,
                                            static_cast<int>(std::max<std::size_t>(
                                                levels.size(), 1)));
            m_progressDetail = std::to_string(unlocked) + " of " +
                               std::to_string(levels.size()) + " regions open";
        }
    }
    if (!m_hasProgress) {
        m_progressDetail = "finish level 1 to unlock more";
    }

    // CONTINUE says where it will resume *to*, not just that it can. "resume where
    // you left off" on a game with ten regions is a question the player has to
    // answer by pressing the key and looking.
    std::string continueDetail = "no saved run";
    if (m_hasSave) {
        continueDetail = "resume " + (m_continueName.empty() ? std::string("your run")
                                                             : m_continueName);
    }

    m_menu.setItems({
        {"NEW GAME",     "choose a region",                 true},
        {"CONTINUE",     continueDetail,                     m_hasSave},
        {"LEVEL SELECT", m_progressDetail,                   m_hasProgress},
        {"SETTINGS",     "graphics, input and the tutorial", true},
        {"CREDITS",      "about Era Shift",                   true},
        {"QUIT",         "exit to desktop",                   true},
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

    // Each press moves the highlight at most once. A menu that skips disabled
    // rows can otherwise be moved twice by one keypress.
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
    if (item == nullptr) {
        return;
    }
    if (!item->enabled) {
        return;
    }

    if (item->label == "NEW GAME") {
        // The region is chosen next, not here: a menu that starts the run
        // immediately would commit to the default region without ever showing
        // the player that there are others.
        ctx.states->push(std::make_shared<LevelSelectState>());
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

    if (item->label == "LEVEL SELECT") {
        // A player with no progression reaches this only if a save file says they
        // have some, so the guard is a real check rather than belt-and-braces.
        if (ctx.progress == nullptr || !m_hasProgress) {
            return;
        }
        ctx.states->push(std::make_shared<LevelSelectState>());
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

    // Never taller than the space above the hints, or the block would slide under
    // them on a short window. The list gets what is left after the title block and
    // the detail caption, so a long menu is cropped and scrolled rather than
    // pushed off the bottom — which is what happened to the level select's twelve
    // rows at 640x360 before the row budget existed.
    const float usableH   = std::max(layout.hints.y - area.y, scale.px(80.0f));
    const float available = std::max(usableH - scale.px(16.0f), scale.px(48.0f));
    const float listBudget = std::max(rowStride,
                                      available - headH - titleToMenu - detailGap - detailH -
                                          padding * 2.0f);
    const float clampedListH = std::min(listH, listBudget);
    layout.visibleRows =
        static_cast<std::size_t>(clampedListH / std::max(1.0f, rowStride));
    if (layout.visibleRows == 0 && rows > 0) {
        layout.visibleRows = 1;
    }
    const float panelH  = clampedListH + padding * 2.0f;

    layout.totalHeight =
        std::min(headH + titleToMenu + panelH + detailGap + detailH, available);

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
    layout.list  = Rect{layout.panel.x + padding, y + padding, panelW - padding * 2.0f,
                        clampedListH};
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

    m_menu.ensureVisible(layout.visibleRows, m_styles);
    m_menu.render(renderer, text, layout.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles,
                  layout.visibleRows, static_cast<float>(m_time));

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

    if (ctx.stats != nullptr) {
        // Nothing is simulated here, so the counter says so rather than leaving
        // the last gameplay frame's number on screen.
        ctx.stats->setEntityCount(0);
    }

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
