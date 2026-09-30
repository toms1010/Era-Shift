# Changelog

All notable changes to Era Shift are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

Phase 3: The game has a voice. Audio, animation and effects are no longer a
TODO — the shift now has a sound, the swing has a windup you can read, a hit has
sparks and a moment of silence, and paradox is something you can hear before you
can see it.

### Added

**Procedural audio (`Audio/Synth.hpp`, `Audio/Synth.cpp` — in `erashift_core`)**
- A runtime synthesiser for everything the game plays. There is no audio asset
  in the repository, and the reason is that music which reacts to the era is a
  *parameter*, not a file: see `assets/README.md`.
- `MusicSynth` and `AmbienceSynth`, both continuous and both stateful, so a bed
  does not restart every frame and a parameter change glides instead of jumping.
- Three genuinely different musical scales. The Past is a minor pentatonic, the
  Present a natural minor, and the Future a whole tone — symmetric, so a phrase
  built from it has no leading tone and nowhere to resolve. That is the sound of
  a timeline coming apart, and it comes out of a degree table.
- `Sfx`, a closed enum of 20 one-shots: movement, combat, pickups, seals, the
  shift signature, hazards, victory. A typo is a compile error, not silence.
- `Envelope`, `Voice`, `EraScale` and `noteFrequency` as separately testable
  pieces, and the whole file is pure arithmetic so it is unit tested with no
  sound card.

**Audio output (`Audio/AudioManager.hpp`, `.cpp` — in `erashift_engine`)**
- SDL3_mixer 3.x plumbing: a looping track per bed over an `SDL_AudioStream` that
  is fed one block of float PCM per fixed step. This is what lets a shift glide
  the music mid-bar, which two crossfading loops cannot do.
- Four buses as mixer tags — master, music, ambience, sfx — driven by
  `config/audio.json` and by the audio settings page, which now changes the live
  mixer instead of only the file. Ambience is separate from music on purpose.
- A pool of six interchangeable one-shot tracks, with oldest-first voice stealing
  so a new hit is never dropped in favour of an old footstep. Pitch is
  `MIX_SetTrackFrequencyRatio`, which is how one footstep becomes a heavy one.
- `MusicState` — Exploring, Alert, Combat, Still — driven by the nearest enemy,
  so a tense room sounds tense before anything has gone wrong.
- Paradox tension: tempo instability, a widened tremolo and an audible drop-out
  as paradox climbs.
- Fails soft everywhere. Audio is not in `kRequiredSubsystems`, a partial
  `MIX_Init` is logged and ignored (a machine with no MIDI tables can still play
  synthesised PCM, which is everything this game uses), and a machine with no
  device plays silently rather than not at all.

**Animation (`Game/Animation.hpp`, `.cpp` — in `erashift_core`)**
- `Game::Pose`: the nine numbers a rectangle needs — squash, stretch, bob, lean,
  limb swing, limb spread, arm extension, alpha. Nothing else.
- `AnimationClip` with key poses and gameplay markers, and 21 clips covering
  the player's twelve states and the enemy's nine.
- `AnimationController` with cross-fades, and a rule that a short non-looping
  action cannot be cut short — so mashing attack does not visually restart the
  swing while the input buffer queues a second one.
- `setTime()`, for clips the simulation owns. The attack's key times and its
  `AttackHit` marker are read off `PlayerTuning`, and a test asserts they still
  agree, because the picture and the hitbox must not be two independent timers.
- `Player::attackProgress()` and `PlayerStepEvents`. Footsteps are now counted in
  ground covered rather than in time, so they land where the foot lands instead
  of sliding at low speed and scrabbling at high.

**Effects (`Graphics/ParticleSystem.hpp`, `.cpp` and `Game/Feedback.hpp`, `.cpp`)**
- A fixed pool of 768 particles with no allocation during play, and four shape
  families that the renderer draws in four passes so the blend mode changes four
  times per system rather than once per particle.
- Emitters for impacts, footfalls, dash trails, pickups, seals, deaths, the
  shift's charge and burst, ambient weather and paradox glitch. Era weather is
  three different particles with different behaviour, not one particle recoloured.
- `FeedbackSystem`: one place that turns `WorldEvent`s into sound, particles,
  light and camera work. It holds only a `const World&`, so no amount of effects
  can make the game unwinnable — and a new event is visibly incomplete until it
  has been given a sound and an effect.
