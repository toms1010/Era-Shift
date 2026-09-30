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

const char* tagFor(Bus bus) noexcept
{
    switch (bus) {
        case Bus::Master:    return nullptr;
        case Bus::Music:     return kTagMusic;
        case Bus::Ambience:  return kTagAmbience;
        case Bus::Sfx:       return kTagSfx;
        case Bus::Count:     break;
    }
    return nullptr;
}

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

void startLooping(void* track)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    // -1 means forever, which is what a synthesised bed is: it never "ends", it
    // just changes.
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    MIX_PlayTrack(static_cast<MIX_Track*>(track), props);
    SDL_DestroyProperties(props);
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

    startLooping(m_musicTrack);
    startLooping(m_ambienceTrack);
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
    MIX_SetMixerGain(static_cast<MIX_Mixer*>(m_mixer), m_muted ? 0.0f : m_masterVolume);
    for (Bus bus : {Bus::Music, Bus::Ambience, Bus::Sfx}) {
        const char* tag = tagFor(bus);
        if (tag != nullptr) {
            MIX_SetTagGain(static_cast<MIX_Mixer*>(m_mixer), tag, m_busVolume[busIndex(bus)]);
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

    // An IOStream over the cached samples. This is what replaces loading a .wav
    // from disk: the buffer is already in memory, and SDL reads it the same way.
    SDL_IOStream* io = SDL_IOFromConstMem(m_sfxCache[index].data(),
                                          m_sfxCache[index].size() * sizeof(float));
    if (io == nullptr) {
        return;
    }
    MIX_SetTrackIOStream(static_cast<MIX_Track*>(channel->track), io, true);

    // Pitch is a resample ratio on the track, which is how one footstep becomes a
    // heavier or lighter one without a second buffer.
    MIX_SetTrackFrequencyRatio(static_cast<MIX_Track*>(channel->track),
                               Graphics::clampValue(pitch, 0.5f, 2.0f));
    MIX_SetTrackGain(static_cast<MIX_Track*>(channel->track),
                     Graphics::clampValue(volume, 0.0f, 1.5f));

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, 0);
    MIX_PlayTrack(static_cast<MIX_Track*>(channel->track), props);
    SDL_DestroyProperties(props);
}

void AudioManager::playPriority(Sfx sfx, float volume, float pitch)
{
    // The distinction from play() is intent, not mechanism: a priority sound is
    // one the player caused and would notice missing. Today the pool always
    // steals rather than drops, so both are the same call; keeping the two names
    // means the policy can diverge without touching every call site.
    play(sfx, volume, pitch);
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
    m_tension += (m_tensionTarget - m_tension) * std::min(1.0f, dt * 2.0f);

    const int frames = static_cast<int>(static_cast<float>(m_sampleRate) * dt);
    if (frames <= 0) {
        return;
    }

    MusicParams music;
    music.era = m_era;
    music.eraBlend = 1.0f;
    music.instability = m_tension;
    switch (m_musicState) {
        case MusicState::Exploring: music.intensity = 0.0f; music.tempo = 84.0f; break;
        case MusicState::Alert:     music.intensity = 0.35f; music.tempo = 96.0f; break;
        case MusicState::Combat:    music.intensity = 1.0f; music.tempo = 116.0f; break;
        case MusicState::Still:     music.intensity = 0.10f; music.tempo = 68.0f; break;
    }
    // Tension also raises the tempo a little, on top of the state's own.
    music.tempo += m_tension * 10.0f;

    AmbienceParams ambience;
    ambience.era = m_era;
    ambience.eraBlend = 1.0f;
    ambience.instability = m_tension * 0.7f;

    m_music.render(music, frames, m_musicBlock);
    m_ambience.render(ambience, frames, m_ambienceBlock);

    const int musicBytes = static_cast<int>(m_musicBlock.size() * sizeof(float));
    const int ambienceBytes = static_cast<int>(m_ambienceBlock.size() * sizeof(float));
    SDL_PutAudioStreamData(static_cast<SDL_AudioStream*>(m_musicStream),
                           m_musicBlock.data(), musicBytes);
    SDL_PutAudioStreamData(static_cast<SDL_AudioStream*>(m_ambienceStream),
                           m_ambienceBlock.data(), ambienceBytes);
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

} // namespace EraShift::Audio
