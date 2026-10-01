// Era Shift - procedural audio synthesis.
//
// Every sound in the game is generated rather than loaded. There are no audio
// assets in the repository, and there are two good reasons not to add any:
//
//   * Music and ambience that change with the era are *parameters*, not files.
//     A generative bed can glide from the Past to the Future across a shift
//     instead of cross-fading between two loops.
//   * A repository of generated audio is a repository of binaries nobody can
//     review. The synthesiser is a few hundred lines of arithmetic and can be
//     unit tested, which an opaque .wav cannot.
//
// Everything here is pure arithmetic: no SDL, no allocation, no globals. That is
// what lets the audio be tested headlessly in the gameplay suite, and it is why
// this file lives in `erashift_core` rather than next to the mixer.

#pragma once

#include "EraShift/Game/Era.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace EraShift::Audio {

/// Interleaved stereo float samples, which is what SDL's audio streams want.
using SampleBuffer = std::vector<float>;

/// Equal temperament, A4 = 440Hz.
[[nodiscard]] float noteFrequency(int semitonesFromA4) noexcept;

/// A scale as semitone offsets from the root. The three eras use deliberately
/// different ones: the Past is a minor pentatonic, the Present a natural minor,
/// the Future a whole-tone scale that never quite resolves.
struct EraScale {
    int   root;        ///< Semitones from A4.
    int   degrees[7];  ///< Scale degrees, semitones above the root.
    int   degreeCount;
    float brightness;  ///< Filter cutoff as a fraction of Nyquist.
    float detune;      ///< Chorus/beating offset in semitones.
};

[[nodiscard]] EraScale scaleFor(Game::Era era) noexcept;

// ---------------------------------------------------------------------------
// Envelopes
// ---------------------------------------------------------------------------

/// Attack/decay/sustain/release, in seconds.
struct Envelope {
    float attack  = 0.005f;
    float decay   = 0.05f;
    float sustain = 0.5f;
    float release = 0.1f;

    [[nodiscard]] float valueAt(float t, float duration) const noexcept;
};

/// A one-shot voice: a waveform, an envelope and a frequency.
struct Voice {
    enum class Wave : std::uint8_t { Sine, Triangle, Saw, Square, Noise };

    Wave    wave   = Wave::Sine;
    float   gain   = 1.0f;
    float   detune = 0.0f;   ///< Semitones, for beating.
    Envelope envelope{};
    bool    exponential = false;   ///< Saw decay rather than linear.
};

/// Renders one voice into `out`, summing.
///
/// `out` is assumed to already be the right length and is *added* to rather
/// than overwritten, so several voices can share a buffer.
void renderVoice(const Voice& voice, float frequency, float amplitude, SampleBuffer& out) noexcept;

// ---------------------------------------------------------------------------
// Generative beds
// ---------------------------------------------------------------------------

/// Parameters for the music bed. Changing them mid-playback is what makes the
/// music react to the era and to combat without any crossfade machinery.
///
/// There is no era blend parameter here, and its absence is deliberate. The blend
/// used to be one - `eraBlend`, defaulted to 1.0 - and it was the only knob in
/// the file that no code path ever read, so an era shift switched the harmony
/// from one era to the next inside a single block instead of gliding. A caller
/// cannot express "how far through the shift am I", which is why the value was
/// always going to be a constant. The synth owns the transition now, and
/// `eraBlend()` reports where it is.
struct MusicParams {
    Game::Era era = Game::Era::Present;
    /// 0 = exploration, 1 = combat. Drives density and brightness.
    float  intensity = 0.0f;
    float  tempo     = 96.0f;    ///< Beats per minute.
    float  gain      = 0.5f;
    /// Temporal instability, 0..1. Adds detune and a tremolo.
    float  instability = 0.0f;
};

/// Parameters for the ambience bed.
struct AmbienceParams {
    Game::Era era = Game::Era::Present;
    float gain = 0.6f;
    float instability = 0.0f;
};

/// A continuously running synthesiser.
///
/// It is stateful because it is a musical instrument, not a file player: it
/// remembers where it is in the bar so a bed does not restart every frame, and
/// it crossfades between era timbres rather than switching them.
class MusicSynth {
public:
    void reset() noexcept;
    /// Renders `frames` of stereo audio into `out`, which is resized to match.
    void render(MusicParams params, int frames, SampleBuffer& out);

