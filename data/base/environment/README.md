# Environment lighting

The light that PBR (glTF) models receive from the sky and the ground: soft fill light from every direction, and the blurred reflections seen in metal and glossy paint. One file per tileset (`arizona.json`, `urban.json`, `rockies.json`); a map can have its own in `maps/<map name>.json`.

To tune one, copy it into an `environment` folder in the Warzone 2126 settings folder (same sub-path) and edit it while the game runs. Changes show within a second.

Keys (all optional; colours are 0..255 sRGB, as in an image editor):

| Key | Default | Effect |
|---|---|---|
| zenith | [120,150,190] | Sky colour straight up |
| horizon | [200,190,175] | Sky colour at the horizon |
| ground | [110,100,90] | Light bounced up from the ground |
| horizonSharpness | 3 | Higher keeps the horizon colour in a thinner band (0.25..16) |
| sunGlow | 0.5 | Glow around the sun in reflections (0..4); the sun itself is direct light |
| image | none | Panorama image (2:1, lat-long PNG), e.g. `environment/images/name.png` |
| imageMix | 1 | Blend of the panorama over the colours |
| haze | | Distant terrain fades into the sky colour behind it (needs HDR Lighting). Sub-keys: `start` (0.55) and `end` (0.92), as fractions of the drawn area around the camera, and `strength` (1, 0 turns it off) |
| sky | | The visible sky dome, drawn behind the cloud band. Sub-keys: `zenith`, `horizon`, `horizonSharpness`, `sunGlow` (default to the values above), `sunDisc` (1, brightness of the sun disc), `brightness` (1), `bandFade` (0.35, how much of the top of the cloud band fades into the dome) |

For lighting, only the colours matter, not the overall brightness: the game scales the environment to the map's ambient light level. Without a file, PBR models use a flat ambient light.
