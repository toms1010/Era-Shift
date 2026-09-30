#include "EraShift/Audio/AudioManager.hpp"

#include "EraShift/Core/Log.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <cmath>

namespace EraShift::Audio {

namespace {

using Game::Era;

/// The tags the three beds and the one-shots are grouped under. These are the
/// mixer-side names; the `Bus` enum is the game-side one.
constexpr const char* kTagMusic = "music";
constexpr const char* kTagAmbience = "ambience";
constexpr const char* kTagSfx = "sfx";

float busIndex(Bus bus) noexcept
{
    return static_cast<float>(static_cast<std::size_t>(bus));
}

/// A silence-filling stream in the mixer's own format. The synthesiser pushes
/// float frames into it; SDL converts to whatever the device wants, so the game
/// does not have to care whether the hardware is 44.1k or 48k.
SDL_AudioStream* makeStream(int sampleRate)
{
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = sampleRate;
    return SDL_CreateAudioStream(&spec, &spec);
}

/// Fills a stream with a moment of silence so a looping track has something to
/// be playing. The synthesiser's first real block arrives on the first update,
/// which is roughly 100ms after start-up; without this the track ends in the
/// meantime.
void primeStream(SDL_AudioStream* stream, int sampleRate)
{
    constexpr int kPrimeFrames = 480;   ///< 10ms at 48kHz.
    const std::vector<float> silence(static_cast<std::size_t>(kPrimeFrames) * 2, 0.0f);
    SDL_PutAudioStreamData(stream, silence.data(),
                           static_cast<int>(silence.size() * sizeof(float)));
    static_cast<void>(sampleRate);
}

bool startLooping(void* track)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    // -1 means forever, which is what a synthesised bed is: it never "ends", it
    // just changes.
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    const bool started = MIX_PlayTrack(static_cast<MIX_Track*>(track), props);
    SDL_DestroyProperties(props);
    return started;
}

constexpr std::size_t kSfxCount = static_cast<std::size_t>(Sfx::Victory) + 1;

} // namespace

AudioManager::AudioManager() = default;

AudioManager::~AudioManager()
{
    shutdown();
}

bool AudioManager::initialise(bool enabled, int sampleRate, Core::Logger* log)
{
    m_log = log;
    if (m_available) {
        return true;
    }
    if (!enabled) {
        if (m_log) {
            m_log->info("audio", "disabled by configuration");
        }
        return false;
    }
    if (sampleRate <= 0) {
        sampleRate = kSampleRate;
    }
    m_sampleRate = sampleRate;

    if (!createMixer()) {
        return false;
    }
    createBeds();
    createSfxCache();
    applyVolumes();
    m_available = m_mixer != nullptr;
    if (m_log) {
        m_log->info("audio", "started on {} at {} Hz", m_driverName, m_sampleRate);
    }
    return m_available;
}

