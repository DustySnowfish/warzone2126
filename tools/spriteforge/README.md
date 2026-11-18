# SpriteForge v0.4

A portable, deterministic directional-greybox reference tool for fixed-camera RTS sprite production.

## Interactive use

Extract the ZIP archive and double-click `spriteforge.html`. Tested by the user in Brave in the previous release. v0.4 retains the same standalone HTML approach. No server or account is required.

Choose Tank (turn off turret and cannon for a separate hull), Turret Only, or Import OBJ/GLB. Set the asset transform, frame size, direction count and camera calibration. Inspect the fixed-pivot flipbook, then export transparent guides and a JSON manifest. `Save project preset` records the settings for subsequent sessions or batch generation. The referenced OBJ/GLB remains external and must be provided again.

## Agent / automation integration

`python spriteforge_batch.py --preset spriteforge_tank_preset.json --output exports`

For a saved imported mesh preset:

`python spriteforge_batch.py --preset imported_project.json --mesh my_asset.glb --output exports`

Dependencies: Python 3, Pillow, Playwright (`pip install pillow playwright` and `python -m playwright install chromium`). If system Chromium is installed, the runner can use it. The runner creates `frame_00.png`, etc., an RGBA `spritesheet.png`, and `manifest.json`, using the exact same JavaScript rendering code as the interactive UI. It does *not* create finished stylised sprites or resolve textures.

The `SpriteForgeAPI` browser object exposes `getPreset()`, `applyPreset(project)`, `params()`, `render()`, `draw(context,config,angle,transparent)`, `angleAt(index, config)`, `exportManifest()`, and `setFrame(index)`.

## Stable conventions

- World up is +Z, object forward at 0 degrees is +Y.
- The camera stays fixed; object rotates around Z.
- Frame anchor is always `(frameWidth/2, frameHeight * anchorY/100)`.
- Render all frames at the same dimensions and scale. Do not independently auto-centre or resize an individual frame.
- Draw hull and turret as separate assets. Align turret sprite origin to its in-game mount point in the game engine.
- `SpriteForge` is for greybox reference; final artwork must be inspected in an animation preview for apparent wobble.

## Known limits

The current renderer is a lightweight 2D canvas triangle renderer, not a full GLTF engine. It does not implement GLB scene node transforms, UV/material texture rendering, complete depth-buffer occlusion, or animation. The clipping indicator is a 0-degree bounds heuristic, not a complete all-facings QA test. Imported assets display a subset of up to 12,000 triangles. To make GLB material rendering production-ready, replace the mesh preview with a true WebGL renderer such as Three.js while preserving the orientation and anchor metadata.

For a production multi-agent system, put the batch runner in a controlled shared repository or invoke it via your agent's shell/code-execution capability. This archive does *not* install a permanent ChatGPT tool or publish a hosted application.