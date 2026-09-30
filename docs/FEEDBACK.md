# Feedback: sound, animation and effects

This is the document for the three systems that turn the simulation into an
experience. They are described together because the interesting problems are the
ones *between* them, not inside any one of them.

- [The shape of the problem](#the-shape-of-the-problem)
- [Audio](#audio)
- [Animation](#animation)
- [Effects](#effects)
- [Hit-stop](#hit-stop)
- [Paradox](#paradox)
- [Testing](#testing)
- [Tuning](#tuning)

---

## The shape of the problem

`Game::World` decides *that* something happened and *where*. It has no opinion
about what that looks or sounds like, and it must not acquire one: the whole
simulation is unit tested headlessly, and a rule that depends on a sound having
finished is not a rule.

Three systems consume what the world reports:

```
World::takeEvents()  ──►  FeedbackSystem::consume()  ──┬─►  AudioManager::play()
                                                       ├─►  ParticleSystem
    (what happened,                                 └─►  Camera2D (shake, punch)
     and where)
```

`FeedbackSystem` is a single class on purpose. The alternative — a sound here, a
particle there, a flash in the state — spreads the answer to "what does a hit
look like" across four files, and the next event to be added is the one that gets
only some of the effects. Routing every `WorldEvent` through one switch means a
new event is *visibly* incomplete until it has been given a sound and an effect.

`FeedbackSystem` holds only a `const World&`. It cannot change the simulation, so
no amount of effects can make the game unwinnable.

## Audio

### The arrangement

Three moving parts, deliberately separated:

| Part | Lives in | Depends on |
| --- | --- | --- |
| `MusicSynth`, `AmbienceSynth`, `renderSfx` | `erashift_core` | nothing |
| `AudioManager` | `erashift_engine` | SDL3_mixer |
| bus volumes | `config/audio.json` | the mixer tags |

The synthesiser is in Core for the same reason the simulation is: it is pure
arithmetic, so it can be tested with no sound card. `AudioManager` is the only
part that needs a device.

### Streaming, not files

Music and ambience are not loops. `AudioManager` creates an `SDL_AudioStream` per
bed, and every fixed step it renders one block of audio and pushes it in with
`SDL_PutAudioStreamData`. The track is set to loop forever over a stream that
never finishes, which is what the arrangement is for:

```cpp
MIX_SetTrackAudioStream(track, stream);
SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);   // forever
MIX_PlayTrack(track, props);
```

This is the reason a shift sounds like a shift. Changing the era is a change to
*parameters* — a different scale, a different filter cutoff, a different detune —
and those are eased over about a third of a second. Two crossfading loops
cannot do that; they can only swap.

### The three eras are different keys, not different presets

| Era | Scale | Root | Character |
| --- | --- | --- | --- |
| Past | minor pentatonic, 5 degrees | −7 | open, unresolved |
| Present | natural minor, 7 degrees | −5 | the only one that resolves |
| Future | whole tone, 6 degrees | +2 | symmetrical, so it never lands |

The Future's whole-tone scale is the important one. A symmetric scale has no
leading tone, so a phrase built from it has nowhere to go. That is the whole
sound of a timeline that has come apart, and it comes out of the degree table
rather than out of a filter sweep.

### Music states

The bed never stops; only its density changes.

| State | Intensity | Tempo | When |
| --- | --- | --- | --- |
| `Exploring` | 0.0 | 84 | no enemy in the current era |
| `Alert` | 0.35 | 96 | nearest enemy > 200px |
| `Combat` | 1.0 | 116 | nearest enemy < 200px |
| `Still` | 0.10 | 68 | the run has ended |

Intensity above zero brings in an eighth-note arpeggio and a bass note. The music
therefore does not merely get louder in a fight — it gains content, which is
legible before the player has consciously decided they are in one.

### Buses

The mixer's tags are the buses. `MIX_TagTrack` puts the two beds and the one-shot
pool into `music`, `ambience` and `sfx`; `MIX_SetTagGain` sets their levels, and
`MIX_SetMixerGain` is the master. Ambience is separate from music on purpose:
ambience is the constant bed a player stops hearing, and ducking it without losing
the music is the difference between a setting and a nuisance. All four levels are
in `config/audio.json` and on the audio settings page, and moving a slider changes
the live mixer rather than only the file.

### One-shots

Rendered once at start-up into a cache, then played through a pool of six
interchangeable tracks. Pitch is `MIX_SetTrackFrequencyRatio`, which is how one
footstep becomes a heavier or lighter one without a second buffer.

When every channel is still sounding, the oldest is stolen rather than the new
sound being dropped — a fresh hit is more informative than the tail of an old
one. A dropped footstep is inaudible; a dropped hit is a missing hit.

### Beds never stop

A mixer track reading a stream that has run dry reaches the end of it and stops.
Both beds are started once, against a stream with nothing in it, so the first
synthesised block arrives a frame or two *after* the track has already ended.

This is worth spelling out because it is the least findable bug in the project.
When it was present:

- the synthesiser produced good audio, peak 0.29 per block
- every `SDL_PutAudioStreamData` returned true
- the log said `audio: started on pulseaudio at 48000 Hz`
- `pactl list sink-inputs` showed an uncorked `EraShift` stream

and the game was silent. Every diagnostic agreed with every other one and all of
them were describing a stream going nowhere.

So the streams are primed with a quarter second of silence, and
`ensureBedsPlaying()` restarts either bed that has stopped. The watchdog is the
real fix rather than the prime, because the ways a bed stops are not exotic: a
PipeWire sink suspends when idle, a Bluetooth headset appearing rewires the
device, a pause stops feeding the streams. All of them should be invisible.

`-DERASHIFT_AUDIO_TRACE` reports the track's own `MIX_TrackPlaying` and the stream
backlog. It is off by default and costs nothing when off. It is kept because the
only symptom a player can report is "it's quiet" — the one thing every log line
was contradicting.

### Failing soft

Audio is never part of `kRequiredSubsystems`. A machine with no sound card, a
container, and a CI runner all still get a playable game; `context().audio` is
always non-null and `available()` is the only question to ask.

`MIX_Init()` failing is also non-fatal, and the log says why:

```
INFO audio: partial mixer init (Couldn't open timidity.cfg: ...); PCM is unaffected
INFO audio: started on dummy at 48000 Hz
```

A machine with no MIDI tables cannot play a MIDI file. It can play 800 float
frames the synthesiser just computed, which is everything this game uses.

## Animation

### Poses, not sprites

A character here is four rectangles. A rectangle animated well needs nine
numbers, and they are the nine fields of `Game::Pose`:

| Field | Range | What it does |
| --- | --- | --- |
| `scaleX`, `scaleY` | 0..2 | squash and stretch |
| `offsetX`, `offsetY` | pixels | bob, in facing space |
| `lean` | degrees | shears the top edge; the feet stay put |
| `limbSwing` | −1..1 | the legs and the trailing arm |
| `limbSpread` | −1..1 | narrows or widens the silhouette |
| `armExtension` | 0..1 | how far the leading arm reaches |
| `alpha` | 0..1 | the death fade |

Squashing is about the *bottom* edge and the lean is applied afterwards, because
squashing about the centre sinks a character into the floor and shearing about
the original centre slides its feet out from under it. That ordering is in
`poseRect()` in `PlayingState.cpp` and is not negotiable.

### The simulation owns the clock

This is the single most important rule in the animation code, and it is worth
stating plainly because the obvious design is the wrong one:

> The simulation decides *when* something is dangerous. The animation is told
> *where the simulation is*. It does not run its own timer over the same event.

`Player::attackProgress()` returns 0..1 through a whole swing, windup included.
`PlayingState` calls `AnimationController::setTime(progress * duration)`. The
attack clip's key times and its `AttackHit` marker are then read off
`PlayerTuning` — the windup ends at 0.06s, the active window closes at 0.16s,
recovery ends the clip at 0.32s — and a test asserts they still agree.

The alternative, letting the controller advance on its own clock, is two
independent timers over the same 0.32 seconds. It works right up until a frame
hitches, and then the swing has visibly recovered while the hitbox is still open.

Everything that is *not* load-bearing free-runs: footstep dust, the dash trail,
the death fade. Those can be on a clock, because nothing about them can be wrong.

### Footsteps are counted in distance, not time

`Player` accumulates `|velocity.x| * dt` and raises a `footstep` edge every 76
pixels. A walk cycle tied to a clock slides its feet at low speed and scrabbles
at high speed; one tied to ground covered puts the sound where the foot actually
landed. The clip still has `Footstep` markers for a controller running free, but
the player's own footsteps come from the simulation.

### Non-looping clips refuse to be interrupted

`play()` returns false if the current clip is a short non-looping action that has
not finished. Mashing attack therefore does not visually restart the swing while
the input buffer queues a second one — the swing you see is the swing the
simulation is running. `force` exists for resets.

## Effects

### The particle pool

Fixed size (768), preallocated, no allocation during play. A game that spawns
particles during combat must never be the thing that decides to call into the
allocator, because a frame spike in the middle of a shift is exactly the moment
it hurts most.

When the pool is full the oldest particle is recycled, so a newer effect always
wins over an older one. `droppedCount()` reports refusals, which is a tuning
signal rather than an error.

Emission is **frame-rate independent**, which takes one specific piece of care. At
60Hz a rate of 3 particles per second is 0.05 per frame, and rounding that to an
integer gives zero — at which point a "at least one per frame" floor takes over
and *every era emits the same number*. Three distinct weathers quietly become one.
The fix is a fractional accumulator carried between calls
(`m_ambientAccumulator`), plus a per-step cap so a one-second hitch cannot spend
the whole budget on the one frame where the game is already late.

### What each era's weather is

| Era | Particle | Behaviour |
| --- | --- | --- |
| Past | leaves | spawned in the upper 40%, drift and fall, spinning |
| Present | dust | nearly weightless, spread through the whole view |
| Future | embers | spawned in the lower half, rise, streaked |

Leaves start high and embers start low, so the two occupy opposite halves of the
screen. That is what the eye reads as "a different place", and it is why the
effect is not one particle recoloured three ways.

### The shift

`emitShiftWave` is one call and it is the most important effect in the game. A
low `charge` pulls particles inward toward the player; a high one throws them
outward. `emitShiftBurst` then adds three staggered rings and a 34-spark debris
burst. The rings are staggered because one reads as a click and three read as a
shockwave.

The charge is driven from whether the key is *actually* held, so a player can see
what they are charging, aim it, and let it go.

### Screen effects

Drawn in two layers, and the split is a design decision rather than tidiness:

| Layer | Contains | Where it goes |
| --- | --- | --- |
| `drawUnderlay` | vignette, glitch | over the world and the HUD bars, **under the text** |
| `drawOverlay` | flash | over everything, under the debug overlay |

Paradox is meant to make the *world* hard to look at, not the objective line. Six
additive glitch bands across the middle of the screen will sit exactly where the
toasts and the objective are, and a player who cannot read what to do next is not
under pressure, they are stuck. So the atmosphere goes under the text and the
glitch's alpha ceiling is set by legibility rather than by how dramatic it looks.

The debug overlay is last of all, because a legible frame timing readout is its
entire job.

| Effect | What it is |
| --- | --- |
| Ripple | Expanding rings from centre, on a shift. A ring, not a wobble: a shift is a wavefront and a wobble is a mistake. |
| Vignette | Paradox darkens the edges. At Collapse the middle is still clear, because a game you cannot see is not tense, it is broken. |
| Glitch | Horizontal bars, regenerated every ~160ms rather than every frame. Per-frame is noise; 160ms is an event, and the eye reads that as the picture tearing. |
| Flash | A white wash, decaying at 4.5/s. |

### Lighting

There is none, properly. `emitGlow` leaves a bounded list of additive
three-square glows, which is what a rectangle renderer can honestly fake. It is
enough to make a hit's light spread as it dies; it is not a lighting model and is
not pretending to be one.

## Hit-stop

`World` freezes the whole simulation for 45ms on a hit, 110ms on a kill, 90ms
when the player is hurt and 200ms on a seal. It is in `World` rather than in the
camera because it has to stop the *enemy*: a freeze that only stops the camera
lets the player land a blow, watch the world stop, and still be hit by something
mid-freeze.

Two rules make it safe:

**It runs on the presentation clock, not the simulation clock.** `World::update`
returns early during a freeze, but `FeedbackSystem::update` is still called, so
the spark travels and the music keeps going while the world it came from is
still. That is the entire point of hit-stop.

**It never swallows a button press.** A step where the player pressed shift,
interact, jump, dash or attack runs normally. A freeze that eats an input is
felt as the game being unresponsive rather than as a stylistic choice. Holding a
direction *through* a freeze is allowed, because that is exactly what should feel
heavy.

**The run timer does not tick during a freeze.** `RunStats::elapsed` is
incremented after the freeze check, so it measures how long the world actually
experienced. Counting frozen steps would make the results screen disagree with
the run it describes: a player who landed many heavy hits would read a longer
time having seen less happen.

Three tests in `TestWorld.cpp` cover this, one of which exists purely as a
regression: the first implementation returned from `update()` before processing
commands, and four gameplay tests failed in a way that looked like a simulation
bug rather than a presentation one.

`Camera2D::punch` is the zoom half of the same idea and is separate from
`setZoom`: the punch decays back to the zoom the player set, rather than pushing
it. `effectiveZoom()` is what the projection code uses.

## Paradox

Paradox is the run's accumulation of shifting and killing. It is the only number
in the game that changes how it *feels* rather than how it plays, so it gets
named tiers rather than a slider:

| Tier | Paradox | What changes |
| --- | --- | --- |
| `Calm` | 0 | nothing |
| `Strained` | 18 | tempo instability, slight vignette, denser weather |
| `Fractured` | 38 | audibly unstable, glitch bars, heavier vignette |
| `Collapse` | 62 | the bar turns red, the music drops out, the edges close in |

Tiers rather than a continuous value because a player can be told "you are at
Tier 3" and understand that, whereas "your paradox is 41.2" means nothing. The
gauge's tick marks are the tier boundaries and the fill carries the tier's
colour, so the HUD and the screen effect are visibly the same system.

`tension()` is the eased paradox ratio, and it is the single value that both the
audio and the renderer read. One number driving two systems is why they stay in
step: a player who hears the music destabilise sees the edges close in at the same
moment, and the causal link is legible.

## Seeing it working

The effects described here are all in one screenshot, and it is a real frame —
`--demo` makes the run reproducible, so the same frame number always gives the
same picture.

```bash
./build/release/EraShift --headless --demo --frames 340 \
    --start-state Playing \
    --screenshot /tmp/shift.png --screenshot-frame 55
```

Frame 55 of the attract script catches a shift at its peak: the three shockwave
rings from `emitShiftBurst`, the debris sparks, the white `m_flash` wash, the
camera punch, and the charge aura the player is still standing in. Frame 80 shows
the same run a moment later with the shift rings around the player and the
Future's embers rising through them.

## Testing

| System | File | Cases |
| --- | --- | --- |
| Synthesiser | `tests/gameplay/TestSynth.cpp` | 13 |
| Animation | `tests/gameplay/TestAnimation.cpp` | 16 |
| Particles | `tests/gameplay/TestParticles.cpp` | 15 |

All three run in the headless `erashift_tests_gameplay` binary, with no sound card
and no display, which is the reason the pure-arithmetic half of this work lives in
`erashift_core`.

What they assert, beyond the obvious:

- **No one-shot is silent, clipping, or a copy of another one-shot.** A pair-wise
  comparison over all 20 sounds catches a copy-paste in a switch statement, which
  is the classic way an eight-way audio table quietly becomes a two-way one.
- **The attack clip's hit window still matches `PlayerTuning`.** This is the test
  that stops the picture and the simulation drifting apart.
- **A frame hitch does not lose a hit marker.** A 200ms step still delivers
  `AttackHit` and `AttackEnd`.
- **A looping clip fires its markers on the wrap.** Otherwise a footstep on the
  last eighth is dropped once every two steps.
- **Emission is frame-rate independent**, and the three eras emit *different*
  amounts. The second of those caught the accumulator bug described above.
- **The pool never grows and never reallocates**, and a full pool recycles rather
  than going silent.

## Tuning

Everything worth tuning is a named constant, not a literal in the middle of a
function:

| Constant | Where | Effect |
| --- | --- | --- |
| `PlayerTuning` | `Player.hpp` | movement, jump, dash, **and the swing's three phases** |
| `EnemyTuning` | `Enemy.hpp` | per-kind AI, **and the enemy's three attack phases** |
| `MusicParams` | `Synth.hpp` | tempo, intensity, gain, instability |
| `EraScale` | `scaleFor()` | the three scales |
| `Bus` volumes | `audio.json` | the four buses |
| `kHitStop*` | `World.cpp` | freeze duration per kind of impact |
| `kTier*` | `Feedback.cpp` | the paradox thresholds |
| `kMaxGlitch` | `Feedback.cpp` | how far the screen may be allowed to distort |

`graphics.maxParticles` in `config/graphics.json` is the pool size. It is set
higher than the pool actually uses (768) and is a ceiling, not a setting: raising
it past the pool's capacity will not make the game busier.