    /// How far the bed has run, in seconds, since the last `reset`.
    ///
    /// Monotonic. This used to return the internal beat clock, which is
    /// deliberately wrapped every eighth-note, so it reported a position between
    /// zero and about 0.36 seconds no matter how long the bed had been playing -
    /// and it went *backwards* every bar. A "how far has it run" that goes
    /// backwards cannot tell a bed that is still running from one that has just
    /// been restarted, which is the only thing it was wanted for.
    [[nodiscard]] float positionSeconds() const noexcept { return m_elapsed; }

    /// How many eighth-notes the bed has played.
    ///
    /// The step counter is the bed's clock at the resolution the music is written
    /// at, and it is what a tempo change actually moves.
    [[nodiscard]] int steps() const noexcept { return m_step; }

    [[nodiscard]] float intensity() const noexcept { return m_intensity; }
    [[nodiscard]] float brightness() const noexcept { return m_brightness; }

    /// The semitone the pad's voice `voice` (0..2) is currently singing.
    ///
    /// Exposed because the property an era transition has to have is not "it
    /// eventually arrives" but "the pitch it is singing never jumps" - and pitch
    /// is invisible in the waveform, since oscillator phases are accumulated
    /// rather than computed. A note that moves by a semitone per block is smooth
    /// as a signal and glaring as music, so it has to be assertable directly.
    [[nodiscard]] float padNote(int voice) const noexcept
    {
        const int v = voice < 0 ? 0 : (voice > 2 ? 2 : voice);
        return m_padTarget[v];
    }

    /// How far through an era transition the bed is: 0 is entirely the era it is
    /// shifting away from, 1 is entirely the new one.
    ///
    /// Exposed because it is the observable that proves the shift glides. It was
    /// private state that nothing read, and a private number nothing reads is a
    /// private number that is wrong.
    [[nodiscard]] float eraBlend() const noexcept { return m_eraBlend; }

private:
    void advance(float seconds, const MusicParams& params) noexcept;

    /// Wall time rendered since the last reset. `m_time` below is *not* this: it
    /// is the beat clock, wrapped every eighth-note, which is what a musical
    /// instrument needs and is useless as a position.
    float  m_elapsed     = 0.0f;
    float  m_time        = 0.0f;
    /// The tempo the step clock actually runs at, eased toward `params.tempo`.
    ///
    /// Held as state rather than read from the parameters because tempo *is* the
    /// step clock's period: using the raw parameter moved the clock the instant
    /// combat began, which re-phased the arpeggio mid-note and produced an
    /// audible tick. Easing it means the arpeggio keeps its place in the bar and
    /// the intensity change is heard as a glide instead.
    float  m_tempo      = 96.0f;
    float  m_intensity  = 0.0f;
    float  m_brightness = 1.0f;
    /// Progress of the current era transition. See `eraBlend()`.
    float  m_eraBlend   = 1.0f;
    /// The era the bed is shifting to.
    Game::Era m_era      = Game::Era::Present;
    /// False until the first era has been seen, so the first one is adopted
    /// rather than treated as a transition away from `Present`.
    bool    m_eraKnown  = false;
    /// Set for the one frame an era changes, which is when the notes being sounded
    /// have to be latched as the far end of the next slide.
    bool    m_latchPending = false;
    int    m_step       = 0;
    float  m_tremolo    = 0.0f;
    /// Running oscillator phases, kept across buffers so a note is continuous
    /// instead of clicking at every buffer boundary.
    float  m_padPhase[3]    = {0.0f, 0.0f, 0.0f};
    float  m_leadPhase      = 0.0f;
    /// Filter state, which has to outlive the call.
    ///
    /// These were locals in `render()`, which is a bug with a name: a one-pole
    /// filter that starts every block from zero is not filtering, it is
    /// re-attacking - so it produced a step discontinuity at every block boundary
    /// (a click, once per fixed step, ~60 times a second) *and* left the pad
    /// bright and pumping. The ambience filters were members all along, which is
    /// exactly why the ambience did not click and the music did.
    float  m_padFilter      = 0.0f;
    float  m_leadFilter     = 0.0f;
    /// The bass note's own phase, because `m_time` is the *beat* clock and is
    /// deliberately wrapped every step - using it as an oscillator phase made the
    /// bass jump a few times a second.
    float  m_bassPhase      = 0.0f;
    /// Semitone targets for the current bar and eighth, recomputed per block.
    ///
    /// `*From` is the note being sounded when the last shift began and `*To` is
    /// the era's note; the target is slid between them. Latching the *sounding*
    /// note rather than resolving it from an era is what makes two shifts in quick
    /// succession continue the slide instead of jumping back to where the first
    /// one started.
    float  m_padTarget[3]   = {0.0f, 0.0f, 0.0f};
    float  m_padFrom[3]     = {0.0f, 0.0f, 0.0f};
    float  m_padTo[3]       = {0.0f, 0.0f, 0.0f};
    float  m_leadTarget     = 0.0f;
    float  m_leadFrom       = 0.0f;
    float  m_leadTo         = 0.0f;
};