- `Camera2D::punch` and `Camera2D::effectiveZoom`: an impact zoom kick that decays
  back to the zoom the player set, rather than pushing it permanently.
- `Renderer2D::drawRectRotated` and `Renderer2D::viewportRect`, for stretched
  sparks and for full-screen effects that survive a resize.
- Hit-stop in the simulation: 45ms on a hit, 110ms on a kill, 90ms when hurt,
  200ms on a seal. It runs on the presentation clock, so the spark keeps
  travelling while the world it came from is still.
- Paradox tiers — Calm, Strained, Fractured, Collapse — driving one eased value
  that both the audio and the renderer read, so a player who hears the music
  destabilise sees the edges close in at the same moment. The HUD gauge grew tick
  marks at the tier boundaries and a live tier label.
- Shift charge aura, shift ripple, paradox vignette and glitch bars. Glitch bars
  regenerate every ~160ms rather than every frame, because per-frame is noise and
  160ms is an event the eye reads as the picture tearing. Drawn in two layers:
  the atmosphere under the text and the shift flash over it, so paradox makes the
  *world* hard to look at without making the objective line unreadable.
- `graphics.maxParticles` is now a real budget rather than a decoration, applied
  once at level load through `ParticleSystem::resize`. A setting that looks live
  and does nothing is worse than no setting.
- **The run timer no longer ticks during hit-stop.** `RunStats::elapsed` counts
  the time the world actually experienced, so it stays in step with how far
  everything moved. It was counting frozen steps, which meant a player who landed
  many heavy hits read a longer time having seen less happen.
- The paradox tier label now pulses on a *tier crossing* rather than on the
  screen flash. A flash also fires on damage and on pickups, and a label that
  pulses when you collect a health cell is lying about why.
- Screen effects split into `drawUnderlay` (vignette, glitch — under the text) and
  `drawOverlay` (flash — over everything). Paradox should make the world hard to
  look at, not the objective line unreadable.
- Menu sounds, and a settings page that adjusts the live mixer.

**World events and player edges**
- `EventRecord` now carries a position, a direction and a strength, so a
  presentation effect knows *where* a thing happened and *how hard* rather than
  guessing.
- Nine new presentation-only `WorldEvent` kinds — jumped, landed, dashed, swing
  started, swing active, footstep, enemy hurt, hazard — carrying no text,
  because a spark at the point of contact says "that landed" better than a toast
  and a toast for every footstep would be unreadable.
- `Player::id()` on enemies, so per-enemy presentation state survives the vector
  compaction that happens when one dies.

**Tests — 47 new cases, all headless**
- `tests/gameplay/TestSynth.cpp`: no one-shot is silent, clipping, or a copy of
  another one-shot; the three eras are in different keys; a parameter change
  glides; synthesis is deterministic.
- `tests/gameplay/TestAnimation.cpp`: every clip is well-formed and its keys are
  in order; a frame hitch does not lose a hit marker; a loop does not drop the
  footstep on its wrap; the attack's hit window still matches `PlayerTuning`.
- `tests/gameplay/TestParticles.cpp`: the pool never grows and never reallocates;
  emission is frame-rate independent; the three eras emit visibly different
  weather; a degenerate region emits nothing rather than dividing by zero.

**Build and tooling**
- `scripts/get_audio_headers.sh`: fetches PulseAudio headers without root, by
  unpacking `libpulse-dev` into `.deps/`. For machines that cannot install a
  package, which is the normal case inside a container.
- `scripts/fetch_deps.sh` now probes ALSA and PulseAudio **independently**. It
  used to gate the PulseAudio driver on ALSA headers being present, which is false
  of exactly the machines that matter: a PipeWire desktop serves audio over the
  PulseAudio protocol and frequently has no ALSA development headers at all. Such
  a machine was left with SDL's dummy backend - silently, forever, with a log
  line that reads like "this machine has no sound card".
- SDL3 is now built with `SDL_AUDIO_DRIVER_PULSEAUDIO=ON` where the headers are
  available, and loaded dynamically, so there is no link-time dependency on
  libpulse.
- `tsan.supp`, wired in through CTest's `ENVIRONMENT` property. libpulsecommon
  spawns its own threads behind mutexes that live inside an uninstrumented
  library, so TSan reports races in it that are not there. The game's own audio
  and input paths are deliberately not suppressed.

