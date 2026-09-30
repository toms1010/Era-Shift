// Era Shift - audio.
//
// Three moving parts, deliberately separated:
//
//   Synth          pure arithmetic, in Core, fully unit tested
//   AudioManager   this: SDL3_mixer plumbing, volumes, and routing
//   audio.json     user-facing mixer settings
//
// The mixer is asked for a *stream*, not a file. Music and ambience are
// synthesised on the main thread a block at a time and pushed into an SDL audio
// stream, so a shift can glide the music from one era to the next without a
// crossfade between two loops and without a single byte of audio asset in the
// repository. One-shots are rendered once, cached in memory, and played through
// a small pool of tracks.
//
// Everything here fails soft. A machine with no audio device, a CI runner, or a
// mixer that will not open must still be able to play the game in silence, so
// every SDL call is checked and `available()` is the only thing the rest of the
// engine has to ask about.

#pragma once

#include "EraShift/Audio/Synth.hpp"

#include <array>
#include <cstdint>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace EraShift {

namespace Core {
class Logger;
}

namespace Audio {

/// Named mixer channels, mirrored into `config/audio.json`.
enum class Bus : std::uint8_t { Master, Music, Ambience, Sfx, Count };

/// What the music should be doing. The bed is always running; this only changes
/// how dense, how fast and how bright it is.
enum class MusicState : std::uint8_t {
    Exploring,  ///< Sparse, slow, a pad and almost nothing else.
    Alert,      ///< An enemy has noticed you.
    Combat,     ///< Arpeggio in, tempo up, bass out.
    /// Reserved: victory and defeat both want the bed to thin out and hold a
    /// single note rather than stop, because a hard stop reads as a bug.
    Still,
};

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    AudioManager(const AudioManager&)            = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    /// Opens the mixer and allocates the tracks. Safe to call twice; the second
    /// call is a no-op. Returns false only if audio was requested *and* could not
    /// be started, in which case the game continues silently.
    ///
    /// `enabled` comes from config, so `audio.enabled = false` is honoured and
    /// not merely ignored in a container.
    bool initialise(bool enabled, int sampleRate, Core::Logger* log);

    void shutdown() noexcept;

    [[nodiscard]] bool available() const noexcept { return m_available; }
    /// True when a device is open and the user has not muted the game.
    [[nodiscard]] bool audible() const noexcept { return m_available && !m_muted; }
    [[nodiscard]] int sampleRate() const noexcept { return m_sampleRate; }

    /// Feeds the synths one frame's worth of audio. Call once per fixed step
    /// from the update path, before rendering.
    void update(float dt, Game::Era era, MusicState state, float tension);

    // --- one-shots ----------------------------------------------------------
    /// Fire and forget. Pool exhaustion is silently tolerated: dropping a
    /// footstep is better than dropping a frame.
    void play(Sfx sfx, float volume = 1.0f, float pitch = 1.0f);
    /// A sound that should be able to retrigger itself (the attack, a landing).
    void playPriority(Sfx sfx, float volume = 1.0f, float pitch = 1.0f);

    // --- continuous control -------------------------------------------------
    void setMusicState(MusicState state) noexcept { m_musicState = state; }
    [[nodiscard]] MusicState musicState() const noexcept { return m_musicState; }
    void setEra(Game::Era era) noexcept { m_era = era; }
    [[nodiscard]] Game::Era era() const noexcept { return m_era; }
    /// 0..1. Paradox raises tempo instability and drops out the ambience, which
    /// is most of why high paradox feels wrong before you can name why.
    void setTension(float tension);
    [[nodiscard]] float tension() const noexcept { return m_tension; }

    // --- mixing -------------------------------------------------------------
    void setBusVolume(Bus bus, float volume);
    [[nodiscard]] float busVolume(Bus bus) const noexcept;
    /// A single 0..1 master slider, as bound in the settings menu. Applied on top
    /// of the configured master volume.
    void setMasterVolume(float volume) noexcept;
    [[nodiscard]] float masterVolume() const noexcept { return m_masterVolume; }
    void setMuted(bool muted) noexcept;
    [[nodiscard]] bool muted() const noexcept { return m_muted; }
    /// Starts or stops the music and ambience beds, leaving one-shots alone.
    void setBedsPlaying(bool playing);
    [[nodiscard]] bool bedsPlaying() const noexcept { return m_bedsPlaying; }

    /// Mixer's own idea of how loud things are, for the debug overlay.
    [[nodiscard]] const char* driverName() const noexcept { return m_driverName; }

private:
    /// One of the interchangeable one-shot voices.
    struct Channel {
        void*     track = nullptr;   ///< MIX_Track*
        bool      busy = false;
        /// Monotonic, so "steal the oldest" is a comparison rather than a search.
        std::uint64_t startedAt = 0;
    };

    bool createMixer();
    bool createBeds();
    void createSfxCache();
    void applyVolumes();
    /// Returns a channel that is free, stealing the oldest if all are busy.
    Channel* claimChannel();
    void pushBeds(float dt);

    bool  m_available = false;
    bool  m_muted = false;
    bool  m_bedsPlaying = true;
    int   m_sampleRate = kSampleRate;

    void* m_mixer = nullptr;      ///< MIX_Mixer*
    const char* m_driverName = "none";

    /// Music and ambience each get a stream and a looping track. The track loops
    /// forever over a stream that is never finished, which is exactly the
    /// arrangement SDL3_mixer's streaming API is built for: the synthesiser
    /// keeps feeding the same stream and the shift glides the parameters.
    void* m_musicStream = nullptr;    ///< SDL_AudioStream*
    void* m_ambienceStream = nullptr;
    void* m_musicTrack = nullptr;     ///< MIX_Track*
    void* m_ambienceTrack = nullptr;

    /// A small pool of interchangeable one-shot tracks. Six is enough for a
    /// busy screen without audible voice stealing.
    static constexpr std::size_t kSfxChannels = 6;
    std::array<Channel, kSfxChannels> m_channels{};
    std::uint64_t m_channelClock = 0;

    /// Rendered once at start-up so playback costs a track start and nothing
    /// else. Indexed by `Sfx`.
    SampleBuffer m_sfxCache[static_cast<std::size_t>(Sfx::Victory) + 1];

    MusicSynth    m_music;
    AmbienceSynth m_ambience;
    MusicState    m_musicState = MusicState::Exploring;
    Game::Era     m_era = Game::Era::Present;
    float m_tension = 0.0f;
    float m_tensionTarget = 0.0f;
    float m_masterVolume = 1.0f;
    /// Master, music, ambience, sfx. The master slider multiplies all of them.
    std::array<float, static_cast<std::size_t>(Bus::Count)> m_busVolume{
        1.0f, 0.7f, 0.6f, 0.9f};

    /// A block of audio generated per update. 48kHz at 60Hz is 800 frames.
    SampleBuffer m_musicBlock;
    SampleBuffer m_ambienceBlock;
    Core::Logger* m_log = nullptr;
};

} // namespace Audio
} // namespace EraShift
