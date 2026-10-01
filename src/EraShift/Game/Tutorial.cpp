#include "EraShift/Game/Tutorial.hpp"

namespace EraShift::Game {

TutorialLesson lessonFor(TutorialStep step) noexcept
{
    switch (step) {
        case TutorialStep::None:
            return {};

        case TutorialStep::Move:
            return {step, "MOVE", "Use A / D or the arrow keys to walk.",
                    "A / D"};

        case TutorialStep::Jump:
            return {step, "JUMP", "Press SPACE to jump. Hold it for height.",
                    "SPACE"};

        case TutorialStep::Shift:
            // The one lesson worth two lines: it is the mechanic, and it is the
            // thing a player who does not understand it will fail the level over.
            return {step, "ERA SHIFT",
                    "Press Q to change which era is real. The world you are "
                    "standing in becomes the world you were in.",
                    "Q"};

        case TutorialStep::Enemy:
            return {step, "IT LIVES IN AN ERA",
                    "This enemy only exists while its era is real. Shift away "
                    "and it is gone — and so is anything only it was guarding.",
                    ""};

        case TutorialStep::Attack:
            return {step, "ATTACK", "Left Mouse to swing. The hit lands a short "
                                    "while after the swing starts.",
                    "LEFT MOUSE"};

        case TutorialStep::Dash:
            return {step, "DASH", "Left Shift to dash. Briefly untouchable.",
                    "LEFT SHIFT"};

        case TutorialStep::Interact:
            return {step, "INTERACT", "Press E to take what is in reach.",
                    "E"};

        case TutorialStep::Seal:
            return {step, "ERA SEALS", "Three seals hold the gate. Each one only "
                                       "yields while its own era is real.",
                    "E"};

        case TutorialStep::Gate:
            return {step, "THE GATE", "With all three seals the gate opens. Step "
                                      "into it to finish.",
                    ""};

        case TutorialStep::Complete:
            return {};
    }
    return {};
}

Tutorial::Tutorial(bool enabled) noexcept
    : m_step(enabled ? TutorialStep::Move : TutorialStep::None), m_enabled(enabled)
{
}

bool Tutorial::perform(TutorialStep action)
{
    if (!m_enabled || action == TutorialStep::None) {
        return false;
    }

    // Only the lesson currently being taught can advance it. This is what stops a
    // player who happens to press space during the movement lesson from skipping
    // ahead: the action happened, but it was not the action that was asked for.
    if (action != m_step) {
        return false;
    }

    ++m_seen;
    skip();
    return true;
}

void Tutorial::skip()
{
    switch (m_step) {
        case TutorialStep::None:
        case TutorialStep::Complete:
            // Already finished. Idempotent, because `finish` can be called
            // repeatedly by a caller that does not check first.
            return;
        case TutorialStep::Gate:
            m_step = TutorialStep::Complete;
            return;
        default:
            m_step = static_cast<TutorialStep>(static_cast<std::uint8_t>(m_step) + 1);
            return;
    }
}

void Tutorial::finish()
{
    m_step = TutorialStep::Complete;
}

void Tutorial::restore(bool enabled, bool complete)
{
    m_enabled = enabled;
    m_step    = complete ? TutorialStep::Complete
                         : (enabled ? TutorialStep::Move : TutorialStep::None);
}

} // namespace EraShift::Game