bool AudioManager::createMixer()
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        if (m_log) {
            m_log->warn("audio", "no audio subsystem: {}", SDL_GetError());
        }
        return false;
    }

    // MIX_Init reports which formats it could not open. A non-zero return here
    // is not fatal: what actually matters is whether the PCM path is usable, and
    // every sound in this game is synthesised float PCM. A machine with no MIDI
    // tables (the usual cause) still gets a working mixer.
    if (MIX_Init() != 0 && m_log) {
        m_log->info("audio", "partial mixer init ({}); PCM is unaffected", SDL_GetError());
    }

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = m_sampleRate;

    MIX_Mixer* mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (mixer == nullptr) {
        if (m_log) {
            m_log->warn("audio", "no playback device, running silently: {}", SDL_GetError());
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    m_mixer = mixer;
    const char* name = SDL_GetCurrentAudioDriver();
    m_driverName = (name != nullptr) ? name : "unknown";
    return true;
}

bool AudioManager::createBeds()
{
    if (m_mixer == nullptr) {
        return false;
    }

    m_musicStream = makeStream(m_sampleRate);
    m_ambienceStream = makeStream(m_sampleRate);
    if (m_musicStream == nullptr || m_ambienceStream == nullptr) {
        if (m_log) {
            m_log->warn("audio", "could not create bed streams: {}", SDL_GetError());
        }
        return false;
    }

    m_musicTrack = MIX_CreateTrack(static_cast<MIX_Mixer*>(m_mixer));
    m_ambienceTrack = MIX_CreateTrack(static_cast<MIX_Mixer*>(m_mixer));
    if (m_musicTrack == nullptr || m_ambienceTrack == nullptr) {
        return false;
    }

    MIX_SetTrackAudioStream(static_cast<MIX_Track*>(m_musicTrack),
                            static_cast<SDL_AudioStream*>(m_musicStream));
    MIX_SetTrackAudioStream(static_cast<MIX_Track*>(m_ambienceTrack),
                            static_cast<SDL_AudioStream*>(m_ambienceStream));
    // Tags turn the mixer into four buses: master, music, ambience and sfx.
    // The settings menu writes to the tag gains.
    MIX_TagTrack(static_cast<MIX_Track*>(m_musicTrack), kTagMusic);
    MIX_TagTrack(static_cast<MIX_Track*>(m_ambienceTrack), kTagAmbience);

    // Prime both streams *before* starting the tracks.
    //
    // This is the whole bug. A track playing a stream that has no data in it yet
    // reads that as end-of-stream: the track starts, immediately reaches the end,
    // and stops. Nothing ever restarts it, so the bed is silent for the rest of
    // the session while every log line cheerfully reports a healthy mixer. The
    // first `pushBeds` call arrives about 100ms later, by which point the track
    // is already gone and the data just accumulates in the stream.
    //
    // The symptom is indistinguishable from "the game is quiet", which is why it
    // took a trace of the track's own state to find rather than any amount of
    // listening.
    primeStream(static_cast<SDL_AudioStream*>(m_musicStream), m_sampleRate);
    primeStream(static_cast<SDL_AudioStream*>(m_ambienceStream), m_sampleRate);

#ifdef ERASHIFT_AUDIO_TRACE
    const bool musicStarted = startLooping(m_musicTrack);
    const bool ambienceStarted = startLooping(m_ambienceTrack);
#else
    // Not checked: `ensureBedsPlaying` restarts a bed the moment it stops, which
    // covers a failure here without needing a branch at start-up.
    static_cast<void>(startLooping(m_musicTrack));
    static_cast<void>(startLooping(m_ambienceTrack));
#endif

#ifdef ERASHIFT_AUDIO_TRACE
    if (m_log) {
        m_log->info("audio-trace", "createBeds: musicStarted={} ambienceStarted={} "
                                   "playing={} paused={} err={}",
                    musicStarted, ambienceStarted,
                    MIX_TrackPlaying(static_cast<MIX_Track*>(m_musicTrack)),
                    MIX_TrackPaused(static_cast<MIX_Track*>(m_musicTrack)), SDL_GetError());
    }
#endif
    return true;
}

void AudioManager::createSfxCache()
{
    for (std::size_t i = 0; i < kSfxCount; ++i) {
        renderSfx(static_cast<Sfx>(i), m_sampleRate, m_sfxCache[i]);
    }

    for (Channel& channel : m_channels) {
        channel.track = MIX_CreateTrack(static_cast<MIX_Mixer*>(m_mixer));
        channel.busy = false;
        if (channel.track != nullptr) {
            MIX_TagTrack(static_cast<MIX_Track*>(channel.track), kTagSfx);
            MIX_SetTrackGain(static_cast<MIX_Track*>(channel.track), 1.0f);
        }
    }
}

void AudioManager::applyVolumes()
{
    if (m_mixer == nullptr) {
        return;
    }
    // Master is the mixer gain, which demonstrably works: setting it to zero
    // silences the output completely.
    MIX_SetMixerGain(static_cast<MIX_Mixer*>(m_mixer), m_muted ? 0.0f : m_masterVolume);

    // The three sub-buses are applied as *per-track* gains rather than through
    // `MIX_SetTagGain`.
    //
    // The tag API looked like the obvious way to build four buses and it was
    // wired up that way first, but the tag gains had no measurable effect on the
    // output: with music, ambience and sfx all set to 0 the sink monitor still
    // read -13 dBFS, identical to full volume, while the master gain at 0 did
    // silence everything. So the bus sliders in the settings menu were connected
    // to nothing. Tags are still assigned, because they are useful for
    // `MIX_PlayTag`-style control, but nothing load-bearing depends on them.
    //
    // Per-track gain is the alternative that can be verified: it is a plain
    // multiplier on the track, and `play()` already relies on it for each
    // sound's own volume.
    if (m_musicTrack != nullptr) {
        MIX_SetTrackGain(static_cast<MIX_Track*>(m_musicTrack),
                         m_busVolume[busIndex(Bus::Music)]);
    }
    if (m_ambienceTrack != nullptr) {
        MIX_SetTrackGain(static_cast<MIX_Track*>(m_ambienceTrack),
                         m_busVolume[busIndex(Bus::Ambience)]);
    }
    // One-shots set their own gain in `play()` as volume * sfx bus, so a channel
    // that is not currently sounding only needs the bus value as a resting
    // state.
    for (Channel& channel : m_channels) {
        if (channel.track != nullptr) {
            MIX_SetTrackGain(static_cast<MIX_Track*>(channel.track),
                             m_busVolume[busIndex(Bus::Sfx)]);
        }
    }
}

AudioManager::Channel* AudioManager::claimChannel()
{
    // First pass: a channel that has finished. Checked against the mixer rather
    // than tracked by a timer, so a sound is never cut short by a frame hitches.
    for (Channel& channel : m_channels) {
        if (channel.track == nullptr) {
            continue;
        }
        if (channel.busy && !MIX_TrackPlaying(static_cast<MIX_Track*>(channel.track))) {
            channel.busy = false;
        }
        if (!channel.busy) {
            channel.busy = true;
            channel.startedAt = ++m_channelClock;
            return &channel;
        }
    }

    // Everything is still sounding. Steal the oldest rather than dropping the
    // new sound: a fresh hit is more informative than the tail of an old one.
    Channel* oldest = nullptr;
    for (Channel& channel : m_channels) {
        if (channel.track == nullptr) {
            continue;
        }
        if (oldest == nullptr || channel.startedAt < oldest->startedAt) {
            oldest = &channel;
        }
    }
    if (oldest != nullptr) {
        MIX_StopTrack(static_cast<MIX_Track*>(oldest->track), 0);
        oldest->startedAt = ++m_channelClock;
    }
    return oldest;
}

void AudioManager::play(Sfx sfx, float volume, float pitch)
{
    if (!audible() || m_mixer == nullptr) {
        return;
    }
    const auto index = static_cast<std::size_t>(sfx);
    if (index >= kSfxCount || m_sfxCache[index].empty()) {
        return;
    }

    Channel* channel = claimChannel();
    if (channel == nullptr || channel->track == nullptr) {
        return;
    }

    // The cached samples go in through an explicitly-formatted audio stream,
    // the same way the music and ambience beds do.
    //
    // This used to hand the samples to `MIX_SetTrackIOStream`, which is the
    // function for *decodable containers* - it reads a WAV or Ogg header to
    // learn the format. The cache is raw interleaved float PCM with no header at
    // all, so the mixer could not determine a format, the track ended up with no
    // input assigned, and every `MIX_PlayTrack` failed with "No audio currently
    // assigned to this track".
    //
    // The failure was invisible from outside: the mixer opened, the buses had
    // gain, the bed tracks played, and the log reported a healthy audio system.
    // Every one-shot was simply dropped, so the game had music, ambience and
    // silence. `MIX_PlayTrack`'s return value was discarded, which is what let
    // it stay hidden - it is checked now.
    SDL_AudioStream* stream = makeStream(m_sampleRate);
    if (stream == nullptr) {
        return;
    }    if (!SDL_PutAudioStreamData(stream, m_sfxCache[index].data(),
                               static_cast<int>(m_sfxCache[index].size() * sizeof(float)))) {
        SDL_DestroyAudioStream(stream);
        return;
    }
    if (!MIX_SetTrackAudioStream(static_cast<MIX_Track*>(channel->track), stream)) {
        SDL_DestroyAudioStream(stream);
        return;
    }
    // Replacing the track's input is the documented point at which the previous
    // stream is no longer needed, so the old one is freed *here* and not before.
    // Doing it the other way round - stopping the track and freeing its stream
    // while the track was still pointing at it - segfaults, because the audio
    // thread is not synchronised with `MIX_StopTrack`.
    if (channel->stream != nullptr) {
        SDL_DestroyAudioStream(channel->stream);
    }
    // `MIX_SetTrackAudioStream` does not take ownership, so the channel owns it
    // from here and frees it on the next play and at shutdown.
    channel->stream = stream;

    // Pitch is a resample ratio on the track, which is how one footstep becomes a
    // heavier or lighter one without a second buffer.
    MIX_SetTrackFrequencyRatio(static_cast<MIX_Track*>(channel->track),
                               Graphics::clampValue(pitch, 0.5f, 2.0f));
    // The per-sound volume and the sfx bus are one multiplier, because the bus
    // is applied per track. See `applyVolumes` for why.
    MIX_SetTrackGain(static_cast<MIX_Track*>(channel->track),
                     Graphics::clampValue(volume, 0.0f, 1.5f) *
                         m_busVolume[busIndex(Bus::Sfx)]);

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, 0);
    const bool started = MIX_PlayTrack(static_cast<MIX_Track*>(channel->track), props);
    SDL_DestroyProperties(props);
    if (!started && m_log != nullptr) {
        // Once per distinct failure rather than once per sound: a footstep that
        // fails sixty times a second would bury every other line in the log.
        if (m_sfxStartFailureLogged) {
            return;
        }
        m_sfxStartFailureLogged = true;
        m_log->warn("audio", "one-shot track refused to start ({}); further "
                              "one-shot failures will not be logged",
                    SDL_GetError());
    }
}

