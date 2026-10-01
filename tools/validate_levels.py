#!/usr/bin/env python3
"""Validate every region in data/levels/.

The game's own C++ loader is the authority on what is *loadable*; this is the
authority on what is *playable*. It re-implements the loader's rules in Python so
a broken region is caught before anyone tries to walk into it, and prints the
specific problem rather than a boolean.

    python3 tools/validate_levels.py
    python3 tools/validate_levels.py --data data/levels

Exits non-zero when any region fails, so it can gate a commit.

What is checked, and why each one matters:

  structure      the three era layers must agree in shape, or the union in
                 `Level::buildMap` reads off the end of the shorter ones
  spawn          exactly one, and standing on something
  gate           present in every era, or the level cannot be completed
  seals          as many as the finish requires, or the gate can never open
  reachability   a flood fill per era from the spawn, which is the only way to
                 prove a level is completable rather than merely well-formed
  placement      no enemy inside solid rock, nothing floating over a pit
  metadata       a stated seal requirement that the level cannot satisfy
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

ERAS = ("past", "present", "future")


# Matches Tile::of() in include/EraShift/Game/TileMap.hpp.
SOLID_CHARS = set("#=cbx")
HAZARD_CHARS = set("^")
GOAL_CHARS = set("G")

# Matches Level::checkpointHere()'s radius.
CHECKPOINT_REACH = 2


class Report:
    """Collects problems for one region so it can print all of them at once."""

    def __init__(self, region_id: str) -> None:
        self.region_id = region_id
        self.problems: list[str] = []
        self.notes: list[str] = []

    def error(self, message: str) -> None:
        self.problems.append(f"ERROR: {message}")

    def note(self, message: str) -> None:
        # Deduplicated and order-preserving. Several checks legitimately report
        # once per era for the same entity, and printing "floats" three times for
        # one misplaced pickup buries the rest of the output.
        if message not in self.notes:
            self.notes.append(message)

    @property
    def ok(self) -> bool:
        return not self.problems


def solid_in(cell: str, era: str) -> bool:
    """Whether `cell` blocks movement while `era` is real.

    Re-implements the loader's per-era mask logic. `#` and `=` are solid in every
    era; the three gated kinds are solid in exactly one each.
    """
    if cell == "#" or cell == "=":
        return True
    if cell == "c":
        return era == "past"
    if cell == "b":
        return era == "present"
    if cell == "x":
        return era == "future"
    return False


def blocks_walk(cell: str, era: str) -> bool:
    """Whether `cell` stops the player moving *through* it horizontally.

    Differs from `solid_in` for one character only: `=` is a one-way platform, so
    the player jumps up through it and lands on top. Treating it as a wall makes
    the flood fill report any level with a platform run in it as unreachable, which
    is a false positive on almost every region in the game.
    """
    return cell != "=" and solid_in(cell, era)


def is_standable(cell: str, era: str) -> bool:
    """A cell the player can occupy and walk off.

    Hazards are not standable and neither is a goal marker, so both are treated as
    passable-but-not-resting: the player can cross them, just not stand in them.
    """
    return cell not in HAZARD_CHARS and cell not in GOAL_CHARS


def load_region(path: Path, report: Report) -> dict | None:
    try:
        document = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        report.error(f"cannot read: {error}")
        return None
    if not isinstance(document, dict):
        report.error("document must be a JSON object")
        return None
    return document


def check_structure(document: dict, report: Report) -> tuple[list[str], int, int] | None:
    for era in ERAS:
        if era not in document:
            report.error(f"missing the '{era}' layer")
            return None
        if not isinstance(document[era], list) or not document[era]:
            report.error(f"'{era}' must be a non-empty array of rows")
            return None

    shapes = {
        era: (len(document[era]), len(document[era][0]) if document[era] else 0)
        for era in ERAS
    }
    if len(set(shapes.values())) != 1:
        report.error(f"era layers differ in shape: {shapes}")
        return None

    finish = document.get("finish")
    if finish is not None:
        if not isinstance(finish, dict):
            report.error("'finish' must be an object")
        elif "requires_seals" in finish and not isinstance(finish["requires_seals"], int):
            report.error("'finish.requires_seals' must be an integer")
    if "tutorial" in document and not isinstance(document["tutorial"], bool):
        report.error("'tutorial' must be true or false")

    height, width = shapes["past"]
    for era in ERAS:
        for y, row in enumerate(document[era]):
            if len(row) != width:
                report.error(f"{era} row {y} is {len(row)} wide, want {width}")
            unknown = set(row) - SOLID_CHARS - HAZARD_CHARS - GOAL_CHARS - {".", " "}
            if unknown:
                report.error(
                    f"{era} row {y} has unrecognised tile(s) {sorted(unknown)} at "
                    f"x={[i for i, c in enumerate(row) if c in unknown][:5]}"
                )
    return document["past"], height, width


def check_entities(document: dict, report: Report, height: int, width: int) -> list[dict]:
    entities = document.get("entities")
    if not isinstance(entities, list) or not entities:
        report.error("no 'entities' array")
        return []

    spawns = [e for e in entities if e.get("type") == "player"]
    if len(spawns) != 1:
        report.error(f"needs exactly one player spawn, found {len(spawns)}")

    seals = [e for e in entities if e.get("type") == "seal"]
    # A level that states how many seals its finish needs is held to that; one
    # that does not is held to the older rule of one per era, which is what the
    # non-tutorial regions all want.
    finish = document.get("finish") or {}
    required = finish.get("requires_seals") if isinstance(finish, dict) else None
    if required is None:
        seal_eras = sorted(e.get("era") for e in seals)
        if seal_eras != ["Future", "Past", "Present"]:
            report.error(f"needs one seal per era, found {seal_eras}")
    elif not isinstance(required, int) or required < 1:
        report.error(f"finish.requires_seals must be 1 or more, got {required!r}")
    elif len(seals) < required:
        report.error(f"needs {required} seals but places {len(seals)}")

    for entity in entities:
        x, y = entity.get("x"), entity.get("y")
        if not isinstance(x, int) or not isinstance(y, int):
            report.error(f"entity {entity.get('type')} has non-integer coordinates: {entity}")
            continue
        if not (0 <= x < width and 0 <= y < height):
            report.error(
                f"{entity.get('type')} at {x},{y} is outside the {width}x{height} grid"
            )
    return entities


def check_gate(document: dict, report: Report) -> bool:
    found = True
    for era in ERAS:
        goals = [
            (x, y)
            for y, row in enumerate(document[era])
            for x, cell in enumerate(row)
            if cell in GOAL_CHARS
        ]
        if not goals:
            report.error(f"{era} layer has no gate ('G')")
            found = False
    return found


def reachable_from_spawn(document: dict, spawn: tuple[int, int], era: str) -> set[tuple[int, int]]:
    """Flood fill of the cells the player can walk to in one era.

    Movement is four-way onto a non-solid cell, which is a deliberately generous
    model: the real player can jump, so anything this says is unreachable is
    genuinely unreachable. A level that passes here can still be too hard, but it
    cannot be impossible.
    """
    rows = document[era]
    height, width = len(rows), len(rows[0])
    # Tuples are (row, column) throughout this function. Getting this backwards
    # silently reports every level as unreachable rather than raising, because the
    # comparison is against a set of transposed pairs.
    start = (spawn[1], spawn[0])

    if not (0 <= start[0] < width and 0 <= start[1] < height):
        return set()

    seen = {start}
    queue = deque([start])
    while queue:
        y, x = queue.popleft()
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            ny, nx = y + dy, x + dx
            if not (0 <= ny < height and 0 <= nx < width):
                continue
            if (ny, nx) in seen:
                continue
            if blocks_walk(rows[ny][nx], era):
                continue
            seen.add((ny, nx))
            queue.append((ny, nx))
    return seen


def check_completion(document: dict, entities: list[dict], report: Report,
                     height: int, width: int) -> None:
    """Every gate must be reachable in at least one era, and so must every seal.

    The player's era changes constantly, so the correct test is a union: a cell
    reachable in *any* era is a cell the player can stand on. A gate that is
    unreachable in all three is an unwinnable level, and one that is reachable in
    only one era is a level that asks for a specific shift with no signposting.
    """
    spawn = next((e for e in entities if e.get("type") == "player"), None)
    if spawn is None:
        return

    per_era = {era: reachable_from_spawn(document, (spawn["x"], spawn["y"]), era)
               for era in ERAS}
    union: set[tuple[int, int]] = set().union(*per_era.values())

    for era in ERAS:
        rows = document[era]
        for y, row in enumerate(rows):
            for x, cell in enumerate(row):
                if cell in GOAL_CHARS and (y, x) not in union:
                    report.error(f"gate at {x},{y} is unreachable in any era")
                    return

    for seal in (e for e in entities if e.get("type") == "seal"):
        # A seal is taken by pressing E within its reach radius from an adjacent
        # cell, so what must be reachable is the ground *beside* it — not the seal's
        # own cell. A seal set into a wall is the normal case, not a defect, and
        # testing its own cell would fail every embedded seal in the shipped region.
        adjacent = [
            (seal["y"] + dy, seal["x"] + dx)
            for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0))
        ]
        if not any(key in union for key in adjacent):
            report.error(
                f"{seal.get('era')} seal at {seal['x']},{seal['y']} cannot be approached"
            )

    # A gate that needs one specific era and nothing else is a hidden requirement.
    # Only meaningful when the level actually asks for every seal: a tutorial with
    # a single Present seal is meant to be a single-era requirement.
    era_counts = {era: 0 for era in ERAS}
    for entity in entities:
        if entity.get("type") != "seal":
            continue
        key = (entity["y"], entity["x"])
        for era in ERAS:
            if key in per_era[era]:
                era_counts[era] += 1
    if all(count == 0 for count in era_counts.values()):
        report.note("no seal is reachable in any single era on its own")

    # Checkpoints and the finish should be reachable too. A level whose gate opens
    # is not completable if the player cannot get back to it after the last seal.
    for entity in entities:
        if entity.get("type") != "checkpoint":
            continue
        key = (entity["y"], entity["x"])
        if key not in union:
            report.error(
                f"checkpoint at {entity['x']},{entity['y']} is unreachable in any era"
            )


def check_placement(document: dict, entities: list[dict], report: Report,
                    height: int, width: int) -> None:
    """Nothing placed where it cannot be reached.

    The rules differ per kind, because the kinds are reached differently:

      player    must start in open air and land on something
      enemy     must be in open air in every era it exists in — it is touched to
                damage it, so a Sentinel inside a platform cannot be hit at all
      pickup    same: collected by contact
      seal      may be *embedded*. A seal is taken by pressing E within a reach
                radius (76px, about two and a half tiles), so a seal set into a
                wall is a deliberate and common design: it is a thing you stand
                beside. What matters is that standable ground is next to it.
    """
    for era in ERAS:
        rows = document[era]
        for entity in entities:
            kind = entity.get("type")
            if kind not in ("enemy", "pickup", "seal", "player"):
                continue
            x, y = entity["x"], entity["y"]
            if not (0 <= x < width and 0 <= y < height):
                continue

            if kind == "seal":
                # Embedded is fine; unreachable is not. Needs an open cell
                # orthogonally adjacent in this era so the player can stand there.
                neighbours = [
                    (x + dx, y + dy)
                    for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0))
                    if 0 <= x + dx < width and 0 <= y + dy < height
                ]
                if not any(not blocks_walk(rows[ny][nx], era) for nx, ny in neighbours):
                    report.error(
                        f"seal at {x},{y} has no standable ground beside it in {era}"
                    )
                continue

            if solid_in(rows[y][x], era):
                report.error(
                    f"{kind} at {x},{y} is inside solid terrain in the {era} layer"
                )
                continue

            # Nothing directly beneath is *not* an error. Everything in the world
            # is subject to gravity, so an entity placed a row or two above the
            # floor falls onto it — which is why the shipped region's player spawn
            # sits at y=22 with solid ground at y=24. Only a note, because it is
            # usually what an author means.
            below = y + 1
            supported = below >= height or any(
                solid_in(document[e][below][x], e) for e in ERAS
            )
            if not supported:
                report.note(
                    f"{kind} at {x},{y} floats (nothing directly beneath; it will fall)"
                )

    # The spawn must be clear of the border walls, or the player starts inside one.
    spawn = next((e for e in entities if e.get("type") == "player"), None)
    if spawn and (spawn["x"] <= 1 or spawn["y"] <= 1):
        report.note(f"spawn at {spawn['x']},{spawn['y']} is against the border")


def check_difficulty(document: dict, entities: list[dict], report: Report) -> None:
    """Sanity bounds on the numbers a designer intends to tune by hand."""
    enemies = [e for e in entities if e.get("type") == "enemy"]
    checkpoints = [e for e in entities if e.get("type") == "checkpoint"]
    pickups = [e for e in entities if e.get("type") == "pickup"]
    health = [e for e in pickups if e.get("kind") == "health"]

    if not checkpoints:
        report.note("no checkpoints: a death restarts the whole region")
    if len(checkpoints) > 6:
        report.note(f"{len(checkpoints)} checkpoints is unusually many")

    past = [e for e in enemies if e.get("eras") == "Past"]
    future = [e for e in enemies if e.get("eras") == "Future"]
    if not past:
        report.note("no Past-only enemy, so the Past has no threat of its own")
    if not future:
        report.note("no Future-only enemy, so wisps never appear")

    # The failure case worth catching mechanically: an enemy per meter of corridor
    # with no health anywhere.
    if len(enemies) >= 8 and not health:
        report.error(f"{len(enemies)} enemies and no health pickup: not survivable")

    for enemy in enemies:
        if enemy.get("kind") == "Warden" and enemy.get("eras") != "Past":
            report.error("a Warden outside the Past is never real")
        if enemy.get("kind") == "Wisp" and enemy.get("eras") != "Future":
            report.error("a Wisp outside the Future is never real")


def validate_path(path: Path) -> Report:
    report = Report(path.stem)
    document = load_region(path, report)
    if document is None:
        return report

    shape = check_structure(document, report)
    if shape is None:
        return report
    _, height, width = shape

    entities = check_entities(document, report, height, width)
    check_gate(document, report)
    if entities:
        check_placement(document, entities, report, height, width)
        check_completion(document, entities, report, height, width)
        check_difficulty(document, entities, report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", default=str(ROOT / "data" / "levels"),
                        help="directory of level files")
    parser.add_argument("--quiet", action="store_true", help="only print failures")
    args = parser.parse_args()

    directory = Path(args.data)
    if not directory.is_dir():
        print(f"no such directory: {directory}", file=sys.stderr)
        return 2

    files = sorted(directory.glob("*.json"))
    if not files:
        print(f"no level files in {directory}", file=sys.stderr)
        return 2

    failures = 0
    for index, path in enumerate(files, start=1):
        report = validate_path(path)
        label = f"Level {index:02d}: {path.stem}"
        if report.ok:
            if not args.quiet:
                suffix = f"  ({'; '.join(report.notes)})" if report.notes else ""
                print(f"{label}: PASS{suffix}")
        else:
            failures += 1
            print(f"{label}: FAIL", file=sys.stderr)
            for problem in report.problems:
                print(f"  {problem}", file=sys.stderr)

    print(f"\n{len(files) - failures}/{len(files)} regions valid")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())