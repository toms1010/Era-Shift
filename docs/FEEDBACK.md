# Feedback: animation and effects

This is the document for the systems that turn the simulation into an
experience. They are described together because the interesting problems are the
ones *between* them, not inside any one of them.

- [The shape of the problem](#the-shape-of-the-problem)
- [Animation](#animation)
- [Effects](#effects)
- [Hit-stop](#hit-stop)
- [Paradox](#paradox)
- [Testing](#testing)
- [Tuning](#tuning)

---

## The shape of the problem

`Game::World` decides *that* something happened and *where*. It has no opinion
about what that looks like, and it must not acquire one: the whole simulation is
unit tested headlessly, and a rule that depends on an effect having finished is
not a rule.

Three systems consume what the world reports:

```
World::takeEvents()  ──►  FeedbackSystem::consume()  ──┬─►  ParticleSystem
                                                       ├─►  Glows
    (what happened,                                 └─►  Camera2D (shake, punch)
     and where)                                          ScreenEffect (ripple, flash)
```

`FeedbackSystem` is a single class on purpose. The alternative — a particle here,
a flash in the state, a shake in the renderer — spreads the answer to "what does a
hit look like" across four files, and the next event to be added is the one that
gets only some of the effects. Routing every `WorldEvent` through one switch means
a new event is *visibly* incomplete until it has been given an effect.

`FeedbackSystem` holds only a `const World&`. It cannot change the simulation, so
no amount of effects can make the game unwinnable.

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
at high speed; one tied to ground covered puts the step where the foot actually
landed, and every step raises a `PlayerFootstep` event so the dust lands with it.
The clip still has `Footstep` markers for a controller running free, but the
player's own footsteps come from the simulation.

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

That is the whole condition — the key down, and chrono to spend — and the
character pose has to use the same one. It is written twice, in
`FeedbackSystem::update` and in `Game::playerClipFor`, and the test
*"the charge pose and the charge aura agree on what charging is"* exists because
they are two expressions of one rule in two files. They disagreed: the pose took
"does the player have any chrono", which is true for the whole game, so the
player spent every run in the translucent charging pose and never showed a walk
cycle. A rule that needs to be in two places needs a test that says so.

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
the sparks keep travelling while the world they came from is still. That is the
entire point of hit-stop.

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
| `Strained` | 18 | slight vignette, denser weather |
| `Fractured` | 38 | glitch bars, heavier vignette |
| `Collapse` | 62 | the bar turns red, the tearing is constant, the edges close in |

Tiers rather than a continuous value because a player can be told "you are at
Tier 3" and understand that, whereas "your paradox is 41.2" means nothing. The
gauge's tick marks are the tier boundaries and the fill carries the tier's
colour, so the HUD and the screen effect are visibly the same system.

`tension()` is the eased paradox ratio, and it is the single value every
presentation effect reads. One number driving the vignette, the glitch bars, the
weather density and the HUD pulse is why they stay in step rather than
destabilising in sequence.

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
| Animation | `tests/gameplay/TestAnimation.cpp` | 26 |
| Particles | `tests/gameplay/TestParticles.cpp` | 15 |

Both run in the headless `erashift_tests_gameplay` binary, with no display, which
is the reason this work lives in `erashift_core`.

What they assert, beyond the obvious:

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
| `kHitStop*` | `World.cpp` | freeze duration per kind of impact |
| `kTier*` | `Feedback.cpp` | the paradox thresholds |
| `kMaxGlitch` | `Feedback.cpp` | how far the screen may be allowed to distort |

`graphics.maxParticles` in `config/graphics.json` is the pool size. It is set
higher than the pool actually uses (768) and is a ceiling, not a setting: raising
it past the pool's capacity will not make the game busier.
