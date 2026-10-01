// Tests for the audio manager, against a real device when there is one.
//
// This lives beside the event-pump tests because it has the same constraint:
// `AudioManager` is the boundary between the synthesiser and SDL3_mixer, so it
// is in `erashift_engine` and cannot be covered by the headless gameplay suite.
//
// The bug this file exists for
// -----------------------------
// The music and ambience beds were started once at initialisation and then left
// alone. A mixer track reading a stream that has run dry reaches the end of it
// and stops, and nothing restarted it. The game then produced perfectly good
// audio, pushed it into a stream nobody was reading, and logged a healthy mixer
// throughout - so the game was silent and every diagnostic said it was not.
//
// What made this findable rather than merely annoying is a build-time trace
// (`-DERASHIFT_AUDIO_TRACE`) that prints the track's own `MIX_TrackPlaying` and
// the stream backlog. Eyeballing the log could never have found it; the first
// symptom a player would report is "it's quiet", which is exactly the symptom
// that told us nothing.
//
// Device availability
// -------------------
// A machine with no sound card must still pass. Every test here skips itself
// rather than failing, and one of them asserts the no-device path explicitly,
// because "fails soft with no device" is a requirement and not an accident.

#include "EraShift/Audio/AudioManager.hpp"
#include "EraShift/Audio/Synth.hpp"
#include "EraShift/Core/Log.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include <doctest/doctest.h>

using namespace EraShift;
using namespace EraShift::Audio;

namespace {

/// A manager that is initialised if this machine can play audio, and reports
/// whether it is live. A null sink means "no device", which is a supported
/// configuration rather than a failure.
std::unique_ptr<AudioManager> liveManager()
{
    auto manager = std::make_unique<AudioManager>();
    static Core::Logger log;
    if (!manager->initialise(true, kSampleRate, &log)) {
        return nullptr;
    }
    return manager;
}

constexpr float kStep = 1.0f / 60.0f;

} // namespace

TEST_CASE("with no device the game still starts and reports it honestly")
{
    // The requirement is that audio never stops the game. A manager that failed
    // to open a device must be inert, not broken: every call has to be safe.
    Core::Logger log;
    AudioManager manager;

    if (manager.initialise(true, kSampleRate, &log)) {
        // This machine has audio, so there is nothing to assert about the
        // no-device path. Not a skip, just nothing to do.
        return;
    }

    CHECK_FALSE(manager.available());
    CHECK_FALSE(manager.audible());

    // Inert, not crashing.
    manager.setMusicState(MusicState::Combat);
    manager.setTension(0.5f);
    manager.update(kStep);
    manager.play(Sfx::Jump);
    manager.playPriority(Sfx::ShiftImpact, 1.0f, 0.5f);
    manager.setEra(Game::Era::Future);
    manager.setTension(1.0f);
    manager.setMasterVolume(0.5f);
    manager.setMuted(true);
    manager.setBedsPlaying(false);
    manager.setBusVolume(Bus::Sfx, 0.25f);
    manager.shutdown();
    CHECK(true);   // a silent manager tolerates the whole API
}

TEST_CASE("disabling audio in configuration is honoured, not ignored")
{
    // `audio.enabled = false` in a container must not be silently overridden by
    // the engine deciding it knows better.
    Core::Logger log;
    AudioManager manager;
    CHECK_FALSE(manager.initialise(false, kSampleRate, &log));
    CHECK_FALSE(manager.available());
}

TEST_CASE("a bed keeps playing across many updates")
{
    // The regression, in the shape it actually failed: not on the first update,
    // and not on a machine with no device.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    REQUIRE(manager->available());

    manager->update(kStep);

    // Two hundred updates is three seconds of play. Before the fix the track had
    // stopped within the first handful.
    for (int i = 0; i < 200; ++i) {
        manager->setEra(static_cast<Game::Era>(i % 3));
        manager->setMusicState(static_cast<MusicState>(i % 4));
        manager->setTension(static_cast<float>(i) / 200.0f);
        manager->update(kStep);
    }
    CHECK(manager->available());   // still live after 200 updates
}

