#include "EraShift/Audio/Synth.hpp"

#include "EraShift/Graphics/Math.hpp"

#include <algorithm>
#include <cstddef>

namespace EraShift::Audio {

using Game::Era;

namespace {

constexpr float kTwoPi = 6.28318530718f;

/// Cheap deterministic noise. A real PRNG would be fine, but a hash keeps the
/// output reproducible across platforms, which matters for a synthesiser whose
/// output a test asserts on.
[[nodiscard]] float hashNoise(std::uint32_t index) noexcept
{
    std::uint32_t x = index * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return static_cast<float>(x & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

/// One-pole low-pass, used to take the edge off a waveform without needing a
/// biquad coefficient solver at runtime.
struct LowPass {
    float state = 0.0f;

    float process(float input, float cutoff01) noexcept
    {
        state += (input - state) * Graphics::clampValue(cutoff01, 0.0005f, 1.0f);
        return state;
    }
};

float waveform(Voice::Wave wave, float phase, std::uint32_t noiseIndex) noexcept
{
    const float t = phase - std::floor(phase);
    switch (wave) {
        case Voice::Wave::Sine:     return std::sin(kTwoPi * t);
        case Voice::Wave::Triangle: return 1.0f - 4.0f * std::fabs(t - 0.5f);
        case Voice::Wave::Saw:      return 2.0f * t - 1.0f;
        case Voice::Wave::Square:   return t < 0.5f ? 1.0f : -1.0f;
        case Voice::Wave::Noise:    return hashNoise(noiseIndex);
    }
    return 0.0f;
}

} // namespace

float noteFrequency(int semitonesFromA4) noexcept
{
    return 440.0f * std::pow(2.0f, static_cast<float>(semitonesFromA4) / 12.0f);
}

EraScale scaleFor(Era era) noexcept
{
    switch (era) {
        case Era::Past:
            // Minor pentatonic: open, unresolved, nothing wrong with it.
            return EraScale{-7, {0, 3, 5, 7, 10, 0, 0}, 5, 0.30f, 0.0f};
        case Era::Present:
            // Natural minor: the era that is supposed to sound like ours, and
            // the only one whose scale actually resolves.
            return EraScale{-5, {0, 2, 3, 5, 7, 8, 10}, 7, 0.46f, 0.02f};
        case Era::Future:
            // Whole tone: symmetrical, so it never lands anywhere.
            return EraScale{2, {0, 2, 4, 6, 8, 10, 0}, 6, 0.68f, 0.06f};
    }
    return EraScale{-5, {0, 2, 3, 5, 7, 8, 10}, 7, 0.46f, 0.02f};
}

float Envelope::valueAt(float t, float duration) const noexcept
{
    if (t < 0.0f) {
        return 0.0f;
    }
    // Note the strict comparison above: at exactly t == 0 with a zero-length
    // attack, falling through is what makes the onset instant. Returning early
    // for t <= 0 would make a zero attack unreachable and every click in the
    // game would start from silence.
    if (t >= duration) {
        // Past the tail the voice releases rather than cutting off.
        const float since = t - duration;
        if (release <= 0.0f) {
            return 0.0f;
        }
        return sustain * std::max(0.0f, 1.0f - since / release);
    }
    if (t < attack) {
        return attack > 0.0f ? t / attack : 1.0f;
    }
    if (t < attack + decay) {
        const float d = decay > 0.0f ? (t - attack) / decay : 1.0f;
        return 1.0f + (sustain - 1.0f) * d;
    }
    return sustain;
}

void renderVoice(const Voice& voice, float frequency, float amplitude, SampleBuffer& out) noexcept
{
    if (out.empty() || frequency <= 0.0f || amplitude <= 0.0f) {
        return;
    }

    const int frames = static_cast<int>(out.size() / 2);
    const float duration = voice.envelope.attack + voice.envelope.decay + 0.25f;

    // Two oscillators: the fundamental and a detuned copy. Detune is what makes
    // a sustained note sound like an instrument rather than a test tone; when it
    // is zero the second oscillator is simply not run.
    const bool detuned = std::fabs(voice.detune) > 0.001f;
    const float detuneRatio = std::pow(2.0f, voice.detune / 12.0f);

    float phaseA = 0.0f;
    float phaseB = 0.0f;
    const float rate = static_cast<float>(kSampleRate);

    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / rate;
        float env = voice.envelope.valueAt(t, duration);
        if (voice.exponential) {
            env *= std::exp(-4.0f * t);
        }

        phaseA += frequency / rate;
        float value = waveform(voice.wave, phaseA, static_cast<std::uint32_t>(i));
        if (detuned) {
            phaseB += frequency * detuneRatio / rate;
            value = value * 0.7f + waveform(voice.wave, phaseB, static_cast<std::uint32_t>(i) + 7919u) * 0.3f;
        }

        const float sample = value * env * amplitude * voice.gain;
        out[static_cast<std::size_t>(i) * 2] += sample;
        out[static_cast<std::size_t>(i) * 2 + 1] += sample;
    }
}

// ---------------------------------------------------------------------------
// MusicSynth
// ---------------------------------------------------------------------------
void MusicSynth::reset() noexcept
{
    m_time = 0.0f;
    m_intensity = 0.0f;
    m_brightness = 1.0f;
    m_eraBlend = 1.0f;
    m_step = 0;
    m_tremolo = 0.0f;
    m_leadPhase = 0.0f;
    m_padFilter  = 0.0f;
    m_leadFilter = 0.0f;
    m_bassPhase  = 0.0f;
    m_padPhase[0] = m_padPhase[1] = m_padPhase[2] = 0.0f;
}

void MusicSynth::advance(float seconds, const MusicParams& params) noexcept
{
    // Parameters are eased rather than set, so changing the era and the combat
    // intensity in the same frame produces one glide instead of two jumps. This
    // is what replaces a crossfade between two tracks.
    const float rate = std::min(1.0f, seconds * 3.0f);
    m_intensity += (params.intensity - m_intensity) * rate;
    m_eraBlend   += (params.eraBlend - m_eraBlend) * rate;
    m_tempo     += (std::max(20.0f, params.tempo) - m_tempo) * rate;

    const EraScale target = scaleFor(params.era);
    m_brightness += (target.brightness - m_brightness) * rate;

    // The eased tempo, not the requested one: this is the clock that decides when
    // the next note fires, and moving it instantly is what used to click.
    const float secondsPerStep = 60.0f / std::max(20.0f, m_tempo) * 0.5f;
    m_time += seconds;
    while (m_time >= secondsPerStep) {
        m_time -= secondsPerStep;
        ++m_step;
    }
}

void MusicSynth::render(MusicParams params, int frames, SampleBuffer& out)
{
    // The guard comes before the resize on purpose. `frames` is a signed int, and
    // casting a negative one to size_t produces a number near the address space
    // size, so checking afterwards is not a cheap mistake to make.
    if (frames <= 0) {
        out.clear();
        return;
    }
    out.assign(static_cast<std::size_t>(frames) * 2, 0.0f);

    const float seconds = static_cast<float>(frames) / static_cast<float>(kSampleRate);
    advance(seconds, params);

    const EraScale scale = scaleFor(params.era);
    // The same eased clock `advance` just ran, rather than a second one derived
    // from the raw parameter. Two clocks reading the same value is fine until
    // the value changes, and then they disagree about where the bar is.
    const float secondsPerStep = 60.0f / std::max(20.0f, m_tempo) * 0.5f;
    const float intensity = Graphics::clampValue(m_intensity, 0.0f, 1.0f);
    const float instability = Graphics::clampValue(params.instability, 0.0f, 1.0f);

    const float padCut  = 0.02f + m_brightness * 0.10f * (1.0f + intensity);
    const float leadCut = 0.04f + m_brightness * 0.28f * (1.0f + intensity * 2.0f);

    const float beat = static_cast<float>(m_time) / std::max(0.01f, secondsPerStep);
    const int   bar   = static_cast<int>(std::floor(beat / 8.0f));
    const float leadPhase = beat * 4.0f;
    const int   leadStep  = static_cast<int>(std::floor(leadPhase));

    // The chord turns over each bar; the arpeggio walks the scale in eighths.
    m_padTarget[0] = static_cast<float>(scale.root + scale.degrees[(bar * 2 + 0) % scale.degreeCount]);
    m_padTarget[1] = static_cast<float>(scale.root + scale.degrees[(bar * 2 + 2) % scale.degreeCount]);
    m_padTarget[2] = static_cast<float>(scale.root + scale.degrees[(bar * 2 + 4) % scale.degreeCount]) + 12.0f;

    m_leadTarget = static_cast<float>(scale.root + scale.degrees[leadStep % scale.degreeCount]) +
                   ((leadStep / scale.degreeCount) % 2) * 12.0f;

    // The filter states are members, deliberately. As locals they were rebuilt
    // from zero on every call, which is a one-pole filter that re-attacks instead
    // of filters: a step discontinuity at every block boundary (a click, once per
    // fixed step, ~60/s) plus a bright, pumping pad that was never actually
    // filtered. The ambience filters were members all along, which is exactly why
    // the ambience never clicked and the music did.
    const float rate = static_cast<float>(kSampleRate);

    for (int i = 0; i < frames; ++i) {
        // --- pad: a slow triad ----------------------------------------------
        float pad = 0.0f;
        for (int voice = 0; voice < 3; ++voice) {
            const float frequency = noteFrequency(static_cast<int>(std::lround(m_padTarget[voice])));
            m_padPhase[static_cast<std::size_t>(voice)] += frequency / rate;
            const float raw = waveform(Voice::Wave::Triangle, m_padPhase[static_cast<std::size_t>(voice)],
                                       static_cast<std::uint32_t>(i));
            m_padFilter += (raw - m_padFilter) * padCut;
            pad += m_padFilter;
        }
        pad /= 3.0f;

        // --- lead: an arpeggio that only exists under pressure --------------
        float lead = 0.0f;
        if (intensity > 0.05f) {
            const float frequency = noteFrequency(static_cast<int>(std::lround(m_leadTarget)));
            m_leadPhase += frequency / rate;
            // Short envelope per eighth so notes articulate instead of blurring.
            const float local = leadPhase - std::floor(leadPhase);
            // A note that starts at full amplitude is a step, which is a click.
            // Ramping the first few percent of the note costs nothing and is the
            // difference between an arpeggio and a string of ticks.
            const float attack = Graphics::clampValue(local / 0.04f, 0.0f, 1.0f);
            const float env = (0.35f + 0.65f * intensity) * attack *
                              std::max(0.0f, 1.0f - local);
            const float raw = waveform(Voice::Wave::Saw, m_leadPhase, static_cast<std::uint32_t>(i));
            m_leadFilter += (raw - m_leadFilter) * leadCut;
            lead = m_leadFilter * env;
        }

        // --- temporal instability --------------------------------------------
        m_tremolo += (seconds / static_cast<float>(frames)) * 6.0f;
        const float wobble =
            1.0f + instability * 0.35f * std::sin(kTwoPi * static_cast<float>(m_tremolo));

        // A low bass note under everything, so combat has weight. Its phase is
        // accumulated rather than read from `m_time`, which is the *beat* clock
        // and is deliberately wrapped every step: feeding that to an oscillator
        // made the note jump discontinuously a few times a second.
        const float bassFreq = noteFrequency(static_cast<int>(std::lround(m_padTarget[0] - 12.0f)));
        m_bassPhase += bassFreq / rate;
        const float bass = std::sin(kTwoPi * m_bassPhase) * (0.10f + 0.18f * intensity);

        const float mix = (pad * 0.55f + lead * 0.30f + bass) * params.gain * wobble;

        // Cheap width: the channels differ by a hair, which is enough to open up
        // the pad without smearing the centre the way a hard pan would.
        out[static_cast<std::size_t>(i) * 2] += mix;
        out[static_cast<std::size_t>(i) * 2 + 1] += mix * (1.0f + scale.detune * (1.0f - instability) * 0.03f);
    }

    // Clamp, because summing a pad, an arpeggio, a bass and a detune can exceed
    // unity and a clipping mixer turns a gentle bed into a buzz.
    for (float& sample : out) {
        sample = Graphics::clampValue(sample, -1.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// AmbienceSynth
// ---------------------------------------------------------------------------
void AmbienceSynth::reset() noexcept
{
    m_time = 0.0f;
    m_noiseState = 0.0f;
    m_filter = 0.0f;
    m_filter2 = 0.0f;
    m_motif = 0.0f;
    m_motifIndex = 0;
    m_eraBlend = 1.0f;
    m_framesUntilMote = 0;
}

void AmbienceSynth::render(AmbienceParams params, int frames, SampleBuffer& out)
{
    if (frames <= 0) {
        out.clear();
        return;
    }
    out.assign(static_cast<std::size_t>(frames) * 2, 0.0f);

    const float seconds = static_cast<float>(frames) / static_cast<float>(kSampleRate);
    m_time += seconds;
    m_eraBlend += (Graphics::clampValue(params.eraBlend, 0.0f, 1.0f) - m_eraBlend) *
                  std::min(1.0f, seconds * 3.0f);

    const EraScale scale = scaleFor(params.era);

    // The same noise through a different filter and a different motif per era.
    // That is enough for the three beds to be identifiable with your eyes shut,
    // which is the actual requirement.
    float windCutoff = 0.10f;
    float windDepth  = 0.9f;
    int   motifEvery = 6;      ///< seconds
    switch (params.era) {
        case Era::Past:
            windCutoff = 0.10f;   // leaves and open air
            windDepth  = 0.9f;
            motifEvery = 7;
            break;
        case Era::Present:
            windCutoff = 0.045f;  // low, through stone
            windDepth  = 0.6f;
            motifEvery = 9;
            break;
        case Era::Future:
            windCutoff = 0.16f;   // thin, synthetic
            windDepth  = 0.8f;
            motifEvery = 4;
            break;
    }
    windCutoff *= 1.0f + params.instability * 1.5f;

    m_framesUntilMote -= frames;
    if (m_framesUntilMote <= 0) {
        m_framesUntilMote = motifEvery * kSampleRate / 2;
        m_motifIndex      = (m_motifIndex + 1) % scale.degreeCount;
        m_motif           = static_cast<float>(scale.root + scale.degrees[m_motifIndex]) +
                             (params.era == Era::Past ? 12.0f : 0.0f);
    }
    m_motif *= std::exp(-seconds / 2.0f);

    const float instability = Graphics::clampValue(params.instability, 0.0f, 1.0f);

    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(m_time) +
                        static_cast<float>(i) / static_cast<float>(kSampleRate);
        const std::uint32_t index =
            static_cast<std::uint32_t>(t * 48000.0f);
        const float noise = hashNoise(index);
        m_noiseState = m_noiseState * 0.92f + noise * 0.08f;

        // Two cascaded one-pole filters approximate a band-pass, which is what
        // makes the noise read as wind rather than as static.
        m_filter  = m_filter * (1.0f - windCutoff) + noise * windCutoff;
        m_filter2 = m_filter2 * (1.0f - windCutoff * 0.4f) + m_filter * windCutoff * 0.4f;
        const float wind = (m_filter2 - m_filter * 0.5f) * windDepth;

        const float swell = 0.75f + 0.25f * std::sin(kTwoPi * 0.07f * static_cast<float>(m_time));

        float tone = 0.0f;
        if (std::fabs(m_motif) > 0.01f) {
            const float f = noteFrequency(static_cast<int>(std::lround(m_motif)));
            tone = std::sin(kTwoPi * f * static_cast<float>(m_time)) * 0.10f;
            if (params.era == Era::Future) {
                // The Future's motif bends as it decays.
                tone += std::sin(kTwoPi * f * 1.5f * static_cast<float>(m_time)) * 0.05f;
            }
        }

        // Instability drops the bed out entirely for a moment.
        const float drop =
            (instability > 0.35f &&
             hashNoise(static_cast<std::uint32_t>(m_time * 7.0f)) > 0.995f - instability * 0.05f)
                ? 0.25f
                : 1.0f;

        // Output scale, applied before the user's gain so the gain stays a clean
        // 0..1 control.
        //
        // The bed is a difference of two heavily-filtered noise signals, so its
        // natural amplitude is small: measured peak 0.18, about -15dBFS. Under
        // the music bus that lands near -28dBFS at the sink, which is not quiet
        // music, it is inaudible. Scaling here rather than raising `gain` keeps
        // the clamp inside `render` out of the way - `gain` at 2.5 would hit the
        // +/-1 clamp and turn gusts into distortion.
        constexpr float kOutputScale = 2.6f;
        const float mix = (wind * swell + tone) * kOutputScale * params.gain * drop;
        out[static_cast<std::size_t>(i) * 2] += mix;
        out[static_cast<std::size_t>(i) * 2 + 1] += mix * (1.0f - instability * 0.1f);
    }

    for (float& sample : out) {
        sample = Graphics::clampValue(sample, -1.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// One-shots
// ---------------------------------------------------------------------------
float sfxDuration(Sfx sfx) noexcept
{
    switch (sfx) {
        case Sfx::UiMove:
        case Sfx::UiConfirm:
        case Sfx::UiDeny:       return 0.12f;
        case Sfx::Jump:         return 0.22f;
        case Sfx::Land:         return 0.18f;
        case Sfx::Dash:         return 0.30f;
        case Sfx::Attack:       return 0.24f;
        case Sfx::HitEnemy:     return 0.20f;
        case Sfx::EnemyHurt:    return 0.18f;
        case Sfx::EnemyDie:     return 0.55f;
        case Sfx::PlayerHurt:   return 0.40f;
        case Sfx::PlayerDie:    return 1.20f;
        case Sfx::PickupChrono: return 0.45f;
        case Sfx::PickupHealth: return 0.45f;
        case Sfx::SealTaken:    return 0.90f;
        case Sfx::GateOpened:   return 1.60f;
        case Sfx::ShiftCharge:  return 0.35f;
        case Sfx::ShiftImpact:  return 0.80f;
        case Sfx::Hazard:       return 0.30f;
        case Sfx::FootstepStone:   return 0.16f;
        case Sfx::FootstepGrass:   return 0.18f;
        case Sfx::FootstepSand:    return 0.20f;
        case Sfx::FootstepMetal:   return 0.30f;
        case Sfx::FootstepCrystal: return 0.26f;
        case Sfx::Victory:      return 1.60f;
    }
    return 0.2f;
}

std::string_view toString(Sfx sfx) noexcept
{
    switch (sfx) {
        case Sfx::UiMove:        return "UiMove";
        case Sfx::UiConfirm:     return "UiConfirm";
        case Sfx::UiDeny:        return "UiDeny";
        case Sfx::Jump:          return "Jump";
        case Sfx::Land:          return "Land";
        case Sfx::Dash:          return "Dash";
        case Sfx::Attack:        return "Attack";
        case Sfx::HitEnemy:      return "HitEnemy";
        case Sfx::EnemyHurt:     return "EnemyHurt";
        case Sfx::EnemyDie:      return "EnemyDie";
        case Sfx::PlayerHurt:    return "PlayerHurt";
        case Sfx::PlayerDie:     return "PlayerDie";
        case Sfx::PickupChrono:  return "PickupChrono";
        case Sfx::PickupHealth:  return "PickupHealth";
        case Sfx::SealTaken:     return "SealTaken";
        case Sfx::GateOpened:    return "GateOpened";
        case Sfx::ShiftCharge:   return "ShiftCharge";
        case Sfx::ShiftImpact:   return "ShiftImpact";
        case Sfx::Hazard:        return "Hazard";
        case Sfx::FootstepStone:   return "FootstepStone";
        case Sfx::FootstepGrass:   return "FootstepGrass";
        case Sfx::FootstepSand:    return "FootstepSand";
        case Sfx::FootstepMetal:   return "FootstepMetal";
        case Sfx::FootstepCrystal: return "FootstepCrystal";
        case Sfx::Victory:       return "Victory";
    }
    return "?";
}

void renderSfx(Sfx sfx, int sampleRate, SampleBuffer& out)
{
    if (sampleRate <= 0) {
        out.clear();
        return;
    }
    const float duration = sfxDuration(sfx);
    const int frames = std::max(1, static_cast<int>(duration * static_cast<float>(sampleRate)));
    out.assign(static_cast<std::size_t>(frames) * 2, 0.0f);

    const float rate = static_cast<float>(sampleRate);

    /// A pitched gesture: sweeps `fromHz` to `toHz` over `length` seconds.
    const auto addTone = [&](float fromHz, float toHz, float amp, float shape, float start,
                             float length) {
        float phase = 0.0f;
        for (int i = 0; i < frames; ++i) {
            const float t = static_cast<float>(i) / rate;
            if (t < start || t > start + length) {
                continue;
            }
            const float local = (t - start) / length;
            phase += (fromHz + (toHz - fromHz) * local) / rate;
            const float value = std::sin(kTwoPi * phase) * amp * std::exp(-shape * local);
            out[static_cast<std::size_t>(i) * 2] += value;
            out[static_cast<std::size_t>(i) * 2 + 1] += value;
        }
    };

    /// A band-passed noise gesture: impacts, whooshes, rubble.
    ///
    /// This was a *single* one-pole low-pass with a coefficient of 0.45-0.60,
    /// which is 6dB/octave - so white noise through it is still overwhelmingly
    /// hiss. The swing whoosh and the dash were, to the ear, static. Measured, the
    /// swing was 54% energy above the filter corner and the dash 61%, and a
    /// capture of a fight had *no musical peaks at all*: the noise layer had
    /// smeared over the music entirely. Muting the SFX brought the peaks straight
    /// back, which is what identified it.
    ///
    /// A band-pass is `high-pass then low-pass`: subtract a low-passed copy to
    /// remove the rumble, then low-pass what is left to remove the hiss. What
    /// survives is the mid band, which is where an impact actually lives.
    ///
    /// `lowCut` and `highCut` are one-pole coefficients, so the corner is roughly
    /// `coefficient * sampleRate / 2pi`.
    const auto addNoise = [&](float amp, float shape, float length, float lowCut, float highCut) {
        LowPass rumble;
        LowPass top;
        for (int i = 0; i < frames; ++i) {
            const float t = static_cast<float>(i) / rate;
            if (t > length) {
                break;
            }
            const float local = t / length;
            const std::uint32_t index = static_cast<std::uint32_t>(t * static_cast<float>(sampleRate)) +
                                        static_cast<std::uint32_t>(sfx) * 104729u;
            const float raw = hashNoise(index);
            // High-pass by subtracting a low-passed copy...
            const float band = raw - rumble.process(raw, lowCut);
            // ...then tame the top, which is what makes it an impact and not a hiss.
            const float value = top.process(band, highCut) * amp * std::exp(-shape * local);
            out[static_cast<std::size_t>(i) * 2] += value;
            out[static_cast<std::size_t>(i) * 2 + 1] += value;
        }
    };

    switch (sfx) {
        case Sfx::UiMove:    addTone(880.0f, 880.0f, 0.12f, 6.0f, 0.0f, duration); break;
        case Sfx::UiConfirm: addTone(660.0f, 990.0f, 0.16f, 5.0f, 0.0f, duration); break;
        case Sfx::UiDeny:    addTone(300.0f, 180.0f, 0.18f, 8.0f, 0.0f, duration); break;

        case Sfx::Jump: addTone(320.0f, 620.0f, 0.16f, 7.0f, 0.0f, duration); break;
        case Sfx::Land:
            addNoise(0.22f, 14.0f, duration, 0.04f, 0.10f);
            addTone(120.0f, 70.0f, 0.14f, 9.0f, 0.0f, duration);
            break;
        case Sfx::Dash: addNoise(0.20f, 4.0f, duration, 0.05f, 0.20f); break;

        // Footsteps, one per material. Each is built from the two things that
        // actually distinguish a surface by ear: how much broadband noise it
        // makes, and how much pitched ring sits on top of it.
        //
        //   stone    hard, bright noise, almost no ring
        //   grass    soft and dark, the noise barely there
        //   sand     the quietest and the dullest, plus a slow swell
        //   metal    noise transient plus two inharmonic partials that ring on
        //   crystal  glassy, high, and slightly detuned so it shimmers
        //
        // Levels are set so every one of them is actually audible. The first
        // versions of the soft two were 15dB below the stone step - quiet enough
        // to be inaudible in play - which is what the `rms > 0.01` floor in the
        // synth tests caught. Material variety is carried by timbre and by a
        // modest level spread, not by making half the surfaces inaudible.
        case Sfx::FootstepStone:
            addNoise(0.30f, 20.0f, duration, 0.02f, 0.09f);
            addTone(150.0f, 95.0f, 0.10f, 13.0f, 0.0f, duration);
            break;
        case Sfx::FootstepGrass:
            addNoise(0.42f, 5.0f, duration, 0.05f, 0.11f);
            break;
        case Sfx::FootstepSand:
            addNoise(0.27f, 2.5f, duration, 0.07f, 0.13f);
            break;
        case Sfx::FootstepMetal:
            addNoise(0.09f, 24.0f, 0.06f, 0.01f, 0.06f);
            addTone(1240.0f, 1235.0f, 0.26f, 3.0f, 0.0f, duration);
            addTone(1867.0f, 1861.0f, 0.18f, 4.0f, 0.0f, duration);
            break;
        case Sfx::FootstepCrystal:
            addNoise(0.055f, 26.0f, 0.05f, 0.01f, 0.05f);
            addTone(1760.0f, 2340.0f, 0.20f, 2.0f, 0.0f, duration);
            addTone(2640.0f, 2630.0f, 0.12f, 5.0f, 0.0f, duration);
            break;

        case Sfx::Attack:
            addNoise(0.24f, 10.0f, 0.16f, 0.06f, 0.22f);
            addTone(900.0f, 420.0f, 0.10f, 11.0f, 0.0f, 0.16f);
            break;
        case Sfx::HitEnemy:
            addNoise(0.30f, 18.0f, 0.14f, 0.03f, 0.15f);
            addTone(180.0f, 90.0f, 0.20f, 14.0f, 0.0f, 0.16f);
            break;
        case Sfx::EnemyHurt: addTone(420.0f, 260.0f, 0.16f, 12.0f, 0.0f, duration); break;
        case Sfx::EnemyDie:
            addNoise(0.26f, 7.0f, 0.45f, 0.03f, 0.10f);
            addTone(300.0f, 60.0f, 0.22f, 6.0f, 0.0f, 0.5f);
            break;

        case Sfx::PlayerHurt:
            addTone(260.0f, 130.0f, 0.26f, 8.0f, 0.0f, duration);
            addNoise(0.14f, 10.0f, 0.2f, 0.02f, 0.08f);
            break;
        case Sfx::PlayerDie:
            addTone(220.0f, 55.0f, 0.30f, 3.0f, 0.0f, duration);
            addNoise(0.20f, 3.0f, 0.6f, 0.015f, 0.06f);
            break;

        case Sfx::PickupChrono:
            addTone(700.0f, 1400.0f, 0.14f, 6.0f, 0.0f, 0.22f);
            addTone(1050.0f, 2100.0f, 0.08f, 7.0f, 0.05f, 0.2f);
            break;
        case Sfx::PickupHealth:
            addTone(520.0f, 780.0f, 0.14f, 5.0f, 0.0f, 0.25f);
            addTone(780.0f, 1170.0f, 0.10f, 5.0f, 0.06f, 0.22f);
            break;
        case Sfx::SealTaken:
            // A rising perfect fifth: unmistakably a "you did a thing" sound.
            addTone(392.0f, 392.0f, 0.16f, 2.0f, 0.00f, 0.5f);
            addTone(587.0f, 587.0f, 0.14f, 2.0f, 0.12f, 0.5f);
            addTone(784.0f, 784.0f, 0.12f, 2.0f, 0.24f, 0.5f);
            break;
        case Sfx::GateOpened:
            addTone(196.0f, 392.0f, 0.20f, 1.5f, 0.0f, 1.2f);
            addTone(294.0f, 588.0f, 0.14f, 1.5f, 0.2f, 1.0f);
            addTone(392.0f, 784.0f, 0.10f, 1.5f, 0.4f, 0.9f);
            break;

        case Sfx::ShiftCharge:
            // Rising, and widening: two tones a semitone apart climbing together.
            addTone(180.0f, 900.0f, 0.14f, 2.0f, 0.0f, 0.35f);
            addTone(181.5f, 906.0f, 0.10f, 2.0f, 0.0f, 0.35f);
            break;
        case Sfx::ShiftImpact:
            // The signature: a low impact under a burst of distortion.
            addTone(90.0f, 40.0f, 0.34f, 5.0f, 0.0f, 0.7f);
            addNoise(0.20f, 6.0f, 0.35f, 0.04f, 0.18f);
            addTone(1400.0f, 300.0f, 0.12f, 9.0f, 0.0f, 0.4f);
            break;

        case Sfx::Hazard:
            addNoise(0.24f, 12.0f, 0.22f, 0.08f, 0.30f);
            addTone(500.0f, 200.0f, 0.12f, 10.0f, 0.0f, 0.22f);
            break;
        case Sfx::Victory:
            addTone(392.0f, 392.0f, 0.18f, 1.2f, 0.00f, 0.9f);
            addTone(523.0f, 523.0f, 0.16f, 1.2f, 0.15f, 0.9f);
            addTone(659.0f, 659.0f, 0.14f, 1.2f, 0.30f, 0.9f);
            addTone(784.0f, 784.0f, 0.12f, 1.2f, 0.45f, 0.9f);
            break;
    }

    for (float& sample : out) {
        sample = Graphics::clampValue(sample * 0.85f, -1.0f, 1.0f);
    }

    applyEdgeFades(out, sampleRate);
}

void applyEdgeFades(SampleBuffer& buffer, int sampleRate)
{
    // Every one-shot starts at full amplitude and stops the instant its length
    // runs out, so both ends are a step discontinuity - which is a click. Twenty
    // sounds times two ends, and during combat the demo fires them often enough
    // to measure: 17 transients a second in a capture, none of which came from
    // the music or ambience beds.
    //
    // They were inaudible while the whole mix sat 20dB too low, which is exactly
    // the trap: fixing the level exposed a defect that had been there all along.
    // It is worth knowing that fixing an unrelated thing can unmask a second bug
    // that was previously hidden by the first.
    //
    // A single fade over the whole buffer, rather than a per-voice envelope, so
    // that every sound gets click-free edges however it happens to be built.
    // Attack is short (a percussive hit must still read as a hit); release is
    // longer (a truncated tail is the more audible of the two). Both are capped as
    // a fraction of the length so the 120ms UI blip is not mostly fade.
    const std::size_t frames = buffer.size() / 2;
    if (frames == 0 || sampleRate <= 0) {
        return;
    }
    constexpr float kAttackSeconds  = 0.0015f;   ///< 1.5ms
    constexpr float kReleaseSeconds = 0.012f;    ///< 12ms
    constexpr float kMaxEdgeFraction = 0.25f;    ///< Never fade more than a quarter.

    const auto framesFor = [&](float seconds) {
        const auto count = static_cast<std::size_t>(seconds * static_cast<float>(sampleRate));
        const auto cap = static_cast<std::size_t>(static_cast<float>(frames) * kMaxEdgeFraction);
        return std::min(std::max<std::size_t>(count, 1), std::max<std::size_t>(cap, 1));
    };

    const std::size_t attack = framesFor(kAttackSeconds);
    const std::size_t release = framesFor(kReleaseSeconds);

    for (std::size_t i = 0; i < frames; ++i) {
        float gain = 1.0f;
        if (i < attack) {
            // Smoothstep rather than a linear ramp: a linear ramp still has a
            // slope discontinuity at each end, which is a smaller click.
            const float t = static_cast<float>(i) / static_cast<float>(attack);
            gain *= t * t * (3.0f - 2.0f * t);
        }
        const std::size_t fromEnd = frames - 1 - i;
        if (fromEnd < release) {
            const float t = static_cast<float>(fromEnd) / static_cast<float>(release);
            gain *= t * t * (3.0f - 2.0f * t);
        }
        if (gain < 1.0f) {
            buffer[i * 2] *= gain;
            buffer[i * 2 + 1] *= gain;
        }
    }
}

} // namespace EraShift::Audio
