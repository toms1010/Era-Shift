# Crash analysis — Phase 1 SIGSEGV

**Status:** resolved and verified.
**Date:** 2026-09-29
**Affected build:** Debug, `build/debug/EraShift` (CMake 4.2.3, GCC 15.2.0)

---

## 1. Summary

A segmentation fault occurred during start-up, on the first call into
`GameApp::initialise`. The `StateContext` handed to the game was never
populated, so its `log` member was a null pointer and the first
`ctx.log->error(...)` dereferenced it.

The report named `timeout` as the crashing executable. That was a
consequence, not the cause: coreutils `timeout` re-raises the child's fatal
signal on itself so the parent sees the correct status, so systemd recorded a
*second* core for `timeout` after the first one for the game. Both cores were
present; the report quoted the second.

---

## 2. Reproduction

```bash
timeout 20 ./build/EraShift --frames 60 ; echo $?
# 139
```

The crash is deterministic — 100 % of runs, on the very first frame, before any
window is drawn.

---

## 3. Evidence

### 3.1 gdb

```
$ gdb -batch -ex run -ex bt --args ./build/EraShift --frames 60

Thread 1 "EraShift" received signal SIGSEGV, Segmentation fault.
0x00005555555c0c33 in EraShift::Core::Logger::isEnabled (this=0x0, level=Error)
    at src/EraShift/Core/Log.cpp:287
#0  EraShift::Core::Logger::isEnabled (this=0x0, ...)      Log.cpp:287
#1  EraShift::Core::Logger::log<> (...)                    Core/Log.hpp:141
#2  EraShift::Core::Logger::error<> (...)                  Core/Log.hpp:170
#3  EraShift::Game::GameApp::initialise (...)              Game/GameApp.cpp:18
#4  EraShift::Engine::initialise (...)                     Application/Engine.cpp:163
#5  main (...)                                              main.cpp:156
```

`this=0x0` at frame #0 is the finding: the `Logger` the game is calling through
does not exist. The call chain is a normal `logger->error()`; only the receiver
is invalid.

### 3.2 The `timeout` question

```bash
$ coredumpctl list --no-pager | tail -4
... SIGSEGV present  /home/tommyp/Era-Shift/build/EraShift            3.4M
... SIGSEGV present  /usr/lib/cargo/bin/coreutils/timeout              610.2K
... SIGSEGV present  /home/tommyp/Era-Shift/build/EraShift            3.4M
... SIGSEGV present  /usr/lib/cargo/bin/coreutils/timeout              609.4K
```

Every crash produces **two** cores. Extracting the game's core:

```bash
$ coredumpctl debug <pid-of-EraShift-core> --no-pager
#0  EraShift::Core::Logger::isEnabled (this=0x0, ...)
...
#5  main (argc=3, argv=...)
```

`timeout` itself has no defect. The child's own core is the primary evidence
and it points straight at Era Shift. `timeout` is only the messenger.

### 3.3 Exit codes

| Command | Exit |
| --- | --- |
| `./build/EraShift --frames 60` | 139 (SIGSEGV) |
| `timeout 20 ./build/EraShift --frames 60` | 139 |
| `/usr/bin/timeout 20 ./build/EraShift --frames 60` | 139 |
| `/usr/lib/cargo/bin/coreutils/timeout 20 ./build/EraShift --frames 60` | 139 |

Both `timeout` implementations behave identically, which is what you'd expect
when the wrapper is faithfully relaying the child's signal.

### 3.4 The code defect

`Engine` owns a `StateContext` member. While the context was still a by-value
factory method, it populated itself; converting it to a stored member left the
population step behind:

```cpp
// Engine::initialise
m_game = std::make_unique<Game::GameApp>();
m_game->initialise(m_context);   // m_context is still all null pointers
```

```cpp
// GameApp::initialise
if (context.states == nullptr) {
    context.log->error("Game", "initialise called without a state machine");
    //                     ^^^^^^ null dereference, before anything is logged
```

The null check that should have caught the problem could not run, because it
reported through the very pointer that was null.

---

## 4. Fix

Populate the context in one place, at the point the subsystems exist:

