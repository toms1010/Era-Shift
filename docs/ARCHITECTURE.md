# Architecture

This document explains how Era Shift is put together and, more importantly,
*why* it is put together that way.

---

## 1. Design goals

| Goal | How it is met |
| --- | --- |
| Gameplay must not depend on SDL | `Core` is an SDL-free library; tests run headless |
| The simulation must be deterministic | Everything depends on `IClock`, never on wall time |
| Adding content must not mean rewriting systems | Data-driven config, data tables, extensible state machine |
| Memory must be safe by construction | RAII everywhere, `unique_ptr` for ownership, `shared_ptr` only when genuinely shared |
| Frame rate must not change behaviour | Fixed-timestep simulation, render interpolation, exponential damping |
| It must be debuggable | Layered logging, performance counters, a live overlay, sanitizers |

---

## 2. Layering

```
                    ┌──────────────────────────────┐
   platform  ──────►│  Application  (SDL lives here)│
                    │  Engine · EventBus · EventPump│
                    └───────────────┬──────────────┘
                                    │
              ┌─────────────────────┼─────────────────────┐
              ▼                     ▼                     ▼
        ┌──────────┐         ┌───────────┐        ┌────────────┐
        │ Graphics │         │   Input   │        │    Debug   │
        │ Renderer │         │  Actions  │        │  Overlay   │
        │  Text    │         │           │        │  Stats     │
        │ Resources│         │           │        │            │
        └────┬─────┘         └─────┬─────┘        └─────┬──────┘
             │                     │                    │
             └─────────────────────┴────────────────────┘
                                   ▼
                    ┌──────────────────────────────┐
                    │  Core  — no SDL anywhere      │
                    │  Log · Config · Time          │
                    │  GameLoop · GameState         │
                    │  SignalHandler · Math         │
                    └──────────────────────────────┘
```

The rule is one-directional: `Core` never includes `Graphics`, `Input` or
`Application`. This is enforced by the build — `erashift_core` does not link
SDL at all, so an accidental include is a link error rather than a layering
violation nobody notices.

The payoff is concrete: **the whole test suite runs without a display server**,
in a container, over SSH.

---

## 3. Ownership

Ownership is explicit at every boundary.

| Kind | When | Why |
| --- | --- | --- |
| Value | Maths, colour, config values, small state | Trivially copyable, no indirection |
| `std::unique_ptr` | `Engine`'s subsystems, `GameApp` | Single owner, deterministic destruction order |
| `std::shared_ptr` | `Texture`, `LogSink`, `TextureHandle` targets | Genuinely shared between many owners |
| Raw pointer / reference | `StateContext` members, callbacks | Non-owning, lifetime documented |
| Borrowed pointer handle | `TextureHandle`, `FontHandle` | Cheap, copyable, invalidated explicitly |

`StateContext` is a struct of raw pointers on purpose: a state should be able
to reach exactly what it needs, and the dependency surface should be visible by
reading twenty lines. The trade-off is that nothing enforces the invariant, which
is precisely how the Phase 1 segfault happened — see
[CRASH_ANALYSIS.md](CRASH_ANALYSIS.md) §8 for the planned fix.

**No global mutable state.** There is no `Logger::instance()`, no
`Input::get()`. Subsystems receive a `Logger&` in their constructor. The single
exception is the signal-handler flag, which is process-global because a process
has exactly one set of signal dispositions, and which is only ever assigned
from a handler.

---

## 4. The game loop

```
tick(now):
    drain OS events                       <- once per frame
    accumulator += frameDelta
    while accumulator >= fixedDelta:      <- fixed 1/60 s
        update(fixedDelta)                <- only the top state
        accumulator -= fixedDelta
    render(frameDelta, accumulator/fixedDelta)
    present()
```

Three decisions matter:

**Simulation is decoupled from presentation.** Physics, collision and AI run at
a fixed rate, so behaviour does not change between a 60 Hz and a 240 Hz
display. Rendering receives an interpolation `alpha` and extrapolates between
the last two simulation states, so motion stays smooth on any refresh rate.

**A stall cannot spiral.** `maxStepsPerFrame` (5) and `maxFrameDelta` (0.25 s)
bound the work done after a long stall — a window drag, a breakpoint, a level
load. Anything beyond that is discarded rather than "caught up", because
simulating two seconds of backlog in one frame is how a game becomes
unresponsive.

**Events are drained once per frame, not per simulation step.** Ten simulation
steps in one frame should not process the mouse ten times.

The loop itself is pure bookkeeping: it knows no SDL types and is driven by
explicit `tick(now)` calls, which is what makes it deterministically testable.

---

