# Assets, provenance and licences

Shadow Worm Farm contains **no third-party art, textures, models or recorded sounds.** Everything seen and heard
is generated at run time by code in this repository, and is covered by the project's MIT licence. That includes
streamed and recorded use.

## Original, generated at run time (this repository, MIT)

| Asset | Where it comes from |
|---|---|
| Bedding (torn cardboard and newsprint strips, coir), compost, soil, castings, moisture, burrows, the dim headspace | `game/shaders/substrate.gdshader`, driven by the simulated substrate (`core/src/sim.cpp`) |
| Worm bodies (segments, clitellum, peristalsis, sheen, young worms) | A tube mesh built in `game/scripts/farm_view.gd`, bent along each simulated body in `game/shaders/worm.gdshader` |
| Food scraps (banana peel, apple core, lettuce leaf, carrot peelings, coffee grounds, eggshell), their decay and mould | `game/shaders/food.gdshader` |
| Cocoons | Godot `SphereMesh` with per-instance colour (`game/scripts/farm_view.gd`) |
| Glass pane, reflection, dust, condensation and drops | `game/shaders/glass.gdshader` |
| Oak frame | Godot `BoxMesh` primitives with `game/shaders/wood.gdshader` |
| All sound (room tone, bedding crackle, rustles, feeding squelches, scraps landing, the spray bottle, drips) | Synthesised from filtered noise, clicks and sine blips in `extension/src/worm_audio.cpp`; no samples |
| Icon | `packaging/shadow-worm-farm.svg` (hand-written SVG) |

## Third-party software

| Component | Version | Licence | Use |
|---|---|---|---|
| [Godot Engine](https://godotengine.org) | 4.7.2-stable (official build and export templates) | MIT | Engine and runtime; its own third-party notices ship with Godot |
| [godot-cpp](https://github.com/godotengine/godot-cpp) | 10.0.0-stable (git submodule, commit `507ed9d`) | MIT | C++ bindings for the GDExtension |
| ffmpeg (system package, run as a separate program for live streaming) | system | LGPL/GPL (as packaged by the distribution) | Encoding and RTMP/RTMPS output; not bundled |
| secret-tool (libsecret, system package) | system | LGPL-2.1+ | Stores stream keys in the desktop keyring; not bundled |
| C++ standard library (GCC libstdc++) | system | GPL-3.0 with the GCC Runtime Library Exception | Standard runtime |

No fonts, textures, audio files, models or data sets are downloaded or bundled.