**Tests — 24 new cases in a new binary**
- `tests/input/` is now `tests/platform/` and produces
  `erashift_tests_platform`. The event pump and the audio manager are the two
  boundaries where SDL types become the game's own, so they are in
  `erashift_engine` and cannot be covered by the headless gameplay suite - which
  is exactly the point of that suite's rule.
- `TestEventPump`: a burst larger than the pump's buffer loses nothing; a key
  pressed in the middle of a large burst is not dropped; input works before any
  focus event is delivered; losing focus releases held keys; a click reports its
  own position.
- `TestAudio`: a bed survives 200 updates; a bed recovers after a pause and a
  stream starvation; all 20 one-shots can be fired repeatedly without wedging the
  pool; a manager with no device tolerates the entire API; `audio.enabled = false`
  is honoured.

**Documentation**
- `docs/FEEDBACK.md`: audio, animation, effects, hit-stop and paradox, with the
  reasoning behind each decision and the tests that protect them.
- `assets/README.md`: the asset layout, and why there is no `audio/` or
  `sprites/` directory.
- `README.md` rewritten, with two Mermaid diagrams — the frame, and a gameplay
  step — both verified to render.

### Fixed

- **The music and ambience beds stopped after about 100ms and never restarted.**
  This is the one that made the game silent, and it is worth reading because
  every diagnostic available at the time said audio was fine. A mixer track
  reading a stream that has run dry reaches the end of it and stops; both beds
  were started once at initialisation against an *empty* stream, the first
  synthesised block arrived a frame or two later, and by then the tracks were
  gone. The synthesiser went on producing good audio (`musicPeak=0.29` per
  block), every `SDL_PutAudioStreamData` succeeded, the log said
  `audio: started on pulseaudio at 48000 Hz`, and PulseWire showed an uncorked
  `EraShift` sink input - all of it true, and all of it streaming silence.

  Two fixes, and the second is the important one. The streams are primed with a
  quarter second of silence so the start-up race cannot happen, and
  `AudioManager::ensureBedsPlaying()` restarts either bed that has stopped for
  any reason. The watchdog is the real fix: a PipeWire sink suspends when idle, a
  Bluetooth headset appearing rewires the device, and a pause stops feeding the
  streams entirely. All of those end a track silently, and all of them should be
  invisible to the player.

  A build-time trace (`-DERASHIFT_AUDIO_TRACE`) now reports the track's own
  `MIX_TrackPlaying` and the stream backlog. It is off by default and costs
  nothing, and it is kept because this bug was unfindable without it: the only
  symptom a player can report is "it's quiet", which is the one thing every log
  line was contradicting.

- **The event pump read past the end of its own buffer.** `pumpFromSDL` polled
  SDL into a 64-slot `SDL_Event` array while counting up to 512, so any burst of
  more than 64 events in a frame made it hand several hundred events that had
  never been written. Mouse motion arrives one event per movement, so passing 64
  in a single frame is ordinary rather than exceptional. The unread garbage
  carried random `type` fields, and a garbage `SDL_EVENT_MOUSE_MOTION` writes a
  random position into the input state - which is precisely why clicking a button
  missed. It now drains in chunks and dispatches each chunk as it is read.

- **All input was gated on a focus event that many systems never send.** Every
  query returned false unless `focused`, and the only thing that ever set it was
  `SDL_EVENT_WINDOW_FOCUS_GAINED`. A probe on the Wayland session this was
  developed against produced *zero* focus events in four seconds, so a stray
  focus-lost with no matching gain left the game permanently unresponsive with no
  way out but a restart. Held state is still released on focus loss, which is the
  behaviour the gate was there to provide; the edges are no longer gated, because
  a press is an event and discarding it because of unrelated window state is a
  lost input.

- **Mouse clicks were hit-tested at a stale position.** A click event carries the
  cursor position, and the pump threw it away, reading the position from motion
  events alone. SDL will deliver a click with no preceding motion - a trackpad
  tap, a click in the first instant after focus, a compositor that coalesces
  motion - so the click landed wherever the cursor last was. Button and wheel
  handlers now take the position from their event, and the attract script reports
  a real position so it can click a menu row rather than clicking at (0, 0).

- **`InputManager::endFrame` and `Engine::pumpEvents` carried comments describing
  a latching mechanism that does not exist.** The edges are cleared by the *next*
  `beginFrame`, on purpose, so that a press stays readable for every fixed step
  in the frame it happened in. A stale comment saying otherwise invites someone to
  "fix" working code.