void AudioManager::playPriority(Sfx sfx, float volume, float pitch)
{
    // The distinction from play() is intent, not mechanism: a priority sound is
    // one the player caused and would notice missing. Today the pool always
    // steals rather than drops, so both are the same call; keeping the two names
    // means the policy can diverge without touching every call site.
    play(sfx, volume, pitch);
}

void AudioManager::setCombatIntensity(float intensity) noexcept
{
    m_combatIntensity = Graphics::clampValue(intensity, 0.0f, 1.0f);
}

void AudioManager::setTempoScale(float scale) noexcept
{
    m_tempoScale = Graphics::clampValue(scale, 0.25f, 4.0f);
}

void AudioManager::setTensionEnabled(bool enabled) noexcept
{
    m_tensionEnabled = enabled;
}

void AudioManager::setTension(float tension)
{
    m_tensionTarget = Graphics::clampValue(tension, 0.0f, 1.0f);
}

void AudioManager::setBusVolume(Bus bus, float volume)
{
    if (bus == Bus::Count) {
        return;
    }
    m_busVolume[busIndex(bus)] = Graphics::clampValue(volume, 0.0f, 1.0f);
    applyVolumes();
}

float AudioManager::busVolume(Bus bus) const noexcept
{
    if (bus == Bus::Count) {
        return 0.0f;
    }
    return m_busVolume[busIndex(bus)];
}

