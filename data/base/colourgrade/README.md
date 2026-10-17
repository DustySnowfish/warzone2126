# Colour grades

One grade per tileset (`arizona.json`, `urban.json`, `rockies.json`). A single map can have its own grade in `maps/<map name>.json` (for example `maps/Sk-Startup.json`), which replaces the tileset grade for that map.

To tune a grade, copy the file into the `colourgrade` folder of the Warzone 2126 settings folder (same sub-path) and edit it while the game runs. Changes show within a second.

Keys (all optional), applied in this order:

| Key | Default | Range | Effect |
|---|---|---|---|
| strength | 1 | 0..1 | Blend of the whole grade |
| exposure | 0 | -4..4 | Brightness in stops |
| temperature | 0 | -1..1 | Cool (blue) to warm (orange) |
| tint | 0 | -1..1 | Green to magenta |
| contrast | 1 | 0.25..4 | S-curve around mid grey |
| saturation | 1 | 0..4 | 0 is black and white |
| lift | [0,0,0] | -0.5..0.5 | Adds colour to the shadows |
| gamma | [1,1,1] | 0.1..4 | Midtones, above 1 brightens |
| gain | [1,1,1] | 0..4 | Multiplies the highlights |
| lut | none | | LUT image applied last, e.g. `colourgrade/luts/name.png` |
| lutStrength | 1 | 0..1 | Blend of the LUT image |

A LUT image is made in an image editor: generate a neutral LUT with `tools/assets/make_neutral_lut.py`, apply your grade to it (Photoshop, Affinity, GIMP, DaVinci Resolve), and save it as PNG. It must stay N*N x N pixels (32 gives 1024 x 32).

Grading needs HDR Lighting on. The player can set it to Off, Subtle or Full in the graphics options.