- **Hit-stop swallowed player input.** The first implementation returned early
  from `World::update` during a freeze, so a shift or an interact pressed during
  the 160ms after a hit was silently dropped. A freeze that eats a button press
  is felt as the game being unresponsive rather than as a stylistic choice, so
  any step on which the player pressed something now runs normally. Holding a
  direction through a freeze still freezes, because that is what should feel
  heavy. Four gameplay tests caught this.
- **`randomRange` mapped a signed noise source as if it were unsigned.** The
  particle pool's noise is in [-1, 1] because that is the useful shape for
  scattering a velocity, but it was being used directly as a range fraction, so
  half the ambient particles spawned *outside* the room they were spawned in. The
  Past's mean spawn height was above the top of the screen.
- **Every era emitted the same ambient density.** At 60Hz a rate of 3 particles
  per second is 0.05 per frame, and rounding that to an integer gave zero — at
  which point the "at least one per frame" floor took over and the three weathers
  became indistinguishable. Fixed with a fractional accumulator carried between
  calls, plus a per-step cap so a one-second hitch cannot spend the whole budget.
- **Attacks never visibly reached, and deaths never faded.** The animation clip
  table was written with a positional key helper, and values intended for
  `armExtension` and `alpha` were landing in `limbSpread` and defaulting. Both
  compiled, ran, and were wrong. The table now uses designated initialisers, and
  a test asserts the death clip actually ends transparent.
- **A negative frame count tried to allocate the address space.** `Synth`'s
  renderers sized their buffer with `size_t(frames)` before checking `frames`,
  so a negative value became a number near `SIZE_MAX`. A frame that took no time
  at all reaches these functions.
- **A zero-length attack envelope was unreachable.** The envelope returned early
  for `t == 0` before it could check whether the attack was zero-length, so every
  click in the game started from a sample of silence.
- **`config/audio.json` pointed at a `.wav` that does not exist.**
  `eraTransitionSfx: "sfx/era_shift/shift.wav"` named a file in a directory the
  project has never had. The config now describes what actually happens.
- **The attack clip's markers did not match `PlayerTuning`.** They were at 0.08s
  and 0.20s; the windup ends at 0.06s and the active window closes at 0.16s. The
  spark appeared before the hitbox opened. The clip's key times are now read off
  the tuning constants and a test keeps them aligned.
- **`Camera2D` read `m_zoom` directly in its projection maths**, so a punch would
  have had to be pushed into the base zoom to be visible — permanently changing
  how far the player could see. Projections now use `effectiveZoom()`.

### Changed

- `Game::EventRecord` grew three fields. Existing call sites are unaffected; the
  two-argument `emit` overload fills in the player's position and a default
  strength.
- `AnimationController::play` returns `bool` where it returned `void`, so a
  caller can tell a refused transition from a played one.
- `StateContext` gained an `audio` pointer, which is always non-null and whose
  `available()` is the only question callers need to ask.
- The audio settings page gained an AMBIENCE row and now applies its changes to
  the live mixer.

### Fixed in the previous phase, still worth noting

- The renderer was drawing geometry in screen space while the camera transform
  was also applied, putting the world a full camera offset away from where the
  collision code thought it was.
- Rectangle edges showed seams because the shader rounded the wrong corners.
- Tile draws were not merging, costing a draw call per tile per frame.

---

## [0.2.0] — Phase 2, the game

`Playing` is no longer an engine verification scene — it is
a complete run of the Ancient Forest vertical slice, from spawn to the Ancient
Gate, with era-dependent geometry, enemies that exist in some eras and not
others, combat, three seals to recover and a save file to continue from.

### Added

**Gameplay simulation (all in `erashift_core`, no SDL, fully headless-tested)**
- `Game/Era.hpp`: the three eras, era masks and the Past → Present → Future
  cycle.
- `Game/TileMap.hpp`: a tile grid whose cells carry an era mask per property,
  so one grid answers "what is solid right now" for any era without rebuilding.
  Era-dependent kinds: crumbling stone (Past), standing structure (Present),
  crystal growth (Future), one-way platforms, hazards, the gate.
- `Game/Actor.hpp`: swept AABB physics resolved one axis at a time, with
  one-way platform support, sub-stepping that prevents tunnelling, and
  penetration resolution for when the world changes under a body.