void AudioManager::setMasterVolume(float volume) noexcept
{
    m_masterVolume = Graphics::clampValue(volume, 0.0f, 1.0f);
    applyVolumes();
}

void AudioManager::setMuted(bool muted) noexcept
{
    m_muted = muted;
    applyVolumes();
}

void AudioManager::setBedsPlaying(bool playing)
{
    if (m_bedsPlaying == playing || m_mixer == nullptr) {
        return;
    }
    m_bedsPlaying = playing;
    if (playing) {
        MIX_ResumeAllTracks(static_cast<MIX_Mixer*>(m_mixer));
    } else {
        // Only the beds, so a paused game does not also swallow the UI click that
        // unpaused it.
        MIX_PauseTrack(static_cast<MIX_Track*>(m_musicTrack));
        MIX_PauseTrack(static_cast<MIX_Track*>(m_ambienceTrack));
    }
}

void AudioManager::pushBeds(float dt)
{
    if (m_musicStream == nullptr || m_ambienceStream == nullptr) {
        return;
    }

    // Tension eases, so paradox creeping up sounds like it is creeping.
    if (m_tensionEnabled) {
        m_tension += (m_tensionTarget - m_tension) * std::min(1.0f, dt * 2.0f);
    } else {
        m_tension = 0.0f;
    }

    const int frames = static_cast<int>(static_cast<float>(m_sampleRate) * dt);
    if (frames <= 0) {
        return;
    }

    MusicParams music;
    music.era = m_era;
    music.eraBlend = 1.0f;
    music.instability = m_tension;
    // The synthesiser's own level, before the mixer's buses.
    //
    // Measured output before this was -24.5dBFS RMS, which is roughly 20dB below
    // where a game should sit: audible on a laptop, gone on a phone speaker or
    // anything with a volume limiter. The attenuation was in two places at once -
    // a 0.5 synth gain *and* the 0.65 music bus - so the bed arrived at about a
    // quarter of full scale. The synth side is raised here so that the two stages
    // together land near -6dBFS peak; the bus volumes stay where the user set
    // them, because those are the ones the player controls.
    music.gain = 0.95f;
    // The per-state numbers are the designed defaults. `musicTempo` scales all
    // of them together and `combatIntensity` scales how dense Combat gets; both
    // were settings in audio.json with no reader, so a player who changed them
    // got silence and no explanation.
    switch (m_musicState) {
        case MusicState::Exploring: music.intensity = 0.0f; music.tempo = 84.0f; break;
        case MusicState::Alert:     music.intensity = 0.35f; music.tempo = 96.0f; break;
        case MusicState::Combat:    music.intensity = m_combatIntensity; music.tempo = 116.0f; break;
        case MusicState::Still:     music.intensity = 0.10f; music.tempo = 68.0f; break;
    }
    music.tempo *= m_tempoScale;
    // Tension also raises the tempo a little, on top of the state's own.
    music.tempo += m_tension * 10.0f;

    AmbienceParams ambience;
    ambience.era = m_era;
    ambience.eraBlend = 1.0f;
    // Gated so a player can turn the paradox effect off without losing the rest
    // of the feedback.
    ambience.instability = m_tensionEnabled ? m_tension * 0.7f : 0.0f;
    // Left at 1.0: the synth scales itself to a sensible absolute level, so this
    // stays a clean user control. The *bus* gain (0.55 by default) is what makes
    // ambience sit under the music, which is the right place for that decision.
    ambience.gain = 1.0f;

    m_music.render(music, frames, m_musicBlock);
    m_ambience.render(ambience, frames, m_ambienceBlock);

    // Peak of what is about to be fed, kept for diagnostics. Computing it is cheap
    // next to the synthesis and it is the difference between "the bed is quiet"
    // and "the bed is producing nothing at all", which look identical from
    // outside.
    //
    // Peak *hold*, decaying, rather than the peak of the last block. A single
    // 16ms window of wind is near-silence most of the time, so reporting it
    // directly made a perfectly working ambience bed read as 0.035 - which reads
    // as broken and sends you looking for a fault that is not there. The hold
    // jumps instantly and falls slowly, so it tracks the loudest recent moment
    // and only decays if the bed really has gone quiet.
    m_lastMusicPeak    = peakHold(m_lastMusicPeak, peakOf(m_musicBlock), dt);
    m_lastAmbiencePeak = peakHold(m_lastAmbiencePeak, peakOf(m_ambienceBlock), dt);
    ++m_blocksFed;

#ifdef ERASHIFT_AUDIO_TRACE
    if (m_traceCounter++ % 60 == 0) {
        float pk = 0.0f;
        for (float v : m_musicBlock) { pk = std::max(pk, std::fabs(v)); }
        float pa = 0.0f;
        for (float v : m_ambienceBlock) { pa = std::max(pa, std::fabs(v)); }
        if (m_log) {
            m_log->info("audio-trace", "frames={} musicPeak={:.4f} ambPeak={:.4f} "
                                       "state={} era={} tension={:.2f} beds={} muted={}",
                        frames, pk, pa, static_cast<int>(m_musicState), static_cast<int>(m_era),
                        m_tension, m_bedsPlaying, m_muted);
        }
    }
#endif

    const int musicBytes = static_cast<int>(m_musicBlock.size() * sizeof(float));
    const int ambienceBytes = static_cast<int>(m_ambienceBlock.size() * sizeof(float));
    if (!SDL_PutAudioStreamData(static_cast<SDL_AudioStream*>(m_musicStream),
                                m_musicBlock.data(), musicBytes) ||
        !SDL_PutAudioStreamData(static_cast<SDL_AudioStream*>(m_ambienceStream),
                                m_ambienceBlock.data(), ambienceBytes)) {
        // Warned once rather than every frame: a stream that has stopped accepting
        // data will do so on every step from here on, and sixty identical lines a
        // second buries everything else in the log. The watchdog below restarts
        // the track regardless, so this is a diagnosis rather than a repair.
        ++m_putFailures;
        if (!m_reportedStreamFailure && m_log != nullptr) {
            m_reportedStreamFailure = true;
            m_log->warn("audio", "could not feed a bed stream: {}", SDL_GetError());
        }
    }

#ifdef ERASHIFT_AUDIO_TRACE
    if (m_traceCounter % 60 == 1) {
        const int backlog =
            SDL_GetAudioStreamAvailable(static_cast<SDL_AudioStream*>(m_musicStream));
        const bool playing =
            m_musicTrack != nullptr && MIX_TrackPlaying(static_cast<MIX_Track*>(m_musicTrack));
        if (m_log) {
            m_log->info("audio-trace", "backlog={}B playing={} mixerGain={}", backlog, playing,
                        m_muted ? 0.0f : m_masterVolume);
        }
    }
#endif

    ensureBedsPlaying();
}

