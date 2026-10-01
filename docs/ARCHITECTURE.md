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

### What else ended up in `Core`, and why

Three subsystems that a reader would reasonably expect to find in the engine
layer are in `Core` instead, and the reason is the same in all three cases:
**they are arithmetic, and arithmetic can be tested.**

| Subsystem | File | What it is |
| --- | --- | --- |
| `ParticleSystem` | `Graphics/ParticleSystem.cpp` | A fixed pool with velocity integration. Draws nothing. |
| `AnimationController` | `Game/Animation.cpp` | Clip sampling and blending. Poses, not pixels. |
| Clip selection | `Game/Animation.cpp` | `playerClipFor` / `enemyClipFor`: which clip the simulation's state calls for. Pure logic over `Player` and `Enemy`, so it lives here rather than in the state that draws them. |
| `Tutorial` | `Game/Tutorial.cpp` | Which lesson is showing and whether the action satisfied it. No SDL, so the whole tutorial is testable without a display. |
| `DialoguePanel` | `Game/Dialogue.hpp` | A struct of strings and a `open()` predicate. The drawing is a free function beside it, because a panel is a rectangle and some text. |
| `ProgressDatabase` | `Core/ProgressDatabase.cpp` | SQLite-backed progression. In `Core` because it is persistence, not presentation, and because its failure paths are worth testing without a window. |

`PlayingState` is the counter-example that proves the rule matters: it holds a
renderer, an input device and a level, so nothing in it can be asserted on
headlessly. Three separate bugs lived in its clip selection — a charge flag that
was always true, an unreachable hurt clip, an unreachable enemy hurt clip — and
all three were found the moment the decision moved next to the table it reads
(`playerClipFor` in `Game/Animation.cpp`). The early return in its `update` that
stopped the death animation from playing is still untested, for the same reason.

`tests/gameplay/TestParticles.cpp` asserts on emission rates with no display at
all, which is the only reason anyone can change a particle constant with any
confidence.

`Graphics/ParticleSystem.hpp` sitting in `Core` while every other file in
`Graphics/` is in the engine looks odd in a directory listing. It is a
consequence of the rule, not an exception to it, and moving it would put a
`std::vector` allocation in the middle of a combat frame behind a layer that
cannot be tested without a window.

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

**One documented exception to that rule**, added with the level select:
`LevelSelectState::chosenLevel()` is a function-local static holding the region
the player picked. It has to be, because the `PlayingState` built on the far side
of the transition needs the choice and the `LevelSelectState` is destroyed by that
transition — so the value cannot live on the object being destroyed. It is
consumed by the next `PlayingState::syncLevel` and cleared immediately, which is
what stops it surviving into a later NEW GAME. It is the only static in the
project that is not a sink or a handle.

---

## 3a. Persistence

Two stores, deliberately, because they answer different questions.

| | `SaveManager` | `ProgressDatabase` |
| --- | --- | --- |
| Holds | one continue slot, exact run state | ten regions of unlocks, results, checkpoints |
| Format | one JSON file | SQLite, four tables |
| Written | on quit, on level change | on a checkpoint, a completion, a region choice |
| Read by | `CONTINUE` | the level select, RETRY CHECKPOINT, Settings |

`continue.json` cannot express "region 7 completed in 4:12 with 3 seals, so region
8 is now unlocked" alongside the live run state, and bolting those fields onto it
would make every new one a change to the format that older builds would
misread. The database is versioned for the same reason, and refuses to write over
a schema from a newer build rather than downgrading it.

Neither is cloud-backed, and neither is required: with no writable config
directory, `context().progress` is null and the game plays normally with unlocks
and checkpoints simply not recorded.

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
  out of the gameplay scene into real modules — which is what Phase 2 did.
* **Phase 4** — `TimelineManager` and `ParadoxManager` are pure `Core` logic
  with no rendering dependency, so they are unit-testable from the start.
* **Phase 7** — the save system serialises the same `ConfigStore` type the
  configuration already uses, so a save file is a config file with a version
  header.
