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
| `erashift_tests_unit` | 94 cases | Maths, logging, config, timing, game loop, state machine, font |
| `erashift_tests_integration` | 10 cases | Full state flow, config round trips and migrations |
| `erashift_tests_gameplay` | 157 cases | The whole simulation — eras, tile collision, physics, the player, enemies, the world, level data, saves — **and the three presentation systems that are pure arithmetic: the synthesiser, the animation controller and the particle pool** |

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

### `TestSynth` — the synthesiser
There is no sound card in CI, and that is the point: `Audio/Synth.cpp` is pure
arithmetic, so the audio is testable. The assertions are about properties rather
than about specific waveforms.

- No one-shot is silent, clipping, or **a copy of another one-shot**. A pair-wise
  comparison across all 20 sounds catches a copy-paste in a switch statement,
  which is the classic way an eight-way audio table quietly becomes a two-way one.
- The three eras are in different keys with different degree counts, and the
  Future's is a whole tone — symmetric, so it has no leading tone and cannot
  resolve.
- Combat is not just louder than exploring, it has content exploring does not:
  the arpeggio only exists under pressure.
- A parameter change *glides*: the synth's intensity is still easing toward its
  target on the first block.
- A negative frame count and a zero sample rate are both safe.
- Two identical calls produce bit-identical output. Without this, "the music
  sounds different on my machine" is not answerable.

### `TestAnimation` — clips, markers and the simulation's clock
The property under test is that the picture and the hitbox cannot disagree.

- Every clip is well-formed: keys in ascending order, markers inside the
  duration. A marker past the end never fires, and a key out of order makes the
  sampler interpolate backwards.
- **The attack clip's hit window still matches `PlayerTuning`.** The windup ends
  at 0.06s and the active window closes at 0.16s; the clip's keys and its
  `AttackHit` marker sit on those boundaries. This is the test that stops the two
  drifting apart when someone retunes the swing.
- A single 200ms step still delivers both attack markers. A frame hitch that
  skipped the hit frame would give the player a blow with no spark and no sound.
- A looping clip fires its markers again on the wrap, or a walk cycle drops a
  footstep once every two steps.
- A short non-looping action refuses to be cut short, so mashing attack does not
  visually restart the swing.
- `setTime` places a simulation-synced clip without emitting markers, and clamps
  rather than wrapping — a swing that wrapped past its end would look recovered
  while still being thrown.
- The death clip ends genuinely transparent. It did not, once: `alpha` was
  landing in the wrong field of a positional literal.

### `TestParticles` — the pool
A particle system has no rules to break, so these are about the things that go
wrong quietly.

- The pool is a fixed size, never grows, and never reallocates — a full pool
  recycles the oldest rather than refusing, because a busy screen that silently
  loses every effect is worse than one that drops a footstep.
- **Emission is frame-rate independent.** At 60Hz a rate of 3/s is 0.05 per
  frame, and rounding that to an integer gives zero, at which point an
  "at least one per frame" floor takes over. That bug made all three eras emit
  exactly the same number.
- **The three eras emit visibly different weather**, and in opposite halves of
  the screen: leaves fall from above, embers rise from below.
- `randomRange` actually lands in its range. It once mapped a signed `[-1, 1]`
  noise source directly as a fraction, so half the ambient particles spawned
  outside the room.
- A one-second step does not flood the pool, and a degenerate region emits
  nothing rather than dividing by zero.
- Two identical calls produce identical particles.

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