float AudioManager::peakHold(float held, float current, float dt) noexcept
{
    // Instant attack, ~1.5s release. Long enough that one loud transient stays
    // visible, short enough that a bed which has genuinely stopped reading as
    // dead within a couple of seconds.
    constexpr float kReleasePerSecond = 0.8f;
    held -= held * kReleasePerSecond * dt;
    return (current > held) ? current : held;
}

float AudioManager::peakOf(const SampleBuffer& buffer) noexcept
{
    float high = 0.0f;
    for (const float sample : buffer) {
        high = std::max(high, std::fabs(sample));
    }
    return high;
}

AudioManager::Diagnostics AudioManager::diagnostics() const noexcept
{
    Diagnostics d;
    d.available  = m_available;
    d.audible    = audible();
    d.muted      = m_muted;
    d.bedsWanted = m_bedsPlaying;
    d.busVolume  = m_busVolume;
    d.masterVolume = m_masterVolume;
    d.lastMusicPeak    = m_lastMusicPeak;
    d.lastAmbiencePeak = m_lastAmbiencePeak;
    d.blocksFed   = m_blocksFed;
    d.putFailures = m_putFailures;

    if (m_musicTrack != nullptr) {
        d.musicPlaying = MIX_TrackPlaying(static_cast<MIX_Track*>(m_musicTrack));
    }
    if (m_ambienceTrack != nullptr) {
        d.ambiencePlaying = MIX_TrackPlaying(static_cast<MIX_Track*>(m_ambienceTrack));
    }
    if (m_musicStream != nullptr) {
        d.musicBacklogBytes =
            SDL_GetAudioStreamAvailable(static_cast<SDL_AudioStream*>(m_musicStream));
    }
    if (m_ambienceStream != nullptr) {
        d.ambienceBacklogBytes =
            SDL_GetAudioStreamAvailable(static_cast<SDL_AudioStream*>(m_ambienceStream));
    }
    for (const Channel& channel : m_channels) {
        if (channel.track != nullptr && MIX_TrackPlaying(static_cast<MIX_Track*>(channel.track))) {
            ++d.sfxVoices;
        }
    }
    return d;
}

