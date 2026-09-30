# The era system

How one world is three worlds. This is the mechanic the whole game is built
around, so it is worth being precise about how it is represented and why.

---

## One grid, three masks

The level is not three grids that get swapped. It is **one grid of cells, and
each cell carries an era mask per property**:

```cpp
struct Tile {
    TileKind kind;        // for presentation
    EraMask  solidIn;     // eras in which this cell blocks movement
    EraMask  hazardIn;    // eras in which this cell damages
    EraMask  oneWayIn;    // eras in which this cell blocks only from above
};
```

A cell is solid in the Past and a hazard in the Present. A cell is a crumbling
ledge in the Past and thin air in the Present and the Future. Both are one
`Tile`, and both fall out of the same representation.

This matters for two reasons:

**Queries are era-parameterised, not era-specific.** `rectBlocked(rect, era)` is
one function, not three. The physics sweep takes the current era and asks. There
is no world to rebuild, no data to stream, no transition to hide.

**Shifting costs nothing structurally.** Changing eras is changing a `uint8_t`
and re-resolving the player out of anything that appeared under them.

The level file lists a character grid per era, and `Level::buildMap` combines
them by intersection per era: a property is copied across only for the era whose
layer declares it. (Combining by overwrite — the obvious implementation — erases
the Past layer's bridge with the Present layer's empty space, and the level
silently loses all of its era-specific geometry. That was a real bug.)

## Tile characters

```
.   empty
#   solid in every era
=   one-way platform, every era
c   solid in the Past only     - ruins that have not rotted
b   solid in the Present only  - the structure still standing
x   solid in the Future only   - matter grown after the fact
^   hazard
G   the gate
```

A level is therefore three grids of the same shape. Writing it this way means
the shared floor is authored once and the differences are the interesting part.

## Physics

`moveBody(map, body, era, dt)` sweeps an axis-aligned box against the grid, one
axis at a time, sub-stepped so a fast body cannot pass through a thin wall.

One-axis-at-a-time is not the most general solution, but it is the one without
the classic platformer bugs: a body cannot be squeezed through a gap by
resolving both axes at once, and "did I land" is unambiguous because only the
vertical pass sets it.

One-way platforms are resolved against the whole vertical move rather than
inline, so a body rising through one is not caught by it, and the query returns
the platform's *height* rather than a boolean — snapping to "the row the body's
bottom is currently in" puts a body that has only just crossed a surface one
row too high.

The result reports `onGround` from geometry rather than from the previous frame,
probing a half-pixel lower. A body flush with a floor has to count as grounded,
or standing still loses the jump every time.

## What the era is allowed to change

| | Past | Present | Future |
| --- | --- | --- | --- |
| Terrain | crumbling stone | standing structure | crystal growth |
| Ground | common | common | common |
| Warden | present | — | — |
| Sentinel | present | present | present |
| Wisp | — | — | present |
| Seal | yields only here | yields only here | yields only here |

Everything else — the player's own health, position and momentum — is
continuous. An era shift is not a teleport and not a reset.

## What it deliberately does not do

- **It does not remember.** A choice made in an era does not persist as a
  consequence after the player leaves. The current model is: shifting changes
  what is true *now*, and nothing else. A real `ParadoxManager` would need
  rules for what happens to an enemy you befriended in an era you have left, and
  those rules are not designed yet. The screens show a paradox counter, which
  is score, not consequence.
- **It does not block movement through geometry that appears.** Shifting into a
  wall ejects you rather than stopping you, because `resolvePenetration` runs
  against the new world immediately after the era changes. The alternative —
  refusing the shift — is worse: it punishes the player for a correct read of
  the level.
- **It does not gate on energy mid-transition.** If a shift is affordable it
  happens. The transition is a visual dissolve over 0.35s; the world is already
  the new one.
