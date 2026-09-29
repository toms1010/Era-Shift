# Controls

Bindings live in `config/controls.json` and are data-driven: rebinding is a
configuration change, not a code change. Every action has a primary and a
secondary binding so a left-handed or keyboard-only layout works without
special cases.

## Default bindings

| Action | Primary | Secondary |
| --- | --- | --- |
| Move left / right | `A` `D` | `Left` `Right` |
| Move up / down | `W` `S` | `Up` `Down` |
| Jump | `Space` | `Z` |
| Dash | `Left Shift` | `C` |
| Interact / confirm | `E` | `F` |
| Attack | `Mouse Left` | `J` |
| Heavy attack | `Mouse Right` | `K` |
| Aim | `Mouse Middle` | |
| **Era Shift** | `Q` | `R` |
| Inventory | `I` | `Tab` |
| Map | `M` | |
| Quest log | `J` | |
| Pause | `Escape` | `P` |
| Debug overlay | `F3` | |
| Screenshot | `F12` | |

`Esc` both confirms and backs out, so a player can never get trapped in a menu.

## Rebinding

Edit `config/controls.json`. The name may be followed by `" Secondary"` to set
the second binding:

```json
{
  "controls": {
    "ShiftEra": "E",
    "ShiftEra Secondary": "Q",
    "Attack": "Mouse Right"
  }
}
```

Key names are matched case- and separator-insensitively, so `"left shift"`,
`"Left_Shift"` and `"LEFT SHIFT"` are all the same key. A warning is logged for
an unrecognised binding and the default is kept.

A live rebinding UI is planned for Phase 8.

## In-game

| Key | Result |
| --- | --- |
| `Q` in the test scene | Shift era (costs chrono energy) |
| `Esc` | Pause menu |
| `F3` | Debug overlay (FPS, frame time, CPU, draw calls, memory, state stack) |