std::string AudioManager::describe() const
{
    const Diagnostics d = diagnostics();
    std::string out;
    out += "driver      : " + std::string(m_driverName) + "\n";
    out += std::string("available   : ") + (d.available ? "yes" : "no") + "\n";
    out += std::string("audible     : ") + (d.audible ? "yes" : "no") +
           (d.muted ? "  (muted)" : "") + "\n";
    out += "buses       : master " + std::to_string(d.masterVolume) + "  music " +
           std::to_string(d.busVolume[static_cast<std::size_t>(Bus::Music)]) + "  ambience " +
           std::to_string(d.busVolume[static_cast<std::size_t>(Bus::Ambience)]) + "  sfx " +
           std::to_string(d.busVolume[static_cast<std::size_t>(Bus::Sfx)]) + "\n";
    out += "music       : " + std::string(d.musicPlaying ? "playing" : "STOPPED") +
           "  peak " + std::to_string(d.lastMusicPeak) + "  backlog " +
           std::to_string(d.musicBacklogBytes) + "B\n";
    out += "ambience    : " + std::string(d.ambiencePlaying ? "playing" : "STOPPED") +
           "  peak " + std::to_string(d.lastAmbiencePeak) + "  backlog " +
           std::to_string(d.ambienceBacklogBytes) + "B\n";
    out += "sfx voices  : " + std::to_string(d.sfxVoices) + "/" +
           std::to_string(kSfxChannels) + "\n";
    out += "blocks fed  : " + std::to_string(d.blocksFed) + "  put failures " +
           std::to_string(d.putFailures) + "\n";
    return out;
}

void AudioManager::setBusPercent(Bus bus, int percent) noexcept
{
    // A percentage straight out of a config file or a slider: anything outside
    // 0..100 is clamped, and `clampValue` resolves a NaN to 0 rather than letting
    // it through, because a NaN gain poisons every sample it multiplies.
    setBusVolume(bus, static_cast<float>(Graphics::clampValue(percent, 0, 100)) / 100.0f);
}

void AudioManager::setMasterPercent(int percent) noexcept
{
    setMasterVolume(static_cast<float>(Graphics::clampValue(percent, 0, 100)) / 100.0f);
}

void AudioManager::ensureBedsPlaying()
{
    // A bed must never stop, for any reason.
    //
    // The first version played each bed once at start-up and trusted it to run
    // forever. It did not: a track reading a stream that has run dry reaches the
    // end and stops, and nothing restarted it. The symptom was indistinguishable
    // from "the game is quiet" - the mixer reported itself healthy, every buffer
    // was non-empty, and the log said audio had started.
    //
    // This is not only about the start-up race. A PipeWire sink suspends when
    // idle, a Bluetooth headset appearing rewires the device, and a pause stops
    // feeding the streams entirely. Any of those ends a track, and every one of
    // them should be invisible to the player. A restart is cheap and idempotent:
    // the track resumes reading the stream from where the stream's read cursor
    // already is.
    const auto heal = [this](void* track) {
        if (track == nullptr) {
            return;
        }
        MIX_Track* t = static_cast<MIX_Track*>(track);
        if (MIX_TrackPlaying(t)) {
            return;
        }
        // Paused is deliberate - that is what setBedsPlaying(false) does - so a
        // paused bed is left exactly as it is.
        if (MIX_TrackPaused(t)) {
            return;
        }
        startLooping(track);
    };
    heal(m_musicTrack);
    heal(m_ambienceTrack);

}

void AudioManager::update(float dt, Era era, MusicState state, float tension)
{
    setEra(era);
    setMusicState(state);
    setTension(tension);
    if (!m_available || dt <= 0.0f) {
        return;
    }
    if (m_muted || !m_bedsPlaying) {
        // Still advance the synths so the beds are where they should be when
        // unmuted, rather than jumping forward from wherever they were.
        m_music.reset();
        m_ambience.reset();
        return;
    }
    pushBeds(dt);
}

void AudioManager::shutdown() noexcept
{
    if (m_mixer != nullptr) {
        for (Channel& channel : m_channels) {
            if (channel.track != nullptr) {
                MIX_StopTrack(static_cast<MIX_Track*>(channel.track), 0);
            }
        }
        MIX_DestroyMixer(static_cast<MIX_Mixer*>(m_mixer));
        m_mixer = nullptr;
    }
    // After the mixer, not before: these streams were being read by the mixer, and
    // destroying them while it still exists is a use-after-free rather than a
    // tidy-up.
    for (Channel& channel : m_channels) {
        if (channel.stream != nullptr) {
            SDL_DestroyAudioStream(channel.stream);
            channel.stream = nullptr;
        }
    }
    // The streams belong to the tracks that were reading them, and the mixer
    // being gone means nothing is reading. SDL frees them on quit.
    m_musicStream = nullptr;
    m_ambienceStream = nullptr;
    m_musicTrack = nullptr;
    m_ambienceTrack = nullptr;
    for (Channel& channel : m_channels) {
        channel = Channel{};
    }
    m_available = false;
    m_driverName = "none";
    MIX_Quit();
}


