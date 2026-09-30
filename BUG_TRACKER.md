# Bug Tracker

> Phase 3 — Feedback systems (audio / animation / effects)
> Last updated: 2026-09-30

## Statistics

| | |
| --- | --- |
| 🔴 Open | 0 |
| 🟡 In progress | 0 |
| 🟢 Fixed | 12 |
| ⚪ Closed | 0 |
| 📝 **Total** | **12** |

Eleven of these twelve were found by tests added alongside the system that had
the bug. The twelfth was reported by a player.

Their shared characteristic is worth stating once: **they compiled, they ran, and
they were wrong.** A defect of that shape cannot be found by running the game and
looking at it, because everything you can see says the system is working. It can
only be found by asserting on a *property* of the behaviour and letting the
assertion fail.

Which means the interesting question about any of these is not "how was it
spotted" but "what property was violated" — that is what the test now protects.

---

## 🟢 BUG-001 · Hit-stop swallowed player input

**Severity**: High · **Status**: Fixed

**Symptom** A player pressing `Q` (era shift) or `E` (interact) during the 160ms
freeze after a heavy hit had the input silently dropped. The world would appear
to hitch, then nothing would happen.

**Root cause** `World::update` returned early while frozen, skipping the entire
step — including command processing. The freeze is a presentation effect, and it
was eating the player's most important input.

**Fix** The freeze only intercepts steps on which the player pressed *nothing*:

```cpp
const bool pressed = commands.shiftPressed || commands.interactPressed ||
                     input.jumpPressed || input.dashPressed || input.attackPressed;
const bool frozen  = (m_hitStop > 0.0f) && !pressed;
```

Holding a direction *through* a freeze still freezes, because that is exactly
where the weight should be felt.

**Verification** Four gameplay tests failed and now pass.

---

## 🟢 BUG-002 · `randomRange` treated signed noise as unsigned

**Severity**: High · **Status**: Fixed

**Symptom** Past-era leaves spawned at an average height *above the top of the
screen* (y = −19). Half the ambient particles were appearing outside the room they
were spawned into.

**Root cause** The particle pool's noise source, `nextRandom()`, returns
`[−1, 1]` — which is the useful shape for scattering a velocity. But
`randomRange(low, high)` used it directly as a range fraction:

```cpp
return low + (high - low) * system.nextRandom();   // half the results are below low
```

**Fix** Split into two functions with unambiguous intent:

| Function | Purpose |
| --- | --- |
| `randomRange(system, low, high)` | The interval `[low, high)`, rescaling internally |
| `randomSigned(system)` | `[−1, 1]`, for velocities and rotations |

**Verification** `TestParticles.cpp` asserts that the three eras' ambient
particles spawn in the upper, middle and lower parts of the screen respectively.

---

## 🟢 BUG-003 · All three eras emitted identical ambient density

**Severity**: Medium · **Status**: Fixed

**Symptom** All three eras emitted exactly the same number of ambient particles
(measured: 120 each). Three distinct weathers had quietly become one.

**Root cause** At 60Hz, a rate of 3 particles per second is 0.05 per frame, and
rounding that to an integer gives zero — at which point the "at least one per
frame" floor took over and the rate parameter was bypassed entirely.

**Fix** Accumulate a fractional balance across calls:

```cpp
m_ambientAccumulator += rate * dt;
int count = static_cast<int>(m_ambientAccumulator);
m_ambientAccumulator -= static_cast<float>(count);
```

A per-step cap of 6 is kept alongside it, so a one-second hitch cannot spend the
whole budget on the one frame where the game is already late.

**Result** Past 13 / Present 8 / Future 21 particles, and frame-rate independent.

**Verification** The `emission is frame-rate independent` and
`the three eras emit visibly different weather` tests.

---

## 🟢 BUG-004 · Attacks never visibly reached, and deaths never faded

**Severity**: Medium · **Status**: Fixed

**Symptom** The arm did not extend during a swing (`armExtension` was always 0),
and a body stayed fully opaque at the end of its death animation (`alpha` was
always 1).

**Root cause** The clip table was written with a positional `key()` helper — nine
floats in a row. A value intended for `armExtension` landed in `limbSpread`, and
a `0.0` intended for `alpha` landed in `armExtension` and was then overwritten by
the default of 1.0.

**Fix** Everything now uses **designated initialisers**, where the field name is
the documentation:

```cpp
key(0.16f, (Rig{.scaleX = 1.06f, .scaleY = 0.94f, .offsetX = 2.0f,
                .lean = 12.0f, .armExtension = 1.0f}.pose()))
```

**Verification** `a non-looping clip finishes and holds its last pose` asserts
that the death clip ends with `alpha < 0.1`.

---

## 🟢 BUG-005 · A negative frame count tried to allocate the address space

**Severity**: Medium · **Status**: Fixed

