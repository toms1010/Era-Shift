// Era Shift - staged settings.
//
// The settings screen edits a *draft*, not the live configuration. Left and right
// change the draft; leaving the screen either commits the draft or throws it away.
// Nothing is written until the commit.
//
// This is not ceremony. The bug it replaces: pressing LEFT on VSYNC called
// `window.setVsync` and `store.setInt` immediately, so ESC — the button that means
// "I changed my mind" — still persisted the change. The player could not try a
// setting and back out of it.
//
// A draft is a flat list of typed settings rather than a bag of `int` members in the
// state, because a bag has no way to answer "is anything changed?" without
// comparing each one by hand, and forgetting one is how a partially-saved settings
// screen happens.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace EraShift::Game {

/// The kinds of value a setting can hold.
///
/// A closed set rather than a variant, because a setting that needs a new kind is
/// a new setting and should say so, and because the editor row is drawn from this.
enum class SettingKind : std::uint8_t {
    /// ON / OFF, one step.
    Toggle,
    /// A percentage, 0..100, in steps of ten.
    Percent,
    /// A scale factor, 50..200, in steps of ten.
    Scale,
};

/// How one setting moves when left or right is pressed.
struct SettingSpec {
    /// The label as the player sees it.
    std::string label;
    /// Config section, e.g. "graphics".
    std::string section;
    /// Config key, e.g. "vsync".
    std::string key;
    SettingKind kind = SettingKind::Toggle;
    int minimum = 0;
    int maximum = 100;
    /// Amount one press changes it by.
    int step = 10;
    /// Value used when the config has none. Also the initial draft value.
    int fallback = 0;
    /// A short caption shown under the list while this row is selected.
    std::string detail;

    /// The value the game is running with, as loaded from the config store.
    ///
    /// Part of the spec rather than a parameter because every row needs one and
    /// because a row whose current value differs from the draft's is a bug worth
    /// being unable to express.
    int current = 0;

    /// Formats a value as the row shows it.
    [[nodiscard]] std::string display(int value) const;
    /// Steps `value` by `direction`, clamped to the range.
    [[nodiscard]] int stepped(int value, int direction) const;
};

/// One setting's current and staged values.
struct SettingValue {
    /// What the game is running with right now.
    int current = 0;
    /// What the player has chosen but not yet applied.
    int pending = 0;

    [[nodiscard]] bool changed() const noexcept { return pending != current; }
};

/// A set of settings being edited.
///
/// Holds no store and no window: it knows numbers, and `commit` hands back the keys
/// that moved so the caller does the writing. That split is what makes "editing
/// writes nothing" a property a test can check without a filesystem.
class SettingsDraft {
public:
    /// Adds a setting with both values at `fallback`.
    void add(const SettingSpec& spec);
    /// Staged and current values for `index`, or nullptr when out of range.
    [[nodiscard]] const SettingValue* valueAt(std::size_t index) const noexcept;
    [[nodiscard]] const SettingSpec* specAt(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return m_specs.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_specs.empty(); }

    /// Stages a change. A no-op when `index` is out of range.
    void adjust(std::size_t index, int direction);
    /// Puts a specific value in the pending slot, clamped.
    ///
    /// For a settings row that reads from live state — a checkbox driven by a
    /// query — rather than by pressing left or right.
    void stage(std::size_t index, int value);

    /// True when anything differs from what the game is running with.
    [[nodiscard]] bool dirty() const noexcept;
    /// The staged values of everything that moved, in insertion order.
    [[nodiscard]] std::vector<std::size_t> changedIndices() const;

    /// Promotes every pending value to current.
    ///
    /// @return the indices that moved, so the caller writes exactly those and not
    ///         the whole set — rewriting unchanged keys makes a settings screen look
    ///         like it saved when it did nothing.
    std::vector<std::size_t> commit();

    /// Discards every pending value.
    void discard();

private:
    [[nodiscard]] int clamp(int value, const SettingSpec& spec) const noexcept;

    std::vector<SettingSpec> m_specs;
    std::vector<SettingValue> m_values;
};

} // namespace EraShift::Game