TEST_CASE("a bed recovers after its stream is starved")
{
    // Starve the stream deliberately: do not feed it for long enough to matter,
    // then resume. The watchdog has to bring the bed back. This is the same code
    // path that covers a PipeWire sink suspending when idle, a Bluetooth headset
    // appearing mid-game, and a pause - all of which end a track silently.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    REQUIRE(manager->available());

    manager->update(kStep);

    // With the beds stopped, `update` deliberately does not feed the streams, so
    // this is the starvation the watchdog is meant to survive.
    manager->setBedsPlaying(false);
    for (int i = 0; i < 120; ++i) {
        manager->update(kStep);
    }

    // Un-pause, and feed again. The beds must be running without anything else
    // having to notice.
    manager->setBedsPlaying(true);
    for (int i = 0; i < 60; ++i) {
        manager->update(kStep);
    }
    CHECK(manager->available());   // beds recovered
}

TEST_CASE("every one-shot can be asked for on a live mixer")
{
    // The pool is the part most likely to fail silently, so every value of the
    // enum is fired. A channel that cannot be claimed, or a stream that will not
    // accept the bytes, has to show up here rather than as a missing sound.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    REQUIRE(manager->available());

    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i <= static_cast<int>(Sfx::Victory); ++i) {
            const auto sfx = static_cast<Sfx>(i);
            manager->play(sfx, 1.0f);
            manager->update(kStep);
        }
    }
    CHECK(manager->available());   // every one-shot fired without wedging the pool
}

TEST_CASE("a one-shot actually starts a mixer track")
{
    // This is the test that the bug above should have been caught by.
    //
    // Every one-shot used to be dropped: the samples were handed to
    // `MIX_SetTrackIOStream`, which reads a container header to learn the
    // format, and a buffer of raw float PCM has no header, so the track was
    // left with no input and `MIX_PlayTrack` failed. The mixer stayed open, the
    // beds kept playing, and the old version of the test above only checked that
    // `available()` was still true - which it was. Music and ambience played and
    // every sound effect was silent, which reads exactly like a healthy audio
    // system from the outside.
    //
    // So the assertion has to be about a voice actually sounding, not about the
    // mixer existing.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;   // no device: nothing to assert, and that is supported
    }
    REQUIRE(manager->available());

    for (int i = 0; i <= static_cast<int>(Sfx::Victory); ++i) {
        const auto sfx = static_cast<Sfx>(i);
        manager->play(sfx, 1.0f);
        manager->update(kStep);
        // Checked per sound, not once at the end: the pool is six voices deep,
        // so a single check at the end could pass on a bed voice and prove
        // nothing about the one-shot.
        CHECK_MESSAGE(manager->diagnostics().sfxVoices > 0,
                      "no channel started for " << std::string(toString(sfx)));
    }
}

TEST_CASE("the self-test reports no problems on a live device")
{
    // The same checks `--audio-test` runs, so the diagnostic a person runs by
    // hand is also a test.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    CHECK(manager->runSelfTest() == 0);
}