**Symptom** Passing −5 frames threw
`cannot create std::vector larger than max_size()`.

**Root cause** Resize happened before the check: `out.assign(size_t(frames) * 2, 0)`
turned a signed negative into an unsigned value near `SIZE_MAX`.

**Fix** The guard moved **before** the resize, in all three places:
`MusicSynth::render`, `AmbienceSynth::render` and `renderSfx`.

**Why it matters** A breakpoint, a pause, or any frame that took no time at all
reaches these functions.

---

## 🟢 BUG-006 · A zero-length envelope was unreachable

**Severity**: Low · **Status**: Fixed

**Symptom** An envelope with `attack = 0` returned 0 at t = 0 instead of 1.

**Root cause** `if (t <= 0.0f) return 0.0f;` returned before it could reach the
`attack > 0.0f ? t / attack : 1.0f` branch.

**Fix** Changed to `t < 0.0f`.

**Consequence** Before the fix, every click in the game began from a sample of
silence.

---

## 🟢 BUG-007 · The attack clip's markers disagreed with `PlayerTuning`

**Severity**: Medium · **Status**: Fixed

**Symptom** The spark appeared *before* the hitbox opened.

**Root cause** The markers were written at 0.08s and 0.20s. The windup actually
ends at 0.06s and the active window closes at 0.16s — a 20–40ms discrepancy,
which is exactly the magnitude of "that looks wrong but I couldn't say what".

**Fix** The clip's key times are now read directly from `PlayerTuning`, and a test
(`the attack clip's hit window matches the player's swing`) keeps the two aligned.

**Why it matters** This was the first return on the rule that *the simulation
decides when something is dangerous, and the animation is told where the
simulation is*. If the picture and the hitbox are two independent timers, this bug
comes back.

---

## 🟢 BUG-008 · `config/audio.json` pointed at a `.wav` that did not exist

**Severity**: Low · **Status**: Fixed

**Symptom** `eraTransitionSfx: "sfx/era_shift/shift.wav"`.

**Root cause** It referenced a directory the project has never had.

**Fix** The config now describes what actually happens: every sound is synthesised
at runtime, and the volumes map to the mixer's four buses. The key was removed.

**Note** The defining characteristic of this one is that it **could never fire**,
because no code read it. It was the first evidence of documentation and reality
coming apart, and it had been sitting in the config the whole time.

---

## 🟢 BUG-009 · The music and ambience beds stopped after ~100ms and never restarted

**Severity**: Critical · **Status**: Fixed · **Reported by** a player

**Symptom** The game was completely silent. This is the only one of the four
player-reported defects that made the game unplayable.

**Root cause** A mixer track reading a stream that has run dry reaches the end of
it and stops. Both beds were started once during initialisation, at which point
the stream was *empty* — the first synthesised block arrived a frame or two later,
by which time the tracks had already ended.

**Why every diagnostic said audio was fine** This is the least findable bug in the
project, and the table is worth keeping:

| Diagnostic | What it said at the time |
| --- | --- |
| Synthesiser output | Peak 0.29 per block — correct |
| `SDL_PutAudioStreamData` | Returned true, every time |
| The log | `audio: started on pulseaudio at 48000 Hz` |
| `pactl list sink-inputs` | `EraShift` present, `Corked: no` |
| What you could hear | **Nothing** |

Every diagnostic agreed with every other one, and together they described a
stream going nowhere.

**Fix** Two changes, of which the second is the real one:

1. Prime both streams with a quarter second of silence before starting the
   tracks, which removes the start-up race.
2. `AudioManager::ensureBedsPlaying()` restarts either bed that has stopped, for
   any reason.

**Why the watchdog is the important half** The ways a bed stops are not exotic: a
PipeWire sink suspends when idle, a Bluetooth headset appearing rewires the
device, and a pause stops feeding the streams entirely. All of them should be
invisible to the player, and priming alone covers none of them.

**Verification** Recording the sink monitor and analysing peak amplitude in
half-second windows: before the fix, 0 of 31 windows had any signal; after,
26 of 31 peaked at roughly 6000 out of 32767. Two new tests — a bed surviving 200
updates, and a bed recovering after a pause and a stream starvation.

**By-product** A `-DERASHIFT_AUDIO_TRACE` build option that reports the track's
own `MIX_TrackPlaying` and the stream backlog. Off by default and free when off,
but kept, because the only symptom a player can report is "it's quiet" — which is
the one sentence every log line was contradicting.

**Related** SDL's audio drivers are compiled in rather than loaded at runtime, so
this could not have been fixed in application code at all. `scripts/fetch_deps.sh`
had been gating the PulseAudio driver on ALSA headers being present, which is
false of exactly the machines that matter: a PipeWire desktop serves audio over
the PulseAudio protocol and frequently has no ALSA development headers at all.
See the follow-up work below.

---

