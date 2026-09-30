// Tests for the synthesiser.
//
// The synthesiser is the one part of the presentation stack that lives in Core,
// and the reason is this file: it is pure arithmetic, so it can be tested
// without a sound card, a window, or a mixer. A `.wav` could not be.
//
// What is worth asserting is mostly *not* "does it make a noise". It is that the
// audio does something audible and deliberate: that it is never silent when it
// should not be, never clipping when it should not be, and that a shift of
// parameters produces a measurably different result. Those are the properties
// that break silently when someone tunes a constant.

#include "EraShift/Audio/Synth.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <doctest/doctest.h>

using namespace EraShift;
using namespace EraShift::Audio;
using EraShift::Game::Era;

namespace {

/// Peak absolute sample, which is the number a mixer would clip on.
float peak(const SampleBuffer& buffer)
{
    float high = 0.0f;
    for (const float sample : buffer) {
        high = std::max(high, std::fabs(sample));
    }
    return high;
}

/// Root-mean-square level. A buffer can have a high peak and be nearly silent, so
/// a test that only checks the peak will pass on a sound that is inaudible.
float rms(const SampleBuffer& buffer)
{
    if (buffer.empty()) {
        return 0.0f;
    }
    double total = 0.0;
    for (const float sample : buffer) {
        total += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return static_cast<float>(std::sqrt(total / static_cast<double>(buffer.size())));
}

/// Counts zero crossings, a cheap proxy for "is there actually a tone here".
int zeroCrossings(const SampleBuffer& buffer, int stride = 2)
{
    int count = 0;
    float previous = 0.0f;
    for (std::size_t i = 0; i < buffer.size(); i += static_cast<std::size_t>(stride)) {
        const float sample = buffer[i];
        if ((sample > 0.0f) != (previous > 0.0f)) {
            ++count;
        }
        previous = sample;
    }
    return count;
}

/// Renders a one second block of the music bed.
SampleBuffer renderMusic(MusicParams params)
{
    MusicSynth synth;
    SampleBuffer out;
    synth.render(params, kSampleRate, out);
    return out;
}

} // namespace

TEST_CASE("the three eras are in genuinely different keys and scales")
{
    const EraScale past    = scaleFor(Era::Past);
    const EraScale present = scaleFor(Era::Present);
    const EraScale future  = scaleFor(Era::Future);

    CHECK(past.root != present.root);
    CHECK(present.root != future.root);

    // The Past's pentatonic has five degrees, the Present's minor seven, the
    // Future's whole tone six. If these ever collapse to the same count the eras
    // stop sounding like different places, which is the entire job.
    CHECK(past.degreeCount == 5);
    CHECK(present.degreeCount == 7);
    CHECK(future.degreeCount == 6);

    // A whole tone is symmetrical: no semitone, so nothing ever resolves. That
    // is the Future's character and it is a property of the numbers, not taste.
    for (int i = 0; i < 5; ++i) {
        CHECK(future.degrees[i + 1] - future.degrees[i] == 2);
    }

    // The Future is also the brightest and the least detuned-in-tune.
    CHECK(future.brightness > present.brightness);
    CHECK(present.brightness > past.brightness);
}

TEST_CASE("equal temperament puts A4 at 440Hz")
{
    CHECK(noteFrequency(0) == doctest::Approx(440.0f));
    CHECK(noteFrequency(12) == doctest::Approx(880.0f));
    CHECK(noteFrequency(-12) == doctest::Approx(220.0f));
    CHECK(noteFrequency(7) == doctest::Approx(440.0f * std::pow(2.0f, 7.0f / 12.0f)));
}

TEST_CASE("an envelope opens, holds and closes")
{
    Envelope envelope;
    envelope.attack  = 0.1f;
    envelope.decay   = 0.2f;
    envelope.sustain = 0.5f;
    envelope.release = 0.5f;
    constexpr float duration = 0.3f;

    CHECK(envelope.valueAt(0.0f, duration) == doctest::Approx(0.0f));
    CHECK(envelope.valueAt(0.05f, duration) == doctest::Approx(0.5f));
    CHECK(envelope.valueAt(0.1f, duration) == doctest::Approx(1.0f));   // end of attack
    CHECK(envelope.valueAt(0.2f, duration) == doctest::Approx(0.75f));  // half way through decay
    CHECK(envelope.valueAt(0.3f, duration) == doctest::Approx(0.5f));  // sustain
    // Past the end it releases, rather than cutting off mid-tone: 0.3s into a
    // 0.5s release is 40% of the way down from the sustain level.
    CHECK(envelope.valueAt(0.6f, duration) == doctest::Approx(0.5f * (1.0f - 0.3f / 0.5f)));
    CHECK(envelope.valueAt(1.0f, duration) == doctest::Approx(0.0f));

    // A zero-length attack is an instant onset rather than a division by zero.
    Envelope instant;
    instant.attack = 0.0f;
    CHECK(instant.valueAt(0.0f, 0.3f) == doctest::Approx(1.0f));
}

TEST_CASE("every one-shot renders sound and none of them clip")
{
    // Every value of the enum, so adding one and forgetting to implement it is a
    // compile-time or test-time failure rather than silence in the game.
    for (int i = 0; i <= static_cast<int>(Sfx::Victory); ++i) {
        const auto sfx = static_cast<Sfx>(i);
        SampleBuffer buffer;
        renderSfx(sfx, kSampleRate, buffer);

        CAPTURE(toString(sfx));
        const int expectedFrames =
            static_cast<int>(sfxDuration(sfx) * static_cast<float>(kSampleRate));
        CHECK(buffer.size() == static_cast<std::size_t>(expectedFrames) * 2);
        CHECK(rms(buffer) > 0.01f);
        CHECK(peak(buffer) <= 1.0f);
        // A one-shot that is pure DC has a level but no sound.
        CHECK(zeroCrossings(buffer) > 8);
    }
}

TEST_CASE("one-shots are actually different from each other")
{
    // Two sounds that are byte-identical mean a copy-paste in the switch, which
    // is the classic way an eight-way audio table quietly becomes a two-way one.
    std::vector<std::vector<float>> rendered;
    for (int i = 0; i <= static_cast<int>(Sfx::Victory); ++i) {
        SampleBuffer buffer;
        renderSfx(static_cast<Sfx>(i), kSampleRate, buffer);
        rendered.push_back(std::move(buffer));
    }

    for (std::size_t a = 0; a < rendered.size(); ++a) {
        for (std::size_t b = a + 1; b < rendered.size(); ++b) {
            CAPTURE(a);
            CAPTURE(b);
            const std::size_t n = std::min(rendered[a].size(), rendered[b].size());
            double difference = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const double d = static_cast<double>(rendered[a][i]) -
                                 static_cast<double>(rendered[b][i]);
                difference += d * d;
            }
            // A small floor, because two very short clicks can legitimately be
            // close; anything smaller than this is the same sound twice.
            CHECK(std::sqrt(difference / static_cast<double>(n)) > 0.005);
        }
    }
}

