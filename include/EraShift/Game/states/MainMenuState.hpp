// Era Shift - main menu (title screen).

#pragma once

#include "EraShift/Application/Engine.hpp"
#include "EraShift/Core/GameState.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"

#include <string>
#include <vector>

namespace EraShift::Game {

using Graphics::Rect;
using Graphics::Vec2;

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

/// A selectable entry in a menu list.
struct MenuItem {
    std::string label;
    std::string detail;
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
    [[nodiscard]] std::size_t hitTest(const Rect& bounds, Vec2 point) const noexcept;

    /// Draws the list. The row geometry is derived from the entry style so hit
    /// testing and drawing can never disagree.
    void render(Graphics::Renderer2D& renderer, Graphics::TextRenderer& text,
                const Rect& bounds, Vec2 mouse, const MenuStyles& styles) const;

    /// Height one row occupies, in pixels.
    [[nodiscard]] float rowHeight(const MenuStyles& styles) const;
    /// Bounds of a single row, or an empty rect when the index is out of range.
    [[nodiscard]] Rect rowBounds(std::size_t index, const Rect& bounds,
                                 const MenuStyles& styles) const;

    void setMetrics(float rowHeight, float spacing) noexcept
    {
        m_rowHeight = rowHeight;
        m_spacing   = spacing;
    }

private:
    std::vector<MenuItem> m_items;
    std::size_t m_selected  = 0;
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

private:
    void drawBackdrop(Graphics::Renderer2D& renderer, const Rect& area, double time) const;
    void activateSelection(StateContext& ctx);
    /// Where the menu panel sits, given the current viewport and scale.
    [[nodiscard]] Rect menuPanel(const Rect& area) const;
    /// Where the title block sits.
    [[nodiscard]] Rect titleBlock(const Rect& area) const;

    MenuList    m_menu;
    MenuStyles  m_styles;
    float       m_time      = 0.0f;
    Graphics::UiScale m_viewportScale{1.0f};

    /// UI scale derived from the current viewport.
    [[nodiscard]] Graphics::UiScale currentUiScale(const StateContext& ctx) const;
};

} // namespace EraShift::Game
