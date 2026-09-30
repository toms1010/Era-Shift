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
struct MusicParams {
    Game::Era era = Game::Era::Present;
    /// 0 = exploration, 1 = combat. Drives density and brightness.
    float  intensity = 0.0f;
    float  tempo     = 96.0f;    ///< Beats per minute.
    float  gain      = 0.5f;
    /// 0..1. How far the era has shifted, used to glide rather than jump.
    float  eraBlend  = 1.0f;
    /// Temporal instability, 0..1. Adds detune and a tremolo.
    float  instability = 0.0f;
};

/// Parameters for the ambience bed.
struct AmbienceParams {
    Game::Era era = Game::Era::Present;
    float gain = 0.6f;
    float eraBlend = 1.0f;
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

    [[nodiscard]] float positionSeconds() const noexcept { return m_time; }
    [[nodiscard]] float intensity() const noexcept { return m_intensity; }
    [[nodiscard]] float brightness() const noexcept { return m_brightness; }

private:
    void advance(float seconds, const MusicParams& params) noexcept;

    float  m_time       = 0.0f;
    float  m_intensity  = 0.0f;
    float  m_brightness = 1.0f;
    float  m_eraBlend   = 1.0f;
    int    m_step       = 0;
    float  m_tremolo    = 0.0f;
    /// Running oscillator phases, kept across buffers so a note is continuous
    /// instead of clicking at every buffer boundary.
    float  m_padPhase[3]    = {0.0f, 0.0f, 0.0f};
    float  m_leadPhase      = 0.0f;
    /// Semitone targets for the current bar and eighth, recomputed per block.
    float  m_padTarget[3]   = {0.0f, 0.0f, 0.0f};
    float  m_leadTarget     = 0.0f;
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

private:
    float  m_time       = 0.0f;
    float  m_noiseState = 0.0f;
    float  m_filter     = 0.0f;
    float  m_filter2    = 0.0f;
    float  m_motif      = 0.0f;
    int    m_motifIndex = 0;
    float  m_eraBlend   = 1.0f;
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
    Victory,
};

/// Renders a one-shot into `out`, which is resized to match.
void renderSfx(Sfx sfx, int sampleRate, SampleBuffer& out);

/// The sound's name, for logs and for failing tests that need to say which one.
[[nodiscard]] std::string_view toString(Sfx sfx) noexcept;

/// Length of a one-shot in seconds, so the caller can size a stream.
[[nodiscard]] float sfxDuration(Sfx sfx) noexcept;

/// Samples per second. Matches `config/audio.json`'s `mixRate` default.
inline constexpr int kSampleRate = 48000;

} // namespace EraShift::Audio