TEST_CASE("the music bed runs, stays in range and is the right size")
{
    MusicParams params;
    params.era = Era::Present;
    const SampleBuffer block = renderMusic(params);

    CHECK(block.size() == static_cast<std::size_t>(kSampleRate) * 2);
    CHECK(rms(block) > 0.005f);
    CHECK(peak(block) <= 1.0f);
}

TEST_CASE("combat is louder and busier than exploring")
{
    MusicParams exploring;
    exploring.intensity = 0.0f;
    MusicParams combat;
    combat.intensity = 1.0f;

    const SampleBuffer quiet = renderMusic(exploring);
    const SampleBuffer loud  = renderMusic(combat);

    // The arpeggio only exists under pressure, so combat is not just louder, it
    // has content the exploration bed does not. That is what makes the state
    // change legible before the player has consciously noticed it.
    CHECK(rms(loud) > rms(quiet));

    MusicSynth synth;
    SampleBuffer warmup;
    synth.render(exploring, kSampleRate / 4, warmup);
    // Parameters are eased, not set, so the very first block of a fresh synth is
    // not yet the requested intensity. Advancing it is how the glide happens.
    CHECK(synth.intensity() < 1.0f);
    SampleBuffer settled;
    synth.render(combat, kSampleRate, settled);
    CHECK(synth.intensity() > 0.9f);
}

