# Technical design

Subsystem-level reference for the engine as it stands in Phase 1. Game design
documents are written as the systems they describe are implemented.

---

## Core

### Logging
`Logger` owns sinks and fans records out to them under a mutex, so background
work can log safely. Levels are an `enum class`; `parseLogLevel` is
case-insensitive. `std::format` handles interpolation, checked at compile time.
`MemoryLogSink` is a bounded ring that the debug overlay tails from.

`Logger` is an ordinary object owned by `Engine`. There is deliberately no
`Logger::instance()`: subsystems take a `Logger&` so the dependency is visible
and tests can capture output.

### Configuration
Four layers, merged in order: built-in defaults, `./config`, the user directory
(`~/.config/EraShift`, honouring `XDG_CONFIG_HOME`), and `--set` overrides.

Nested JSON is flattened to `section.key` → scalar so lookups are a single hash
probe and merging is trivial. Values keep their JSON form, so booleans stay
booleans and strings stay strings.

Rules that matter:
* A malformed file is logged and skipped. **The game always starts.**
* Numeric getters require the *whole* value to be numeric, so `"0.1.0"` does
  not become `0.1`.
* `setBool` writes an unquoted `true`/`false`; routing it through `setString`
  would store the string `"true"` and the setting would silently never apply.
* Out-of-range values are clamped and reported.

### Timing
`IClock` abstracts time. `ManualClock` is what makes the game loop testable:
a test advances it in exact increments and asserts on the result, with no
flakiness and no sleeping.

`FrameTimeWindow` is a fixed-size ring of recent frame times. `Stopwatch`
excludes paused time from its total.

### Game loop
Fixed timestep, clamped backlog, interpolation alpha for rendering. See
[ARCHITECTURE.md §4](ARCHITECTURE.md).

### State machine
An explicit stack with automatic pause/resume. `StateMachine::update()` ticks
only the top state; `render()` visits the whole stack bottom-up. Null states
are rejected; a null context is safe.

### Signals
`sigaction` handlers for `SIGINT`/`SIGTERM`/`SIGHUP` that set a
`volatile sig_atomic_t`. The handler does nothing else — doing real work in a
signal handler is unsafe — and the main loop polls the flag, so cleanup happens
on the main thread where it is legal.

---

## Graphics

### Window and logical resolution
The "logical resolution" is the coordinate space every draw call works in. With
no explicit setting it equals the window size, so the rest of the engine never
special-cases zero (a 0×0 viewport silently draws nothing — a bug found and
fixed during Phase 1).

Setting `logicalWidth`/`logicalHeight` enables letterboxed presentation, which
keeps the UI identical on every display.

### Renderer
`Renderer2D` covers textured quads with source rectangles, fills, outlines,
lines, circles, nine-slice panels, an intersecting clip stack, and command
batching. Every submission increments a draw-call counter.

With the camera enabled, world coordinates are transformed to clip space
manually (including the y flip, since SDL's origin is bottom-left). With it
disabled, draws are in screen space — which is what UI wants.

### Camera
Exponential damping via `dampFactor(halfLife, dt)`, which is frame-rate
independent by construction. Clamps to world bounds, centring the view when it
is larger than the world. Screen shake is a decaying sinusoid pair rather than
random noise, so a replay looks identical every time.

### Resources
`ResourceManager` loads each asset once. Textures are reference counted and
keyed by path; fonts by (path, pixel size); documents by path. Handles are
borrowed pointers plus a stable id. A failed load returns an invalid handle and
logs — it never throws.

### Text
`TextRenderer` rasterises TTF strings once and caches them as GPU textures.
Supports left/centre/right and top/middle/bottom alignment, word wrapping with
an ellipsis when `maxLines` truncates, and drop shadows.

Two SDL 3.2 API details are load-bearing and are commented in the source:
`TTF_GetStringSize` and `TTF_RenderText_Blended` take an explicit byte count
(passing `std::string::npos` produces a `memcpy` of size −1), and a surface's
size must be read before it is destroyed.

`BitmapFont` is the always-available fallback. Its advances are proportional,
which is what stops `I` and punctuation from looking gappy.

---

## Input

`EventPump` translates SDL events and is the only file in the engine that
includes `<SDL3/SDL.h>` for event handling. `InputManager` keeps device state
and computes edges once per frame, so `wasPressed()` is idempotent. The action
map is data-driven from `controls.json`.

Losing focus clears all held buttons, so a player returning from an alt-tab is
not still running.

---

## Debug

`PerformanceStats` keeps exponential moving averages for CPU, update and render
time, and a ring for frame times. Memory is sampled twice a second from
`/proc/self/status`.

`DebugOverlay` is a developer tool that stays available in release. Gameplay
systems contribute rows by implementing `DebugInfoProvider` and pushing labelled
strings, which keeps the debug layer free of gameplay dependencies.

---

## Engine

`Engine` owns every long-lived subsystem, wires them, and owns one
`StateContext` — a struct of pointers handed to states. It is a plain aggregate
rather than a god object: the class has no behaviour beyond construction and
destruction order, and every system receives what it needs through its
constructor.

The engine clears the frame before dispatching to states, so no state has to
remember to clear and no frame can bleed into the next.

Shutdown is explicit, in reverse construction order: states are reset (running
`onExit` on each), then resources, renderer and window, then SDL subsystems.
