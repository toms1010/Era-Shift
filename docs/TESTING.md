# Testing

```bash
ctest --preset debug --output-on-failure
```

No display server is required. The `Core` library has no SDL dependency, and
the graphics tests exercise data structures rather than opening a window, so the
suite runs in a container, over SSH, and in CI.

---

## Test binaries

| Binary | Tests | Contents |
| --- | --- | --- |
| `erashift_tests_unit` | 76 cases | Maths, logging, config, timing, game loop, state machine, font |
| `erashift_tests_integration` | 10 cases | Full state flow, config round trips and migrations |

```bash
./build/debug/erashift_tests_unit --success              # verbose
./build/debug/erashift_tests_unit --test-case="*"Log*"   # filter
./build/debug/erashift_tests_unit --list-test-cases
./build/debug/erashift_tests_unit --reporters=xml        # JUnit for CI
```

---

## What is covered, and why

### `TestMath` — collision and camera foundations
AABB overlap and the smallest-intersection rectangle, segment intersection,
ray-vs-AABB slab clipping, `dampFactor` frame-rate independence, `wrap` with
negative inputs. These underpin collision detection, the camera and projectiles,
so they are tested hardest.

### `TestLog` — the logging contract
Level filtering, `Off` silencing, source-location prefixing, `std::format`
interpolation, multi-sink dispatch, and concurrent writes from several threads
(the logger is used from the audio and asset threads later).

### `TestConfig` — configuration behaviour
Nested JSON flattening, layer precedence, type coercion, bounds clamping, and
round trips through both a string and a file. Includes the migration case: a
save written by an older build must gain new default keys without losing old
values.

### `TestTime` — timing primitives
Frame-time windowing, rejection of non-positive and non-finite samples,
`Stopwatch` pause/resume arithmetic, duration formatting.

### `TestGameLoop` — the fixed timestep
A 144 Hz display still produces 60 simulation steps per second; a 30 Hz display
produces two steps per frame. A 30-second stall is clamped to `maxStepsPerFrame`
and reported as dropped steps. A non-monotonic clock produces a zero delta, not
a negative one. `alpha` stays within [0,1] under irregular frame times.

### `TestGameState` — transitions
Push pauses, pop resumes, only the top state simulates, `switchTo` unwinds the
whole stack, `popToRoot` resumes the root, rendering is bottom-up, and every
enter is matched by an exit.

### `TestBitmapFont` — the font table
Every printable ASCII character has a glyph; lowercase reuses uppercase art;
out-of-range characters fall back to a visible box; advances are proportional
(`I` is narrower than `M`); and **glyph counters are not filled in** — a
regression test for a rasteriser that once drew `E`, `I` and `A` as solid
blocks.

### `TestStateFlow` — the documented flow
`MainMenu → Playing → Paused → Playing → GameOver → MainMenu` with every enter
matched by an exit, plus a pause storm and a nested-menu unwind in the correct
order.

### `TestConfigRoundTrip` — persistence
A settings file survives save and load, untouched keys keep their defaults, a
newer build adds new defaults without disturbing saved values, command-line
overrides win over files, and a corrupt user file does not stop the game
starting.

---

## Writing tests

```cpp
#include <doctest/doctest.h>

using namespace EraShift::Core;

TEST_CASE("the accumulator runs the right number of steps")
{
    GameLoop loop;
    ManualClock clock;

    int steps = 0;
    loop.setUpdateFn([&steps](double) { ++steps; });
    loop.setRenderFn([](double, double) {});

    loop.tick(clock.seconds());
    for (int i = 1; i <= 144; ++i) {
        clock.advance(1.0 / 144.0);
        loop.tick(clock.seconds());
    }

    CHECK(steps >= 59);
    CHECK(steps <= 61);
}
```

Guidelines:

* **Drive time explicitly.** Use `ManualClock`; never sleep.
* **No display, no real files** outside `std::filesystem::temp_directory_path()`.
* **Test behaviour, not implementation.** Assert that `getInt` returns 42, not
  that it calls `from_chars`.
* **Name the failure.** `CHECK_MESSAGE(cond, "glyph '", c, " is solid")` prints
  the offending character.

---

## Sanitizers

Sanitizers are mutually exclusive; enabling two is a configure error.

```bash
cmake --preset asan && cmake --build --preset asan

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
    ./build/asan/EraShift --headless --frames 300

ctest --preset asan
```

| Preset | Flags | Catches |
| --- | --- | --- |
| `asan` | `-fsanitize=address,undefined` | Use-after-free, overflow, leaks, UB |
| `ubsan` | `-fsanitize=undefined` | Bad casts, overflow, misaligned access, invalid enums |
| `tsan` | `-fsanitize=thread` | Data races, lock-order inversions |

Current status: clean. The one TSan report is a lock-order inversion inside
`libdbus-1` reached through `SDL_Init`, with no frames in Era Shift code.

### Valgrind

```bash
valgrind --error-exitcode=1 --leak-check=full --track-origins=yes \
    ./build/debug/EraShift --headless --frames 30
```

Not installed by default on Ubuntu minimal; ASan covers the same ground and is
much faster.

---

## Crash regression harness

```bash
./scripts/reproduce_crash.sh
./scripts/reproduce_crash.sh --no-build
./scripts/reproduce_crash.sh --sanitize
```

Runs the reported scenario, frame-count boundaries, signal handling, argument
validation, both video drivers and the test suite, printing the exit status or
signal for each. Non-zero exit if anything regresses.

---

## Adding a new test

1. Put it in `tests/unit/` or `tests/integration/`.
2. Add the file to the relevant list in `tests/CMakeLists.txt`.
3. Prefer `CHECK` over `REQUIRE`; `REQUIRE` aborts the whole case.
4. Run `ctest --preset debug --output-on-failure`.
5. If it tests something a future phase will change, say so in a comment — a
   test that encodes a temporary placeholder is worse than no test.
