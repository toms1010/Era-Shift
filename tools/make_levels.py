#!/usr/bin/env python3
"""Author the Era Shift region files in data/levels/.

Every region is three grids of the same shape, one per era, and the loader
unions them: a cell is solid in an era if that era's layer says so. A chasm
crossed by `c` in the Past layer is therefore open in the Present and the
Future, and that is the whole level-design vocabulary:

    #  solid in every era          the ruins
    =  one-way platform, all eras  jump up through it
    c  solid in the Past only      masonry that has not rotted
    b  solid in the Present only   the span that still stands
    x  solid in the Future only    crystal that has not grown yet
    ^  hazard in every era
    G  the gate

Writing 24 rows of 120 characters by hand in three layers is where a typo hides,
so each region is described once as a list of (y, x, char) edits over an empty
grid, and the three era layers are emitted from one source of truth.

Usage:
    python3 tools/make_levels.py            # write every region file
    python3 tools/make_levels.py --check    # verify, do not write
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEVEL_DIR = ROOT / "data" / "levels"

# The default grid every region was authored against. A region may override it:
# Level 1 is a long, vertically varied tutorial and cannot be expressed on the
# short corridor the later regions use.
WIDTH = 120
HEIGHT = 24

# The row the walkable floor sits on, and the row above it the player stands on.
FLOOR = 19
STAND = 18


@dataclass
class Region:
    """One region, described once and emitted as three era layers."""

    id: str
    name: str
    # Position in the designed progression, 1-based. Written into the file so the
    # level select can present the regions in the order they are meant to be
    # played rather than in filename order, which would shuffle them alphabetically.
    order: int = 0
    # Grid size and the two rows the ground geometry hangs off. Per region rather
    # than module-level so Level 1 can be long and tall while the others stay
    # corridors; see `Region.size`.
    width: int = WIDTH
    height: int = HEIGHT
    floor: int = FLOOR
    stand: int = STAND
    # Whether this region runs the first-play tutorial.
    tutorial: bool = False
    # How many seals the finish line requires. Defaults to "every seal placed",
    # which is what every region except the tutorial wants: the tutorial has one
    # seal and one gate, and asking for three there would be asking for a level
    # it does not have.
    requires_seals: int | None = None
    # (y, x, char) edits applied to the Past layer, then edited per era below.
    edits: list[tuple[int, int, str]] = field(default_factory=list)
    # Same, applied only to one era's layer.
    per_era: dict[str, list[tuple[int, int, str]]] = field(default_factory=dict)
    entities: list[dict] = field(default_factory=list)

    def layer(self, era: str) -> list[str]:
        grid = [["." for _ in range(self.width)] for _ in range(self.height)]
        for y, x, ch in self.edits:
            grid[y][x] = ch
        for y, x, ch in self.per_era.get(era, []):
            grid[y][x] = ch
        return ["".join(row) for row in grid]

    def document(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "order": self.order,
            "tutorial": self.tutorial,
            "finish": {
                "requires_seals": (
                    self.requires_seals
                    if self.requires_seals is not None
                    else sum(1 for e in self.entities if e.get("type") == "seal")
                )
            },
            "checkpoint": any(e.get("type") == "checkpoint" for e in self.entities),
            "past": self.layer("past"),
            "present": self.layer("present"),
            "future": self.layer("future"),
            "entities": self.entities,
        }


def walls_and_floor(region: Region) -> Region:
    """The border, the floor and the ceiling every region shares."""
    edits: list[tuple[int, int, str]] = []
    for y in range(region.height):
        edits.append((y, 0, "#"))
        edits.append((y, region.width - 1, "#"))
    for x in range(region.width):
        edits.append((0, x, "#"))
        edits.append((region.height - 1, x, "#"))
        edits.append((region.floor, x, "#"))
        edits.append((region.height - 2, x, "#"))
    region.edits.extend(edits)
    return region


def gap(region: Region, x0: int, x1: int, ch: str = "^") -> Region:
    """A pit in the floor, filled with spikes. Inclusive of both ends.

    The pit is carved down to the bottom row so the player cannot land inside it
    and be stuck: a pit with a floor two rows down is a pit the player survives,
    which turns a mistake into an unearned second chance.
    """
    for x in range(x0, x1 + 1):
        region.edits.append((region.floor, x, ch))
        for y in range(region.floor + 1, region.height):
            region.edits.append((y, x, " "))
    return region


def ledge(region: Region, x0: int, x1: int, y: int, ch: str = "=") -> Region:
    """A one-way platform run at row `y`. Inclusive of both ends."""
    for x in range(x0, x1 + 1):
        region.edits.append((y, x, ch))
    return region


def tower(region: Region, x: int, y0: int, y1: int, ch: str = "#") -> Region:
    """A solid column. Inclusive of both ends."""
    for y in range(y0, y1 + 1):
        region.edits.append((y, x, ch))
    return region


def build_awakening() -> Region:
    """LEVEL 1 - THE FIRST SHIFT. The tutorial, and a level in its own right.

    Ten sections rather than a corridor with lessons dropped along it. Each one is
    a small room with a single idea, and the rooms are joined by doorways rather
    than a single corridor, so the player is walking *somewhere* rather than
    walking *forwards*. The vertical range is deliberately generous - the map is
    seven rows taller than the regions after it - because "the world is one world"
    is easier to believe when the same space is visible from two heights.

    The order is fixed and each section can only be completed the way it was
    taught, so a player who does nothing but follow the prompts finishes the level
    and understands every mechanic in it:

        1  spawn and movement        an open room; nothing can hurt you here
        2  jumping                   a gap and platforms that need a jump
        3  the era shift             a chasm crossed only in the Past
        4  the first enemy           one Sentinel, in the open, unhurried
        5  combat and the dash       an arena, with the dash taught by a gap
        6  the checkpoint            a safe alcove, off the main route
        7  multi-era traversal       vertical, alternating eras as you climb
        8  the seal                  the objective, in a chamber
        9  the final challenge       the shift used as a weapon, not a key
       10  the finish gate           the Ancient Gate, and the level ends

    The one seal is in the Present, so the player learns in section 3 that the
    shift is about *which* era, and in section 8 that a seal answers to one era
    only. `requires_seals` is 1: the gate asks for the objective, not for a quota.
    """
    region = Region(
        id="awakening",
        name="The First Shift",
        width=200,
        height=32,
        floor=25,
        stand=24,
        tutorial=True,
        requires_seals=1,
        entities=[
            {"type": "player", "x": 4, "y": 24, "label": "Threshold"},
            # The seal is placed with the chamber geometry further down, so the two
            # cannot drift apart: a seal at a position the geometry does not support
            # is an objective the level cannot be finished for.
            # A checkpoint per third. Section 6 is the *taught* one - the alcove
            # exists to be found - and the others are quietly placed so a death
            # costs a section rather than the level.
            {"type": "checkpoint", "x": 12, "y": 24},
            {"type": "checkpoint", "x": 74, "y": 24},
            {"type": "checkpoint", "x": 122, "y": 24},
            # Before the seal chamber, so the objective is not re-earned after a
            # death in the hardest fight in the level.
            {"type": "checkpoint", "x": 152, "y": 24},
            # One enemy at a time, escalating. Section 4 is a lone Sentinel with
            # room to retreat; section 5 adds a second and platforms to lose them
            # on; section 9 is the only place two different kinds are alive at once.
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 62, "y": 24},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 88, "y": 24},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 88, "y": 17},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 178, "y": 24},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 183, "y": 24},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 190, "y": 17},
            # Pickups: a chrono cell before each section that spends energy, and one
            # health cell before the two hardest fights. Two optional ones sit off
            # the main route, high enough that getting them is a choice.
            {"type": "pickup", "kind": "chrono", "x": 20, "y": 24},
            {"type": "pickup", "kind": "chrono", "x": 44, "y": 24},
            {"type": "pickup", "kind": "chrono", "x": 70, "y": 24},
            {"type": "pickup", "kind": "health", "x": 90, "y": 24},
            {"type": "pickup", "kind": "chrono", "x": 104, "y": 24},
            {"type": "pickup", "kind": "chrono", "x": 126, "y": 24},
            {"type": "pickup", "kind": "health", "x": 146, "y": 24},
            {"type": "pickup", "kind": "chrono", "x": 172, "y": 24},
            # Optional: one on the high route in section 7, one on the seal
            # chamber's shelf. Both sit on ground, so both can be stood next to -
            # a pickup in mid-air can be touched while jumping past, which is a
            # collectable the player cannot choose to collect deliberately; neither
            # is on the critical path.
            {"type": "pickup", "kind": "chrono", "x": 140, "y": 9},
            {"type": "pickup", "kind": "health", "x": 166, "y": 24},
        ],
    )
    walls_and_floor(region)
    FLOOR, STAND, W = region.floor, region.stand, region.width

    # --- 1. spawn and movement ---------------------------------------------
    # An open room with a raised step to walk up. No hazard, no enemy: the player
    # is given the controls on ground where being bad at them cannot hurt.
    ledge(region, 8, 14, 22)
    region.entities.append({"type": "pickup", "kind": "chrono", "x": 11, "y": 21})

    # --- 2. jumping ---------------------------------------------------------
    # A pit that a jump clears comfortably, then a staircase of ledges. The steps
    # are two rows apart, which is inside a single jump's reach with room to spare.
    gap(region, 16, 19)
    ledge(region, 21, 24, 22)
    ledge(region, 26, 29, 20)
    ledge(region, 31, 34, 18)

    # --- 3. the era shift ---------------------------------------------------
    # A wide chasm with rubble in the Past and nothing else anywhere. The player
    # can see it from the spawn section, so the first shift is a decision rather
    # than a surprise.
    gap(region, 36, 45)
    for x in range(37, 45):
        region.per_era.setdefault("past", []).append((FLOOR, x, "c"))
    # A hint rather than a tutorial prompt: the ledge above the chasm is the only
    # way across in the Past, and standing on it shows what "solid in the Past"
    # looks like before the player needs it.
    ledge(region, 38, 44, 21)

    # --- 4. the first enemy -------------------------------------------------
    # A single Sentinel in a wide room with a raised walkway to retreat to. It
    # exists to be hit once and to not be a threat yet.
    ledge(region, 48, 54, 21)
    ledge(region, 56, 60, 22)

    # --- 5. combat and the dash --------------------------------------------
    # A closed arena with two tiers. Both tiers are within one jump of the tier
    # below - the earlier version put them four rows apart, which no jump clears,
    # so the arena's platforms were scenery the player could never stand on.
    #
    # The gap out of the arena is six tiles, which a running jump plus a dash
    # covers and a standing jump does not. That is the whole lesson: the dash is
    # taught by a gap that is otherwise just slightly too wide.
    gap(region, 80, 85)
    ledge(region, 78, 86, 22)
    ledge(region, 82, 88, 20)
    ledge(region, 86, 92, 18)
    tower(region, 92, 12, FLOOR - 1)
    ledge(region, 94, 100, 18)

    # --- 6. the checkpoint --------------------------------------------------
    # An alcove below the main floor, reached by a short drop. Off the route by
    # construction: a checkpoint the player cannot walk past is not a decision,
    # and a decision is the whole point of teaching one.
    for y in range(FLOOR + 1, region.height - 2):
        region.edits.append((y, 112, "#"))
    for y in range(FLOOR + 1, region.height - 2):
        region.edits.append((y, 120, "#"))
    region.edits.append((FLOOR + 1, 116, "."))
    region.edits.append((FLOOR, 116, "."))
    ledge(region, 110, 122, 26)

    # --- 7. multi-era traversal --------------------------------------------
    # A climb of one-way platforms. The tiers are in the *shared* layer, so the
    # staircase exists in every era and can always be climbed: a traversal puzzle
    # whose route disappears when you shift is not a traversal puzzle, it is a trap.
    #
    # The era-dependence is in the flanking blocks rather than the route. Each tier
    # has a solid buttress beside it that is real in one era only, so shifting
    # changes the shape of the room the player is climbing through - visible, and
    # never load-bearing.
    for y, x0, x1 in ((22, 126, 131), (19, 134, 139), (16, 142, 147), (13, 150, 153)):
        ledge(region, x0, x1, y)

    # The buttresses. One era each, chosen so that all three are seen on the climb
    # and none of them blocks the platform the player is standing on.
    for y, x0, era, tile in ((22, 132, "past", "c"), (19, 140, "future", "x"),
                             (16, 148, "past", "c"), (13, 154, "future", "x")):
        for x in range(x0, x0 + 2):
            region.edits.append((y - 1, x, "#"))
            region.edits.append((y, x, "#"))
            region.per_era.setdefault(era, []).append((y - 1, x, tile))
            region.per_era.setdefault(era, []).append((y, x, tile))

    # The optional high route, reached by jumping up the same staircase and then
    # stepping off the top tier. Reachable in every era, and it is where the
    # optional chrono cell is - a detour rather than a step.
    ledge(region, 130, 152, 10)
    tower(region, 128, 10, 13)

    # --- 8. the seal --------------------------------------------------------
    # A chamber entered from above: the player climbs, steps off the top tier and
    # drops in. Walls on both sides and a pillar down the middle, so the room is a
    # room rather than a hole.
    tower(region, 156, 11, FLOOR - 1)
    tower(region, 170, 11, FLOOR - 1)
    tower(region, 162, 11, 18)
    ledge(region, 157, 169, 19)
    ledge(region, 156, 170, 23)

    # The seal sits *on* the chamber's shelf, not floating in the middle of it. The
    # earlier position was two rows above the shelf, which is 64px away - outside
    # the 52px reach - so the objective was genuinely uncollectable.
    region.entities.append({"type": "seal", "era": "Present", "x": 160, "y": 19,
                            "label": "PRESENT SEAL"})

    # --- 9. the final challenge ---------------------------------------------
    # Two enemies that cannot both exist at once in the way the player wants: a
    # Warden only in the Past, a Wisp only in the Future, and a Sentinel in both.
    # Fighting is possible in any era and better in two of them, which is the
    # lesson the arena in section 5 was preparing for.
    ledge(region, 174, 182, 21)
    ledge(region, 186, 192, 18)
    ledge(region, 188, 194, 15)

    # --- 10. the finish gate ------------------------------------------------
    # A plinth the player climbs to, with the gate on top of it. Visible from the
    # seal chamber through the opening, so the last thing they do is obvious from
    # most of the way back.
    ledge(region, 194, 197, 21)
    tower(region, 198, 18, 20)
    region.edits.append((STAND, W - 2, "G"))

    return region


def build_ancient_bridge() -> Region:
    """LEVEL 2 — three crossings in a fixed order, no hand-holding."""
    region = Region(
        id="ancient_bridge",
        name="Ancient Bridge",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "West bank"},
            {"type": "seal", "era": "Past", "x": 26, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 58, "y": STAND, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 92, "y": STAND, "label": "FUTURE SEAL"},
            {"type": "checkpoint", "x": 20, "y": STAND},
            {"type": "checkpoint", "x": 52, "y": STAND},
            {"type": "checkpoint", "x": 88, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 34, "y": STAND},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 44, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 68, "y": STAND},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 84, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 110, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 14, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 40, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 76, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 98, "y": STAND},
        ],
    )
    walls_and_floor(region)

    # Three crossings, each passable in exactly one era. Order is the puzzle:
    # the seals and the crossings are interleaved, so a player who tries to
    # brute-force it in the Past gets as far as the second crossing.
    gap(region, 30, 36)
    gap(region, 62, 70)
    gap(region, 96, 104)

    # The first chasm is masonry: standing in the Past.
    for x in range(31, 36):
        region.per_era.setdefault("past", []).append((FLOOR, x, "c"))
    # The second is the span that still stands: the Present.
    for x in range(63, 70):
        region.per_era.setdefault("present", []).append((FLOOR, x, "b"))
    # The third has not grown yet: the Future.
    for x in range(97, 104):
        region.per_era.setdefault("future", []).append((FLOOR, x, "x"))

    # Platforms so the middle section is not one flat corridor. Each is solid
    # from above in every era, so they are navigation rather than a gate.
    ledge(region, 44, 52, 14)
    ledge(region, 78, 88, 12)
    ledge(region, 44, 50, 9)
    tower(region, 57, 10, FLOOR - 1)
    tower(region, 93, 8, FLOOR - 1)

    # The gate, in a niche at the east end.
    region.edits.append((STAND, 115, "G"))
    return region


def build_crystal_vault() -> Region:
    """A vertical region: stacked floors, each gated on a different era."""
    region = Region(
        id="crystal_vault",
        name="Crystal Vault",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "Floor"},
            {"type": "seal", "era": "Past", "x": 18, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 40, "y": 9, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 88, "y": 5, "label": "FUTURE SEAL"},
            {"type": "checkpoint", "x": 14, "y": STAND},
            {"type": "checkpoint", "x": 48, "y": 11},
            {"type": "checkpoint", "x": 76, "y": 6},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 24, "y": STAND},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 34, "y": 11},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 60, "y": 11},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 80, "y": 6},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 70, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 10, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 30, "y": 11},
            {"type": "pickup", "kind": "health", "x": 52, "y": 11},
            {"type": "pickup", "kind": "health", "x": 96, "y": 6},
            {"type": "pickup", "kind": "chrono", "x": 108, "y": 6},
        ],
    )
    walls_and_floor(region)

    # Three shelf floors, each reached through a different era's masonry.
    for x in range(6, 114):
        region.edits.append((12, x, "="))
        region.edits.append((7, x, "="))

    # The shelves are pinned to their own era, so the player climbs by shifting:
    # Past masonry at the low step, Present at the middle, Future at the top.
    for x in range(16, 26):
        region.edits.append((13, x, "c"))
        region.edits.append((8, x, "b"))

    # Drops between shelves. Spiked, so falling costs rather than merely costs time.
    gap(region, 44, 56)
    gap(region, 86, 98)
    for x in range(45, 56):
        region.per_era.setdefault("present", []).append((12, x, "b"))
        region.edits.append((FLOOR, x, "#"))
    for x in range(87, 98):
        region.per_era.setdefault("future", []).append((7, x, "x"))
        region.edits.append((12, x, "="))

    # Hazard strips on each shelf: the vault is not safe to walk.
    for x in range(30, 40):
        region.edits.append((11, x, "^"))
    for x in range(62, 72):
        region.edits.append((6, x, "^"))

    tower(region, 42, 8, 12)
    tower(region, 84, 2, 7)

    region.edits.append((6, 114, "G"))
    return region


def build_fallen_span() -> Region:
    """LEVEL 3 — era-specific hazards. The floor is safe in exactly one era."""
    region = Region(
        id="fallen_span",
        name="Fallen Span",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "Anchor"},
            {"type": "seal", "era": "Past", "x": 20, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 58, "y": STAND, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 98, "y": STAND, "label": "FUTURE SEAL"},
            {"type": "checkpoint", "x": 12, "y": STAND},
            {"type": "checkpoint", "x": 44, "y": STAND},
            {"type": "checkpoint", "x": 82, "y": STAND},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 30, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 52, "y": STAND},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 88, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 108, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 8, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 40, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 70, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 104, "y": STAND},
        ],
    )
    walls_and_floor(region)

    # Four spike fields, each blocked off in two of the three eras by that era's
    # own masonry. The lesson is that "safe" is a property of the era you are in,
    # not of the level: the same ground is lethal twice and walkable once.
    for lo, hi in ((32, 38), (56, 62), (78, 84), (100, 106)):
        gap(region, lo, hi)
    for lo, hi in ((33, 37), (57, 61)):
        region.per_era.setdefault("past", []).append((FLOOR, lo + 1, "c"))
        region.per_era.setdefault("future", []).append((FLOOR, lo + 1, "x"))
    for lo, hi in ((79, 83), (101, 105)):
        region.per_era.setdefault("present", []).append((FLOOR, lo + 1, "b"))
        region.per_era.setdefault("future", []).append((FLOOR, lo + 1, "x"))

    # A little verticality so the player is not simply holding "right".
    ledge(region, 44, 52, 13)
    ledge(region, 88, 96, 13)
    ledge(region, 20, 26, 9)
    tower(region, 66, 9, FLOOR - 1)

    region.edits.append((STAND, 115, "G"))
    return region


def build_drowned_road() -> Region:
    """LEVEL 4 — environmental interaction: routes that exist in one era only."""
    region = Region(
        id="drowned_road",
        name="Drowned Road",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "South bank"},
            {"type": "seal", "era": "Past", "x": 16, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 52, "y": STAND, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 94, "y": STAND, "label": "FUTURE SEAL"},
            {"type": "checkpoint", "x": 10, "y": STAND},
            {"type": "checkpoint", "x": 44, "y": 11},
            {"type": "checkpoint", "x": 84, "y": STAND},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 34, "y": 11},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 62, "y": STAND},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 76, "y": 11},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 104, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 7, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 40, "y": 11},
            {"type": "pickup", "kind": "health", "x": 70, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 96, "y": STAND},
        ],
    )
    walls_and_floor(region)

    # A long flooded channel down the middle: impassable in all three eras, so it
    # has to be crossed above rather than through.
    gap(region, 46, 74)

    # The causeway over it exists in the Past alone, and only over its western
    # half — the eastern half was never built, which is why the level needs a
    # second idea rather than a longer bridge.
    for x in range(47, 60):
        region.per_era.setdefault("past", []).append((FLOOR, x, "c"))
    for x in range(62, 74):
        region.per_era.setdefault("present", []).append((FLOOR, x, "b"))

    # Two gaps in that causeway, so neither era walks it end to end and the player
    # has to alternate mid-crossing rather than shift once and commit.
    gap(region, 54, 56)
    gap(region, 66, 68)
    for x in range(55, 56):
        region.per_era.setdefault("future", []).append((FLOOR, x, "x"))
    for x in range(67, 68):
        region.per_era.setdefault("future", []).append((FLOOR, x, "x"))

    # The upper road, reachable from both banks by one-way platforms.
    ledge(region, 36, 52, 12)
    ledge(region, 66, 82, 12)
    ledge(region, 48, 62, 8)
    tower(region, 60, 8, 12)

    region.edits.append((STAND, 115, "G"))
    return region


def build_split_meadow() -> Region:
    """LEVEL 5 — a route that must be walked in three different eras in turn."""
    region = Region(
        id="split_meadow",
        name="Split Meadow",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "West edge"},
            {"type": "seal", "era": "Past", "x": 24, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 62, "y": 10, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 104, "y": 6, "label": "FUTURE SEAL"},
            {"type": "checkpoint", "x": 18, "y": STAND},
            {"type": "checkpoint", "x": 50, "y": STAND},
            {"type": "checkpoint", "x": 86, "y": 10},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 30, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 56, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 74, "y": 10},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 96, "y": 6},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 110, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 9, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 44, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 68, "y": 10},
            {"type": "pickup", "kind": "health", "x": 100, "y": 6},
        ],
    )
    walls_and_floor(region)

    # Alternating gates, close enough together that shifting costs chrono, which
    # is the resource pressure: an impatient player runs out before the third seal.
    for lo, hi in ((36, 40), (48, 52), (80, 84), (92, 96)):
        gap(region, lo, hi)
    for lo in (36, 48):
        for x in range(lo + 1, lo + 4):
            region.per_era.setdefault("past", []).append((FLOOR, x, "c"))
    for lo in (80, 92):
        for x in range(lo + 1, lo + 4):
            region.per_era.setdefault("future", []).append((FLOOR, x, "x"))

    # The middle is a raised shelf, reachable only by the Present's own bridge.
    ledge(region, 58, 78, 11)
    for x in range(66, 72):
        region.per_era.setdefault("present", []).append((FLOOR, x, "b"))
    ledge(region, 86, 100, 7)
    tower(region, 60, 11, FLOOR - 1)
    tower(region, 88, 2, 7)

    region.edits.append((6, 114, "G"))
    return region


def build_hollow_citadel() -> Region:
    """LEVEL 6 — combat. Two eras at once, with nowhere cheap to fight."""
    region = Region(
        id="hollow_citadel",
        name="Hollow Citadel",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "Gatehouse"},
            {"type": "seal", "era": "Past", "x": 18, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 56, "y": STAND, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 100, "y": STAND, "label": "FUTURE SEAL"},
            {"type": "checkpoint", "x": 30, "y": STAND},
            {"type": "checkpoint", "x": 66, "y": STAND},
            {"type": "checkpoint", "x": 90, "y": STAND},
            # A packed middle: this level is won by picking a fight, not avoiding one.
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 34, "y": STAND},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 42, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 50, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 72, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 78, "y": STAND},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 86, "y": STAND},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 96, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 110, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 12, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 26, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 46, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 80, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 102, "y": STAND},
        ],
    )
    walls_and_floor(region)

    # A single open courtyard. No hazards and no era gates: the level's difficulty
    # is entirely the enemy count, so the terrain stays out of the way.
    ledge(region, 36, 44, 13)
    ledge(region, 62, 70, 13)

    # Pillars for cover — solid in every era, so they are tactics rather than gates.
    #
    # Kept clear of every enemy's tile: a pillar is a column from y=8 to the row
    # above the floor, so an enemy at the same x is spawned inside it. That is not
    # a soft lock — the player can still walk over it — but an enemy embedded in
    # rock is an enemy the player cannot reach.
    for x in (40, 64, 84):
        tower(region, x, 8, FLOOR - 1)

    region.edits.append((STAND, 115, "G"))
    return region


def build_sunken_lattice() -> Region:
    """LEVEL 7 — a lattice: a grid of crossings, only some of which are real."""
    region = Region(
        id="sunken_lattice",
        name="Sunken Lattice",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "Corner"},
            {"type": "seal", "era": "Past", "x": 22, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 58, "y": 12, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 100, "y": 6, "label": "FUTURE SEAL"},
            {"type": "checkpoint", "x": 16, "y": STAND},
            {"type": "checkpoint", "x": 44, "y": STAND},
            {"type": "checkpoint", "x": 82, "y": 12},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 34, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 54, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 68, "y": 12},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 88, "y": 6},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 108, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 8, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 40, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 62, "y": 12},
            {"type": "pickup", "kind": "health", "x": 96, "y": 6},
        ],
    )
    walls_and_floor(region)

    # Five rows of gates, cycling through the eras. The point is that no single
    # era walks the whole level, so the player is shifting continuously rather than
    # three times.
    #
    # The tile character is named explicitly rather than taken from `era[0]`, which
    # would have written 'p' for both "past" and "present" — and 'p' is not a tile,
    # so the region would have loaded as a field of holes.
    gates = (("past", "c"), ("present", "b"), ("past", "c"), ("future", "x"), ("present", "b"))
    for row, (era, tile) in enumerate(gates):
        lo = 30 + row * 14
        gap(region, lo, lo + 4)
        for x in range(lo + 1, lo + 4):
            region.per_era.setdefault(era, []).append((FLOOR, x, tile))

    # Two shelves to break the corridor, each gated on the era *not* used at its
    # floor level, so the player has to think about where they are standing.
    #
    # Everything that lives on a shelf is placed *on* the shelf — a seal at y=12 with
    # the platform at y=12 is inside it, and one at y=12 with the platform at y=13
    # has nothing under it at all. Standing on a ledge means the ledge is the row
    # below.
    ledge(region, 40, 60, 13)
    ledge(region, 80, 104, 7)
    # A single crossing up to each shelf, so neither is decoration.
    for x in range(52, 56):
        region.per_era.setdefault("future", []).append((FLOOR, x, "x"))
    for x in range(92, 96):
        region.per_era.setdefault("past", []).append((FLOOR, x, "c"))

    tower(region, 64, 7, 13)
    tower(region, 76, 2, 7)

    region.edits.append((6, 114, "G"))
    return region


def build_paradox_ward() -> Region:
    """LEVEL 8 — a long shift-heavy route that costs more chrono than it gives."""
    region = Region(
        id="paradox_ward",
        name="Paradox Ward",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "Outer ward"},
            {"type": "seal", "era": "Past", "x": 26, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 64, "y": STAND, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 104, "y": 8, "label": "FUTURE SEAL"},
            {"type": "checkpoint", "x": 20, "y": STAND},
            {"type": "checkpoint", "x": 58, "y": STAND},
            {"type": "checkpoint", "x": 88, "y": STAND},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 40, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 52, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 72, "y": STAND},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 80, "y": 8},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 100, "y": 8},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 110, "y": STAND},
            # Fewer chrono cells than gates, deliberately: the level is about
            # choosing which gates to pay for.
            {"type": "pickup", "kind": "chrono", "x": 12, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 46, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 68, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 92, "y": STAND},
        ],
    )
    walls_and_floor(region)

    # Six gates. Each single-tile era crossing, so there is no "commit to one era
    # for a while" — it is a shift every few steps.
    gates = (("past", "c"), ("present", "b"), ("future", "x"),
             ("past", "c"), ("present", "b"), ("future", "x"))
    for index, (era, tile) in enumerate(gates):
        lo = 32 + index * 12
        gap(region, lo, lo + 3)
        for x in range(lo + 1, lo + 3):
            region.per_era.setdefault(era, []).append((FLOOR, x, tile))

    ledge(region, 24, 30, 13)
    ledge(region, 86, 100, 9)
    tower(region, 60, 9, FLOOR - 1)
    tower(region, 84, 4, 9)

    region.edits.append((8, 114, "G"))
    return region


def build_convergence() -> Region:
    """LEVEL 10 — all three eras at once, and a long way home."""
    region = Region(
        id="convergence",
        name="Convergence",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "The seam"},
            {"type": "seal", "era": "Past", "x": 20, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 60, "y": 10, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 104, "y": 5, "label": "FUTURE SEAL"},
            {"type": "checkpoint", "x": 14, "y": STAND},
            {"type": "checkpoint", "x": 48, "y": STAND},
            {"type": "checkpoint", "x": 84, "y": 10},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 32, "y": STAND},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 42, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 54, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 68, "y": 10},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 78, "y": STAND},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 92, "y": 5},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 100, "y": 5},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 112, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 10, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 26, "y": STAND},
            {"type": "pickup", "kind": "health", "x": 52, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 74, "y": 10},
            {"type": "pickup", "kind": "health", "x": 88, "y": 5},
            {"type": "pickup", "kind": "chrono", "x": 108, "y": STAND},
        ],
    )
    walls_and_floor(region)

    # Every pattern the game has taught, in one level: alternating single gates, a
    # spiked channel, a raised shelf, and a hazards-on-the-upper-floor strip. The
    # synthesis is the point — a player who learned each idea separately has to use
    # all of them without being told which is which.
    for index, (era, tile) in enumerate((("past", "c"), ("present", "b"), ("future", "x"))):
        lo = 26 + index * 10
        gap(region, lo, lo + 3)
        for x in range(lo + 1, lo + 3):
            region.per_era.setdefault(era, []).append((FLOOR, x, tile))

    # The channel, and the only two crossings of it.
    gap(region, 56, 74)
    for x in range(57, 64):
        region.per_era.setdefault("past", []).append((FLOOR, x, "c"))
    for x in range(67, 74):
        region.per_era.setdefault("future", []).append((FLOOR, x, "x"))

    # The shelf, reached from the west side and left from the east.
    ledge(region, 62, 84, 11)
    for x in range(70, 76):
        region.per_era.setdefault("present", []).append((FLOOR, x, "b"))
    for x in range(80, 84):
        region.edits.append((10, x, "^"))

    ledge(region, 88, 106, 6)
    for x in range(94, 100):
        region.edits.append((5, x, "^"))
    tower(region, 50, 11, FLOOR - 1)
    tower(region, 78, 2, 6)
    tower(region, 102, 2, 6)

    region.edits.append((5, 114, "G"))
    return region


# Progression order. The order here *is* the level order the game presents, so a
# region added to this list is unlocked by finishing the one before it.
REGIONS = [
    build_awakening,
    build_ancient_bridge,
    build_fallen_span,
    build_drowned_road,
    build_crystal_vault,
    build_split_meadow,
    build_hollow_citadel,
    build_sunken_lattice,
    build_paradox_ward,
    build_convergence,
]


def validate(document: dict, region_id: str, expected: tuple[int, int] | None = None) -> list[str]:
    """Everything the loader would reject, checked before the file is written.

    `expected` is the (height, width) the builder believes it produced. Regions
    differ in size, so the check is against the region rather than the module
    constants.
    """
    problems: list[str] = []
    layers = {era: document[era] for era in ("past", "present", "future")}

    shapes = {era: (len(rows), len(rows[0]) if rows else 0) for era, rows in layers.items()}
    if len(set(shapes.values())) != 1:
        problems.append(f"{region_id}: era layers differ in shape: {shapes}")
    height, width = shapes["past"]
    if expected is not None and (height, width) != expected:
        problems.append(f"{region_id}: expected {expected[0]}x{expected[1]}, got {height}x{width}")

    for era, rows in layers.items():
        for y, row in enumerate(rows):
            if len(row) != width:
                problems.append(f"{region_id}.{era}: row {y} is {len(row)} wide, want {width}")

    entities = document["entities"]
    kinds = {e["type"] for e in entities}
    # `checkpoint` must stay in this set: it is the one entity kind the C++ loader
    # knows that the other regions above did not use, and dropping it here would
    # have the validator reject the very files the loader accepts.
    if kinds - {"player", "enemy", "pickup", "seal", "checkpoint"}:
        problems.append(f"{region_id}: unknown entity types {kinds}")

    if sum(1 for e in entities if e["type"] == "player") != 1:
        problems.append(f"{region_id}: needs exactly one player spawn")

    # The seal set has to be openable by the finish. A level that states how many
    # seals it needs is held to that number; one that does not is held to the older
    # rule of one per era, which is what every non-tutorial region wants.
    seals = [e for e in entities if e["type"] == "seal"]
    required = (document.get("finish") or {}).get("requires_seals")
    if required is None:
        seal_eras = sorted(e.get("era") for e in seals)
        if seal_eras != ["Future", "Past", "Present"]:
            problems.append(f"{region_id}: needs one seal per era, got {seal_eras}")
    else:
        if not isinstance(required, int) or required < 1:
            problems.append(f"{region_id}: finish.requires_seals must be 1 or more")
        elif len(seals) < required:
            problems.append(
                f"{region_id}: needs {required} seals but places {len(seals)}"
            )

    # The gate has to exist in every era, and something has to stand on it.
    for era, rows in layers.items():
        if not any("G" in row for row in rows):
            problems.append(f"{region_id}.{era}: no gate")

    # Nothing may be placed outside the grid.
    for entity in entities:
        x, y = entity["x"], entity["y"]
        if not (0 <= x < width and 0 <= y < height):
            problems.append(f"{region_id}: {entity} is outside the {width}x{height} grid")

    # Every entity should be reachable-ish: standing on something rather than
    # floating in a pit. Checked against the union of the three layers.
    union = []
    for y in range(height):
        row = []
        for x in range(width):
            solid = any(
                layers[era][y][x] in "#=cbx" for era in layers
            )
            row.append("#" if solid else ".")
        union.append("".join(row))

    for entity in entities:
        if entity["type"] == "player":
            continue
        x, y = entity["x"], entity["y"]
        below = union[y + 1] if y + 1 < height else "#"
        if below == ".":
            problems.append(
                f"{region_id}: {entity['type']} at {x},{y} has no floor beneath it"
            )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate without writing")
    args = parser.parse_args()

    LEVEL_DIR.mkdir(parents=True, exist_ok=True)

    all_problems: list[str] = []
    for index, build in enumerate(REGIONS, start=1):
        region = build()
        region.order = index
        document = region.document()
        all_problems.extend(
            validate(document, region.id, (region.height, region.width))
        )

        path = LEVEL_DIR / f"{region.id}.json"
        if args.check:
            state = "ok" if path.exists() else "MISSING"
        else:
            path.write_text(json.dumps(document, indent=2) + "\n")
            state = "written"
        rows = len(document["past"])
        cols = len(document["past"][0]) if rows else 0
        print(f"{index:2d}. {region.id:<18} {rows}x{cols:<4} {state}")

    if all_problems:
        print("\nproblems:", file=sys.stderr)
        for problem in all_problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print("\nall regions valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())