#!/usr/bin/env python3
"""Era Shift - generate the Ancient Forest level.

The level is authored here rather than hand-typed into JSON because it is
mostly rectangles: three overlapping grids of tiles plus a list of placements.
Writing it as a script means the geometry can be expressed in terms that are
easy to check ("a pit from column 16 to 22, floored with a Past-only bridge")
and the JSON is a build product rather than something edited by hand.

Usage:
    python3 tools/generate_level.py [--output data/levels/ancient_forest.json]

Tile characters (see Game/Level.hpp):
    .  empty
    #  solid in every era
    =  one-way platform, every era
    c  solid in the Past only    - ruins that have not rotted yet
    b  solid in the Present only - the structure still standing
    x  solid in the Future only  - matter grown after the fact
    ^  hazard
    G  the gate

Each era is written as a full grid, then the layers are combined with "later
era wins", so the shared floor is authored once in the Past grid.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

WIDTH = 200
HEIGHT = 30
TILE = 32

EMPTY = "."
SOLID = "#"
PLATFORM = "="
CRUMBLE = "c"   # Past only
BRIDGE = "b"    # Present only
CRYSTAL = "x"   # Future only
HAZARD = "^"
GATE = "G"

# The top of the main floor. Everything below is filled solid so the camera can
# never see past the bottom of the world.
#
# The height is chosen so that with the player standing on the floor the camera
# bottoms out exactly on the last row: any shorter and there is a strip of
# nothing under the dirt, any taller and the top of the level is never seen.
GROUND = 24
FILL_FROM = GROUND

# Rows above this are only ever seen in the crystal ascent, so placement
# scanning starts here rather than at row 0.
SURFACE_TOP = 6

PAST = "past"
PRESENT = "present"
FUTURE = "future"


class Grid:
    """A mutable character grid with rectangle helpers."""

    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self.rows = [[EMPTY] * width for _ in range(height)]

    def put(self, x: int, y: int, ch: str) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            self.rows[y][x] = ch

    def rect(self, x: int, y: int, w: int, h: int, ch: str) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.put(xx, yy, ch)

    def carve(self, x: int, y: int, w: int, h: int) -> None:
        self.rect(x, y, w, h, EMPTY)

    def ground(self) -> None:
        self.rect(0, FILL_FROM, self.width, self.height - FILL_FROM, SOLID)

    def side_walls(self, top: int) -> None:
        self.rect(0, top, 2, self.height - top, SOLID)
        self.rect(self.width - 2, top, 2, self.height - top, SOLID)

    def as_list(self) -> list[str]:
        return ["".join(row) for row in self.rows]


def build() -> tuple[dict[str, list[str]], list[dict]]:
    past = Grid(WIDTH, HEIGHT)
    present = Grid(WIDTH, HEIGHT)
    future = Grid(WIDTH, HEIGHT)

    # --- shared structure ---------------------------------------------------
    for grid in (past, present, future):
        grid.ground()
        grid.side_walls(top=8)

    # One-way ledges in two tiers. Every tier is exactly two tiles above the one
    # below it, which is the most a jump from a standing start clears
    # comfortably: the apex is a shade under three tiles, so a three-tile step
    # is possible but tight, and anything taller is not a jump at all.
    for grid in (past, present, future):
        for x in range(8, WIDTH - 10, 19):
            grid.rect(x, GROUND - 2, 6, 1, PLATFORM)
        for x in range(16, WIDTH - 12, 25):
            grid.rect(x, GROUND - 4, 5, 1, PLATFORM)

    # --- section 1: the Green Hollow ---------------------------------------
    # A chasm that only the Past still bridges. The floor is written into the
    # Past grid as crumbling stone and left empty in the other two.
    for grid in (present, future):
        grid.carve(17, GROUND, 6, HEIGHT - GROUND)
        grid.rect(17, GROUND, 6, 2, HAZARD)
    past.rect(17, GROUND, 6, 1, CRUMBLE)

    # A step up to the ledge holding the Past seal, so the first shift is
    # needed to reach the seal as well as to cross the chasm.
    for grid in (past, present, future):
        grid.rect(12, GROUND - 2, 3, 2, SOLID)

    # --- section 2: the Broken Span ----------------------------------------
    # The Present's standing structure bridges the second gap.
    for grid in (past, future):
        grid.carve(58, GROUND, 11, HEIGHT - GROUND)
        grid.rect(58, GROUND, 11, 2, HAZARD)
    present.rect(58, GROUND, 11, 1, BRIDGE)

    # A raised shelf the Past Warden paces along.
    for grid in (past, present, future):
        grid.rect(44, GROUND - 2, 8, 2, SOLID)

    # A third gap, floored only by Future crystal growth.
    for grid in (past, present):
        grid.carve(80, GROUND, 6, HEIGHT - GROUND)
        grid.rect(80, GROUND, 6, 2, HAZARD)
    future.rect(80, GROUND, 6, 1, CRYSTAL)

    # --- section 3: the Ascension ------------------------------------------
    # Three crystal shelves climbing away from the floor. Only the Future has
    # anything to stand on, so the climb is a shift as well as a jump.
    for grid in (past, present):
        grid.carve(100, GROUND, 5, HEIGHT - GROUND)
        grid.carve(107, GROUND, 5, HEIGHT - GROUND)
        grid.carve(114, GROUND, 5, HEIGHT - GROUND)
        grid.rect(100, GROUND, 5, 2, HAZARD)
        grid.rect(107, GROUND, 5, 2, HAZARD)
        grid.rect(114, GROUND, 5, 2, HAZARD)
    # A staircase of four shelves, each exactly two tiles above the last and
    # two columns across. Overlapping or steeper shelves leave a pocket walled
    # in crystal that the player cannot jump out of, which is exactly the kind
    # of bug a generated level is supposed to make impossible.
    future.rect(100, GROUND - 2, 5, 3, CRYSTAL)   # rises out of the ground
    future.rect(107, GROUND - 4, 5, 2, CRYSTAL)
    future.rect(113, GROUND - 6, 5, 2, CRYSTAL)
    future.rect(119, GROUND - 8, 6, 2, CRYSTAL)

    # --- section 4: the Gate Approach --------------------------------------
    # The gate stands between two plinths at the far end.
    past.put(191, GROUND - 1, GATE)
    present.put(191, GROUND - 1, GATE)
    future.put(191, GROUND - 1, GATE)

    # A final hazard strip before the gate so arriving is not automatic.
    for grid in (past, present, future):
        grid.rect(184, GROUND, 4, 1, HAZARD)

    rows = {PAST: past.as_list(), PRESENT: present.as_list(), FUTURE: future.as_list()}

    # --- placements ---------------------------------------------------------
    solid = (SOLID, BRIDGE, CRUMBLE, CRYSTAL)

    def surface_y(x: int) -> int | None:
        """The topmost surface at column `x`, or None if it is a pit.

        Scanned across all three grids at once. A placement only has to be
        reachable in *some* era - the player can shift anywhere - so anchoring
        to the union is what stops a chrono cell from being placed in mid-air
        because the era it belongs to is not the era the player starts in.
        """
        for y in range(SURFACE_TOP, HEIGHT):
            if any(rows[era][y][x] in solid for era in (PAST, PRESENT, FUTURE)):
                return y
        return None

    def placement_row(x: int, height: int) -> int:
        """The tile row a body of `height` px occupies when resting on column x.

        A tile is 32px but bodies are 28x44, so "standing on row S" does not mean
        "placed on row S" - it means placed a row and a bit lower, and it has to
        be computed or every entity sinks into the floor.
        """
        found = surface_y(x)
        if found is None:
            # A pit in every era. Callers avoid these, but a hard failure here
            # would be a silent misplacement rather than a visible one.
            raise SystemExit(f"column {x} has no floor in any era")
        return (found * TILE - height) // TILE

    # Heights, matching Game/Actor.hpp's body sizes.
    ACTOR_HEIGHT = 44      # player and ground enemies
    HOVER_HEIGHT = 30      # wisps
    PICKUP_HEIGHT = 20     # chrono cells and medkits

    def stand(x: int) -> int:
        return placement_row(x, ACTOR_HEIGHT)

    def hover(x: int) -> int:
        return placement_row(x, HOVER_HEIGHT)

    def pickup_row(x: int) -> int:
        return placement_row(x, PICKUP_HEIGHT)

    def seal_row(x: int) -> int:
        """A seal's y is the world position of its base, so it sits exactly on
        the surface rather than a third of a tile above it."""
        found = surface_y(x)
        if found is None:
            raise SystemExit(f"column {x} has no floor in any era")
        return found

    entities: list[dict] = [{"type": "player", "x": 4, "y": stand(4), "label": "Entrance"}]

    # Seals. One per era, each reachable only from that era's geometry or from
    # the floor, and each refused unless the world is in its era.
    entities.append({"type": "seal", "era": "Past", "x": 13, "y": seal_row(13),
                     "label": "PAST SEAL"})
    entities.append({"type": "seal", "era": "Present", "x": 70, "y": seal_row(70),
                     "label": "PRESENT SEAL"})
    entities.append({"type": "seal", "era": "Future", "x": 121, "y": seal_row(121),
                     "label": "FUTURE SEAL"})

    # Columns to avoid when auto-placing: the pits are one tile wider than they
    # look and an enemy standing in one is invisible and harmless.
    pit_columns = set(range(16, 24)) | set(range(57, 70)) | set(range(79, 87))

    enemies = [
        ("Sentinel", "All", 26, None),
        ("Sentinel", "All", 40, None),
        ("Warden", "Past", 46, None),
        ("Sentinel", "All", 72, None),
        ("Sentinel", "All", 90, None),
        ("Wisp", "Future", 101, None),
        ("Wisp", "Future", 115, None),
        ("Sentinel", "All", 130, None),
        ("Warden", "Past", 150, None),
        ("Sentinel", "All", 162, None),
        ("Sentinel", "All", 176, None),
        ("Wisp", "Future", 188, None),
    ]
    for _, _, x, _y in enemies:
        if x in pit_columns:
            raise SystemExit(f"enemy column {x} is over a pit")
    for kind, eras, x, y in enemies:
        if y is None:
            flying = kind == "Wisp"
            y = hover(x) if flying else stand(x)
        entities.append({"type": "enemy", "kind": kind, "eras": eras, "x": x, "y": y})

    # Chrono cells along the route, plus two health packs on the harder halves.
    chrono_at = (10, 20, 33, 52, 63, 74, 90, 102, 120, 133, 148, 170, 186)
    for x in chrono_at:
        if x in pit_columns:
            continue
        entities.append({"type": "pickup", "kind": "chrono", "x": x, "y": pickup_row(x)})
    for x in (37, 95, 137, 178):
        entities.append({"type": "pickup", "kind": "health", "x": x, "y": pickup_row(x)})

    return rows, entities


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",
                        default=os.path.join(os.path.dirname(__file__), os.pardir,
                                             "data", "levels", "ancient_forest.json"))
    args = parser.parse_args()

    rows, entities = build()

    # Sanity checks. A level that fails these would start and then be
    # unplayable, which is far worse than not being written at all.
    for name, grid in rows.items():
        for index, row in enumerate(grid):
            if len(row) != WIDTH:
                print(f"error: {name} row {index} is {len(row)} wide, expected {WIDTH}",
                      file=sys.stderr)
                return 1

    spawn = next(e for e in entities if e["type"] == "player")
    # A 28x44 body spans more than one 32px cell, so the check has to look at
    # both rows it covers rather than the one it is nominally placed on.
    for era_name in (PAST, PRESENT, FUTURE):
        for dy in (0, 1):
            row = rows[era_name][spawn["y"] + dy]
            if row[spawn["x"]] != EMPTY:
                print(f"error: the player spawn at ({spawn['x']}, {spawn['y']}) is inside a "
                      f"{era_name} tile", file=sys.stderr)
                return 1
        below = rows[era_name][spawn["y"] + 2]
        if below[spawn["x"]] == EMPTY:
            print(f"error: the player spawn at ({spawn['x']}, {spawn['y']}) has no floor "
                  f"under it in the {era_name}", file=sys.stderr)
            return 1

    seals = [e for e in entities if e["type"] == "seal"]
    if len(seals) != 3:
        print(f"error: expected 3 seals, got {len(seals)}", file=sys.stderr)
        return 1

    document = {
        "id": "ancient_forest",
        "name": "Ancient Forest",
        "past": rows[PAST],
        "present": rows[PRESENT],
        "future": rows[FUTURE],
        "entities": entities,
    }

    output = os.path.normpath(args.output)
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=2)
        handle.write("\n")

    print(f"wrote {output}: {WIDTH}x{HEIGHT} tiles, {len(entities)} entities")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
