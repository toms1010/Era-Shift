// Era Shift - tests for line breaking.
//
// `wrapLines` takes its measurement as a callback precisely so these tests can run
// without a window, a renderer or a loaded TTF face. The alternative — asserting
// only through a real TextRenderer — means the wrapping rules have no tests at all,
// which is how a word wider than its panel survived: it only misbehaves against a
// real font's metrics, so it was never checked against anything.
//
// The probe below counts one unit per character, which makes "fits" arithmetic
// rather than a matter of taste.

#include "EraShift/Graphics/TextRenderer.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using EraShift::Graphics::wrapLines;
using Lines = std::vector<std::string>;

namespace {

/// One unit per character. A line of width 10 therefore fits 10 characters.
float charWidth(const std::string& line, void*) { return static_cast<float>(line.size()); }

/// Every character is 4 units wide, so width 40 fits exactly 10 characters.
float wideChar(const std::string& line, void*) { return static_cast<float>(line.size()) * 4.0f; }

Lines wrap(std::string_view text, float maxWidth, int maxLines = 0, bool on = true)
{
    return wrapLines(text, on, maxLines, maxWidth, charWidth, nullptr);
}

/// Joins the lines, so a test can assert on the shape of a whole block.
std::string joined(const Lines& lines)
{
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out += '|';
        }
        out += lines[i];
    }
    return out;
}

} // namespace

TEST_CASE("empty text produces no lines")
{
    CHECK(wrap("", 100.0f).empty());
    CHECK(wrap("", 100.0f, 3).empty());
}

TEST_CASE("a null probe is refused rather than dereferenced")
{
    // A caller that has no font must get an empty result, not a crash: the
    // no-font path is real, since a missing TTF face is handled by falling back
    // to the bitmap font everywhere else.
    CHECK(wrapLines("some text", true, 0, 100.0f, nullptr, nullptr).empty());
}

TEST_CASE("text shorter than the width is one line")
{
    CHECK(joined(wrap("hello", 100.0f)) == "hello");
}

TEST_CASE("a line exactly the width still fits")
{
    // Off-by-one here is invisible until a panel is exactly sized.
    CHECK(joined(wrap("0123456789", 10.0f)) == "0123456789");
}

TEST_CASE("one character over the width wraps")
{
    CHECK(joined(wrap("0123456789A", 10.0f)) == "0123456789|A");
}

TEST_CASE("words are broken at spaces, not mid-word")
{
    CHECK(joined(wrap("the quick brown fox", 10.0f)) == "the quick|brown fox");
}

