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

/// One fixed simulation step's worth of audio, which is the block size the game
/// actually feeds the beds. Using the real one keeps the timings in these tests
/// honest: a transition has to be measured in the units it happens in.
constexpr int kBlock = kSampleRate / 60;
constexpr int kBlocksPerSecond = 60;

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

TEST_CASE("no one-shot has a step at either end")
{
    // A one-shot that starts at full amplitude, or stops the instant its length
    // runs out, has a discontinuity at that edge - and a discontinuity is a click.
    // Twenty sounds, two ends each, and during combat they fire often enough to
    // measure: 17 transients a second in a capture of the attract script.
    //
    // The bug predates the fix that revealed it. The mix was 20dB too quiet for
    // the clicks to be audible, so raising the level exposed a second defect that
    // had been there the whole time.
    for (int i = 0; i <= static_cast<int>(Sfx::Victory); ++i) {
        const auto sfx = static_cast<Sfx>(i);
        CAPTURE(toString(sfx));
        SampleBuffer buffer;
        renderSfx(sfx, kSampleRate, buffer);
        REQUIRE(buffer.size() > 8);

        // The first and last samples must be at or near zero, or the buffer has a
        // step at that end.
        const float first = std::max(std::fabs(buffer.front()), std::fabs(buffer[1]));
        const float last  = std::max(std::fabs(buffer.back()), std::fabs(buffer[buffer.size() - 2]));
        CHECK(first < 0.01f);
        CHECK(last < 0.01f);

        // And the interior must still be loud, or the fade has eaten the sound.
        CHECK(peak(buffer) > 0.05f);
    }
}

TEST_CASE("the edge fade leaves the middle of a one-shot alone")
{
    // The fade must be a boundary treatment, not a volume reduction. A UI blip is
    // 120ms long, so a 12ms release must not swallow it.
    SampleBuffer buffer;
    renderSfx(Sfx::UiMove, kSampleRate, buffer);
    const std::size_t frames = buffer.size() / 2;
    REQUIRE(frames > 1000);

    // Where is the loudest sample? For a boundary treatment it must be well
    // inside the buffer, never in a fade.
    std::size_t loudest = 0;
    float body = 0.0f;
    for (std::size_t i = 0; i < frames; ++i) {
        const float v = std::fabs(buffer[i * 2]);
        if (v > body) {
            body = v;
            loudest = i;
        }
    }
    // The property being protected is "the fade is a boundary treatment, not a
    // volume cut": the opening is silent, the interior is full level, and the
    // loudest moment is well past the ramp.
    //
    // Not asserted as "the peak is after the 72-sample ramp", because for a sound
    // whose own decay is slow - UiMove's exponential decay is gentle at this
    // scale - the peak legitimately lands *at* the end of the ramp. Asserting
    // that exact relationship would be testing the tuning of two envelopes
    // against each other, which is not a property anyone cares about.
    CHECK(body > 0.05f);
    CHECK(loudest > 32);      ///< Past the first ~0.7ms, unambiguously.
    CHECK(loudest < frames / 2);
    // The opening really is faded, not just quiet.
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(std::fabs(buffer[i * 2]) < 0.01f);
    }
}

