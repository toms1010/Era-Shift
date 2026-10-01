# Assets

Almost nothing is in here, and that is a decision rather than an omission.

| Directory | Contents |
| --- | --- |
| `fonts/` | One TTF and its licence. The only shipped asset. |

## Why there is no `sprites/` or `vfx/` directory

Two reasons, in order of how much they matter.

**The visuals are procedural.** Everything on screen is a rectangle, including
the characters. A "sprite" here is a pose: a squash, a lean, a swing, an alpha.
`src/EraShift/Game/Animation.cpp` holds the clips and
`src/EraShift/Graphics/ParticleSystem.cpp` holds the effects, both as data and
arithmetic rather than as image files.

**A repository of generated binaries is a repository nobody can review.** A
reviewer can read a pose table and argue about whether a swing leans the right way.
They cannot read a 3-second sprite sheet and say anything useful about it. This is
the same argument the project already made about level data, which is why
`data/levels/ancient_forest.json` is a readable JSON file rather than a tilemap
image.

## If binary assets are added later

Put them here and keep the same discipline:

```
assets/
  fonts/
  textures/       tiles, characters, UI nine-slice
```

The loader is `Graphics::ResourceManager`, and the path convention is
`<contentRoot>/assets/...`.
