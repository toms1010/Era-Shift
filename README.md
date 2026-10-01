<div align="center">

# Era Shift

**One world. Three states. The state is the mechanic.**

A 2D action-adventure built on a single world that changes across three eras —
the Past, the Present and the Future — where changing time is both your movement
ability and your only defence.

![An era shift in flight: expanding shockwave rings, debris sparks, a screen flash and a camera punch](docs/screenshots/era-shift.png)

*An era shift caught mid-flight — expanding shockwave rings, debris sparks, a
screen flash and a camera punch, all triggered by one simulation event.*

</div>

> *The world is one world, but time changes its state.*

**Status: Phase 3 — playable vertical slice, with procedural animation and
effects.** The Ancient Forest runs from spawn to the Ancient Gate: era-dependent
terrain, enemies that only exist in some eras, melee combat, three era seals,
hazards, pickups, saves, and the full menu set. Every character is a procedurally
animated rig, and every impact has particles, light and hit-stop behind it.

There is not a single sprite asset in the repository, and
[`assets/README.md`](assets/README.md) explains why that is a decision rather
than an omission.

---

## Contents

- [How to run it](#how-to-run-it) · [Controls](#controls) · [How to play](#how-to-play)
- [Regions](#regions) · [Screenshots](#screenshots) · [Architecture](#architecture)
- [What is in the box](#what-is-in-the-box) — animation, effects
- [Project layout](#project-layout) · [Documentation](#documentation)
- [Build configurations](#build-configurations) · [Testing](#testing)
- [Development rules](#development-rules) · [Dependencies](#dependencies)

---

## How to run it

### Prerequisites

Either the distribution packages, or nothing to install:

```bash
sudo apt install build-essential cmake ninja-build \
                 libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev \
                 nlohmann-json3-dev libsqlite3-dev doctest-dev
```

If your distribution has no SDL3 packages — or you have no root access — build
them into the project instead. Nothing is installed system-wide:

```bash
./scripts/fetch_deps.sh          # into .deps/install, picked up automatically
```

`CMakeLists.txt` prefers system packages and only falls back to that local
prefix, so you can do either and skip the other.

### Build

Two ways, producing two different output paths. Pick one and stay with it.

**The presets** — the documented route, output in `build/<preset>/`:

```bash
cmake --preset debug
cmake --build --preset debug -j"$(nproc)"
./build/debug/EraShift
```

**A plain build directory** — output directly in `build/`:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
./build/EraShift
```

After either, the binary is `EraShift` next to its build directory. If you
reconfigure into `build/` and later reach for `cmake --build --preset debug`,
you will get "no work to do" and no binary — the presets use a *different*
directory, so mixing the two is the most common way to appear to have built
nothing.

### Run it

```bash
./build/debug/EraShift                      # or ./build/EraShift
```

It opens on the title screen. **NEW GAME** goes to the region select; pick a
region and the run begins. **CONTINUE** resumes a save if one exists.

No display attached — over SSH, or in a container:

```bash
./build/debug/EraShift --headless --frames 120
```

`--headless` forces SDL's dummy video driver. It quits after `--frames` frames
and prints a clean shutdown, so it works as a smoke test in CI.

### Test

```bash
ctest --preset debug --output-on-failure
```

348 cases across four binaries. All headless, no display required.

### Validate the regions

```bash
python3 tools/validate_levels.py
```

Checks every file in `data/levels/` for playability, not just loadability: era
layers agreeing in shape, one spawn, one seal per era, a gate in all three eras,
and a per-era flood fill from the spawn proving each gate and seal is reachable.
Exits non-zero when any region fails, so it can gate a commit.

### Command line

| Option | Purpose |
| --- | --- |
| `--frames <n>` | Quit after n frames. `0` means no limit. |
| `--headless` | Dummy video driver, for CI, SSH and containers. |
| `--screenshot <png>` | Write a PNG of the window. |
| `--screenshot-frame <n>` | Which frame to capture (default 30). |
| `--start-state <s>` | Open on `playing`, `settings`, `credits`, `paused` or `levelselect`. |
| `--level <file>` | Load a specific region instead of choosing one. |
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

# Check the region select renders and lists what is on disk
./build/debug/EraShift --headless --start-state levelselect --frames 60

# Jump straight into one region, skipping the menu
./build/debug/EraShift --headless --start-state playing --level data/levels/crystal_vault.json
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
It does not stop you playing; it changes how the game *feels* — the screen starts
to tear, the edges of the world close in. See [Paradox](#paradox).

---

## Regions

Eleven regions ship in `data/levels/`: a ten-region progression plus
`ancient_forest`, which predates the ordering and appears last. **NEW GAME** on the
title screen opens the region select, which lists whatever is in that directory and
locks anything the player has not reached yet.

| # | Region | The idea |
| --- | --- | --- |
| 1 | **Awakening** | The tutorial. One mechanic at a time; three crossings, one per era. |
| 2 | **Ancient Bridge** | Three crossings in a fixed order, no hand-holding. |
| 3 | **Fallen Span** | Era-specific hazards: the same ground is safe in exactly one era. |
| 4 | **Drowned Road** | A causeway half-built in the Past and half-standing in the Present. |
| 5 | **Crystal Vault** | Vertical. Three stacked shelves, each gated on a different era. |
| 6 | **Split Meadow** | Six alternating gates; the chrono on the floor does not cover all of them. |
| 7 | **Hollow Citadel** | Combat. Nine enemies, one open courtyard, nowhere to hide. |
| 8 | **Sunken Lattice** | A grid of five crossing-rows, cycling through the eras. |
| 9 | **Paradox Ward** | Six single-tile gates: a shift every few steps. |
| 10 | **Convergence** | Every earlier pattern at once, and a long way home. |
| — | **Ancient Forest** | The original hand-authored region. |

Each region declares an `order`, and the level select sorts on it rather than on
the filename — alphabetical order would have put Ancient Bridge before Awakening.
Finishing a region records it and unlocks the next.

The regions are authored by `tools/make_levels.py` rather than by hand. Each one
is described once as a list of edits and emitted as the three era layers, which
is what stops a typo in one layer from silently disagreeing with the other two:

```bash
python3 tools/make_levels.py           # regenerate the JSON files
python3 tools/make_levels.py --check   # validate without writing
```

The script refuses to emit a region that has no player spawn, not one seal per
era, no gate, an entity outside the grid, or an entity with nothing beneath it.

### Adding your own region

Drop a `.json` file in `data/levels/`. It appears in the select on next launch —
there is no menu to edit and nothing to keep in step.

```json
{
  "id": "my_region",
  "name": "My Region",
  "past":    ["##......##", "##......##", "##########"],
  "present": ["##......##", "##......##", "##########"],
  "future":  ["##......##", "##......##", "##########"],
  "entities": [
    { "type": "player", "x": 1, "y": 1 },
    { "type": "seal",   "era": "Past",    "x": 3, "y": 1 },
    { "type": "seal",   "era": "Present", "x": 5, "y": 1 },
    { "type": "seal",   "era": "Future",  "x": 7, "y": 1 },
    { "type": "enemy",  "kind": "Sentinel", "eras": "All",    "x": 6, "y": 1 },
    { "type": "pickup", "kind": "chrono",                    "x": 4, "y": 1 }
  ]
}
```

The three layers are **unioned**, not overwritten: a cell is solid in an era if
that era's layer says so. That is what lets one grid answer "what is solid right
now" for any era without rebuilding anything, and it is the whole vocabulary:

| Char | Tile | Solid in |
| --- | --- | --- |
| `#` | Solid | every era |
| `=` | Platform | every era, from above only |
| `c` | Crumble | **Past only** — masonry that has not rotted |
| `b` | Bridge | **Present only** — the span that still stands |
| `x` | Crystal | **Future only** — matter grown after the fact |
| `^` | Hazard | not solid; damages whatever stands in it |
| `G` | Gate | the exit, reachable only once all three seals are taken |
| `.` | Empty | never |

So a chasm crossed by `c` in the Past layer is open in the Present and the
Future. That is the level design: write a gap three times, once per era, and the
player has to change time to cross it.

Entity fields: `type` is `player`, `enemy`, `pickup` or `seal`. Enemies take a
`kind` (`Sentinel`, `Warden`, `Wisp`) and `eras` (`All`, `Past`, `Present`,
`Future`); wisps only exist in the Future and wardens only in the Past. Seals
take `era`. Pickups take `kind`: `chrono` or `health`. `label` is optional and
appears as the toast text.

A file that fails to parse is skipped rather than fatal — one broken region does
not make the select unopenable — and the skip is logged.

---

## Screenshots

Every image here is a real frame from the build, captured with the game's own
`--screenshot` flag. Nothing is mocked up.

### The three eras, one world

The same view of the Ancient Forest in two of its three time states. The terrain
is the same grid — what is solid, what is a hazard and what glows is all era
data, so the level is a sequence of era switches rather than a corridor.

| Future | Present |
| --- | --- |
| ![Future era: purple palette, cyan embers rising, crystal platforms](docs/screenshots/era-future.png) | ![Present era: grey palette, drifting dust, standing structures](docs/screenshots/era-present.png) |
| **Future.** Whole-tone palette, embers rising from below, crystal that has not grown yet. The only era where the ground is *added* rather than taken away. | **Present.** Natural-minor palette, near-weightless dust. The era that is supposed to look like ours, and the only one whose scale actually resolves. |

### Paradox at Collapse

![Paradox Collapse: red gauge, glitch bars, heavy vignette](docs/screenshots/paradox-collapse.png)

Paradox is the run's accumulation of shifting and killing, and it is the only
number in the game that changes how it *feels* rather than how it plays. At the
top tier the gauge reads `COLLAPSE`, the picture tears in horizontal bands, and
the edges of the world close in. The centre of the
screen stays clear on purpose — a game you cannot see is not tense, it is broken.
The objective line stays legible for the same reason.

### The title screen

![Title screen](docs/screenshots/title.png)

<details>
<summary>How to regenerate these</summary>

Every image above is a real frame, produced by the same flag documented above.
Nothing is drawn by hand, so they cannot drift out of date the way a mock-up
would.

```bash
# A gameplay frame
./build/release/EraShift --headless --demo --frames 340 \
    --start-state Playing \
    --screenshot docs/screenshots/era-future.png --screenshot-frame 80

# The title screen
./build/release/EraShift --headless --frames 120 \
    --screenshot docs/screenshots/title.png --screenshot-frame 45
```

`--demo` replaces input with the built-in attract script, which is why the run is
reproducible: the same frame number gives the same picture.

</details>

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
        Anim["AnimationController"]
        Particles["ParticleSystem"]
    end

    subgraph Engine["erashift_engine &nbsp;·&nbsp; everything SDL"]
        Cam["Camera2D<br/><i>shake · punch</i>"]
        Render["Renderer2D"]
        Win["Window"]
    end
    Loop -->|"render · alpha"| Render

    Playing -->|"update(dt)"| World
    World -.->|"takeEvents()<br/>what happened, and where"| Feedback["FeedbackSystem"]
    Playing --> Feedback
    Playing --> Anim

    Feedback --> Particles
    Feedback --> Cam
    Feedback --> Anim

    Particles --> Render
    Cam --> Render
    Anim --> Render
    Render --> Win
```

The box at the bottom left is the whole point of the split. `World`,
`ParticleSystem` and `AnimationController` are **pure arithmetic with no SDL
anywhere**, which is why the entire simulation — and the animation controller and
the particle pool — can be unit tested on a headless machine with no display.

### A gameplay step

```mermaid
sequenceDiagram
    autonumber
    participant P as Player
    participant W as World
    participant F as FeedbackSystem
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
    F->>PS: emitShiftBurst(pos, accent)
    F->>C: punch(0.035) + shake(9)
    F->>F: m_ripple, m_flash = 1
    PS->>PS: update(dt) — runs even during hit-stop

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

### Animation — poses, clips, and markers

A character is four rectangles. A rectangle animated well needs nine numbers.

- **`Game::Pose`** is those nine: squash, stretch, bob, lean, limb swing, limb
  spread, arm extension, alpha.
- **21 clips** — idle, walk, run, jump, fall, land, dash, attack, hurt, death,
  shift-charge, and eight enemy states.
- **Clips carry markers**, and a marker can be a gameplay event. The attack's
  hit window is at 0.06s because `PlayerTuning` says the windup ends there, and a
  test asserts the two still agree.
- **Which clip plays is a function of the simulation, in `Game/Animation.cpp`**,
  next to the clip table rather than in the state that draws the player — so the
  headless gameplay suite can assert on the decision. Being struck and dying are
  forced past the rule below, because a reaction the player cannot see is a
  reaction that did not happen.
- **Non-looping clips refuse to be cut short**, so mashing attack does not
  visually restart the swing while the input buffer queues a second one.
- **The charge pose is translucent, and only appears while the shift key is
  actually down** — the same condition that drives the charge aura, so the
  character and the particles cannot disagree about whether the player is
  charging.
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
| `Strained` | 18 | slight vignette, denser weather |
| `Fractured` | 38 | glitch bars, heavier vignette |
| `Collapse` | 62 | the bar turns red, the tearing is constant, the edges close in |

One eased value, `FeedbackSystem::tension()`, drives the vignette, the glitch bars
and the weather density, so everything destabilises together rather than in
sequence.

---

## Project layout

```
include/EraShift/       Public headers, mirroring src/
  Core/                 Logging, config, timing, game loop, state machine  (no SDL)
  Application/          Engine, event bus, state context
  Graphics/             Window, renderer, textures, text, maths, colour,
                        ParticleSystem
  Input/                Keyboard, mouse, action bindings
  Debug/                Performance counters, overlay
  Game/                 World, player, enemies, level, Animation, Feedback,
                        EraTheme
  Game/states/          Title, level select, playing, paused, results,
                        settings, credits
src/EraShift/           Implementations
tests/
  unit/                 No SDL, no display
  integration/          No display, several systems together
  gameplay/             The whole simulation, plus animation, particles, the
                        tutorial, every region and the progression database.
                        Still no SDL, still no display
assets/                 One font. See assets/README.md for why that is all
data/levels/            Data-driven content (JSON)
config/                 Layered JSON configuration
scripts/                Dependency bootstrap, crash harness, font generator
tools/                  Region authoring and validation (Python)
docs/                   Architecture and design documents
  screenshots/          Real captured frames, referenced by this file
```

`erashift_core` contains **no SDL dependency at all**. That is not tidiness — it
is the reason the whole test suite, including the animation controller and the
particle system, runs on a headless machine.

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

The four binaries, and what each is for:

| Binary | Cases | Covers |
| --- | --- | --- |
| `erashift_tests_unit` | 119 | Maths, logging, config, timing, the game loop, the state machine, the fonts, line breaking, menus |
| `erashift_tests_integration` | 10 | The documented state flow, config round trips and migrations |
| `erashift_tests_gameplay` | 209 | The whole simulation, plus the animation controller, the particle pool, the tutorial, every region, and the progression database |

Line breaking has its own tests because it is where "text stays inside the panel"
is actually enforced: `wrapLines` takes its measurement as a callback, so the
rules are testable without a window, a renderer or a loaded font.
| `erashift_tests_platform` | 10 | The SDL boundary: the event pump |

`gameplay` links **only** `erashift_core` and no SDL whatsoever. That is the rule
that proves the whole simulation is testable on a machine with no display, and it
is why the platform tests are a separate binary rather than folded into it.

Four binaries, all headless: `unit` (no SDL), `integration` (no display),
`gameplay` (the whole simulation) and `platform` (the SDL boundary). The feedback
systems are tested alongside the gameplay, because they are pure arithmetic — 41
cases covering the animation controller and the particle pool, with assertions on
clip timings and emission rates rather than on pictures.

Every region is loaded through the game's own loader in
`tests/gameplay/TestRegions.cpp`, and the progression database's failure paths —
an unopenable file, a corrupt one, a schema from a newer build — are covered in
`tests/gameplay/TestProgressDatabase.cpp`. Neither needs a display or a sound
card.

`platform` covers what cannot be: the event pump, which turns SDL events into the
game's own.

See [`docs/TESTING.md`](docs/TESTING.md).

---

## Documentation

| Document | Contents |
| --- | --- |
| [docs/BUILD.md](docs/BUILD.md) | Dependencies, presets, troubleshooting |
| [docs/CONTROLS.md](docs/CONTROLS.md) | Controls and rebinding |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layering, ownership, the game loop |
| [docs/FEEDBACK.md](docs/FEEDBACK.md) | **Animation, effects, hit-stop, paradox** |
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
3. Add tests alongside the system they cover. The animation controller and the
   particle system are both testable headlessly, so there is no excuse.
4. The simulation decides *what* happens. Presentation decides what it looks like.
   A rule that depends on an effect having finished is not a rule.
5. Update `CHANGELOG.md` and the affected doc in the same commit.
6. Never remove existing functionality without a note in the changelog.

---

## Dependencies

| Library | Version | Purpose |
| --- | --- | --- |
| SDL3 | 3.4.16 | Windowing, input, rendering, events |
| SDL3_image | 3.4.6 | Image loading |
| SDL3_ttf | 3.2.2 | Font rasterisation |
| nlohmann/json | 3.11.3 | Configuration and save data |
| SQLite | 3.45+ | Progression: unlocks, per-region results, checkpoints |
| doctest | 2.4.12 | Unit testing |

`scripts/fetch_deps.sh` builds all of them into `.deps/install` **without
needing root**. Distribution packages are used automatically when present.

Fonts: `assets/fonts/SpaceGrotesk.ttf` (SIL Open Font License, see
`assets/fonts/SpaceGrotesk-OFL.txt`).

---

## License

See [LICENSE](LICENSE).
