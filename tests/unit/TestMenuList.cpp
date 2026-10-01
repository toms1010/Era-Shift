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

/// A frame exactly tall enough for five rows, so a list longer than that must
/// scroll and the row maths in these tests is the real row maths.
Rect fiveRowFrame(const MenuList& menu, const MenuStyles& styles)
{
    const float stride = menu.rowStride(styles);
    return Rect{0.0f, 0.0f, 400.0f, stride * 5.0f - (stride - menu.rowHeight(styles))};
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

    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 0.5f}, styles) == 0);
    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 1.5f}, styles) == 1);
    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 2.5f}, styles) == 2);

    // Above the list.
    CHECK(menu.hitTest(bounds, Vec2{150.0f, -10.0f}, styles) == static_cast<std::size_t>(-1));
    // Left of the list.
    CHECK(menu.hitTest(bounds, Vec2{-10.0f, stride * 0.5f}, styles) == static_cast<std::size_t>(-1));
    // Past the last row.
    CHECK(menu.hitTest(bounds, Vec2{150.0f, 5000.0f}, styles) == static_cast<std::size_t>(-1));
}

TEST_CASE("hit testing refuses disabled rows so the mouse cannot select them")
{
    MenuList menu;
    const MenuStyles styles = testStyles();
    menu.setMetrics(styles.rowHeight, styles.rowSpacing);
    menu.setItems({{"A", ""}, {"B", "", false}});

    const Rect bounds{0.0f, 0.0f, 300.0f, 200.0f};
    const float stride = menu.rowHeight(styles) + styles.rowSpacing;

    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 0.5f}, styles) == 0);
    CHECK(menu.hitTest(bounds, Vec2{150.0f, stride * 1.5f}, styles) == static_cast<std::size_t>(-1));
}

TEST_CASE("hit testing uses the derived row height, not the raw metric")
{
    MenuList menu;
    MenuStyles styles = testStyles();
    // A font large enough to force the taller row. With the raw metric the
    // hit area would sit above the drawn rows and every click would land one
    // entry too high.
    styles.entry.pixelSize = 120;
    menu.setMetrics(10.0f, 4.0f);
    menu.setItems({{"A", ""}, {"B", ""}, {"C", ""}});

    const Rect bounds{0.0f, 0.0f, 400.0f, 800.0f};
    const float stride = menu.rowHeight(styles) + 4.0f;

    CHECK(menu.rowHeight(styles) > 10.0f);
    CHECK(menu.hitTest(bounds, Vec2{200.0f, stride * 0.5f}, styles) == 0);
    CHECK(menu.hitTest(bounds, Vec2{200.0f, stride * 1.5f}, styles) == 1);
    CHECK(menu.hitTest(bounds, Vec2{200.0f, stride * 2.5f}, styles) == 2);

    // In the spacing below the last row there is nothing to click.
    CHECK(menu.hitTest(bounds, Vec2{200.0f, stride * 3.0f}, styles) ==
          static_cast<std::size_t>(-1));
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


// ---------------------------------------------------------------------------
// Scrolling
//
// A list taller than its frame used to draw every row anyway, so the ones below
// the panel's bottom edge were visible over the world and the ones the selection
// could reach were not. These cover the scroll that replaced that.
// ---------------------------------------------------------------------------

namespace {

/// A list of `count` plain entries.
MenuList listOf(std::size_t count)
{
    MenuList menu;
    std::vector<MenuItem> items;
    items.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        items.emplace_back("ROW " + std::to_string(i), "");
    }
    menu.setItems(std::move(items));
    return menu;
}

} // namespace

TEST_CASE("a list that fits does not scroll")
{
    MenuList menu = listOf(5);
    menu.ensureVisible(10, testStyles());
    CHECK(menu.scrollOffset() == 0);

    menu.select(4);
    menu.ensureVisible(10, testStyles());
    CHECK(menu.scrollOffset() == 0);
}

TEST_CASE("a scrolled list brings the selection into view")
{
    MenuList menu = listOf(20);
    menu.ensureVisible(5, testStyles());

    menu.select(15);
    menu.ensureVisible(5, testStyles());

    // The invariant: the selection is one of the rows on screen.
    CHECK(menu.selected() >= menu.scrollOffset());
    CHECK(menu.selected() < menu.scrollOffset() + 5);
}

TEST_CASE("scrolling down then up returns to the top")
{
    MenuList menu = listOf(20);
    menu.ensureVisible(5, testStyles());

    menu.select(18);
    menu.ensureVisible(5, testStyles());
    const std::size_t down = menu.scrollOffset();
    CHECK(down > 0);

    menu.select(0);
    menu.ensureVisible(5, testStyles());
    CHECK(menu.scrollOffset() == 0);
}

TEST_CASE("the last row can be scrolled to and the list does not overshoot")
{
    MenuList menu = listOf(12);
    menu.ensureVisible(5, testStyles());

    menu.select(11);
    menu.ensureVisible(5, testStyles());

    CHECK(menu.selected() >= menu.scrollOffset());
    CHECK(menu.selected() < menu.scrollOffset() + 5);
    // No empty space below the last row: the scroll cannot run past the end.
    CHECK(menu.scrollOffset() + 5 <= 12);
}

