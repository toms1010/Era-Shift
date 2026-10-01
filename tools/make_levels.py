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
    # (y, x, char) edits applied to the Past layer, then edited per era below.
    edits: list[tuple[int, int, str]] = field(default_factory=list)
    # Same, applied only to one era's layer.
    per_era: dict[str, list[tuple[int, int, str]]] = field(default_factory=dict)
    entities: list[dict] = field(default_factory=list)

    def layer(self, era: str) -> list[str]:
        grid = [["." for _ in range(WIDTH)] for _ in range(HEIGHT)]
        for y, x, ch in self.edits:
            grid[y][x] = ch
        for y, x, ch in self.per_era.get(era, []):
            grid[y][x] = ch
        return ["".join(row) for row in grid]

    def document(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "past": self.layer("past"),
            "present": self.layer("present"),
            "future": self.layer("future"),
            "entities": self.entities,
        }


def walls_and_floor(region: Region) -> Region:
    """The border, the floor and the ceiling every region shares."""
    edits: list[tuple[int, int, str]] = []
    for y in range(HEIGHT):
        edits.append((y, 0, "#"))
        edits.append((y, WIDTH - 1, "#"))
    for x in range(WIDTH):
        edits.append((0, x, "#"))
        edits.append((HEIGHT - 1, x, "#"))
        edits.append((FLOOR, x, "#"))
        edits.append((HEIGHT - 2, x, "#"))
    region.edits.extend(edits)
    return region


def gap(region: Region, x0: int, x1: int, ch: str = "^") -> Region:
    """A pit in the floor, filled with spikes. Inclusive of both ends."""
    for x in range(x0, x1 + 1):
        region.edits.append((FLOOR, x, ch))
        region.edits.append((HEIGHT - 2, x, " "))
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


def build_ancient_bridge() -> Region:
    """A short, teaching region: three crossings, one per era."""
    region = Region(
        id="ancient_bridge",
        name="Ancient Bridge",
        entities=[
            {"type": "player", "x": 3, "y": STAND, "label": "West bank"},
            {"type": "seal", "era": "Past", "x": 26, "y": STAND, "label": "PAST SEAL"},
            {"type": "seal", "era": "Present", "x": 58, "y": STAND, "label": "PRESENT SEAL"},
            {"type": "seal", "era": "Future", "x": 92, "y": STAND, "label": "FUTURE SEAL"},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 34, "y": STAND},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 44, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 68, "y": STAND},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 84, "y": STAND},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 104, "y": STAND},
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
            {"type": "seal", "era": "Future", "x": 88, "y": 4, "label": "FUTURE SEAL"},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 24, "y": STAND},
            {"type": "enemy", "kind": "Warden", "eras": "Past", "x": 34, "y": 12},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 60, "y": 12},
            {"type": "enemy", "kind": "Wisp", "eras": "Future", "x": 80, "y": 7},
            {"type": "enemy", "kind": "Sentinel", "eras": "All", "x": 70, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 10, "y": STAND},
            {"type": "pickup", "kind": "chrono", "x": 30, "y": 12},
            {"type": "pickup", "kind": "health", "x": 52, "y": 12},
            {"type": "pickup", "kind": "health", "x": 96, "y": 7},
            {"type": "pickup", "kind": "chrono", "x": 108, "y": 7},
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


REGIONS = [
    build_ancient_bridge,
    build_crystal_vault,
]


def validate(document: dict, region_id: str) -> list[str]:
    """Everything the loader would reject, checked before the file is written."""
    problems: list[str] = []
    layers = {era: document[era] for era in ("past", "present", "future")}

    shapes = {era: (len(rows), len(rows[0]) if rows else 0) for era, rows in layers.items()}
    if len(set(shapes.values())) != 1:
        problems.append(f"{region_id}: era layers differ in shape: {shapes}")
    height, width = shapes["past"]
    if (height, width) != (HEIGHT, WIDTH):
        problems.append(f"{region_id}: expected {HEIGHT}x{WIDTH}, got {height}x{width}")

    for era, rows in layers.items():
        for y, row in enumerate(rows):
            if len(row) != width:
                problems.append(f"{region_id}.{era}: row {y} is {len(row)} wide, want {width}")

    entities = document["entities"]
    kinds = {e["type"] for e in entities}
    if kinds - {"player", "enemy", "pickup", "seal"}:
        problems.append(f"{region_id}: unknown entity types {kinds}")

    if sum(1 for e in entities if e["type"] == "player") != 1:
        problems.append(f"{region_id}: needs exactly one player spawn")

    # The three seals must be one per era, or the gate can never open.
    seal_eras = sorted(e.get("era") for e in entities if e["type"] == "seal")
    if seal_eras != ["Future", "Past", "Present"]:
        problems.append(f"{region_id}: needs one seal per era, got {seal_eras}")

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
    for build in REGIONS:
        region = build()
        document = region.document()
        all_problems.extend(validate(document, region.id))

        path = LEVEL_DIR / f"{region.id}.json"
        if args.check:
            state = "ok" if path.exists() else "MISSING"
        else:
            path.write_text(json.dumps(document, indent=2) + "\n")
            state = "written"
        rows = len(document["past"])
        print(f"{region.id:<18} {rows}x{WIDTH:<4} {state}")

    if all_problems:
        print("\nproblems:", file=sys.stderr)
        for problem in all_problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print("\nall regions valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())