namespace {

const char* musicStateName(MusicState state) noexcept
{
    switch (state) {
        case MusicState::Exploring: return "Exploring";
        case MusicState::Alert:     return "Alert";
        case MusicState::Combat:    return "Combat";
        case MusicState::Still:     return "Still";
    }
    return "Unknown";
}

/// RMS and peak of a buffer, and how much of it is exactly zero.
///
/// The zero count is the one that catches the failure a peak cannot: a sound
/// that decays to silence for its last few milliseconds is correct, a sound
/// that is *entirely* zero is a synthesis or routing bug wearing a healthy
/// peak.
struct PcmStats {
    float rms      = 0.0f;
    float peak     = 0.0f;
    int   zeros    = 0;
    int   samples  = 0;
    float seconds  = 0.0f;
    int   clipped  = 0;
};

PcmStats measure(const SampleBuffer& buffer, int sampleRate) noexcept
{
    PcmStats s;
    s.samples = static_cast<int>(buffer.size());
    if (s.samples == 0 || sampleRate <= 0) {
        return s;
    }
    double sumSquares = 0.0;
    for (const float sample : buffer) {
        const float magnitude = std::fabs(sample);
        s.peak = std::max(s.peak, magnitude);
        if (sample == 0.0f) {
            ++s.zeros;
        }
        if (magnitude > 1.0f) {
            ++s.clipped;
        }
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }
    s.rms     = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(s.samples)));
    s.seconds = static_cast<float>(s.samples) / static_cast<float>(sampleRate * 2);
    return s;
}

} // namespace

