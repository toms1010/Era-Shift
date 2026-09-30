# Assets

Almost nothing is in here, and that is a decision rather than an omission.

| Directory | Contents |
| --- | --- |
| `fonts/` | One TTF and its licence. The only shipped asset. |

## Why there is no `audio/`, `sprites/` or `vfx/` directory

Three reasons, in order of how much they matter.

**The audio is generative.** Every sound in Era Shift is computed at runtime by
`src/EraShift/Audio/Synth.cpp`. There is no `.wav` to ship because the music is
*parameters*: a scale, a tempo, a brightness, filtered by how much paradox the
run has accumulated. A crossfade between two loops cannot glide the Past into the
Future mid-bar; a synthesiser does it because the scale is a variable. It is also
why the audio can be unit tested — see `tests/gameplay/TestSynth.cpp`, which
asserts on waveform properties with no sound card present.

**The visuals are procedural.** Everything on screen is a rectangle, including
the characters. A "sprite" here is a pose: a squash, a lean, a swing, an alpha.
`src/EraShift/Game/Animation.cpp` holds the clips and
`src/EraShift/Graphics/ParticleSystem.cpp` holds the effects, both as data and
arithmetic rather than as image files.

**A repository of generated binaries is a repository nobody can review.** A
reviewer can read a scale table and argue about whether a minor seventh is right.
They cannot read a 3-second `.wav` and say anything useful about it. This is the
same argument the project already made about level data, which is why
`data/levels/ancient_forest.json` is a readable JSON file rather than a tilemap
image.

## If binary assets are added later

Put them here and keep the same discipline:

```
assets/
  audio/
    sfx/          one file per Sfx enum value, named after it
    music/        never - the music is synthesised
  fonts/
  textures/       tiles, characters, UI nine-slice
```

The loader is `Graphics::ResourceManager`, and the path convention is
`<contentRoot>/assets/...`. A new sound added as a file would need an `Sfx` value
to match the filename, because `AudioManager` renders and caches one-shots by
enum and the enum is what the call sites name.