- `Game/Player.hpp`: acceleration and friction, coyote time, jump buffering,
  variable jump height, a dash with i-frames, health with invulnerability
  windows, knockback, chrono energy and era shifting.
- `Game/Enemy.hpp`: three kinds with genuinely different tuning — a Sentinel in
  every era, a Future-only Wisp that hovers, a Past-only Warden — each with a
  state machine, a telegraphed wind-up, a punishable recovery, staggered
  thinking and era-gated existence. An enemy outside the current era is frozen,
  not removed: shifting back finds it where it was left, with its health intact.
- `Game/World.hpp`: the rules that connect everything — shifting costs chrono
  energy and rewrites what is solid, a swing only connects with enemies that
  exist in the current era, each seal only yields in its own era, and the gate
  stays shut until all three are taken.
- `Game/Level.hpp` + `tools/generate_level.py`: data-driven levels with three
  character grids per level, plus the Ancient Forest itself.
- `data/levels/ancient_forest.json`: 200 × 30 tiles, three seals, twelve
  enemies, thirteen chrono cells and four health packs, generated by the script
  and reproducible with it.
- `Core/SaveGame.hpp`: versioned JSON saves written atomically, so
  `CONTINUE` restores a run's position, era, health, chrono, paradox, seals and
  statistics.

**Interface**
- `PlayingState` is now a renderer for the simulation: measured render
  interpolation, a three-layer parallax backdrop, era cross-dissolve between
  tile layers, a HUD with health pips, chrono energy, paradox, an era badge with
  a transition progress bar, seal pips, the live objective and a contextual
  interaction prompt.
- `ResultState`: one screen for victory and defeat with the run's statistics.
- `CreditsState`: reachable from the main menu.
- Live control rebinding on the Settings → Controls page, persisted to the
  controls config.
- Settings rows now show their current value, and Settings pages have titles,
  breadcrumbs and back navigation.
- Shared interface layout: `uiScaleFor` and `layoutPanel` are defined once, so
  the main menu, the pause menu and the settings screen cannot drift apart.
- `graphics.uiScale` is now applied, as well as merely stored.
- `Core/DemoDriver.hpp` + `--demo`: a scripted input sequence, used both for the
  attract loop and for automated visual capture.
- `--start-state <playing|settings|credits|paused>` and `--level <file>` as
  developer and content tools.

**Tests**
- `tests/gameplay`: 110 cases over the whole simulation, including a
  reachability proof that walks the shipped level with a movement model matched
  to the controller's actual jump arc, and refuses to pass if the gate, a seal,
  a pickup or an enemy cannot be reached.

### Fixed

**Renderer**
- `drawRect` and the other primitives ignored the camera entirely; only
  textured quads were transformed. The entire world was being drawn in screen
  coordinates.
- Fill rectangles snapped their *size* rather than their edges, leaving a
  one-pixel gap that the cleared framebuffer showed through as a black line
  across every gradient band, every parallax ridge and every tile seam.
- The world-to-screen transform had a leftover vertical flip from an earlier
  presentation mode, which drew the level upside down.
- `drawTriangle` and `drawCircle` silently broke an open batch; they now flush
  it.
- `Renderer2D::toScreen` / `worldScale` exposed so there is one definition of
  the transform.

**Interface**
- The main menu's tagline was drawn inside the title's descenders: the layout
  used fixed pixel offsets that cannot stay correct across type sizes and
  window sizes. It is now measured and centred as one group.
- The pause and settings panels drew their border as a *filled* rectangle over
  the fill, so every panel came out as a solid slab of the border colour.
- Menu hit-testing used the raw row metric rather than the derived row height,
  so a large font put every click one entry too high.
- The era badge and the chrono bar were drawn on top of the health pips.
- HUD text was unreadable over the bright Past palette; the HUD now has soft
  top and bottom scrims.
- Selection chevrons were positioned from the label width but the label was no
  longer centred in the full row once a value was present.
- Buried tile rows were outlined as if they were ledges, so a floor six rows deep
  read as six separate shelves.
- The world drawing passes inherited whatever camera and blend mode the previous
  pass left behind; each now states its own.
- Backdrop bands used a stride that did not match their height, leaving black
  hairlines across the sky.

**Simulation**
- `Level::buildMap` overwrote earlier eras with later ones, so the Present
  layer's empty space erased the Past layer's bridge and the level silently lost
  all of its era-specific geometry.
