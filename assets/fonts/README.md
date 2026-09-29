# Fonts

## SpaceGrotesk.ttf

The game's interface face. A geometric sans with a technical character that
suits Era Shift's mix of ancient stone and temporal machinery.

* **Upstream:** https://github.com/floriankarsten/space-grotesk
* **Licence:** SIL Open Font License 1.1 — see `SpaceGrotesk-OFL.txt`
* **Weights bundled:** the variable font (100–700). The engine requests the
  regular and bold instances through `TTF_SetFontStyle`.

## Adding a font

Drop the `.ttf` into this directory, add the licence text, and point
`graphics.uiFont` in `config/graphics.json` at it. Keep the licence file
alongside; redistribution of an OFL font requires the licence to travel with
it.

If no font loads, the engine falls back to its built-in 5x7 `BitmapFont`, which
is compiled into the binary. The game is never completely textless.