## 🟢 BUG-010 · The event pump read past the end of its own buffer

**Severity**: Critical · **Status**: Fixed · **Reported by** a player

**Symptom** Clicking a menu button did nothing. The keyboard behaved erratically.

**Root cause** `pumpFromSDL` polled SDL into a 64-slot `SDL_Event` array while
counting up to 512, then handed the count to `pump`:

```cpp
SDL_Event events[64];          // 64 slots
do { count = SDL_PollEvent(events); total += count; } while (count > 0 && total < 512);
pump(events, total);           // reads 512
```

`SDL_PollEvent` never writes more than the array holds, so `total` grows past 64
and `pump` then reads several hundred events that were **never written**. Every
mouse movement is its own event, so passing 64 in a single frame is ordinary
rather than exceptional. The uninitialised garbage carried random `type` fields,
and a garbage `SDL_EVENT_MOUSE_MOTION` writes a random position into the input
state — which is precisely why the click missed.

**Fix** Drain in chunks and dispatch each chunk as it is read. Nothing is
overrun, and nothing is dropped.

**Verification** New `TestEventPump`: 200 events lose nothing, a key pressed in
the middle of a large burst is not dropped, and a click reports its own position.

---

## 🟢 BUG-011 · All input depended on an event many systems never send

**Severity**: High · **Status**: Fixed · **Reported by** a player

**Symptom** The game was completely unresponsive, and unrecoverable without a
restart.

**Root cause** All three queries were gated on `focused`, and the only thing that
ever set it was `SDL_EVENT_WINDOW_FOCUS_GAINED`. Measured on the Wayland session
this was developed against: **zero** focus-gained and **zero** focus-lost events
in four seconds. Any focus-lost without a matching gain left the game permanently
dead.

**Fix** Key *edges* are no longer gated on focus. Held state is still released on
focus loss — which is the behaviour the gate was there to provide — but a press
is an event, and discarding it because of unrelated window state is a lost input.

**Verification** New tests: input must work before any focus event has been
delivered, and losing focus releases held keys.

---

## 🟢 BUG-012 · Mouse clicks were hit-tested at a stale position

**Severity**: Medium · **Status**: Fixed · **Reported by** a player

**Symptom** Clicking a button hit whatever the cursor had been over last.

**Root cause** An SDL click event **carries the cursor position**, and the pump
threw it away, taking the position from motion events alone. SDL will happily
deliver a click with no preceding motion event — a trackpad tap, a click in the
first instant after focus, or a compositor that coalesces motion.

**Fix** The button and wheel handlers now take the position from their own event.
The attract script reports a real position too, so it can actually click a menu
row rather than clicking at (0, 0).

---

## Checked, and *not* found

Recorded to show what was looked at, not to pad the list.

| Checked | Result |
| --- | --- |
| `MIX_Init()` fails on a machine with no `timidity.cfg` | Expected and non-fatal. Everything synthesised is PCM; the log says `PCM is unaffected` and the mixer still starts at 48kHz |
| No audio device (CI, container) | Expected and non-fatal. `context().audio` is always non-null, `available()` returns false, the game runs silently |
| A looping clip wrapping past its end under `setTime` | A swing would wrap to the end and look recovered while still being thrown. Now clamped |
| A very long single step (1 second) | Every emitter has a per-step cap, so a hitch cannot request hundreds of particles at once |
| Synthesis determinism | Two identical calls produce bit-identical output. Without this, a report of the form "the sparks looked wrong" would be unanswerable |
| The particle pool running out | Recycles the oldest rather than dropping. A busy screen that silently loses every effect is worse than one that drops a footstep |

---

## Follow-up work

1. **SDL's audio backends are a build-time decision, and nothing checks it.**
   The drivers are compiled in, not loaded at runtime, so "no sound" is not a
   configuration problem and no amount of in-game setting will fix it. Add a
   post-build self-check that inspects SDL's driver list for `pulseaudio` or
   `alsa` and fails loudly if neither is present. A game that runs silently is
   much worse than a build that stops.

2. **`graphics.maxParticles` is a ceiling, not a setting.** It is now wired up
   (it was dead config) but raising it above the pool's capacity will not make the
   game busier. Either make it meaningful or delete it — a setting that looks
   live and does nothing is worse than no setting.

3. **Footstep pitch is a linear function of speed.** Stepping on metal and
   stepping on grass are the same sound. `TileKind` already carries enough
   information to tell them apart.

4. **There is no loudness test for the mixer.** The tests assert `peak <= 1.0`, but
   an effect that is uniformly 20dB below the peak passes every test and is
   inaudible in the game. A per-effect loudness floor would catch a sound that was
   scaled too far down.

5. **`-DERASHIFT_AUDIO_TRACE` is not documented in the build docs.** It is the
   only thing that found BUG-009, which is an argument for documenting it
   properly rather than leaving it as a flag in a source file.
