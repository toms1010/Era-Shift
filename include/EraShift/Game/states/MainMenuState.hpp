// Era Shift - main menu (title screen).

#pragma once

#include "EraShift/Application/Engine.hpp"
#include "EraShift/Core/GameState.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace EraShift::Game {

using Graphics::Rect;
using Graphics::Vec2;

/// The UI scale for the current viewport, honouring the player's
/// `graphics.uiScale` preference.
///
/// Defined once, here, because two screens having slightly different ideas of
/// what "one UI pixel" means is exactly how a menu ends up with one screen
/// using 24px type and the next using 34px in the same window.
[[nodiscard]] Graphics::UiScale uiScaleFor(const StateContext& ctx);

/// Typography for a menu, shared by every screen that uses MenuList so the
/// title, the entries and the hints cannot drift apart.
struct MenuStyles {
    Graphics::TextStyle title;
    Graphics::TextStyle tagline;
    Graphics::TextStyle heading;
    Graphics::TextStyle entry;
    Graphics::TextStyle entrySelected;
    Graphics::TextStyle entryDisabled;
    Graphics::TextStyle detail;
    Graphics::TextStyle hint;

    /// Base panel geometry, in reference-resolution pixels, before `scale`.
    float rowHeight  = 46.0f;
    float rowSpacing = 4.0f;
    float listWidth  = 360.0f;

    Graphics::UiScale scale;

    /// The default Era Shift menu look at the given UI scale.
    [[nodiscard]] static MenuStyles make(Graphics::UiScale scale);
};

/// A panel whose size is derived from its contents rather than hard-coded.
///
/// Every screen that shows a list of entries lays it out through this, so the
/// main menu, the pause menu and the settings screen cannot drift apart as the
/// row count, the type size or the window size changes.
struct PanelLayout {
    Rect panel;    ///< The panel background and border.
    Rect heading;  ///< Where the title line sits. Empty when there is none.
    Rect list;     ///< Where the rows sit, inset by the panel padding.
    Rect footer;   ///< Where the hint line sits. Empty when there is none.

    /// How many rows fit in `list` without being cut by its bottom edge.
    ///
    /// Always at least one, even on a viewport too short for a whole row: a panel
    /// showing nothing is worse than one showing a cropped row.
    std::size_t visibleRows = 0;

    /// True when the list is taller than the frame it was given.
    ///
    /// Every screen with more rows than fit — the twelve-row controls page at any
    /// window size, and any list at 320x180 — must scroll, and this is what tells
    /// it to. A caller that ignores it shows a list whose tail is unreachable,
    /// because the rows exist and the selection can still move onto them.
    bool clipped = false;
};

/// Lays out a panel for `rowCount` entries, centred in `area`.
///
/// `area` is the space the panel may occupy (normally the whole viewport).
/// The returned panel is never larger than `area`, so a long list on a short
/// window degrades by clipping rather than by running off the screen.
[[nodiscard]] PanelLayout layoutPanel(const Rect& area, const MenuStyles& styles,
                                      std::size_t rowCount, std::string_view heading,
                                      std::string_view footer,
                                      Graphics::TextRenderer& text);

/// A selectable entry in a menu list.
///
/// Constructors rather than aggregate initialisation, because the three- and
/// four-field forms mean different things and an aggregate would happily accept
/// `{"ROW", "caption", true}` as a three-element list of the wrong fields.
struct MenuItem {
    MenuItem() = default;

    MenuItem(std::string itemLabel, std::string itemDetail, bool itemEnabled = true)
        : label(std::move(itemLabel)),
          detail(std::move(itemDetail)),
          enabled(itemEnabled)
    {
    }

    MenuItem(std::string itemLabel, std::string itemDetail, std::string itemValue,
             bool itemEnabled = true)
        : label(std::move(itemLabel)),
          detail(std::move(itemDetail)),
          value(std::move(itemValue)),
          enabled(itemEnabled)
    {
    }

    std::string label;
    /// Caption shown once, centred, under the list, for the highlighted entry.
    std::string detail;
    /// Right-aligned value shown on the row itself: "ON", "100%", "Left Shift".
    ///
    /// Kept separate from `detail` because the two answer different questions -
    /// "what does this do" versus "what is it set to" - and a settings screen
    /// where the current value is not on the row is a settings screen the player
    /// has to change things to find out about.
    std::string value;
    bool        enabled = true;
};

/// Reusable keyboard-navigable list. Extracted so the settings, load and
/// inventory screens all behave identically instead of each re-implementing
/// selection, wrapping and keyboard navigation.
class MenuList {
public:
    void setItems(std::vector<MenuItem> items);
    [[nodiscard]] const std::vector<MenuItem>& items() const noexcept { return m_items; }
    [[nodiscard]] bool empty() const noexcept { return m_items.empty(); }

    /// Moves the selection by `delta`, skipping disabled entries and wrapping
    /// around the ends. A no-op when every entry is disabled.
    void move(int delta);

    /// Selects an index directly. Ignored when out of range or disabled.
    void select(std::size_t index);
    [[nodiscard]] std::size_t selected() const noexcept { return m_selected; }
    [[nodiscard]] const MenuItem* currentItem() const noexcept;
    [[nodiscard]] bool isSelectedEnabled() const noexcept;

    /// Index of the hovered item, or npos when the pointer is not over the list.
    ///
    /// Takes the styles because row height is derived from the entry type size;
    /// using the raw metric instead would put the hit area out of step with what
    /// is drawn whenever a font is large enough to force the taller row.
    [[nodiscard]] std::size_t hitTest(const Rect& bounds, Vec2 point,
                                      const MenuStyles& styles) const noexcept;