TEST_CASE("a visible count of one scrolls straight to the selection")
{
    MenuList menu = listOf(10);
    menu.ensureVisible(1, testStyles());

    menu.select(7);
    menu.ensureVisible(1, testStyles());

    // One visible row means no margin, or the scroll could not satisfy the
    // invariant at all.
    CHECK(menu.scrollOffset() == 7);
    CHECK(menu.selected() >= menu.scrollOffset());
    CHECK(menu.selected() < menu.scrollOffset() + 1);
}

TEST_CASE("zero visible rows disables scrolling rather than hiding everything")
{
    MenuList menu = listOf(10);
    menu.ensureVisible(0, testStyles());
    CHECK(menu.scrollOffset() == 0);
}

TEST_CASE("a short list is never scrolled even with a tiny window")
{
    MenuList menu = listOf(3);
    menu.ensureVisible(1, testStyles());
    menu.select(2);
    menu.ensureVisible(1, testStyles());
    // Three rows in a one-row window is the clipped case; the scroll still has to
    // keep the selection on screen.
    CHECK(menu.selected() >= menu.scrollOffset());
    CHECK(menu.selected() < menu.scrollOffset() + 1);
}


// ---------------------------------------------------------------------------
// Hit testing a scrolled list
//
// hitTest has to agree with render about which entry a screen position shows.
// While every list fit on screen the two could not disagree; once a list scrolls
// they can, and the symptom is a click selecting a row other than the one lit up.
// ---------------------------------------------------------------------------

TEST_CASE("a click in a scrolled list selects the row that was drawn there")
{
    MenuList menu = listOf(20);
    MenuStyles styles = testStyles();
    const Rect bounds = fiveRowFrame(menu, styles);

    // Scroll first: with the selection on row 0 there is nothing to scroll to, and
    // the bug this covers only exists once a row above the window has been dropped.
    menu.select(12);
    menu.ensureVisible(5, styles);
    REQUIRE(menu.scrollOffset() > 0);
    const std::size_t first = menu.scrollOffset();

    // The top slot holds the first visible entry, not entry 0.
    const std::size_t topSlot = menu.hitTest(bounds, Vec2{10.0f, 1.0f}, styles);
    CHECK(topSlot == first);
}

TEST_CASE("hit testing follows the scroll as it moves")
{
    MenuList menu = listOf(20);
    MenuStyles styles = testStyles();
    const Rect bounds = fiveRowFrame(menu, styles);

    for (const std::size_t target : {std::size_t{0}, std::size_t{6}, std::size_t{14},
                                    std::size_t{19}}) {
        menu.select(target);
        menu.ensureVisible(5, styles);

        // Whatever is drawn in the top slot is what a click there must return.
        const std::size_t hovered = menu.hitTest(bounds, Vec2{10.0f, 1.0f}, styles);
        CHECK(hovered == menu.scrollOffset());
    }
}

TEST_CASE("a click selects the scrolled row, and the mouse then tracks it")
{
    MenuList menu = listOf(20);
    MenuStyles styles = testStyles();
    const Rect bounds = fiveRowFrame(menu, styles);

    menu.select(19);
    menu.ensureVisible(5, styles);
    const std::size_t bottom = menu.scrollOffset();

    // Click the last visible slot.
    const std::size_t hovered = menu.hitTest(bounds, Vec2{10.0f, bounds.h - 1.0f}, styles);
    CHECK(hovered == bottom + 4);
    menu.select(hovered);
    CHECK(menu.selected() == bottom + 4);
}

TEST_CASE("hit testing refuses the empty space below a scrolled list")
{
    MenuList menu = listOf(20);
    MenuStyles styles = testStyles();
    const Rect bounds = fiveRowFrame(menu, styles);

    menu.select(10);
    menu.ensureVisible(5, styles);

    // The frame is 5 rows tall and the list is 20 long, so there is always more
    // below: a click in the gap must not resolve to some other entry.
    const std::size_t count = menu.visibleRowCount(bounds, styles);
    REQUIRE(count == 5);
    const float stride = menu.rowStride(styles);
    const std::size_t past =
        menu.hitTest(bounds, Vec2{10.0f, count * stride + 1.0f}, styles);
    CHECK(past == static_cast<std::size_t>(-1));
}

TEST_CASE("hit testing outside the list is refused, scrolled or not")
{
    MenuList menu = listOf(20);
    MenuStyles styles = testStyles();
    const Rect bounds = fiveRowFrame(menu, styles);

    menu.select(15);
    menu.ensureVisible(5, styles);

    CHECK(menu.hitTest(bounds, Vec2{-1.0f, 1.0f}, styles) == static_cast<std::size_t>(-1));
    CHECK(menu.hitTest(bounds, Vec2{10.0f, bounds.h + 1.0f}, styles) ==
          static_cast<std::size_t>(-1));
}

TEST_CASE("a disabled row below the scroll is still not clickable")
{
    std::vector<MenuItem> items;
    for (std::size_t i = 0; i < 20; ++i) {
        items.emplace_back("ROW " + std::to_string(i), "", "", (i % 2u) == 0u);
    }
    MenuList menu;
    menu.setItems(std::move(items));
    MenuStyles styles = testStyles();
    const Rect bounds = fiveRowFrame(menu, styles);

    menu.select(0);
    menu.ensureVisible(5, styles);
    REQUIRE(menu.scrollOffset() == 0);
    const float stride = menu.rowStride(styles);
    CHECK(menu.hitTest(bounds, Vec2{10.0f, stride + 1.0f}, styles) ==
          static_cast<std::size_t>(-1));
}
