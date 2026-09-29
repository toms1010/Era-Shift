// Era Shift - tests for resolution-independent UI scaling.
//
// The interface is designed against 1280x720 and scaled to fit. If the scale is
// wrong the UI is not merely ugly: text can overflow its panel and the whole
// layout shifts, so the rules are pinned down here.

#include "EraShift/Graphics/TextRenderer.hpp"

#include <doctest/doctest.h>

using namespace EraShift::Graphics;

TEST_CASE("the reference resolution scales to exactly one")
{
    CHECK(UiScale::forViewport(1280.0f, 720.0f).factor == doctest::Approx(1.0f));
}

TEST_CASE("the smaller axis wins so a wide window does not blow the type up")
{
    // Fits by width.
    CHECK(UiScale::forViewport(1920.0f, 1080.0f).factor == doctest::Approx(1.5f));
    // Fits by height even though the width is enormous.
    CHECK(UiScale::forViewport(3840.0f, 1080.0f).factor == doctest::Approx(1.5f));
    // Fits by width on a tall, narrow window.
    CHECK(UiScale::forViewport(1024.0f, 1400.0f).factor == doctest::Approx(0.8f));
}

TEST_CASE("scaling is clamped so text stays legible and does not become absurd")
{
    // Tiny window: clamped up to the minimum.
    CHECK(UiScale::forViewport(320.0f, 240.0f).factor == doctest::Approx(UiScale::kMin));
    CHECK(UiScale::forViewport(100.0f, 100.0f).factor == doctest::Approx(UiScale::kMin));

    // Huge window: clamped to the maximum.
    CHECK(UiScale::forViewport(7680.0f, 4320.0f).factor == doctest::Approx(UiScale::kMax));
    CHECK(UiScale::forViewport(20000.0f, 20000.0f).factor == doctest::Approx(UiScale::kMax));
}

TEST_CASE("a degenerate viewport cannot produce a zero or negative scale")
{
    for (const auto size : {std::pair<float, float>{0.0f, 0.0f},
                            {0.0f, 720.0f},
                            {1280.0f, 0.0f},
                            {-100.0f, -100.0f}}) {
        const float factor = UiScale::forViewport(size.first, size.second).factor;
        CHECK(factor >= UiScale::kMin);
        CHECK(factor <= UiScale::kMax);
    }
}

TEST_CASE("px and font agree, and font never rounds to zero")
{
    const UiScale scale = UiScale::forViewport(1920.0f, 1080.0f);   // 1.5x

    CHECK(scale.px(100.0f) == doctest::Approx(150.0f));
    CHECK(scale.font(20.0f) == 30);

    // Rounding to the nearest pixel must never produce 0, which would make the
    // font invisible rather than small.
    const UiScale tiny{UiScale::kMin};
    CHECK(tiny.font(1.0f) >= 1);
    CHECK(tiny.font(20.0f) >= 1);
}

TEST_CASE("scaling is monotonic")
{
    float previous = 0.0f;
    for (int width = 320; width <= 7680; width += 160) {
        const float factor = UiScale::forViewport(static_cast<float>(width),
                                                  static_cast<float>(width) * 0.5625f).factor;
        CHECK(factor >= previous);
        previous = factor;
    }
}
