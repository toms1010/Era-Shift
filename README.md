# Era Shift

A 2D open-world action-adventure / puzzle-platformer built on **one shared world
that changes across three eras** (Past, Present, Future).

> *The world is one world, but time changes its state.*

**Current status: Phase 1 — Engine Foundation.** The window opens, the title
screen renders, the main menu is navigable, and a test scene demonstrates
movement, the camera, era shifting, pause and settings. Gameplay systems
(player, timeline, combat, AI) are not implemented yet.

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
```

Then:

| Key | Action |
| --- | --- |
| `W` `A` `S` `D` / arrows | Move |
| `Space` | Jump |
| `E` | Interact / confirm |
| `Esc` | Pause |
| `Q` | Era Shift (the signature mechanic) |
| `F3` | Debug overlay |
| `F12` | Screenshot |

See [`docs/BUILD.md`](docs/BUILD.md) for build options and
[`docs/CONTROLS.md`](docs/CONTROLS.md) for the full list.

---

## Command line

```
./build/debug/EraShift [options]

  --set <key=value>      Override a configuration value
  --content <dir>        Load assets and data from <dir>
  --log-level <level>    TRACE | DEBUG | INFO | WARN | ERROR | FATAL | OFF
  --log-file <path>      Also write the log to <path>
  --frames <n>           Quit after n frames (0 = no limit)
  --headless             Use the dummy video driver (CI, SSH, containers)
  --screenshot <png>     Write a PNG of the window
  --screenshot-frame <n>  Take that screenshot on frame <n>
  --help                 Show usage
```

Examples:

```bash
# Headless smoke test, as used in CI
./build/debug/EraShift --headless --frames 120

# Capture a frame for review
./build/debug/EraShift --frames 60 --screenshot /tmp/shot.png

# Override a setting for one run
./build/debug/EraShift --set graphics.windowWidth=1920
```

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
ctest --preset debug --output-on-failure     # 890+ assertions
./build/debug/erashift_tests_unit --success  # verbose
./scripts/reproduce_crash.sh                 # crash regression harness
```

The suite needs no display server. See [`docs/TESTING.md`](docs/TESTING.md).

---

## Project layout

```
include/EraShift/     Public headers, mirroring src/
  Core/               Logging, config, timing, game loop, state machine  (no SDL)
  Application/        Engine, event bus, state context
  Graphics/           Window, renderer, textures, text, maths, colour
  Input/              Keyboard, mouse, action bindings
  Debug/              Performance counters, overlay
  Game/states/        Title screen, test scene, pause, settings
src/EraShift/         Implementations
tests/                unit/ integration/
assets/               Sprites, audio, fonts
data/                 Data-driven content (JSON)
config/               Layered JSON configuration
scripts/              Dependency bootstrap, crash harness, font generator
docs/                 Architecture and design documents
```

The `Core` library contains **no SDL dependency at all**, which is what lets the
whole test suite run headlessly.

---

## Architecture in one diagram

```
   OS events
       |
       v
  EventPump  ----------------->  EventBus
       |                              |
       v                              v
  InputManager  (device state)   Input::Action
       |                              |
       +-----------+------------------+
                   v
              GameLoop  ---- fixed 1/60 s ---->  StateMachine.update()
                   |                                     |
                   |                                     v
                   |                              Playing / Menu / ...
                   |
                   +-------- once per frame ---->  StateMachine.render(alpha)
                                                              |
                                   +--------------------------+------------------+
                                   v                                             v
                            Renderer2D -> Window -> present              TextRenderer (TTF)
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the reasoning behind each
layer.

---

## Documentation

| Document | Contents |
| --- | --- |
| [docs/BUILD.md](docs/BUILD.md) | Dependencies, presets, troubleshooting |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layering, ownership, the game loop |
| [docs/TESTING.md](docs/TESTING.md) | Test strategy, sanitizers |
| [docs/PERFORMANCE.md](docs/PERFORMANCE.md) | Profiling, budgets |
| [docs/TECHNICAL_DESIGN.md](docs/TECHNICAL_DESIGN.md) | Engine subsystem designs |
| [docs/CRASH_ANALYSIS.md](docs/CRASH_ANALYSIS.md) | Post-mortem of the Phase 1 SIGSEGV |
| [CHANGELOG.md](CHANGELOG.md) | What changed and when |

Game design documents (`GAME_DESIGN.md`, `ERA_SYSTEM.md`, `TIMELINE_SYSTEM.md`,
and the rest) are written phase by phase as the systems they describe are
implemented, so they stay true rather than aspirational.

---

## Development rules

1. Build after every meaningful change; the tree must stay warning-free.
2. Run `ctest` before committing.
3. Add tests alongside the system they cover.
4. Update `CHANGELOG.md` and the affected doc in the same commit.
5. Never remove existing functionality without a note in the changelog.

---

## Dependencies

| Library | Version | Purpose |
| --- | --- | --- |
| SDL3 | 3.4.16 | Windowing, input, rendering, events |
| SDL3_image | 3.4.6 | Image loading |
| SDL3_ttf | 3.2.2 | Font rasterisation |
| SDL3_mixer | 3.2.4 | Audio (wired up in Phase 5) |
| nlohmann/json | 3.11.3 | Configuration and save data |
| doctest | 2.4.12 | Unit testing |

`scripts/fetch_deps.sh` builds all of them into `.deps/install` **without
needing root**. Distribution packages are used automatically when present.

Fonts: `assets/fonts/SpaceGrotesk.ttf` (SIL Open Font License, see
`assets/fonts/SpaceGrotesk-OFL.txt`).

---

## License

See [LICENSE](LICENSE).