int AudioManager::runSelfTest()
{
    // The eleven events the audio design commits to, in the order a player
    // meets them. Kept as a table so the log and the checks cannot drift apart.
    struct Step {
        const char* label;
        Sfx         sfx;
    };
    static constexpr Step kOneShots[] = {
        {"ui_click",        Sfx::UiConfirm},
        {"ui_cancel",       Sfx::UiDeny},
        {"ui_hover",        Sfx::UiMove},
        {"player_attack",   Sfx::Attack},
        {"player_hit",      Sfx::PlayerHurt},
        {"enemy_hit",       Sfx::EnemyHurt},
        {"enemy_death",     Sfx::EnemyDie},
        {"player_death",    Sfx::PlayerDie},
        {"era_shift",       Sfx::ShiftImpact},
        {"era_shift_charge", Sfx::ShiftCharge},
        {"chrono_pickup",   Sfx::PickupChrono},
        {"health_pickup",   Sfx::PickupHealth},
        {"seal_pickup",     Sfx::SealTaken},
        {"gate_opening",    Sfx::GateOpened},
        {"jump",            Sfx::Jump},
        {"land",            Sfx::Land},
        {"dash",            Sfx::Dash},
        {"hazard",          Sfx::Hazard},
        {"victory",         Sfx::Victory},
        // Every footstep surface, so a regression in one material shows up here
        // rather than only as "the floor sounds wrong somewhere".
        {"footstep_stone",  Sfx::FootstepStone},
        {"footstep_grass",  Sfx::FootstepGrass},
        {"footstep_sand",   Sfx::FootstepSand},
        {"footstep_metal",  Sfx::FootstepMetal},
        {"footstep_crystal", Sfx::FootstepCrystal},
    };

    const int rate = sampleRate();
    int failures  = 0;

    auto emit = [this](const std::string& line) {
        if (m_log != nullptr) {
            m_log->info("AUDIO TEST", "{}", line);
        }
    };

    // Which backends this SDL actually has compiled in. Runtime availability and
    // compiled-in availability are different questions, and the difference is how
    // a machine ends up with a working SDL and no way to make sound: asking SDL
    // for `alsa` on a build without it fails at open time, long after the point
    // where anyone would think to check.
    {
        const int count = SDL_GetNumAudioDrivers();
        std::string drivers;
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetAudioDriver(i);
            if (name != nullptr) {
                if (!drivers.empty()) {
                    drivers += ",";
                }
                drivers += name;
            }
        }
        emit("--- compiled-in SDL audio drivers ---");
        emit("available_drivers=" + (drivers.empty() ? std::string("none") : drivers));
    }

    emit("--- device ---");
    emit("device=" + std::string(m_driverName) + " rate=" + std::to_string(rate) +
         " channels=2 format=f32 mixer=" + std::string(m_mixer != nullptr ? "open" : "none") +
         " available=" + (m_available ? "true" : "false") +
         " audible=" + (audible() ? "true" : "false") +
         " muted=" + (m_muted ? "true" : "false"));
    emit("bus_gain master=" + std::to_string(masterVolume()) +
         " music=" + std::to_string(busVolume(Bus::Music)) +
         " ambience=" + std::to_string(busVolume(Bus::Ambience)) +
         " sfx=" + std::to_string(busVolume(Bus::Sfx)));

    if (!m_available) {
        emit("no audio device: measuring generated PCM only, playback not checked");
    }

    // --- one-shots -----------------------------------------------------------
    for (const Step& step : kOneShots) {
        // Rendered fresh rather than read from the cache, so this measures the
        // synthesiser. The cache is exercised immediately afterwards by
        // playing it.
        SampleBuffer rendered;
        renderSfx(step.sfx, rate, rendered);
        const PcmStats stats = measure(rendered, rate);

        // Feed the beds too, so the mixer keeps draining while the one-shot is
        // being timed, exactly as it would in play.
        const bool silent = stats.peak <= 0.0f || stats.rms <= 0.0f;
        const bool allZero = stats.zeros == stats.samples;
        if (silent || allZero) {
            ++failures;
        }
        if (stats.clipped > 0) {
            ++failures;
        }

        play(step.sfx);

        int voicesNow = 0;
        for (const Channel& channel : m_channels) {
            if (channel.track != nullptr && MIX_TrackPlaying(static_cast<MIX_Track*>(channel.track))) {
                ++voicesNow;
            }
        }

        // Let it actually sound: a fixed step per iteration keeps this the same
        // code path the game uses rather than a test-only shortcut.
        for (int i = 0; i < 12; ++i) {
            update(1.0f / 60.0f, m_era, m_musicState, m_tension);
        }

        int voices = 0;
        for (const Channel& channel : m_channels) {
            if (channel.track != nullptr && MIX_TrackPlaying(static_cast<MIX_Track*>(channel.track))) {
                ++voices;
            }
        }
        // Voices only mean something when there is a device to play them on.
        if (m_available && voices == 0) {
            ++failures;
        }

        std::string line = "[AUDIO TEST] event=" + std::string(step.label) +
                           " generated_samples=" + std::to_string(stats.samples) +
                           " state=GENERATED+QUEUED" +
                           " rms=" + std::to_string(stats.rms) +
                           " peak=" + std::to_string(stats.peak) +
                           " zero_samples=" + std::to_string(stats.zeros) +
                           " clipped=" + std::to_string(stats.clipped) +
                           " duration_s=" + std::to_string(stats.seconds) +
                           " track_playing=" + (voices > 0 ? "true" : "false") +
                           " voices_at_play=" + std::to_string(voicesNow) +
                           " bus_gain=" + std::to_string(busVolume(Bus::Sfx)) +
                           " master_gain=" + std::to_string(masterVolume()) +
                           " device=" + std::string(m_driverName) +
                           " verdict=" + (silent || allZero ? "SILENT" : "ok");
        emit(line);
    }

    // --- ambience bed --------------------------------------------------------
    {
        setBedsPlaying(true);
        for (int i = 0; i < 60; ++i) {
            update(1.0f / 60.0f, m_era, MusicState::Still, m_tension);
        }
        const Diagnostics d = diagnostics();
        // `lastAmbiencePeak` is a decaying hold, so a working bed that happens to
        // be between gusts still reads as non-zero.
        if (d.lastAmbiencePeak <= 0.0f) {
            ++failures;
        }
        if (m_available && !d.ambiencePlaying) {
            ++failures;
        }
        emit("[AUDIO TEST] event=ambient_loop generated_samples=n/a" +
             std::string(" rms=n/a peak=") + std::to_string(d.lastAmbiencePeak) +
             " stream_available=" + std::to_string(d.ambienceBacklogBytes) + "B" +
             " track_playing=" + (d.ambiencePlaying ? "true" : "false") +
             " bus_gain=" + std::to_string(busVolume(Bus::Ambience)) +
             " master_gain=" + std::to_string(masterVolume()) +
             " device=" + std::string(m_driverName) +
             " verdict=" + (d.lastAmbiencePeak > 0.0f ? "ok" : "SILENT"));
    }

    // --- music bed -----------------------------------------------------------
    {
        // All four documented states, so a state that produced nothing would
        // show up as a silent bed rather than as a plausible-looking log line.
        for (const MusicState state :
             {MusicState::Exploring, MusicState::Alert, MusicState::Combat, MusicState::Still}) {
            setMusicState(state);
            for (int i = 0; i < 30; ++i) {
                update(1.0f / 60.0f, m_era, state, m_tension);
            }
            const Diagnostics d = diagnostics();
            if (d.lastMusicPeak <= 0.0f) {
                ++failures;
            }
            if (m_available && !d.musicPlaying) {
                ++failures;
            }
            emit("[AUDIO TEST] event=music_loop state=" + std::string(musicStateName(state)) +
                 " generated_samples=n/a" +
                 " rms=n/a peak=" + std::to_string(d.lastMusicPeak) +
                 " stream_available=" + std::to_string(d.musicBacklogBytes) + "B" +
                 " track_playing=" + (d.musicPlaying ? "true" : "false") +
                 " bus_gain=" + std::to_string(busVolume(Bus::Music)) +
                 " master_gain=" + std::to_string(masterVolume()) +
                 " device=" + std::string(m_driverName) +
                 " verdict=" + (d.lastMusicPeak > 0.0f ? "ok" : "SILENT"));
        }
    }

    emit("--- result ---");
    emit("failures=" + std::to_string(failures));
    return failures;
}

} // namespace EraShift::Audio