TEST_CASE("a tempo change moves the bed's clock")
{
    MusicSynth slow;
    MusicSynth fast;
    MusicParams slowParams;
    slowParams.tempo = 60.0f;
    MusicParams fastParams = slowParams;
    fastParams.tempo = 180.0f;

    SampleBuffer buffer;
    slow.render(slowParams, kSampleRate, buffer);
    slow.render(slowParams, kSampleRate, buffer);
    const float slowPosition = slow.positionSeconds();

    fast.render(fastParams, kSampleRate, buffer);
    fast.render(fastParams, kSampleRate, buffer);
    CHECK(fast.positionSeconds() != doctest::Approx(slowPosition));
}

TEST_CASE("instability is audible and bounded")
{
    MusicParams calm;
    calm.instability = 0.0f;
    MusicParams unstable = calm;
    unstable.instability = 1.0f;

    const SampleBuffer steady = renderMusic(calm);
    const SampleBuffer shaky  = renderMusic(unstable);

    // The tremolo widens the dynamic range, so the peak-to-rms ratio grows. This
    // is a proxy for "you can hear the instability", which is the actual
    // requirement, rather than asserting on a synthesis constant.
    CHECK(peak(shaky) / std::max(0.001f, rms(shaky)) >
          peak(steady) / std::max(0.001f, rms(steady)));
    CHECK(peak(shaky) <= 1.0f);
}

TEST_CASE("every era produces a distinct ambience bed")
{
    std::vector<std::vector<float>> beds;
    for (const Game::Era era : {Era::Past, Era::Present, Era::Future}) {
        AmbienceParams params;
        params.era = era;
        AmbienceSynth synth;
        SampleBuffer buffer;
        // A couple of seconds, because a motif only fires every few seconds and
        // a one-block render would not exercise it.
        synth.render(params, kSampleRate * 3, buffer);
        CHECK(rms(buffer) > 0.002f);
        CHECK(peak(buffer) <= 1.0f);
        beds.push_back(std::move(buffer));
    }
    for (std::size_t a = 0; a < beds.size(); ++a) {
        for (std::size_t b = a + 1; b < beds.size(); ++b) {
            double difference = 0.0;
            for (std::size_t i = 0; i < beds[a].size(); ++i) {
                const double d = static_cast<double>(beds[a][i]) - static_cast<double>(beds[b][i]);
                difference += d * d;
            }
            CAPTURE(a);
            CAPTURE(b);
            CHECK(std::sqrt(difference / static_cast<double>(beds[a].size())) > 0.005);
        }
    }
}

TEST_CASE("ambience survives a change of era without restarting")
{
    // The bed is an instrument, not a file. A shift mid-playback should glide;
    // the position clock proves it did not reset.
    AmbienceSynth synth;
    AmbienceParams first;
    first.era = Era::Past;
    SampleBuffer buffer;
    synth.render(first, kSampleRate, buffer);
    const float after = synth.positionSeconds();

    AmbienceParams second;
    second.era = Era::Future;
    synth.render(second, kSampleRate, buffer);
    CHECK(synth.positionSeconds() > after);
}

TEST_CASE("a zero-length render is safe")
{
    // Called with a zero frame count by a frame that took no time at all, which
    // happens on a pause or a breakpoint. It must not divide by zero.
    MusicSynth music;
    SampleBuffer musicBlock;
    music.render(MusicParams{}, 0, musicBlock);
    CHECK(musicBlock.empty());

    AmbienceSynth ambience;
    SampleBuffer ambienceBlock;
    ambience.render(AmbienceParams{}, -5, ambienceBlock);
    CHECK(ambienceBlock.empty());

    // A one-shot always resizes to its own length, so a zero-length request
    // cannot shrink it; the guard is a sample rate of zero, which is what an
    // unconfigured device would report.
    renderSfx(Sfx::Jump, 0, musicBlock);
    CHECK(musicBlock.empty());
}

TEST_CASE("synthesis is deterministic")
{
    // Two synths given identical parameters produce identical output. Without
    // this, a bug report of the form "the music sounds different on my machine"
    // is not answerable.
    const auto run = [] {
        MusicSynth synth;
        MusicParams params;
        params.era      = Era::Future;
        params.intensity = 0.7f;
        SampleBuffer buffer;
        synth.render(params, 4096, buffer);
        return buffer;
    };
    CHECK(run() == run());
}
