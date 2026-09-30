<div align="center">

# Era Shift

**One world. Three states. The state is the mechanic.**

A 2D action-adventure built on a single world that changes across three eras —
the Past, the Present and the Future — where changing time is both your movement
ability and your only defence.

</div>

> *The world is one world, but time changes its state.*

**Status: Phase 3 — playable vertical slice, with procedural audio, animation
and effects.** The Ancient Forest runs from spawn to the Ancient Gate: era-dependent
terrain, enemies that only exist in some eras, melee combat, three era seals,
hazards, pickups, saves, and the full menu set. Every sound is synthesised at
runtime, every character is a procedurally animated rig, and every impact has
particles, light and hit-stop behind it.

There is not a single audio or sprite asset in the repository, and
[`assets/README.md`](assets/README.md) explains why that is a decision rather
than an omission.

---

## Contents

- [Quick start](#quick-start) · [Controls](#controls) · [How to play](#how-to-play)
- [Architecture](#architecture) — the two diagrams
- [What is in the box](#what-is-in-the-box) — audio, animation, effects
- [Project layout](#project-layout) · [Documentation](#documentation)
- [Build configurations](#build-configurations) · [Testing](#testing)
- [Development rules](#development-rules) · [Dependencies](#dependencies)

---

## Quick start

```bash
# 1. Dependencies (only needed if your distro has no SDL3 packages)
./scripts/fetch_deps.sh

# 2. Build
cmake --preset debug
cmake --build --preset debug

# 3. Run
./build/debug/EraShift

# 4. Tests — headless, no display, no sound card
ctest --preset debug --output-on-failure
```

### Command line

| Option | Purpose |
| --- | --- |
| `--frames <n>` | Quit after n frames. `0` means no limit. |
| `--headless` | Dummy video driver, for CI, SSH and containers. |
| `--screenshot <png>` | Write a PNG of the window. |
| `--screenshot-frame <n>` | Which frame to capture (default 30). |
| `--start-state <s>` | Open on `playing`, `settings`, `credits` or `paused`. |
| `--level <file>` | Load a specific level instead of the shipped one. |
| `--demo` | Replace input with the built-in attract script. |
| `--content <dir>` | Load assets and data from elsewhere. |
| `--set key=value` | Override any configuration value. |
| `--log-level`, `--log-file` | Logging. |

`--start-state`, `--level` and `--demo` exist so every screen can be captured and
smoke-tested without playing through to it. That is also how the screenshots in
this repository were produced.

```bash
# Headless smoke test, as used in CI
./build/debug/EraShift --headless --frames 120

# Capture a frame mid-run
./build/debug/EraShift --demo --frames 200 --start-state playing \
    --screenshot /tmp/shot.png --screenshot-frame 200

# Force a fully mixed, fully paradoxed run without playing it
./build/debug/EraShift --demo --set audio.enabled=false --frames 400
```

---

## Controls

| Key | Action |
| --- | --- |
| `A` `D` / arrows | Move |
| `Space` | Jump — hold for height, tap for a short hop |
| `Left Shift` | Dash, with brief invulnerability |
| `Left Mouse` | Attack |
| `E` | Interact, take a seal, confirm |
| `Q` | **Era Shift** — the signature mechanic |
| `Esc` | Pause |
| `F3` | Debug overlay |
| `F12` | Screenshot |

Everything is rebindable in Settings → Controls, and the bindings persist.

---

## How to play

The Ancient Gate stays shut until all three era seals are recovered, and each seal
only yields while the world is in *its* era. The terrain is written the same way:
one chasm is bridged by crumbling stone that only exists in the Past, the next by
a structure that still stands in the Present, the last by crystal that has not
grown yet in the Future. The level is therefore a sequence of era switches rather
than a corridor — walk until the ground runs out, then change time.

**Shifting costs chrono energy**, which regenerates slowly and is refilled by the
blue cells. Wardens only exist in the Past and wisps only in the Future, and
neither can be hit while it is not in the world. So:

- **Shifting out** is a way to escape an enemy you cannot fight.
- **Shifting in** is the only way to fight it.

Each shift and each kill adds **paradox**, and paradox is the run's own pressure.
It does not stop you playing; it changes how the game *feels* — the music loses
its footing, the screen starts to tear, the edges of the world close in. See
[Paradox](#paradox).

---

## Architecture

Two diagrams, because one is not enough to say anything useful. The first is the
frame: who calls whom, and when. The second is the data flow *within* a frame for
the gameplay path, which is where the interesting constraint lives.

### The frame

```mermaid
flowchart TD
    OS["OS events"] --> Pump["EventPump"]
    Pump --> Bus["EventBus"]
    Pump --> Input["InputManager<br/><i>device state</i>"]
    Input -->|"mapped"| Action["Input::Action"]
    Action --> Loop
    Bus --> Loop

    Loop["GameLoop"] -->|"update · fixed 1/60 s"| States["StateMachine"]
    States --> Playing["PlayingState<br/>Paused · Menu · Result · Settings"]

    subgraph Core["erashift_core &nbsp;·&nbsp; no SDL anywhere"]
        World["World<br/><i>the simulation</i>"]
        Synth["MusicSynth · AmbienceSynth<br/>renderSfx"]
        Anim["AnimationController"]
        Particles["ParticleSystem"]
    end

    subgraph Engine["erashift_engine &nbsp;·&nbsp; everything SDL"]
        Audio["AudioManager"]
        Cam["Camera2D<br/><i>shake · punch</i>"]
        Render["Renderer2D"]
        Win["Window"]
    end
    Loop -->|"render · alpha"| Render

    Playing -->|"update(dt)"| World
    World -.->|"takeEvents()<br/>what happened, and where"| Feedback["FeedbackSystem"]
    Playing --> Feedback
    Playing --> Anim

    World --> Synth
    Feedback --> Audio
    Audio --> Synth
    Feedback --> Particles
    Feedback --> Cam
    Feedback --> Anim

    Particles --> Render
    Cam --> Render
    Anim --> Render
    Render --> Win
```

The two boxes at the bottom left are the whole point of the split. `World` and
`ParticleSystem` are **pure arithmetic with no SDL anywhere**, which is why the
entire simulation — and now the synthesiser, the animation controller and the
particle pool — can be unit tested on a headless machine with no sound card and
no display.

### A gameplay step

```mermaid
sequenceDiagram
    autonumber
    participant P as Player
    participant W as World
    participant F as FeedbackSystem
    participant A as AudioManager
    participant PS as ParticleSystem
    participant C as Camera2D
    participant AC as AnimationController

    Note over P,W: fixed step, 1/60 s
    W->>W: hit-stop? freeze and return
    W->>P: update(map, input, dt)
    P-->>W: stepEvents (jumped · landed · footstep · swing)
    W->>W: enemies, combat, pickups, seals
    W->>W: emit WorldEvent into the queue

    Note over F: the same step
    W-->>F: takeEvents()
    F->>A: play(ShiftImpact)
    F->>PS: emitShiftBurst(pos, accent)
    F->>C: punch(0.035) + shake(9)
    F->>F: m_ripple, m_flash = 1
    F->>PS: update(dt) — runs even during hit-stop
    F->>A: setMusicState(Combat), setTension(t)

    Note over AC: simulation owns the clock
    P-->>AC: attackProgress() 0..1
    AC->>AC: setTime(progress x clip duration)

    Note over PS,C: render, next frame
    PS-->>PS: drawWorld — four passes, one blend change each
    F->>F: drawScreen — ripple · vignette · glitch · flash
```

Two things in that diagram are worth reading twice.

**`FeedbackSystem` runs even when the world is frozen.** On a heavy hit the
simulation stops for 45–200ms, and the spark that caused the freeze keeps
travelling. That is what hit-stop *is*: a still world and a live effect.

**The animation has no clock of its own.** It is told where the simulation is,
via `attackProgress()`. Two independent timers over the same 0.32 seconds would
work right up until a frame hitched, and then the swing would have visibly
recovered while the hitbox was still open.

The reasoning behind every layer is in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md); the feedback systems in
[`docs/FEEDBACK.md`](docs/FEEDBACK.md).

---

## What is in the box

### Audio — synthesised, streamed, four buses

Every sound is computed at runtime. There is no `.wav` in the repository, and
that is what makes the music able to do the thing the game needs it to do.

- **Music and ambience are streams, not loops.** An `SDL_AudioStream` per bed is
  fed a block of float PCM every fixed step and the track loops forever over it.
  Changing era is a change to *parameters* — scale, cutoff, detune — eased over a
  third of a second. Two crossfading loops cannot glide the Past into the Future
  mid-bar; a synthesiser does it because the scale is a variable.
- **The three eras are different keys.** Past is a minor pentatonic, Present a
  natural minor, Future a whole tone. A symmetric scale has no leading tone, so a
  phrase built from it has nowhere to go — that is the sound of a timeline coming
  apart, and it falls out of the degree table rather than a filter sweep.
- **Four buses** — master, music, ambience, sfx — as SDL3_mixer tags, on the
  audio settings page, changing the live mixer rather than only the file.
- **20 one-shots**, rendered once at start-up and played through a pool of six
  tracks. Pitch and level follow the impact, so a step off a ledge is quiet and a
  drop from height is not.
- **Fails soft.** No sound card, no container, no CI runner: the game is still
  playable, and `MIX_Init` failing on a machine with no MIDI tables is logged and
  ignored, because every sound here is PCM.

### Animation — poses, clips, and markers

A character is four rectangles. A rectangle animated well needs nine numbers.

- **`Game::Pose`** is those nine: squash, stretch, bob, lean, limb swing, limb
  spread, arm extension, alpha.
- **21 clips** — idle, walk, run, jump, fall, land, dash, attack, hurt, death,
  shift-charge, and eight enemy states.
- **Clips carry markers**, and a marker can be a gameplay event. The attack's
  hit window is at 0.06s because `PlayerTuning` says the windup ends there, and a
  test asserts the two still agree.
- **Non-looping clips refuse to be cut short**, so mashing attack does not
  visually restart the swing while the input buffer queues a second one.
- **Footsteps are counted in ground covered, not in time.** A clock-tied footstep
  slides at low speed and scrabbles at high speed.

### Effects — particles, light, camera

- **A fixed pool of 768 particles, no allocation during play.** When it is full
  the oldest is recycled, so a new shift beats an old footstep.
- **Era weather that is actually different.** Leaves fall from above in the Past,
  dust drifts in the Present, embers rise from below in the Future — opposite
  halves of the screen, which is what the eye reads as a different place.
- **The shift**, in one call: particles pulled inward while charging, then a
  three-ring shockwave and 34 sparks outward.
- **Camera punch** as well as shake, decaying back to the zoom the player set
  rather than pushing it permanently.
- **Screen effects**: a ripple on a shift, a paradox vignette, glitch bars that
  regenerate every 160ms rather than every frame, and a flash.
- **Hit-stop** in the simulation, not the renderer, so the enemy stops too.

### Paradox

Paradox is the run's accumulation of shifting and killing. It is the only number
in the game that changes how it *feels* rather than how it plays, so it has named
tiers rather than a slider — a player can be told "you are at Tier 3" and
understand that, whereas "your paradox is 41.2" means nothing.

| Tier | Paradox | What changes |
| --- | --- | --- |
| `Calm` | 0 | — |
| `Strained` | 18 | tempo instability, slight vignette, denser weather |
| `Fractured` | 38 | audibly unstable, glitch bars, heavier vignette |
| `Collapse` | 62 | the bar turns red, the music drops out, the edges close in |

One eased value, `FeedbackSystem::tension()`, drives both the audio and the
renderer. A player who hears the music destabilise sees the edges close in at the
same moment, and the causal link is legible.

---

## Project layout

```
include/EraShift/       Public headers, mirroring src/
  Core/                 Logging, config, timing, game loop, state machine  (no SDL)
  Application/          Engine, event bus, state context
  Graphics/             Window, renderer, textures, text, maths, colour,
                        ParticleSystem
  Audio/                Synth (no SDL) and AudioManager (SDL3_mixer)
  Input/                Keyboard, mouse, action bindings
  Debug/                Performance counters, overlay
  Game/                 World, player, enemies, level, Animation, Feedback,
                        EraTheme
  Game/states/          Title, playing, paused, results, settings, credits
src/EraShift/           Implementations
tests/
  unit/                 No SDL, no display
  integration/          No display, several systems together
  gameplay/             The whole simulation, plus the synth, animation and
                        particle systems. Still no SDL, still no sound card
assets/                 One font. See assets/README.md for why that is all
data/levels/            Data-driven content (JSON)
config/                 Layered JSON configuration
scripts/                Dependency bootstrap, crash harness, font generator
docs/                   Architecture and design documents
```

`erashift_core` contains **no SDL dependency at all**. That is not tidiness — it
is the reason the whole test suite, including the synthesiser and the particle
system, runs on a headless machine.

---

## Build configurations

| Preset | Path | Sanitizers | Tests |
| --- | --- | --- | --- |
| `debug` | `build/debug` | – | yes |
| `release` | `build/release` | – | no, `-Werror` |
| `relwithdebinfo` | `build/relwithdebinfo` | – | yes |
| `asan` | `build/asan` | Address + UB | yes |
| `ubsan` | `build/ubsan` | Undefined behaviour | yes |
| `tsan` | `build/tsan` | Thread | yes |

```bash
cmake --preset asan && cmake --build --preset asan
ASAN_OPTIONS=detect_leaks=1 ./build/asan/EraShift --headless --frames 300
ctest --preset asan
```

---

## Testing

```bash
ctest --preset debug --output-on-failure
./build/debug/erashift_tests_gameplay --success   # verbose
./scripts/reproduce_crash.sh                       # crash regression harness
```

Three binaries, all headless: `unit` (no SDL), `integration` (no display) and
`gameplay` (the whole simulation). The feedback systems are tested in the same
place as the gameplay, because they are pure arithmetic — 44 cases covering the
synthesiser, the animation controller and the particle pool, with assertions on
waveform properties, clip timings and emission rates rather than on pictures.

See [`docs/TESTING.md`](docs/TESTING.md).

---

## Documentation

| Document | Contents |
| --- | --- |
| [docs/BUILD.md](docs/BUILD.md) | Dependencies, presets, troubleshooting |
| [docs/CONTROLS.md](docs/CONTROLS.md) | Controls and rebinding |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layering, ownership, the game loop |
| [docs/FEEDBACK.md](docs/FEEDBACK.md) | **Audio, animation, effects, hit-stop, paradox** |
| [docs/TESTING.md](docs/TESTING.md) | Test strategy, sanitizers |
| [docs/PERFORMANCE.md](docs/PERFORMANCE.md) | Profiling, budgets |
| [docs/TECHNICAL_DESIGN.md](docs/TECHNICAL_DESIGN.md) | Engine subsystem designs |
| [docs/GAME_DESIGN.md](docs/GAME_DESIGN.md) | The loop, the resources, the enemies, the combat |
| [docs/ERA_SYSTEM.md](docs/ERA_SYSTEM.md) | How one world is three worlds |
| [docs/CRASH_ANALYSIS.md](docs/CRASH_ANALYSIS.md) | Post-mortem of the Phase 1 SIGSEGV |
| [CHANGELOG.md](CHANGELOG.md) | What changed and when |

`GAME_DESIGN.md`, `ERA_SYSTEM.md` and `FEEDBACK.md` describe the game as it is
implemented, so they stay true rather than aspirational. `TIMELINE_SYSTEM.md` is
deferred: the era cycle is complete, but the persistence rules a full timeline
system needs are not designed yet, and a document describing them would be
fiction.

---

## Development rules

1. Build after every meaningful change; the tree must stay warning-free.
   `release` builds with `-Werror`.
2. Run `ctest` before committing. All presets, including the sanitizers.
3. Add tests alongside the system they cover. The synthesiser, the animation
   controller and the particle system are all testable headlessly, so there is no
   excuse.
4. The simulation decides *what* happens. Presentation decides what it looks and
   sounds like. A rule that depends on a sound having finished is not a rule.
5. Update `CHANGELOG.md` and the affected doc in the same commit.
6. Never remove existing functionality without a note in the changelog.

---

## Dependencies

| Library | Version | Purpose |
| --- | --- | --- |
| SDL3 | 3.4.16 | Windowing, input, rendering, events, audio streams |
| SDL3_image | 3.4.6 | Image loading |
| SDL3_ttf | 3.2.2 | Font rasterisation |
| SDL3_mixer | 3.2.4 | Mixing and routing the synthesised beds |
| nlohmann/json | 3.11.3 | Configuration and save data |
| doctest | 2.4.12 | Unit testing |

`scripts/fetch_deps.sh` builds all of them into `.deps/install` **without
needing root**. Distribution packages are used automatically when present.

Fonts: `assets/fonts/SpaceGrotesk.ttf` (SIL Open Font License, see
`assets/fonts/SpaceGrotesk-OFL.txt`).

---

## License

See [LICENSE](LICENSE).