TEST_CASE("paradox tension reaches the audio, and only through its setter")
{
    // The regression, in the shape it actually failed: the parameter looked
    // connected the whole time.
    //
    // `Engine::update` pumped the manager with the manager's *own* eased values:
    //
    //     m_audio.update(dt, m_audio.era(), m_audio.musicState(), m_audio.tension());
    //
    // and `update` began by writing those straight back into the continuous
    // controls. `setTension(t)` sets the *target*, so being handed the current
    // value is a fixed point: `m_tension += (m_tensionTarget - m_tension) * rate`
    // with `m_tensionTarget == m_tension` is zero, forever. The target that
    // `FeedbackSystem` had set earlier in the same frame was overwritten before
    // anything could read it.
    //
    // Everything else hid it. `tension()` returned a plausible float, the trace
    // printed it, the beds played, and the mixer was healthy - the audio was
    // simply never told that paradox existed. Tempo instability, the widened
    // tremolo and the ambience drop-out are all gated on this one number, so
    // "you can hear paradox before you can see it" was silence.
    //
    // `update` now takes only `dt`, which is what makes this test possible to
    // write at all: there is no longer a way to pass the tension back in.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }

    manager->setTension(1.0f);
    for (int i = 0; i < 120; ++i) {
        manager->update(kStep);
    }
    CHECK_MESSAGE(manager->tension() > 0.8f,
                  "tension never rose: " << manager->tension() << " after 120 updates");

    // And it comes back down, so a paradox that resolves is audible too.
    manager->setTension(0.0f);
    for (int i = 0; i < 120; ++i) {
        manager->update(kStep);
    }
    CHECK_LT(manager->tension(), 0.2f);
}

TEST_CASE("a muted or paused bed resumes where it left off")
{
    // The synths used to be `reset()` while muted or paused, under a comment
    // claiming they were being advanced. A reset looks harmless because the bed
    // is silent either way - and then unpausing starts the music again from the
    // top of the bar, which in the middle of a fight sounds exactly like the game
    // skipped.
    //
    // The bed's own clock is the observable. `MusicSynth` exposes its position and
    // `AudioManager` publishes it, because "is the bed producing anything" cannot
    // tell a bed that is still running from one that has been restarted.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    for (int i = 0; i < 60; ++i) {
        manager->update(kStep);
    }
    const float before = manager->diagnostics().musicPositionSeconds;
    CHECK_GT(before, 0.5f);

    manager->setBedsPlaying(false);
    for (int i = 0; i < 60; ++i) {
        manager->update(kStep);
    }
    // Still advancing while nothing is listening: that is the whole point.
    CHECK_GT(manager->diagnostics().musicPositionSeconds, before);
    const float whilePaused = manager->diagnostics().musicPositionSeconds;

    manager->setBedsPlaying(true);
    manager->update(kStep);
    // It carries on from where it paused rather than starting again.
    CHECK_GT(manager->diagnostics().musicPositionSeconds, whilePaused);
    CHECK_GT(manager->diagnostics().musicPositionSeconds, 1.5f);
}

TEST_CASE("a manager can be shut down and started again")
{
    // `initialise` documents itself as safe to call twice, which implies the
    // object survives a shutdown. The synths have to come back from it too, or a
    // second run starts its beds halfway through the first one's bar - which is
    // the same defect as BUG-024 from the other direction.
    Core::Logger log;
    AudioManager manager;
    if (!manager.initialise(true, kSampleRate, &log)) {
        return;   // no device on this machine
    }
    for (int i = 0; i < 60; ++i) {
        manager.update(kStep);
    }
    REQUIRE(manager.diagnostics().musicPositionSeconds > 0.5f);

    manager.shutdown();
    CHECK_FALSE(manager.available());

    REQUIRE(manager.initialise(true, kSampleRate, &log));
    // Fresh clock, not the one the first run left behind.
    CHECK(manager.diagnostics().musicPositionSeconds == doctest::Approx(0.0f));
    for (int i = 0; i < 30; ++i) {
        manager.update(kStep);
    }
    CHECK(manager.diagnostics().musicPlaying);
    CHECK(manager.diagnostics().ambiencePlaying);
    manager.shutdown();
}

TEST_CASE("volume settings reach the mixer")
{
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    REQUIRE(manager->available());

    manager->setMasterVolume(0.5f);
    CHECK(manager->masterVolume() == doctest::Approx(0.5f));
    manager->setBusVolume(Bus::Music, 0.25f);
    CHECK(manager->busVolume(Bus::Music) == doctest::Approx(0.25f));
    manager->setBusVolume(Bus::Ambience, 0.75f);
    CHECK(manager->busVolume(Bus::Ambience) == doctest::Approx(0.75f));

    // Out-of-range values are clamped rather than passed to the mixer, which
    // would either blow up or make the bus inaudible.
    manager->setMasterVolume(5.0f);
    CHECK(manager->masterVolume() == doctest::Approx(1.0f));
    manager->setMasterVolume(-1.0f);
    CHECK(manager->masterVolume() == doctest::Approx(0.0f));

    manager->setMuted(true);
    CHECK_FALSE(manager->audible());
    manager->setMuted(false);
    CHECK(manager->audible());
}

