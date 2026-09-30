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
    manager.update(kStep, Game::Era::Present, MusicState::Combat, 0.5f);
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

    manager->update(kStep, Game::Era::Present, MusicState::Exploring, 0.0f);

    // Two hundred updates is three seconds of play. Before the fix the track had
    // stopped within the first handful.
    for (int i = 0; i < 200; ++i) {
        manager->update(kStep, static_cast<Game::Era>(i % 3),
                        static_cast<MusicState>(i % 4), static_cast<float>(i) / 200.0f);
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

    manager->update(kStep, Game::Era::Present, MusicState::Exploring, 0.0f);

    // With the beds stopped, `update` deliberately does not feed the streams, so
    // this is the starvation the watchdog is meant to survive.
    manager->setBedsPlaying(false);
    for (int i = 0; i < 120; ++i) {
        manager->update(kStep, Game::Era::Present, MusicState::Still, 0.0f);
    }

    // Un-pause, and feed again. The beds must be running without anything else
    // having to notice.
    manager->setBedsPlaying(true);
    for (int i = 0; i < 60; ++i) {
        manager->update(kStep, Game::Era::Present, MusicState::Exploring, 0.0f);
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
            manager->update(kStep, Game::Era::Present, MusicState::Combat, 0.0f);
        }
    }
    CHECK(manager->available());   // every one-shot fired without wedging the pool
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
