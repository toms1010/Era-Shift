# Era Shift — game design

What the game is, as implemented. This document describes the code that exists;
where the code and the design disagree, the code is the bug.

---

## The premise

One physical space, observed from three points in time. The player is the only
thing present in all three. Everything else — the ground, the bridges, the
enemies, the objectives — is in one era or another, and which era you are in
decides what you can walk on, what you can fight, and what you can take.

The consequence is the whole design: **an obstacle is never removed, it is
erased from one era and preserved in another.** The player does not find a key
for a locked door. They find the same door, open, in a different century.

## The core loop

```
   read the terrain  ->  it does not go where you are going  ->  shift era
        ^                                                          |
        |                                                          v
   take what this era holds  <-  fight what this era has  <-  cross
```

Concretely, in the Ancient Forest:

1. Walk right. The floor stops.
2. The Past has a crumbling stone bridge there. Shift.
3. Cross, and take the Past seal on the way — it only yields in the Past.
4. The next gap has no bridge in the Past. Shift again.
5. Repeat until all three seals are held.
6. The Ancient Gate opens. Reach it.

Each shift costs chrono energy, which drains and regenerates, so the loop is
paced by a resource rather than by a timer. The player is never stranded: with
no energy they simply wait.

## Resources

| Resource | Source | Sink | What it is for |
| --- | --- | --- | --- |
| **Health** | Health packs, rest | Enemies, hazards, pits | Five pips. Damage grants ~0.9s of invulnerability and knockback. |
| **Chrono energy** | Regenerates 9/s, chrono cells (+30) | 25 per shift | The shift itself. Below 25 the HUD turns warm: the key is about to stop working. |
| **Paradox** | Every shift, every seal, every kill | — | Score. Nothing is bought with it. |

Health is deliberately small. Five pips against a level with three enemy kinds
means the player is expected to avoid rather than absorb, and to use the dash
as a defensive option.

## Enemies

An enemy has an era mask. It is either *present* or *frozen*, and those are
very different things:

- A **Sentinel** exists in every era. Patrols, turns at ledges, chases on sight,
  telegraphs with a wind-up, lunges, and has a long recovery you can punish.
- A **Warden** exists only in the Past. Slow, heavy, twice the health, twice the
  contact damage, and a wind-up long enough to walk away from. Shifting out of
  the Past is the clean answer to a Warden — which is the point of it existing
  only there.
- A **Wisp** exists only in the Future. Hovers, ignores gravity, harasses from
  range. Fragile, but it is never where you expect it because it is only there
  when you have chosen to be.

The rule that makes them mechanics rather than filters: **a frozen enemy is
where it was left, with the health it had.** Coming back to its era resumes the
fight rather than restarting it. And a swing cannot touch an enemy that is not
in the world, so the era is a defensive tool as well as a traversal one.

Enemy AI is a small state machine — idle, patrol, chase, wind-up, attack,
recover — driven by timers rather than re-decided, so the attack window is exact
regardless of how often the enemy is allowed to think. Thinking is staggered:
an enemy re-evaluates every 2 or 3 frames rather than every frame, which is
what keeps a dozen enemies affordable.

## Combat

One attack, three phases:

```
wind-up 0.06s  ->  active 0.10s  ->  recover 0.16s
```

The hit box exists only during the active window, sits in front of the player,
and flips with facing. A single swing hits at most one enemy — a swing that
stayed active across frames would otherwise shred an enemy it was merely resting
against.

The design intent is that combat is a positioning problem. The player out-ranges
the Sentinel, is out-reached by the Warden, and cannot reach a Wisp at all
without chasing it. Attacking is committing.

## Failure

Losing all health, or falling out of the world, ends the run. Falling is fatal
in every era and is how a mistimed shift is punished: the ground you were
standing on is not there in the era you shifted into, and there may be nothing
below it.

The finished world stays on screen for 1.4 seconds before the results panel
replaces it. Cutting straight to a panel takes away the last frame of whatever
killed the player.

## Victory

The gate is a marker in the tile grid. It is only a gate once all three seals
are held. Standing on it before then produces an explicit "the gate is sealed"
message rather than silence, because silence reads as a bug.

---

## What this document does not cover

- **The timeline system.** The era cycle is complete, but the persistence rules
  a full timeline needs — what happens to a choice made in an era you later
  leave — are not designed. The current model is deliberately conservative:
  shifting changes what is true *now* and nothing else.
- **Audio detail.** Audio is implemented and playable — see `AUDIO.md` for the
  streaming architecture, buses and per-event mapping. What is *not* designed
  here is a music/variation authoring pass: the tracks are synthesised from
  fixed code rather than composed per region, so every playthrough of Ancient
  Forest hears the same progression.
- **Content beyond one region.** The level format supports as many regions as
  anyone wants to write; there is one.