    /// Draws the list. The row geometry is derived from the entry style so hit
    /// testing and drawing can never disagree.
    ///
    /// `visibleRows` is how many fit in `bounds`; rows past it are not drawn, and
    /// `ensureVisible` should have been called first so the selection is one of the
    /// ones on screen. Zero means "no limit", for a caller that has not measured.
    ///
    /// `elapsed` drives the selected row's pulse. Passed in rather than read from a
    /// clock so the animation is a function of the simulation clock the caller
    /// already owns, and is therefore deterministic and frame-rate independent.
    ///
    /// `revealStagger` > 0 makes the rows arrive one after another, each fading and
    /// rising into place. Used by the screens that are entered *because* something
    /// happened — the results screen after a death, a victory — so the options read
    /// as a list the player is being offered rather than as a slab that was already
    /// there. Left at zero for menus that were always there and need no ceremony.
    void render(Graphics::Renderer2D& renderer, Graphics::TextRenderer& text,
                const Rect& bounds, Vec2 mouse, const MenuStyles& styles,
                std::size_t visibleRows = 0, float elapsed = 0.0f,
                float revealStagger = 0.0f) const;

    /// Height one row occupies, in pixels, excluding the gap below it.
    [[nodiscard]] float rowHeight(const MenuStyles& styles) const noexcept;
    /// Vertical distance from one row's top to the next's: the row plus its gap.
    /// Layout code needs this to size a frame that holds a given number of rows.
    [[nodiscard]] float rowStride(const MenuStyles& styles) const noexcept;
    /// Bounds of a single row, or an empty rect when the index is out of range.
    [[nodiscard]] Rect rowBounds(std::size_t index, const Rect& bounds,
                                 const MenuStyles& styles) const noexcept;
    /// How many entries fit in `bounds` at the current scroll, and are therefore
    /// both drawn and clickable.
    [[nodiscard]] std::size_t rowCountFor(const Rect& bounds,
                                           const MenuStyles& styles) const noexcept;

    void setMetrics(float rowHeight, float spacing) noexcept
    {
        m_rowHeight = rowHeight;
        m_spacing   = spacing;
    }

    /// Scrolls so the selected row is inside `visibleRows` rows of `bounds`.
    ///
    /// The minimum number of rows kept visible when scrolling; a longer list moves
    /// by this much so the row above or below the selection stays on screen as
    /// context. A no-op when everything already fits.
    ///
    /// Must be called whenever the selection or the visible height changes, and
    /// *before* rendering — a scrolled list whose selection is off-screen draws a
    /// list with nothing highlighted on it, which reads as a broken menu rather than
    /// as a scrolled one.
    void ensureVisible(std::size_t visibleRows, const MenuStyles& styles) noexcept;

    /// How many rows are drawn and clickable inside `bounds` right now. Pass zero
    /// `visibleRows` to `ensureVisible` and the whole list counts as visible.
    [[nodiscard]] std::size_t visibleRowCount(const Rect& bounds,
                                               const MenuStyles& styles) const noexcept;

    /// First row drawn, as an index. Zero when the list is not scrolled.
    [[nodiscard]] std::size_t scrollOffset() const noexcept { return m_scroll; }
    void setScrollOffset(std::size_t offset) noexcept { m_scroll = offset; }

private:
    std::vector<MenuItem> m_items;
    std::size_t m_selected  = 0;
    std::size_t m_scroll     = 0;
    float m_rowHeight = 46.0f;
    float m_spacing   = 8.0f;
};

class MainMenuState final : public Core::IGameState {
public:
    explicit MainMenuState(Core::GameState id = Core::GameState::MainMenu);

    void onEnter(StateContext& ctx) override;
    void onExit(StateContext& ctx) override;
    void update(StateContext& ctx, double fixedDelta) override;
    void render(StateContext& ctx, double alpha) override;

    /// Where the list sits, given the current viewport and scale.
    /// Used by update() for hit testing and by render() for drawing, so the
    /// two can never disagree about where an entry is.
    [[nodiscard]] Rect listBounds(const StateContext& ctx) const;

private:
    void drawBackdrop(Graphics::Renderer2D& renderer, const Rect& area, double time) const;
    void activateSelection(StateContext& ctx);
    /// Rebuilds the entry list, enabling entries whose backing feature exists.
    void buildMenu(StateContext& ctx);
    /// Rebuilds the styles when the viewport or the UI-scale preference changes.
    void syncScale(StateContext& ctx);
    /// Full vertical layout for this frame: title block, menu and captions
    /// stacked from measured text heights and centred as one group.
    struct Layout {
        Rect title;
        Rect tagline;
        Rect build;
        Rect panel;
        Rect list;
        Rect detail;
        Rect hints;
        float totalHeight = 0.0f;
        /// Rows that fit in `list`. See `PanelLayout::visibleRows`; the main menu
        /// computes its own panel rather than using `layoutPanel`, so it has to
        /// carry the same figure.
        std::size_t visibleRows = 0;
    };
    [[nodiscard]] Layout computeLayout(const StateContext& ctx) const;
    void drawTitleBlock(const StateContext& ctx, const Layout& layout) const;

    MenuList   m_menu;
    MenuStyles m_styles;
    float      m_time = 0.0f;
    Graphics::UiScale m_viewportScale{1.0f};

    /// Build label, tinted warm. Kept apart from the tagline so the two cannot
    /// be accidentally styled as one.
    Graphics::TextStyle m_buildStyle;

    /// True when a save file exists, which is what enables CONTINUE.
    bool m_hasSave = false;
    /// Whether any progression exists, which is what gates LEVEL SELECT.
    bool m_hasProgress = false;
    /// The saved run's region name, for the CONTINUE caption.
    std::string m_continueName;
    /// "3 of 10 regions open", for the LEVEL SELECT caption.
    std::string m_progressDetail;
};

} // namespace EraShift::Game