```cpp
// Engine.cpp
void Engine::rebuildContext() noexcept
{
    m_context.log       = &m_log;
    m_context.states    = &m_states;
    m_context.loop      = &m_gameLoop;
    m_context.input     = &m_input;
    m_context.renderer  = &m_renderer2D;
    m_context.resources = m_resources.get();
    m_context.stats     = &m_stats;
    m_context.overlay   = &m_overlay;
    m_context.events    = &m_eventBus;
    m_context.clock     = &m_clock;
    m_context.config    = &m_config;
    m_context.window    = &m_window;
    m_context.text      = m_text.get();
    m_context.buildLabel = m_buildLabel;
    m_context.version    = ERASHIFT_VERSION;
}
```

`rebuildContext()` is called from `initialise()` and is available to any later
code that swaps a subsystem. `StateContext::valid()` documents the invariant:

```cpp
[[nodiscard]] bool valid() const noexcept
{
    return log != nullptr && states != nullptr && renderer != nullptr;
}
```

### Root cause, stated precisely

Not memory corruption, not a dangling pointer, not a third-party library, and
not `timeout`: **a partially constructed object was used before its
initialisation step ran**. The `StateContext` is an aggregate of raw pointers
with no invariant enforcement, which is what made the mistake silent — the
failure surfaced three frames deep in the logging subsystem instead of at the
call site.

---

## 5. Second defect found while verifying the fix

A regression sweep turned up a second, separate bug:

* `SIGTERM` / `SIGINT` (Ctrl-C, `kill`, `timeout`) did **not** end the game.
* Closing the window did not end the game.

The quit event was published to an `EventBus` that nothing subscribed to, so
the loop never learned it should stop. `timeout 12 ./build/EraShift` therefore
ran for the full 12 s and then hung waiting for a process that ignored the
signal.

Fix, in two parts:

1. `Engine` subscribes to `QuitRequested` and `WindowClosed` and calls
   `requestExit()`.
2. `Core::installShutdownHandlers()` installs `sigaction` handlers for
   `SIGINT`/`SIGTERM`/`SIGHUP` that set a `volatile sig_atomic_t` flag. The main
   loop polls it. The handler performs **no** work beyond the assignment, which
   is the only thing guaranteed async-signal-safe; the actual cleanup happens on
   the main thread, where it is legal.

---

## 6. Third defect found while testing the argument parser

`--frames -5` and `--frames abc` were accepted, and both silently meant "no
limit" — the process then ran until killed. A missing value for an option was
also tolerated.

`main.cpp` now validates every option, reports the problem on stderr and exits
with status 3 instead of starting a process the user cannot reason about.

---

## 7. Verification

`scripts/reproduce_crash.sh` runs all of the following:

| Check | Result |
| --- | --- |
| `timeout 20 <bin> --frames 60` | exit 0 |
| `<bin> --frames 60` | exit 0 |
| `--frames 1 / 10 / 60 / 300 / 1000` | exit 0 |
| `SIGTERM` / `SIGINT` / `SIGHUP` | clean exit 0 |
| `--frames -5`, `--frames abc`, 1e30, missing value | rejected, exit 3 |
| `--unknownflag`, `--content /nope` | rejected, exit 3 / 2 |
| `SDL_VIDEODRIVER=dummy` and `--headless` | exit 0 |
| `ctest` (890 assertions) | pass |

Sanitizers:

| Build | Frames | Result |
| --- | --- | --- |
| ASan + UBSan | 1, 60, 300 | clean, no leaks |
| UBSan | 300 | clean |
| TSan | 120 | no races in Era Shift code |
| ASan + UBSan (tests) | – | pass |

TSan reports one lock-order inversion entirely inside `libdbus-1` (reached via
`SDL_Init` and window teardown), with no frames in Era Shift sources and no
data race. It is a known characteristic of dbus and is not actionable here.

Reproduce the original failure at any time by removing the `rebuildContext()`
call from `Engine::initialise`; the crash returns with an identical backtrace.

No core dumps are generated for this scenario any more.

---

## 8. Follow-up

The `StateContext` design is the underlying weakness: twelve raw pointers with
no runtime invariant. Two changes reduce the chance of a repeat:

* `rebuildContext()` is now the single place the context is populated.
* `StateContext::valid()` states the invariant and is checked before states are
  dispatched.

The planned change for Phase 2 is to have `StateMachine` refuse to dispatch to a
state whose context is invalid, turning this class of mistake into an
immediate, obvious failure instead of a null dereference deep in an unrelated
subsystem.
