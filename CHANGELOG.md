# Changelog

All notable changes to Era Shift are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

Nothing yet.

---

## [0.1.0] — 2026-09-29

Phase 1: Engine Foundation. A window opens, the title screen renders, the menu
is navigable, and a test scene exercises movement, camera, era shift, pause and
settings. No gameplay systems yet.

### Added

**Core (no SDL dependency)**
- `Logger` with pluggable sinks (console, file, in-memory ring), level
  filtering, `std::format` support and thread-safe writes.
- `ConfigStore` / `ConfigManager`: layered JSON configuration (defaults →
  project → user directory → command line) with typed, bounds-checked accessors.
- `IClock` / `SystemClock` / `ManualClock`, `FrameTimeWindow`, `Stopwatch` — the
  game loop is fully deterministic under test.
- `GameLoop`: fixed-timestep simulation with a step clamp, backlog discard and
  render interpolation.
- `StateMachine`: a stack of game states with automatic pause/resume, so
  `MainMenu → Playing → Paused` needs no per-state bookkeeping.
- `SignalHandler`: async-signal-safe `SIGINT`/`SIGTERM`/`SIGHUP` handling that
  requests a clean shutdown instead of dying mid-frame.

**Graphics**
- `Window` with resizing, fullscreen, vsync and a logical resolution that
  defaults to the window size.
- `Renderer2D`: textured quads, fills, outlines, lines, circles, nine-slice
  panels, clip stack, draw-call counting and command batching.
- `Camera2D` with frame-rate-independent damping, world bounds, zoom and
  deterministic screen shake.
- `Texture` (move-only RAII) and `ResourceManager` with reference-counted
  texture, font and document caches.
- `BitmapFont`: a built-in 5x7 font so the UI is never completely textless.
- `TextRenderer`: TTF text with left/centre/right alignment, top/middle/bottom
  vertical alignment, word wrapping, ellipsis truncation, drop shadows and a
  texture cache keyed by (font, size, colour, string).
- `Math` / `Color`: AABB overlap, segment intersection, ray-vs-AABB,
  frame-rate-independent damping.

**Input**
- `InputManager`: edge-detected keyboard and mouse state, focus handling that
  releases held buttons, and a data-driven action map.
- `EventBus` with RAII subscriptions, and `EventPump` translating SDL events
  into engine events.

**Debug**
- `PerformanceStats`: FPS, frame time, CPU/update/render time, entity and draw
  call counts, resident memory.
- `DebugOverlay` (F3) showing the above plus state stack, simulation alpha and a
  rolling log tail.

**Game**
- Title screen with an animated three-era backdrop, keyboard and mouse
  navigation, and a menu list shared with the other screens.
- Phase 1 test scene: walk, run, jump, gravity, camera follow with bounds, era
  shifting on a chrono-energy budget, and a HUD.
- Pause menu and settings menu (fullscreen, vsync, UI scale, master/music/SFX
  volume) applied live and persisted to the user config directory.

**Infrastructure**
- CMake with six presets: `debug`, `release`, `relwithdebinfo`, `asan`,
  `ubsan`, `tsan`. Sanitizers are mutually exclusive by configuration error.
- `scripts/fetch_deps.sh` builds the SDL3 family, doctest and nlohmann/json into
  a project-local prefix **without root**, auto-disabling optional platform
  features that are missing.
- doctest suite: 890+ assertions across 86 cases, no display server required.
- `scripts/reproduce_crash.sh` regression harness.
- `tools/font/generate_bitmap_font.py` generates the glyph table.
- VS Code configuration: IntelliSense include paths and C++20, CMake Tools,
  tasks and debug launch profiles.

### Fixed

Bugs found and fixed during Phase 1 verification. All are documented in
[docs/CRASH_ANALYSIS.md](docs/CRASH_ANALYSIS.md).

- **SIGSEGV on start-up.** `Engine` handed a `StateContext` to the game before
  populating it, so the first log call dereferenced a null `Logger*`. Fixed by
  populating the context in `Engine::rebuildContext()`.
- **`SIGTERM`/`SIGINT` and window close did not end the game.** The quit event
  was published to an event bus with no subscribers, so the loop never stopped
  and `timeout` hung. Fixed with event subscriptions plus an explicit
  `sigaction` handler that sets a flag the main loop polls.
- **Argument parsing silently ignored errors.** `--frames -5`, `--frames abc`
  and a missing option value all started an unkillable process. Options are now
  validated and rejected with a message and a non-zero exit code.
- **The frame was never cleared.** States assumed a background, so a state that
  forgot to draw one let the previous frame bleed through. The engine now clears
  before dispatching.
- **A zero logical resolution produced a 0x0 viewport**, so every
  full-screen draw was silently skipped and the game rendered as an almost
  black screen. The logical size now defaults to the window size.
- **`ConfigStore::setBool` wrote the string `"true"` instead of the boolean
  `true`,** so every boolean setting silently failed to persist.
- **Numeric config getters accepted partial parses,** turning `"0.1.0"` into
  `0.1` and `"12abc"` into `12`.
- **The state machine did not update the top state** when a menu was stacked on
  top, freezing menus as well as the world.
- **The bitmap font rasterised `E`, `I` and `A` as solid blocks.** Ink runs were
  merged across rows instead of within each row, filling every counter. Found by
  inspecting a captured frame; the glyph data was correct all along.
- **Use-after-free in `TextRenderer::rasterise`:** the surface size was read
  after `SDL_DestroySurface`.
- **`TTF_GetStringSize`/`TTF_RenderText_Blended` were passed
  `std::string::npos` as the length,** which SDL_ttf 3.2 turned into a `memcpy`
  of size −1. Caught by AddressSanitizer.
- **A free function named `toString` in namespace `EraShift::Core` shadowed
  doctest's own `toString` via ADL,** breaking test compilation. Renamed to
  `logLevelName`, `stateName`, `keyName`, `eraName`.
- **A user-declared copy constructor suppressed the implicit default
  constructor** on `LogSink` and `IClock`.
- `-Wshadow` and `-Wformat-truncation` warnings cleared; Release builds with
  `-Werror`.

### Known issues

- **Audio is not initialised.** SDL was built with the dummy audio backend
  because ALSA development headers are absent on this machine. `SDL3_mixer` is
  linked and the audio configuration is read, but nothing plays yet; audio is
  Phase 5 work regardless.
- **TSan reports a lock-order inversion inside `libdbus-1`** (via `SDL_Init` and
  window teardown). No frames in Era Shift code and no data race.
- **`Continue` and `Credits` in the main menu are disabled placeholders**; the
  save system is Phase 7.
- **The `Playing` state is an engine verification scene**, not the real player
  controller, which arrives in Phase 2.

---

[Unreleased]: https://example.invalid/erashift/compare/v0.1.0...HEAD
[0.1.0]: https://example.invalid/erashift/releases/tag/v0.1.0