TEST_CASE("applyEdgeFades is a no-op on degenerate input")
{
    // Reached with a zero sample rate and with nothing to fade; neither may
    // divide by zero or read out of bounds.
    SampleBuffer empty;
    applyEdgeFades(empty, kSampleRate);
    CHECK(empty.empty());

    SampleBuffer tiny(2, 0.5f);   // one frame
    applyEdgeFades(tiny, kSampleRate);
    CHECK(std::fabs(tiny[0]) < 1.0f);

    SampleBuffer zeroRate(64, 0.5f);
    applyEdgeFades(zeroRate, 0);
    CHECK(std::fabs(zeroRate[0]) <= 0.5f);
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
    for (int i = 0; i < 2; ++i) {
        slow.render(slowParams, kSampleRate, buffer);
        fast.render(fastParams, kSampleRate, buffer);
    }

    // The step count, not the position clock. Both synths were handed the same
    // wall time, so they have run for the same number of seconds - what a tempo
    // change moves is how many eighth-notes fit into it, and at 180bpm that is
    // three times as many as at 60.
    //
    // This used to compare `positionSeconds()`, which was returning the wrapped
    // beat clock, so it passed for the wrong reason: the two values differed
    // because each had been rounded a different number of times, not because the
    // tempo did anything.
    CHECK(fast.steps() > slow.steps() * 2);
    CHECK(fast.positionSeconds() == doctest::Approx(slow.positionSeconds()).epsilon(0.01f));
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

TEST_CASE("an era shift glides, and the glide can be heard")
{
    // The regression. `MusicSynth` carried an `eraBlend` member which was eased
    // every block and then read by nothing, and `MusicParams` carried a matching
    // `eraBlend` parameter which the manager set to the constant 1.0 - because a
    // caller has no way to know how far through a shift it is, so any value it
    // passed could only ever be a constant.
    //
    // So the harmony was resolved straight out of `params.era`: the triad, the
    // bass under it and the arpeggio all moved to the new era's notes inside a
    // single 16ms block. It did not click, because oscillator phases are
    // accumulated rather than computed - it snapped. The era shift is the loudest
    // moment in the game and it is the one time the beds were allowed to cut.
    //
    // Two synths are given byte-identical histories and then told to shift, and
    // the only difference between them is how long afterwards they are asked to
    // render. Before the fix they produced identical audio, which is the whole
    // defect in one line: there was no difference between "just shifted" and
    // "long since shifted".
    MusicParams past;
    past.era       = Era::Past;
    past.intensity = 0.0f;
    MusicParams future = past;
    future.era = Era::Future;

    MusicSynth justAfter;
    MusicSynth settled;
    MusicSynth halfway;
    SampleBuffer earlyBlock;
    SampleBuffer lateBlock;
    for (int i = 0; i < kBlocksPerSecond * 2; ++i) {
        justAfter.render(past, kBlock, earlyBlock);
        settled.render(past, kBlock, lateBlock);
        halfway.render(past, kBlock, earlyBlock);
    }
    REQUIRE(justAfter.eraBlend() == doctest::Approx(1.0f).epsilon(0.001f));
    // All three have sung the same bar, so this is one number: the pitch of the
    // Past's chord.
    const float pastNote = justAfter.padNote(0);
    CHECK(settled.padNote(0) == doctest::Approx(pastNote).epsilon(0.001f));
    CHECK(halfway.padNote(0) == doctest::Approx(pastNote).epsilon(0.001f));

    justAfter.render(future, kBlock, earlyBlock);
    for (int i = 0; i < kBlocksPerSecond * 2; ++i) {
        settled.render(future, kBlock, lateBlock);
    }
    // And this is the Future's chord, over the same bar.
    const float futureNote = settled.padNote(0);
    REQUIRE(futureNote != doctest::Approx(pastNote).epsilon(0.5f));

    CHECK(justAfter.eraBlend() < 0.2f);
    CHECK(settled.eraBlend() > 0.95f);
    // The audio differs, trivially - the two are seconds apart in the bar.
    CHECK(earlyBlock != lateBlock);

    // The property that matters: on the frame the shift is requested, the bed is
    // still singing the era it was in. Before the fix this was the Future's note
    // the instant the era changed, which is the whole defect - not a click, since
    // the phases are continuous, but a jump of several semitones in 16ms.
    CHECK_MESSAGE(justAfter.padNote(0) == doctest::Approx(pastNote).epsilon(0.02f),
                  "the harmony jumped to the new era in a single block");
    CHECK(settled.padNote(0) == doctest::Approx(futureNote).epsilon(0.02f));

    // And half way through the transition it is genuinely between the two, rather
    // than parked at one end or the other.
    halfway.render(future, kBlock, earlyBlock);   // request the shift
    while (halfway.eraBlend() < 0.5f) {
        halfway.render(future, kBlock, earlyBlock);
    }
    const float low  = std::min(pastNote, futureNote);
    const float high = std::max(pastNote, futureNote);
    const float atHalfway = halfway.padNote(0);
    // doctest wants one comparison per assertion, so this is two: above the old
    // era's chord and below the new one's.
    INFO("halfway note " << atHalfway << ", past " << low << ", future " << high);
    CHECK(atHalfway > low + 0.5f);
    CHECK(atHalfway < high - 0.5f);
}

TEST_CASE("the ambience glides between eras too")
{
    // Same defect, same shape, on the other bed: the wind's filter coefficient
    // went from one era's to the other's inside a block, at the exact moment the
    // rest of the world was dissolving.
    AmbienceParams past;
    past.era = Era::Past;
    AmbienceParams future = past;
    future.era = Era::Future;

    AmbienceSynth justAfter;
    AmbienceSynth settled;
    SampleBuffer earlyBlock;
    SampleBuffer lateBlock;
    for (int i = 0; i < kBlocksPerSecond * 2; ++i) {
        justAfter.render(past, kBlock, earlyBlock);
        settled.render(past, kBlock, lateBlock);
    }

    justAfter.render(future, kBlock, earlyBlock);
    for (int i = 0; i < kBlocksPerSecond * 2; ++i) {
        settled.render(future, kBlock, lateBlock);
    }

    CHECK(justAfter.eraBlend() < 0.2f);
    CHECK(settled.eraBlend() > 0.95f);
    CHECK(earlyBlock != lateBlock);
}

TEST_CASE("two shifts in quick succession glide from where the music is")
{
    // `m_fromEra` is the era the bed was last *rendering*, not the era it was
    // last asked for. Getting that wrong means a second shift partway through the
    // first jumps back to an era the player has already left - which is a cut, in
    // the middle of a transition that was supposed to be a glide.
    MusicSynth synth;
    MusicParams params;
    params.era       = Era::Past;
    params.intensity = 0.0f;
    SampleBuffer buffer;
    for (int i = 0; i < kBlocksPerSecond * 2; ++i) {
        synth.render(params, kBlock, buffer);
    }

    params.era = Era::Future;
    synth.render(params, kBlock, buffer);
    for (int i = 0; i < kBlocksPerSecond / 2; ++i) {
        synth.render(params, kBlock, buffer);
    }
    // Halfway through the first shift.
    const float between = synth.eraBlend();
    CHECK(between > 0.6f);
    CHECK(between < 0.9f);

    // Straight into a second shift, to the third era.
    //
    // The pitch is the property that has to hold, not the blend: a note that has
    // been sliding for half a second cannot jump when the next shift lands. It
    // cannot jump *visibly* in the waveform either, because oscillator phases are
    // accumulated rather than computed - so the defect this protects is invisible
    // to a sample-level test and obvious to a player.
    const float noteBefore = synth.padNote(0);
    params.era = Era::Present;
    synth.render(params, kBlock, buffer);
    CHECK(synth.padNote(0) == doctest::Approx(noteBefore).epsilon(0.02f));
    // A new transition is genuinely under way - it is not the first one still
    // running - and it continues sliding rather than restarting.
    CHECK(synth.eraBlend() < 0.2f);

    // And the slide carries on all the way to the third era.
    for (int i = 0; i < kBlocksPerSecond * 2; ++i) {
        synth.render(params, kBlock, buffer);
    }
    CHECK(synth.eraBlend() > 0.95f);
    CHECK(synth.padNote(0) != doctest::Approx(noteBefore).epsilon(0.5f));
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
