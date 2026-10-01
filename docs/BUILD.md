# Building Era Shift

Primary platform: **Linux / Kubuntu**, desktop PC.

---

## 1. Requirements

| Requirement | Version | Notes |
| --- | --- | --- |
| C++ compiler | GCC 13+ or Clang 16+ | C++20 required (`std::format`, `std::span` usage) |
| CMake | 3.24+ | Presets need 3.24 |
| Ninja | any | The default generator for the presets |
| SDL3 | 3.2+ | 3.4 recommended |
| SDL3_image, SDL3_ttf | current | |
| nlohmann/json | 3.11+ | Header only |
| doctest | 2.4+ | Tests only |

Check what is already installed:

```bash
cmake --version && g++ --version
pkg-config --modversion sdl3 2>/dev/null || echo "SDL3 not found via pkg-config"
```

### Option A — distribution packages

```bash
sudo apt install build-essential cmake ninja-build \
                 libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev \
                 nlohmann-json3-dev doctest-dev
```

`CMakeLists.txt` prefers system packages, so nothing else is needed.

### Option B — project-local build (no root required)

```bash
./scripts/fetch_deps.sh
```

This downloads and builds the SDL3 family plus doctest and nlohmann/json into
`.deps/install`, and `CMakeLists.txt` picks that prefix up automatically.

The script probes for optional system libraries and disables SDL features that
are unavailable, so it works on a minimal install (no `libXss`). It also parses
the configure log and retries with each missing optional feature turned off,
rather than hard-coding a list that varies per machine.

Re-run with `--clean` to rebuild from scratch.

---

## 2. Configure and build

### Using the presets (recommended)

```bash
cmake --preset debug
cmake --build --preset debug
./build/debug/EraShift
```

| Preset | Type | Use for |
| --- | --- | --- |
| `debug` | Debug | Day-to-day development, tests |
| `release` | Release, `-Werror` | Performance, final builds |
| `relwithdebinfo` | RelWithDebInfo | Profiling |
| `asan` | Debug + ASan/UBSan | Memory and UB bugs |
| `ubsan` | Debug + UBSan | Undefined behaviour only |
| `tsan` | Debug + TSan | Data races |

Sanitizers are mutually exclusive by design; enabling two is a configure error
rather than a confusing link failure.

### Using a single build directory

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/EraShift
```

`CMakePresets.json` puts each preset in its own subdirectory, so the two
approaches can coexist.

### Build options

| Option | Default | Meaning |
| --- | --- | --- |
| `ERASHIFT_BUILD_TESTS` | `ON` | Build the test binaries |
| `ERASHIFT_WARNINGS_AS_ERRORS` | `OFF` (`ON` in release preset) | `-Werror` |
| `ERASHIFT_USE_SYSTEM_DEPS` | `ON` | Prefer system SDL over `.deps/install` |
| `ERASHIFT_ENABLE_SANITIZERS` | `OFF` | ASan + UBSan |
| `ERASHIFT_ENABLE_UBSAN` | `OFF` | UBSan only |
| `ERASHIFT_ENABLE_TSAN` | `OFF` | TSan only |

---

## 3. Run

```bash
./build/debug/EraShift                     # windowed
./build/debug/EraShift --headless          # no window (CI, SSH, containers)
./build/debug/EraShift --frames 120        # exit after 120 frames
./build/debug/EraShift --screenshot out.png
```

The binary embeds an RPATH to `.deps/install/lib`, so it runs from any working
directory without setting `LD_LIBRARY_PATH`.

---

## 4. Run the tests

```bash
ctest --preset debug --output-on-failure
ctest --preset asan                        # under sanitizers
./build/debug/erashift_tests_unit --success
```

No display server is required: the `Core` library has no SDL dependency, and the
graphics tests exercise data structures rather than opening a window.

---

## 5. Crash regression harness

```bash
./scripts/reproduce_crash.sh              # build, then run every scenario
./scripts/reproduce_crash.sh --no-build   # reuse the existing binary
./scripts/reproduce_crash.sh --sanitize   # also run under ASan
```

Covers the reported SIGSEGV, frame-count boundaries, signal handling, argument
validation, both video drivers and the test suite. See
[`CRASH_ANALYSIS.md`](CRASH_ANALYSIS.md).

---

## 6. Editor configuration

`.vscode/` contains ready-made configuration:

* `c_cpp_properties.json` — include paths and **C++20** for IntelliSense
* `settings.json` — CMake Tools on the `debug` preset
* `tasks.json` — build, run, test, smoke test
* `launch.json` — F5 to run or debug, including a "debug a crash" profile

If IntelliSense reports `cannot open source file "EraShift/..."` or complains
that `std::string` has no `ends_with`, the C++ standard or include path is wrong.
Select the **EraShift** configuration in the status bar, or let CMake Tools
generate `compile_commands.json` (it lands in `build/<preset>/`).

---

## 7. Troubleshooting

**`SDL3 was not found`**
Run `./scripts/fetch_deps.sh`, or install the distribution packages. The error
message repeats the exact command.

**`doctest not found`**
Same — the fetch script installs it into `.deps/install/include`.

**Blank window / nothing drawn**
The logical resolution defaults to the window size. If you set
`graphics.logicalWidth` to `0` and `logicalHeight` to `0` the game tracks the
window; setting only one of them falls back to windowed.

**Segfault immediately after start-up**
Run it under gdb (see `CRASH_ANALYSIS.md`) or with `--log-level trace`. A null
`Logger*` in a `StateContext` is the usual culprit and is asserted against in
`StateContext::valid()`.

**The window closes but the process lingers**
Fixed in this version — see `CRASH_ANALYSIS.md` §5. If it recurs, confirm
`Core::installShutdownHandlers()` is being called and that no other code
overrides `SIGINT`/`SIGTERM`.
