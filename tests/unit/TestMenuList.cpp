// Era Shift - tests for the shared menu list.
//
// MenuList drives selection, skipping and hit testing for every screen in the
// game, so the navigation rules are tested directly rather than through a
// screenshot.

#include "EraShift/Game/states/MainMenuState.hpp"

#include <doctest/doctest.h>

using namespace EraShift::Game;
using namespace EraShift::Graphics;

namespace {

MenuStyles testStyles()
{
    return MenuStyles::make(UiScale{1.0f});
}

} // namespace

TEST_CASE("selection starts on the first item")
{
    MenuList menu;
    menu.setMetrics(40.0f, 4.0f);
    menu.setItems({{"A", ""}, {"B", ""}, {"C", ""}});

    CHECK(menu.selected() == 0);
    CHECK(menu.currentItem() != nullptr);
    CHECK(menu.currentItem()->label == "A");
}

TEST_CASE("move wraps around both ends")
{
    MenuList menu;
    menu.setItems({{"A", ""}, {"B", ""}, {"C", ""}});

    menu.move(1);
    CHECK(menu.selected() == 1);
    menu.move(1);
    CHECK(menu.selected() == 2);
    menu.move(1);
    CHECK(menu.selected() == 0);   // wraps forward

    menu.move(-1);
    CHECK(menu.selected() == 2);   // wraps backward
}

TEST_CASE("move skips disabled entries")
{
    MenuList menu;
    menu.setItems({{"A", ""}, {"B", "", false}, {"C", ""}});

    menu.move(1);
    CHECK(menu.selected() == 2);   // skipped "B"
    menu.move(1);
    CHECK(menu.selected() == 0);   // wrapped past "B" again
    menu.move(-1);
    CHECK(menu.selected() == 2);
}

TEST_CASE("an all-disabled list does not loop forever")
{
    MenuList menu;
    menu.setItems({{"A", "", false}, {"B", "", false}, {"C", "", false}});

    // Must terminate; the selection simply lands wherever it lands.
    menu.move(1);
    CHECK(menu.isSelectedEnabled() == false);
    menu.move(-1);
    CHECK(menu.isSelectedEnabled() == false);
}

TEST_CASE("setItems lands on an enabled entry")
{
    MenuList menu;
    // The first item is disabled, so the list must not sit on it.
    menu.setItems({{"A", "", false}, {"B", ""}, {"C", ""}});
    CHECK(menu.isSelectedEnabled());
    CHECK(menu.selected() == 1);
}

TEST_CASE("an empty list is safe to query")
{
    MenuList menu;
    menu.setItems({});

    CHECK(menu.empty());
    CHECK(menu.currentItem() == nullptr);
    CHECK(menu.selected() == 0);
    menu.move(1);
    menu.move(-1);
    menu.select(0);
    CHECK(menu.selected() == 0);
}

TEST_CASE("row geometry is a pure function of the style and the index")
{
    MenuList menu;
    const MenuStyles styles = testStyles();
    menu.setMetrics(styles.rowHeight, styles.rowSpacing);
    menu.setItems({{"A", ""}, {"B", ""}, {"C", ""}});

    const Rect bounds{100.0f, 200.0f, 300.0f, 200.0f};
    const Rect first  = menu.rowBounds(0, bounds, styles);
    const Rect second = menu.rowBounds(1, bounds, styles);
    const Rect third  = menu.rowBounds(2, bounds, styles);

    CHECK(first.x == doctest::Approx(bounds.x));
    CHECK(first.y == doctest::Approx(bounds.y));
    CHECK(second.y > first.y);
    CHECK(third.y > second.y);
    CHECK(first.w == doctest::Approx(bounds.w));
    CHECK(first.h >= styles.rowHeight);

    // Out of range is empty, which render() skips.
    CHECK(menu.rowBounds(99, bounds, styles).isEmpty());
}

TEST_CASE("rows are never shorter than their own text")
{
    MenuList menu;
    MenuStyles styles = testStyles();
    // A deliberately huge font must not be clipped by the configured row height.
    styles.entry.pixelSize = 200;
    menu.setMetrics(10.0f, 0.0f);
    menu.setItems({{"A", ""}});

    CHECK(menu.rowHeight(styles) >= 200.0f * 1.6f);
}

TEST_CASE("hit testing maps a point to its row")
{
    MenuList menu;
    const MenuStyles styles = testStyles();
    menu.setMetrics(styles.rowHeight, styles.rowSpacing);
    menu.setItems({{"A", ""}, {"B", ""}, {"C", ""}});

    const Rect bounds{0.0f, 0.0f, 300.0f, 200.0f};
    const float stride = menu.rowHeight(styles) + styles.rowSpacing;

    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 0.5f}) == 0);
    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 1.5f}) == 1);
    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 2.5f}) == 2);

    // Above the list.
    CHECK(menu.hitTest(bounds, Vec2{150.0f, -10.0f}) == static_cast<std::size_t>(-1));
    // Left of the list.
    CHECK(menu.hitTest(bounds, Vec2{-10.0f, stride * 0.5f}) == static_cast<std::size_t>(-1));
    // Past the last row.
    CHECK(menu.hitTest(bounds, Vec2{150.0f, 5000.0f}) == static_cast<std::size_t>(-1));
}

TEST_CASE("hit testing refuses disabled rows so the mouse cannot select them")
{
    MenuList menu;
    const MenuStyles styles = testStyles();
    menu.setMetrics(styles.rowHeight, styles.rowSpacing);
    menu.setItems({{"A", ""}, {"B", "", false}});

    const Rect bounds{0.0f, 0.0f, 300.0f, 200.0f};
    const float stride = menu.rowHeight(styles) + styles.rowSpacing;

    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 0.5f}) == 0);
    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 1.5f}) == static_cast<std::size_t>(-1));
}

TEST_CASE("select() ignores disabled and out-of-range indices")
{
    MenuList menu;
    menu.setItems({{"A", ""}, {"B", "", false}, {"C", ""}});

    menu.select(1);
    CHECK(menu.selected() == 0);   // disabled, unchanged

    menu.select(2);
    CHECK(menu.selected() == 2);

    menu.select(99);
    CHECK(menu.selected() == 2);
}