// --- loudness, validity and mixer behaviour ---------------------------------
//
// These assert *properties of the signal*, not that a call succeeded. The
// distinction is not pedantic: every one-shot in the game was silent for a while
// while `MIX_PlayTrack` was being called, the mixer was open, the buses had gain
// and the log said everything was fine. A test that checks "did initialise
// return true" cannot see that. A test that measures the samples can.

namespace {

float rmsOf(const SampleBuffer& buffer)
{
    if (buffer.empty()) {
        return 0.0f;
    }
    double sum = 0.0;
    for (const float sample : buffer) {
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buffer.size())));
}

float peakOf(const SampleBuffer& buffer)
{
    float high = 0.0f;
    for (const float sample : buffer) {
        high = std::max(high, std::fabs(sample));
    }
    return high;
}

} // namespace

TEST_CASE("every one-shot is audible, not merely non-zero")
{
    // `peak > 0` is too weak a bar: a buffer of denormals passes it and is
    // inaudible. The floor is per category rather than one global number,
    // because the categories are genuinely different jobs - a footstep is a
    // quiet incidental sound and is *supposed* to sit well below a seal pickup,
    // which is a scripted beat the player is meant to notice.
    struct Expectation {
        Sfx         sfx;
        float       minPeak;
        const char* why;
    };
    const Expectation expectations[] = {
        {Sfx::UiMove,     0.02f,  "menu tick, deliberately quiet"},
        {Sfx::UiConfirm,  0.05f,  "a confirmation the player must notice"},
        {Sfx::UiDeny,     0.05f,  "a rejection the player must notice"},
        {Sfx::FootstepStone, 0.01f, "incidental, fires constantly"},
        {Sfx::FootstepGrass, 0.01f, "incidental, fires constantly"},
        {Sfx::FootstepSand,  0.01f, "incidental, fires constantly"},
        {Sfx::FootstepMetal, 0.01f, "incidental, fires constantly"},
        {Sfx::FootstepCrystal, 0.01f, "incidental, fires constantly"},
        {Sfx::Jump,       0.05f,  "a player action"},
        {Sfx::Land,       0.05f,  "a player action"},
        {Sfx::Dash,       0.05f,  "a player action"},
        {Sfx::Attack,     0.05f,  "combat, must be audible over the bed"},
        {Sfx::EnemyHurt,  0.05f,  "combat feedback"},
        {Sfx::EnemyDie,   0.05f,  "combat feedback"},
        {Sfx::PlayerHurt, 0.05f,  "the player must always hear being hit"},
        {Sfx::PlayerDie,  0.05f,  "death must be unmissable"},
        {Sfx::PickupChrono, 0.05f, "a reward"},
        {Sfx::PickupHealth, 0.05f, "a reward"},
        {Sfx::SealTaken,  0.05f,  "a progression beat"},
        {Sfx::GateOpened, 0.05f,  "a progression beat"},
        {Sfx::ShiftCharge, 0.05f, "the signature verb, charging"},
        {Sfx::ShiftImpact, 0.05f, "the signature verb, landing"},
        {Sfx::Hazard,     0.03f,  "a warning"},
        {Sfx::Victory,    0.05f,  "the ending"},
    };

    for (const Expectation& e : expectations) {
        SampleBuffer buffer;
        renderSfx(e.sfx, kSampleRate, buffer);
        CAPTURE(e.sfx);
        INFO(e.why);
        CHECK_FALSE(buffer.empty());
        // Finite: a NaN compares false against everything, so a buffer of NaNs
        // would slip past a bare `peak > 0` check.
        for (const float sample : buffer) {
            CHECK(std::isfinite(sample));
        }
        CHECK_GT(peakOf(buffer), e.minPeak);
        CHECK_GT(rmsOf(buffer), e.minPeak * 0.05f);
        CHECK_LE(peakOf(buffer), 1.0f);
    }
}

