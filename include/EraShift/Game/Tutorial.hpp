// Era Shift - the first-run tutorial.
//
// Contextual and action-driven: one lesson at a time, each shown when the player
// reaches the situation it teaches and dismissed by *performing* it rather than
// by pressing a key to dismiss it.
//
// The alternative — a wall of text on spawn — teaches nothing, because a player
// who has not yet moved has no context for "shift to the Past to cross this".
// Here the player walks to the chasm, sees the prompt, presses Q, and the prompt
// goes away because the thing it asked for happened.
//
// State lives here rather than in `PlayingState` because it is pure bookkeeping
// over simulation events: no SDL, no rendering, and therefore testable in the
// headless gameplay suite.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace EraShift::Game {

/// The lessons, in the order they are taught.
///
/// Sequential by construction: `advance` only ever moves to the next step, so a
/// player cannot be shown step 7 while standing where step 2 belongs.
enum class TutorialStep : std::uint8_t {
    None = 0,
    Move,
    Jump,
    Shift,
    Enemy,
    Attack,
    Dash,
    Interact,
    Seal,
    Gate,
    Complete,
};

/// One thing the player is being taught.
struct TutorialLesson {
    TutorialStep step = TutorialStep::None;
    /// Short label, drawn as the heading.
    const char* title = "";
    /// The instruction itself.
    const char* body = "";
    /// Key or control shown in the prompt footer.
    const char* key = "";

    /// Empties every field. Safe on a lesson that was never set, so a state can
    /// clear unconditionally rather than tracking whether it drew anything.
    void clear() noexcept
    {
        step  = TutorialStep::None;
        title = "";
        body  = "";
        key   = "";
    }
};

/// The lesson for a step, or a blank lesson for `None` and `Complete`.
[[nodiscard]] TutorialLesson lessonFor(TutorialStep step) noexcept;

/// How long a lesson prompt takes to arrive, and then to leave once satisfied.
///
/// On the presentation clock, so a player standing in hit-stop still watches the
/// prompt finish rather than seeing it freeze half-transparent.
inline constexpr float kLessonEnter = 0.28f;
inline constexpr float kLessonLeave = 0.34f;

/// Opacity of the lesson prompt: 0 hidden, 1 fully shown.
///
/// `leaving` means the lesson was satisfied and is playing its exit; `age` is the
/// time since either the lesson arrived or the exit began, so the two cannot
/// disagree about what "fully shown" means.
///
/// Pure and time-based rather than a set of booleans so the whole lifecycle can be
/// checked without a renderer — the interesting failures here are off-by-one-frame
/// ones, which are exactly what a screenshot cannot show.
[[nodiscard]] float lessonOpacity(bool active, bool leaving, float age) noexcept;

/// Whether a lesson prompt should be drawn at all.
///
/// `leaving` keeps the last prompt alive after the tutorial itself reports itself
/// complete, so the final lesson animates out like every other one.
[[nodiscard]] bool lessonVisible(bool active, bool leaving) noexcept;

/// Tracks tutorial progress for one session.
///
/// Enabled/disabled lives in the progression database rather than here, so a
/// player who has finished the tutorial is never shown it again and a player who
/// turned it off is never nagged.
class Tutorial {
public:
    /// Starts enabled at `Move`. Does nothing when `enabled` is false, which is
    /// the difference between "no tutorial" and "a tutorial that finished".
    explicit Tutorial(bool enabled = true) noexcept;

    [[nodiscard]] bool enabled() const noexcept { return m_enabled; }
    [[nodiscard]] TutorialStep step() const noexcept { return m_step; }
    [[nodiscard]] TutorialLesson lesson() const noexcept { return lessonFor(m_step); }
    /// True while a prompt should be on screen.
    [[nodiscard]] bool active() const noexcept
    {
        return m_enabled && m_step != TutorialStep::None && m_step != TutorialStep::Complete;
    }
    [[nodiscard]] bool complete() const noexcept { return m_step == TutorialStep::Complete; }

    /// Records that the player performed the current lesson's action.
    ///
    /// An action that is not the one being taught is ignored rather than
    /// consuming the step: pressing space during the "move" lesson should not
    /// skip the movement lesson, it should just do nothing about it. Only the
    /// matching action advances, and only forwards.
    ///
    /// @return true when this call advanced the tutorial.
    bool perform(TutorialStep action);

    /// Skips to the next lesson without waiting for the action.
    ///
    /// For the lessons that cannot be detected — "use the gate to leave" is the
    /// player walking into it, not a key — and for a player who already knows the
    /// game and wants the prompts gone.
    void skip();

    /// Whether the player has reached the situation this lesson is about.
    ///
    /// The lessons that have no key to wait for are satisfied by *arriving*, and
    /// this is how the caller says the player has arrived. Separate from `skip`
    /// because the two answer different questions: `skip` says "get off this
    /// lesson", this says "is the player in the situation the lesson describes" —
    /// and a caller that used the first for the second would dismiss the prompt
    /// before the player had seen what it was about, which is the one thing the
    /// lesson exists to do.
    ///
    /// @param playerNearSomething true when the player has reached an enemy, a
    ///        seal or the gate. Which of the three is decided by the step.
    [[nodiscard]] bool reachedSituation(bool playerNearSomething) noexcept
    {
        switch (m_step) {
            // These three are taught by arriving rather than by pressing: the
            // player cannot swing at an enemy they cannot see, and cannot walk
            // into a gate they have not found.
            case TutorialStep::Enemy:
            case TutorialStep::Seal:
            case TutorialStep::Gate:
                if (!playerNearSomething) {
                    return false;
                }
                ++m_seen;
                skip();
                return true;

            // Everything else is satisfied by performing it, and arriving at
            // something must not skip a lesson the player has not done yet.
            default:
                return false;
        }
    }

    /// Marks the tutorial finished and remembers it, so the next run starts clean.
    void finish();

    /// Restores a previous session's progress.
    void restore(bool enabled, bool complete);

    /// The number of lessons taught this session. Used by tests and the log.
    [[nodiscard]] int lessonsSeen() const noexcept { return m_seen; }

private:
    // Declaration order matches the constructor's initialiser order, so the
    // compiler does not warn about a reordering that is not happening.
    TutorialStep m_step = TutorialStep::Move;
    bool m_enabled = true;
    int  m_seen = 0;
};

} // namespace EraShift::Game