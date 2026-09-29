# Performance

**Target: 60 FPS** with a 16.67 ms budget on desktop Linux hardware.

---

## 1. Measuring

Press `F3` for the in-game overlay, or run headless with a frame limit:

```bash
./build/debug/EraShift --headless --frames 600
```

The overlay reports:

| Metric | Meaning |
| --- | --- |
| FPS | Mean over a 120-frame window |
| Frame | Mean frame time in ms |
| Peak | Worst frame time in the window |
| CPU | Mean time spent in update + render |
| update / render | The same, split |
| Entities | Live entity count |
| Draw calls | Submissions issued this frame |
| Memory | Resident set size |
| State / Stack | Active state and stack depth |
| Sim steps / Alpha | Fixed steps last frame, interpolation factor |
| Backlog / Dropped | Unconsumed simulation time, steps discarded |

Counters stay compiled in release builds. A handful of adds per frame is a fair
price for being able to *see* a regression instead of guessing at it.

Command line:

```bash
./build/debug/EraShift --log-level trace --log-file /tmp/frame.log
```

---

## 2. Budget

| Stage | Budget | Notes |
| --- | --- | --- |
| Event pump | 0.2 ms | Bounded to 512 events per frame |
| Simulation (fixed 1/60) | 2.0 ms | Physics, AI, gameplay |
| Render submission | 3.0 ms | Recorded into the batch |
| Present / GPU | 9.0 ms | The remainder; bounded by vsync |
| Headroom | 2.4 ms | |

The fixed timestep means a slow frame does not change behaviour, it only drops
frames — up to `maxStepsPerFrame` (5) of catch-up before the backlog is
discarded.

---

## 3. What is already optimised

**Frame-rate independence.** `dampFactor(halfLife, dt)` is exponential decay
rather than a per-frame lerp, so the camera and every smoothed value move
identically at 60, 144 and 240 Hz. A unit test asserts that two half-second
steps match one one-second step.

**Bounded simulation.** `maxFrameDelta` and `maxStepsPerFrame` prevent the
"spiral of death" after a stall, and `LoopStats` counts discarded steps so a
systematic backlog is visible rather than silent.

**No per-frame allocation in the UI.** Rendered TTF strings are cached as GPU
textures keyed by (font, size, colour, text), and row geometry is derived from
the text style so hit testing and drawing cannot disagree.

**Texture caching.** Every asset is loaded once through `ResourceManager` and
shared by handle. Textures are reference counted, so a spritesheet shared by
forty enemies is uploaded once.

**Command batching.** `Renderer2D::beginBatch()` records geometry as plain data
and flushes it in one pass, so UI and particles avoid per-quad state changes.

**Horizontal run merging in the font.** A glyph is emitted as one rectangle per
contiguous ink run per row — 7 quads for an `E` rather than 35 pixel rects.

**Frame pacing.** With vsync off the loop sleeps off the remaining budget rather
than spinning a core at 100 %.

---

## 4. Planned

These are the things that will matter once the world is populated, and how each
is being approached:

| Concern | Approach |
| --- | --- |
| Tile rendering | One texture per tileset, one draw call per visible chunk; cull by camera AABB |
| Culling | Chunked broadphase, rebuilt only when an entity moves between chunks |
| Entity updates | Components updated by system over a dense array; no per-entity virtual dispatch in the hot loop |
| Particles | Pre-allocated pool, fixed capacity, no allocation at emit time |
| Physics | Fixed timestep, spatial hash broadphase, AABB narrow phase (already implemented in `Math.hpp`) |
| Sorting | Transparent geometry back-to-front per draw, computed on chunk boundaries |
| AI | Staggered update budgets; distant agents tick at 10 Hz, not 60 |

The rule applied throughout: **profile before optimising.** The counters above
exist so each of these is adopted on evidence.

---

## 5. Profiling

```bash
cmake --preset relwithdebinfo && cmake --build --preset relwithdebinfo
perf record -g -- ./build/relwithdebinfo/EraShift --headless --frames 2000
perf report
```

```bash
# Cache behaviour
valgrind --tool=cachegrind ./build/debug/EraShift --headless --frames 200
```

---

## 6. Memory

Resident memory is sampled twice a second from `/proc/self/status` rather than
every frame, since the value changes far more slowly than a frame does.

Current usage is dominated by the SDL runtime and the font. Texture memory is
tracked separately in `ResourceManager::stats()` as total pixels, which is a
better proxy for VRAM pressure than process RSS.

`assets/` currently contains one font. Textures arrive with the art pass in
Phase 2.