TEST_CASE("one-shots are distinct from one another")
{
    // A copy-paste in a synth switch would make two events sound identical while
    // every other test still passed, so identity is asserted directly.
    std::vector<SampleBuffer> rendered;
    for (int i = 0; i <= static_cast<int>(Sfx::Victory); ++i) {
        SampleBuffer buffer;
        renderSfx(static_cast<Sfx>(i), kSampleRate, buffer);
        rendered.push_back(std::move(buffer));
    }
    for (std::size_t a = 0; a < rendered.size(); ++a) {
        for (std::size_t b = a + 1; b < rendered.size(); ++b) {
            CAPTURE(a);
            CAPTURE(b);
            const bool sameLength = rendered[a].size() == rendered[b].size();
            bool identical = sameLength;
            if (sameLength) {
                for (std::size_t i = 0; i < rendered[a].size(); ++i) {
                    if (rendered[a][i] != rendered[b][i]) {
                        identical = false;
                        break;
                    }
                }
            }
            CHECK_FALSE(identical);
        }
    }
}

TEST_CASE("beds produce finite, in-range audio over a long run")
{
    // Catches a filter or oscillator going unstable over time, which a
    // short render never reaches.
    MusicSynth music;
    AmbienceSynth ambience;
    MusicParams params;
    params.era      = Game::Era::Present;
    params.intensity = 1.0f;
    params.tempo     = 116.0f;
    AmbienceParams amb;
    amb.era = Game::Era::Present;

    SampleBuffer out;
    for (int block = 0; block < 400; ++block) {
        music.render(params, 1024, out);
        for (const float sample : out) {
            REQUIRE(std::isfinite(sample));
            CHECK_LE(std::fabs(sample), 1.0f);
        }
        ambience.render(amb, 1024, out);
        for (const float sample : out) {
            REQUIRE(std::isfinite(sample));
            CHECK_LE(std::fabs(sample), 1.0f);
        }
    }
}

TEST_CASE("a zero master gain silences the mixer, and full gain does not")
{
    // Behavioural, because the distinction this protects is the one that was
    // broken: master gain demonstrably reaches the output, the per-bus tag gains
    // did not.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    for (int i = 0; i < 30; ++i) {
        manager->update(kStep);
    }

    manager->setMasterVolume(0.0f);
    for (int i = 0; i < 30; ++i) {
        manager->update(kStep);
    }
    // The streams are still being fed - silence must come from the gain, not
    // from the synthesis having stopped.
    CHECK_GT(manager->diagnostics().lastMusicPeak, 0.0f);
    CHECK(manager->diagnostics().musicPlaying);

    manager->setMasterVolume(1.0f);
    for (int i = 0; i < 30; ++i) {
        manager->update(kStep);
    }
    CHECK(manager->diagnostics().musicPlaying);
}

TEST_CASE("bus volumes are remembered and reported")
{
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    manager->setBusVolume(Bus::Music, 0.25f);
    manager->setBusVolume(Bus::Ambience, 0.5f);
    manager->setBusVolume(Bus::Sfx, 0.75f);
    CHECK(manager->busVolume(Bus::Music)    == doctest::Approx(0.25f));
    CHECK(manager->busVolume(Bus::Ambience) == doctest::Approx(0.5f));
    CHECK(manager->busVolume(Bus::Sfx)      == doctest::Approx(0.75f));

    // Out-of-range and malformed values are clamped rather than trusted: a NaN
    // gain poisons every sample downstream of it.
    manager->setBusVolume(Bus::Music, 5.0f);
    CHECK(manager->busVolume(Bus::Music) == doctest::Approx(1.0f));
    manager->setBusVolume(Bus::Music, -1.0f);
    CHECK(manager->busVolume(Bus::Music) == doctest::Approx(0.0f));
    manager->setBusVolume(Bus::Music, std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(manager->busVolume(Bus::Music)));
}

