// Era Shift - shared UI sounds.
//
// Three functions rather than a general "play a UI sound" helper, because the
// interesting property is the *ratio* between them: a move is a tick, a confirm
// rises, a deny falls. Spelling that out in three named functions is clearer
// than three call sites each picking a pitch, and it means a menu that moves
// quietly and confirms loudly does not happen by accident.

#pragma once

#include "EraShift/Application/StateContext.hpp"
#include "EraShift/Audio/AudioManager.hpp"
#include "EraShift/Audio/Synth.hpp"

namespace EraShift::Game {

/// The selection moved. Deliberately quiet and high: it happens several times a
/// second while a player is reading a menu, so anything louder is a nuisance.
inline void playUiMove(StateContext& ctx) noexcept
{
    if (ctx.audio != nullptr) {
        ctx.audio->play(Audio::Sfx::UiMove, 0.45f);
    }
}

/// Something was accepted and the screen is about to change.
inline void playUiConfirm(StateContext& ctx) noexcept
{
    if (ctx.audio != nullptr) {
        ctx.audio->play(Audio::Sfx::UiConfirm, 0.8f);
    }
}

/// The action cannot be taken: not enough chrono, a seal in the wrong era, a
/// disabled row. Deliberately a falling interval, which is the one contour the
/// ear reads as "no" without having been taught it.
inline void playUiDeny(StateContext& ctx) noexcept
{
    if (ctx.audio != nullptr) {
        ctx.audio->play(Audio::Sfx::UiDeny, 0.7f);
    }
}

/// A value moved under a settings slider. Played on every step so the slider
/// audibly tracks, rather than only on release, which is what makes a
/// continuous control feel continuous.
inline void playUiTick(StateContext& ctx) noexcept
{
    if (ctx.audio != nullptr) {
        ctx.audio->play(Audio::Sfx::UiMove, 0.3f, 1.15f);
    }
}

} // namespace EraShift::Game
