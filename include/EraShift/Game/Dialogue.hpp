// Era Shift - the dialogue panel.
//
// A framed panel with a speaker, a body of text that wraps inside the frame, and
// a confirm/cancel footer. Drawn by whichever state owns it; it holds no state of
// its own beyond "is there anything to say", because a dialogue box with its own
// lifecycle is how a previous character's name ends up on screen during the next
// conversation.
//
// Deliberately not a subsystem: it is a rectangle, some text and two buttons, and
// a class hierarchy around that would be larger than the thing.

#pragma once

#include "EraShift/Application/StateContext.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"

#include <string>

namespace EraShift::Game {

/// What a dialogue panel is currently showing.
///
/// A closed panel has a `speaker` of empty and draws nothing, which is what makes
/// "did the last panel clear itself?" a question with an observable answer rather
/// than a matter of trust.
struct DialoguePanel {
    /// Who is speaking. Empty for narration with no speaker.
    std::string speaker;
    /// What they are saying. Wrapped and clipped to the panel.
    std::string message;
    /// Optional heading above the speaker line.
    std::string title;
    /// Optional line in the footer, e.g. "E to confirm".
    std::string hint;
    /// How long the panel stays up in seconds. Zero or less means it stays until
    /// something clears it.
    float duration = 0.0f;

    /// True when there is anything to draw. An empty speaker *and* an empty message
    /// means closed: a panel holding only a hint is still closed, because a stray
    /// footer with no content above it is worse than nothing.
    [[nodiscard]] bool open() const noexcept
    {
        return !message.empty() || !speaker.empty();
    }

    /// Replaces the contents, resets the timer, and clears `title` and `hint`.
    ///
    /// Clearing those two is the point. A caller that shows a line under a heading
    /// and then shows another without one would otherwise leave the first heading
    /// above the second speaker — which is the stale-content failure this type
    /// exists to make impossible.
    void show(std::string newSpeaker, std::string newMessage, float seconds = 0.0f)
    {
        clear();
        speaker = std::move(newSpeaker);
        message = std::move(newMessage);
        duration = seconds;
    }

    /// Empties every field, so the next `show` cannot inherit a hint or a title
    /// from whatever was on screen before.
    void clear() noexcept
    {
        speaker.clear();
        message.clear();
        title.clear();
        hint.clear();
        duration = 0.0f;
    }
};

/// Draws the panel, centred horizontally on `playerCentre`, wrapping the message
/// to the frame.
///
/// Draws nothing when the panel is closed, so a caller can invoke it every frame
/// without asking first. Text is clipped to the frame: a long message wraps to
/// more lines and is cut with an ellipsis rather than spilling over the world
/// behind it, which is the failure this exists to prevent.
///
/// `bottomReserved` is how many pixels at the bottom of the viewport are already
/// taken. The HUD's hint row, objective line and interaction prompt all live there,
/// and a panel drawn over them is a panel the player cannot read — so the caller
/// states what it has claimed rather than this guessing.
void drawDialoguePanel(const StateContext& ctx, const DialoguePanel& panel,
                       Graphics::Vec2 playerCentre, float viewHeight,
                       Graphics::UiScale scale, float bottomReserved);

} // namespace EraShift::Game