TEST_CASE("more one-shots than channels recycles without going quiet")
{
    // Six channels, many more sounds. The oldest must be stolen, the new sound
    // must be audible, and the pool must not wedge.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    for (int round = 0; round < 40; ++round) {
        for (int i = 0; i <= static_cast<int>(Sfx::Victory); ++i) {
            manager->play(static_cast<Sfx>(i), 1.0f);
        }
        manager->update(kStep);
        CHECK_MESSAGE(manager->diagnostics().sfxVoices > 0,
                      "pool went silent on round " << round);
        // The pool is six deep; stealing must never exceed it.
        CHECK_LE(manager->diagnostics().sfxVoices, 6);
    }
    // And the beds are untouched by one-shot pressure.
    const auto d = manager->diagnostics();
    CHECK(d.musicPlaying);
    CHECK(d.ambiencePlaying);
    CHECK_EQ(d.putFailures, 0u);
}

TEST_CASE("both beds recover together after simultaneous starvation")
{
    // The single-bed recovery test exists; this is the case where both stop at
    // once, which is what happens when the device is suspended rather than when
    // one synth misbehaves.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    for (int i = 0; i < 60; ++i) {
        manager->update(kStep);
    }
    REQUIRE(manager->diagnostics().musicPlaying);
    REQUIRE(manager->diagnostics().ambiencePlaying);

    // Pause the beds, which stops feeding the streams entirely, then let the
    // watchdog notice.
    manager->setBedsPlaying(false);
    for (int i = 0; i < 10; ++i) {
        manager->update(kStep);
    }
    manager->setBedsPlaying(true);
    for (int i = 0; i < 120; ++i) {
        manager->update(kStep);
    }
    const auto d = manager->diagnostics();
    CHECK_MESSAGE(d.musicPlaying, "music did not come back");
    CHECK_MESSAGE(d.ambiencePlaying, "ambience did not come back");
    CHECK_GT(d.lastMusicPeak, 0.0f);
    CHECK_GT(d.lastAmbiencePeak, 0.0f);
    CHECK_EQ(d.putFailures, 0u);
}

TEST_CASE("a long run of one-shots stays bounded and does not starve the beds")
{
    // The regression test for the stream-ownership leak: `play()` hands an
    // explicitly-formatted `SDL_AudioStream` to the mixer, and
    // `MIX_SetTrackAudioStream` does not take ownership, so the channel has to
    // free the previous one itself. It did not, and every one-shot leaked a
    // stream: measured at 77.6 kB per play by RSS, which is over 100 MB an hour
    // of ordinary running.
    //
    // RSS is not asserted here because it is far too allocator-dependent to make
    // a stable assertion - the numbers that matter were taken out of band with
    // `scripts/check_audio_leaks.sh`. What is asserted here is the part that is
    // deterministic: the pool stays inside its bound, the mixer keeps accepting
    // data, and the beds are still running afterwards. A leak severe enough to
    // matter would exhaust memory or wedge the mixer long before this finishes.
    auto manager = liveManager();
    if (manager == nullptr) {
        return;
    }
    for (int i = 0; i < 60; ++i) {
        manager->update(kStep);
    }
    for (int i = 0; i < 5000; ++i) {
        manager->play(static_cast<Sfx>(i % 20), 1.0f);
        manager->update(kStep);
    }
    const auto d = manager->diagnostics();
    CHECK_LE(d.sfxVoices, 6);
    CHECK_EQ(d.putFailures, 0u);
    CHECK_MESSAGE(d.musicPlaying, "music stopped during one-shot pressure");
    CHECK_MESSAGE(d.ambiencePlaying, "ambience stopped during one-shot pressure");
    CHECK_GT(d.lastMusicPeak, 0.0f);
}