/// A continuously running ambience synthesiser: filtered noise, wind, and the
/// occasional tone that gives each era its character.
class AmbienceSynth {
public:
    void reset() noexcept;
    void render(AmbienceParams params, int frames, SampleBuffer& out);

    /// How far the bed has run. Proves a parameter change glided rather than
    /// restarting the bed from zero.
    [[nodiscard]] float positionSeconds() const noexcept { return m_time; }
    /// See `MusicSynth::eraBlend()`. The ambience crosses between eras over the
    /// same time constant as the music.
    [[nodiscard]] float eraBlend() const noexcept { return m_eraBlend; }

private:
    float  m_time       = 0.0f;
    float  m_noiseState = 0.0f;
    float  m_filter     = 0.0f;
    float  m_filter2    = 0.0f;
    float  m_motif      = 0.0f;
    int    m_motifIndex = 0;
    float  m_eraBlend   = 1.0f;
    Game::Era m_era      = Game::Era::Present;
    /// See `MusicSynth::m_eraKnown` and `m_latchPending`.
    bool    m_eraKnown = false;
    bool    m_latchPending = false;
    /// The wind filter's cutoff and depth at the moment the last shift began, and
    /// the era's own values. Blended between, for the same reason as the music
    /// bed's harmony.
    float  m_windCutoffFrom = 0.045f;
    float  m_windCutoffTo   = 0.045f;
    float  m_windDepthFrom  = 0.6f;
    float  m_windDepthTo    = 0.6f;
    /// The Past's motif sits an octave up; latched and slid rather than switched.
    float  m_motifOctaveFrom = 0.0f;
    float  m_motifOctaveTo   = 0.0f;
    int    m_framesUntilMote = 0;
};

// ---------------------------------------------------------------------------
// One-shots
// ---------------------------------------------------------------------------

/// Every distinct sound the game makes. A closed enum rather than string keys,
/// so a typo is a compile error instead of silence.
enum class Sfx : std::uint8_t {
    UiMove,
    UiConfirm,
    UiDeny,

    Jump,
    Land,
    Dash,

    Attack,
    HitEnemy,
    EnemyHurt,
    EnemyDie,

    PlayerHurt,
    PlayerDie,

    PickupChrono,
    PickupHealth,
    SealTaken,
    GateOpened,

    /// The era shift has a signature: a charge that rises into a distortion,
    /// then a low impact, then the new era bleeding in underneath.
    ShiftCharge,
    ShiftImpact,

    Hazard,

    /// Footsteps, one per surface material.
    ///
    /// These exist because "footstep" was a single sound whose pitch rose with
    /// speed, which meant walking on grass and walking on stone were the same
    /// noise. The distinction is the whole point of hearing where you are: soft
    /// and dull underfoot on vegetation, sharp and ringing on metal, bright and
    /// glassy on crystal.
    FootstepStone,
    FootstepGrass,
    FootstepSand,
    FootstepMetal,
    FootstepCrystal,

    /// Last. Used as the one-shot count, so nothing may be added after it.
    Victory,
};

/// Renders a one-shot into `out`, which is resized to match.
void renderSfx(Sfx sfx, int sampleRate, SampleBuffer& out);

/// The sound's name, for logs and for failing tests that need to say which one.
[[nodiscard]] std::string_view toString(Sfx sfx) noexcept;

/// Fades the first and last few milliseconds of a rendered one-shot.
///
/// A one-shot that starts at full amplitude and stops at its exact length has a
/// step discontinuity at both ends, and a step is a click. Applied once to the
/// finished buffer rather than per voice, so every sound gets clean edges however
/// it was built. `renderSfx` calls this; it is public so a test can assert the
/// property directly.
void applyEdgeFades(SampleBuffer& buffer, int sampleRate);

/// Length of a one-shot in seconds, so the caller can size a stream.
[[nodiscard]] float sfxDuration(Sfx sfx) noexcept;

/// Samples per second. Matches `config/audio.json`'s `mixRate` default.
inline constexpr int kSampleRate = 48000;

} // namespace EraShift::Audio
