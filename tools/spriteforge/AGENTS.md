# SpriteForge agent instructions

Use SpriteForge as the **geometry authority** for directional RTS sprites.

1. Build one shared preset for an asset. Never independently rotate or recenter each image.
2. Export frames using `python spriteforge_batch.py --preset spriteforge_tank_preset.json --output exports`.
3. Use `--mesh path/to/asset.glb` for imported mesh presets.
4. Preserve canvas width, height, ground anchor, camera projection, world scale and facing order across frames.
5. World up is +Z and model forward at 0° is +Y; rotate model around Z while camera remains fixed.
6. Export hull and turret separately. A turret's in-game mount position must be aligned using the game engine's offset.
7. Compare each drawn image to the corresponding rendered reference. Check cardinal and diagonal directions, silhouette, shadows and apparent jitter.
8. This greybox renderer is not a full GLTF texture renderer. Do not claim imported materials or GLB node transforms are faithfully rendered.
9. Commit preset changes with the associated sprite sheet so further agents can reproduce each sequence.