- A hazard written into one era's layer was applied to all three, painting
  spikes on solid floor.
- `Tile` could not express "solid in the Past, a hazard in the Present"; its
  properties are now era masks.
- `TileMap::oneWayLanding` compared against the body's own top edge, refusing
  every landing from the first frame, and then reported a boolean rather than a
  height, so a body snapped to the row it had just passed and floated one tile
  above the platform.
- `resolvePenetration` initialised its "smallest push" search to zero and then
  rejected every candidate, so it never resolved anything.
- `World` cleared its event log at the start of every step, so anything a caller
  had not yet polled was discarded.
- A world with no level loaded reported a defeat for a run that never started.
- Healing a dead player resurrected them.
- The save directory resolved relative to the working directory, because the
  engine held an unloaded `ConfigManager` rather than the one `main` built.

**Tooling and documentation**
- `--screenshot-frame` was parsed and then ignored.
- `Action::Screenshot` (F12) was bound and documented but never handled.
- The SDL3_image capability log reported three hard-coded `true` values.
- The debug overlay's `update`, `render` and `Entities` rows were permanently
  zero; all three now report real measurements.
- `graphics.uiScale` was adjustable and persisted but never applied.
- `ctest --preset asan` was documented in the README with no matching preset.
- `tests/CMakeLists.txt` described four test binaries and defined two.
- `docs/TESTING.md` claimed 76 unit cases against an actual 94.
- `SettingsState` hard-coded 34px row metrics, ignoring the UI scale, so its
  rows did not match the pause menu's.
- Removed dead code: `InputManager::m_previous`, the unused
  `ResourceManager::m_scaleModeApplied`, the collinear branch in
  `segmentIntersectsSegment`, the always-discarded `quitRequested` out-parameter
  on `EventPump::pumpFromSDL`, and a missing `<cstdio>` include that only
  worked transitively.

### Known issues

- **Audio is not initialised.** `SDL3_mixer` is linked and the volume settings
  on the Audio settings page are stored and persisted, but nothing plays. Audio
  is deferred.
- **`ParadoxManager` / `TimelineManager` do not exist.** The era cycle is
  complete and the paradox counter is scored, but nothing persists across a
  shift. See `docs/ERA_SYSTEM.md` for what that rules out.
- **TSan reports a lock-order inversion inside `libdbus-1`** (via `SDL_Init` and
  window teardown). No frames in Era Shift code and no data race.

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

### Changed

Interface and typography rework, driven by inspecting captured frames rather
than by eye.

- **Resolution-independent UI.** Every size is now expressed against a
  1280x720 reference and multiplied by `UiScale`, which fits the smaller viewport
  axis and is clamped to `[0.70, 2.20]`. Previously all sizes were hard-coded
  pixels, so the HUD either swamped a small window or vanished on a 4K one.
  Styles are rebuilt when the viewport scale changes, so resizing a window
  relayouts the interface instead of leaving stale metrics behind.
- **Rebalanced type hierarchy.** Menu entries dropped from 26px to 21px and
  their captions rose from 15px to 16px, so the two read as a pair. Hierarchy
  now comes from weight and colour rather than a large size jump.
- **The menu sits on a panel.** Previously the animated horizon ran straight
  through the entries, which read as a rendering artefact. The horizon was moved
  to 78% of the viewport height and the list got a panel with a border.
- **Entries are centred** to match the title, tagline and hints; short labels
  pinned to the left of a wide panel looked stranded.
- **A row separator and selection chevrons** replace the left accent bar, which
  became disconnected once the text was centred. The chevrons bracket the label
  at a distance derived from the measured text, so they never overlap it, and
  keep the selection readable without relying on colour alone.
- **Menu captions moved below the list**, shown once for the selected entry
  instead of as a right-aligned column with a 200px gap beside each label.

### Added

- `UiScale` — resolution-independent scaling for the whole interface, with
  tests covering clamping, degenerate viewports, rounding and monotonicity.
- `MenuList` unit tests covering wrap-around, skipping disabled entries, the
  all-disabled list, row geometry and hit testing.
- `.vscode/` launch profile for debugging a crash.

---

[Unreleased]: https://example.invalid/erashift/compare/v0.1.0...HEAD
[0.1.0]: https://example.invalid/erashift/releases/tag/v0.1.0