## 5. State management

States form an explicit stack. Pushing pauses the one below; popping resumes
it. This is exactly the required flow, with no per-state bookkeeping:

```
MainMenu
   |  switchTo
   v
Playing  <-------->  Paused  <-------->  Settings
   |
   |  switchTo
   v
GameOver
```

`StateMachine::update()` ticks **only the top** state. `render()` visits the
whole stack bottom-up so an overlay draws above the world it dims.

`isInStack()` exists so a caller cannot push a second pause menu when one is
already open, which is the usual way menu handling goes wrong.

---

## 6. Input

Three layers, each with one job:

1. **`EventPump`** translates SDL events into engine events. The rest of the
   engine never sees `SDL_Event`.
2. **`InputManager`** maintains per-frame device state and computes edges once,
   so `wasPressed()` returns the same answer no matter how often it is called.
3. **`InputMap`** turns device state into named actions (`Jump`, `ShiftEra`).
   Rebinding is a data change in `controls.json`, not a code change.

Losing focus releases every held button. Without that, a player who alt-tabs
away comes back to a character still running.

---

## 7. Text

Two systems, deliberately:

* **`TextRenderer`** — TTF, with real hinting, kerning and a full Unicode
  range. Rasterised strings are cached as GPU textures keyed by
  (font, size, colour, text); UI text is a few dozen distinct strings, so the
  hit rate is very high and per-frame rasterisation never happens.
* **`BitmapFont`** — a 5x7 face compiled into the binary. It exists so the game
  is never completely textless: if `SDL3_ttf` fails to initialise or the font
  file is missing, the title screen still shows the game's name.

The bitmap font has **proportional advances**. A monospaced grid leaves a
visually huge gap around `I` and punctuation, which is the single biggest
reason cheap bitmap fonts look amateurish.

---

## 8. Resource management

Every asset is loaded through `ResourceManager` exactly once and shared by
handle.

* Textures are **reference counted** — many sprites share one texture, and
  `unloadTexture` only frees it when the last reference goes.
* Fonts are keyed by (path, pixel size) so the UI at two scales does not fight
  over one handle.
* Handles are lightweight value types (`const Texture*` plus an id). They are
  borrowed, and `ResourceManager` is guaranteed to outlive them because the
  engine owns it for the whole session.
* A missing or corrupt asset **logs and returns an invalid handle**; it never
  throws and never crashes. Gameplay code checks `handle.valid()`.

---

## 9. Error handling

There is no exception in the engine's own logic. Errors are values:

* `Texture::load` returns a null handle and reports a `TextureError` enum.
* `ConfigStore::mergeJson` returns `false` and logs; a malformed file is skipped,
  not fatal. **The game always starts.**
* `Engine::initialise` returns `false` and fills an error string.
* `EventBus` skips a handler that was removed during dispatch.

The only exceptions in the codebase are at the process boundary, where `main()`
catches and reports.

---

## 10. Performance

* **Fixed timestep with a clamped backlog** — bounded work per frame.
* **Damping instead of lerping** — `dampFactor(halfLife, dt)` gives identical
  motion at any frame rate.
* **Text and texture caching** — no per-frame allocation or upload in the UI.
* **Command batching** — `Renderer2D::beginBatch()` records geometry and flushes
  it once.
* **Draw-call counting** — the overlay reports it, so regressions are visible
  rather than guessed at.
* **Frame pacing** — with vsync off, the loop sleeps off the remaining budget
  instead of spinning a core.

The counters exist in release builds too: a handful of adds per frame is a fair
price for being able to see a regression.

---

## 11. Testing strategy

| Level | What it covers | Needs a display? |
| --- | --- | --- |
| Unit | Maths, logging, config, timing, loop, state machine, font tables | No |
| Integration | Full state flow, config round trips and migrations | No |
| Regression | The crash harness in `scripts/reproduce_crash.sh` | No |
| Sanitizers | ASan, UBSan, TSan over the whole binary | No |

The fixed-timestep loop is the reason the simulation is testable at all: a
`ManualClock` advances in exact increments, so a test can assert that a 144 Hz
display still produces exactly 60 simulation steps per second.

---

## 12. Where this is going

The architecture is built for the systems that come next, not for the systems
that exist:

* **Phase 2–3** — `Player`, `Camera2D`, collision and the `Era` enum graduate
  out of the test scene into real modules.
* **Phase 4** — `TimelineManager` and `ParadoxManager` are pure `Core` logic
  with no rendering dependency, so they are unit-testable from the start.
* **Phase 7** — the save system serialises the same `ConfigStore` type the
  configuration already uses, so a save file is a config file with a version
  header.