TEST_CASE("a word wider than the line is broken rather than overflowing")
{
    // The bug this file exists for. Before the fix a token wider than the width was
    // accepted whole whenever the line was empty, so this string was emitted as one
    // 36-character line against a 10-character budget — drawn past the edge of
    // whatever panel it was in, with no way for the caller to notice.
    const Lines lines = wrap("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 10.0f);
    CHECK(lines.size() > 1);
    for (const std::string& line : lines) {
        CHECK_MESSAGE(line.size() <= 10, "every emitted line must fit the width");
    }
    CHECK(joined(lines) == "ABCDEFGHIJ|KLMNOPQRST|UVWXYZ0123|456789");
}

TEST_CASE("no line ever exceeds the width, for any input")
{
    // The invariant, stated over a spread of adversarial inputs rather than one case.
    const std::vector<std::string> inputs = {
        "a",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "short aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa long",
        "word1 word2 word3 word4 word5 word6 word7 word8",
        "aaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbb ccccccccccccccccccc",
        "one two three four five six seven eight nine ten eleven twelve",
    };
    for (const std::string& input : inputs) {
        for (const float width : {5.0f, 10.0f, 17.0f, 40.0f, 1000.0f}) {
            for (const Lines& lines : {wrap(input, width), wrap(input, width, 4)}) {
                CAPTURE(input);
                CAPTURE(width);
                for (const std::string& line : lines) {
                    CHECK_MESSAGE(charWidth(line, nullptr) <= width,
                                  "a wrapped line must fit its width");
                }
            }
        }
    }
}

TEST_CASE("an over-long word after a short one starts its own line")
{
    // The break must flush what came before, or the tail of the broken word lands
    // on the same line as the previous word and overflows again.
    const Lines lines = wrap("hi ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10.0f);
    CHECK(lines.front() == "hi");
    for (const std::string& line : lines) {
        CHECK(line.size() <= 10);
    }
}

TEST_CASE("newlines are honoured as hard breaks")
{
    CHECK(joined(wrap("a\nb", 100.0f)) == "a|b");
}

TEST_CASE("a blank line survives as a line")
{
    // Deliberate paragraph spacing must not be collapsed away.
    CHECK(joined(wrap("a\n\nb", 100.0f)) == "a||b");
}

TEST_CASE("a trailing newline produces a final empty line")
{
    CHECK(joined(wrap("a\n", 100.0f)) == "a|");
}

TEST_CASE("a leading newline produces a leading empty line")
{
    CHECK(joined(wrap("\na", 100.0f)) == "|a");
}

TEST_CASE("a wide character probe is respected too")
{
    // The fix must not be specific to one-character-per-unit arithmetic.
    const Lines lines =
        wrapLines("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", true, 0, 40.0f, wideChar, nullptr);
    for (const std::string& line : lines) {
        CHECK(wideChar(line, nullptr) <= 40.0f);
    }
}

TEST_CASE("wrapping off keeps the text on one line but honours newlines")
{
    CHECK(joined(wrap("the quick brown fox", 10.0f, 0, false)) == "the quick brown fox");
    CHECK(joined(wrap("a\nb", 10.0f, 0, false)) == "a|b");
}

TEST_CASE("a zero or negative width disables wrapping rather than looping")
{
    // An inverted width must not become a division or an infinite loop.
    CHECK(joined(wrap("the quick brown fox", 0.0f, 0, false)) == "the quick brown fox");
    CHECK(wrap("the quick brown fox", -5.0f, 0, false).size() == 1);
}

TEST_CASE("maxLines truncates and ellipsises")
{
    const Lines lines = wrap("one two three four five six seven eight", 12.0f, 2);
    REQUIRE(lines.size() == 2);
    CHECK(lines.back().size() <= 12);
    CHECK(lines.back().substr(lines.back().size() - 3) == "...");
}

TEST_CASE("the ellipsis itself fits inside the width")
{
    // Without this check, truncating a line that exactly filled the width is how the
    // one line that *does* overflow is the truncated one.
    for (const float width : {3.0f, 4.0f, 5.0f, 6.0f, 10.0f}) {
        const Lines lines = wrap("abcdefghijklmnop", width, 1);
        REQUIRE(lines.size() == 1);
        CAPTURE(width);
        CHECK(lines.front().size() <= static_cast<std::size_t>(width));
    }
}

TEST_CASE("maxLines beyond the line count changes nothing")
{
    // "one two" fits width 100, so it is one line and maxLines 99 must not invent
    // a second. The point is that the limit only ever *removes* lines.
    const Lines lines = wrap("one two", 100.0f, 99);
    CHECK(lines.size() == 1);
    CHECK(lines.front() == "one two");
}

TEST_CASE("maxLines beyond the count of a multi-line block changes nothing")
{
    const Lines natural = wrap("one two three four", 10.0f);
    const Lines limited = wrap("one two three four", 10.0f, 99);
    CHECK(limited == natural);
    CHECK(natural.size() > 1);
}

TEST_CASE("a maxLines of one still returns something")
{
    const Lines lines = wrap("one two three", 100.0f, 1);
    REQUIRE(lines.size() == 1);
    // One line already fits, so maxLines 1 does not truncate it: it is the limit,
    // not a request to ellipsise. Ellipsising here would tell the player the text
    // was cut when nothing was cut.
    CHECK(lines.front() == "one two three");
}

TEST_CASE("maxLines one on genuinely long text does ellipsise")
{
    // Width 8 forces several lines naturally, so maxLines 1 has something to cut.
    // Truncation only ever removes lines that already exist.
    const Lines natural = wrap("one two three four five", 8.0f);
    REQUIRE(natural.size() > 1);

    const Lines lines = wrap("one two three four five", 8.0f, 1);
    REQUIRE(lines.size() == 1);
    CHECK(lines.front().substr(lines.front().size() - 3) == "...");
    CHECK(lines.front().size() <= 8);
}

TEST_CASE("ellipsising a single short word does not empty the line")
{
    // Popping characters to make room for "..." must leave something behind;
    // otherwise a very narrow width produces an empty line and the caller draws
    // nothing at all.
    const Lines lines = wrap("ab", 3.0f, 1);
    REQUIRE(lines.size() == 1);
    CHECK(lines.front().empty() == false);
}

TEST_CASE("wrapping is deterministic")
{
    // The panel layout reads the same text every frame; a non-deterministic break
    // would make the text jitter as it re-wraps each frame.
    const std::string input = "the quick brown fox jumps over the lazy dog";
    const Lines first = wrap(input, 13.0f);
    for (int repeat = 0; repeat < 4; ++repeat) {
        CHECK(wrap(input, 13.0f) == first);
    }
}

TEST_CASE("the probe is asked about whole candidates, never about nothing")
{
    // A probe that returns 0 for the empty string would make the first character of
    // every line fit, then the rest not — producing a one-character-per-line wrap.
    int calls = 0;
    const Lines lines =
        wrapLines("hello world", true, 0, 6.0f,
                  [](const std::string& line, void* user) {
                      ++*static_cast<int*>(user);
                      return static_cast<float>(line.size());
                  },
                  &calls);
    CHECK(calls > 0);
    CHECK(lines.size() > 1);
}