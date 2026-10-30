# Menu footage

The main menu background plays short clips of battles recorded in the game itself
(`src/menufootage2126.cpp`; clips in `data/base/misc/menufootage/*.webm`). This folder holds
the scenes and the steps to record them again, for example after new art arrives.

## Scenes

Each scene is a visual bench config (`visualbench_menu_<name>.json`) plus an extra script
(`menu_<name>.js`) that places the units and gives their orders. The bench sets the time of
day and weather (`"timeOfDay"`: 1 dawn, 2 day, 3 dusk, 4 night; `"weather"`: rain, snow, dust
or none, with `"weatherStrength"`), and `"record": { "fps": 30 }` makes it:

- run on a virtual clock that advances 1/30 s per rendered frame (so the clip is smooth
  however slowly the frames render),
- hide the interface and messages,
- save every frame of each shot as `benchmarks/<bench>_<time>/<shot>/f00000.png`.

The bench uses its own synchronised seed, so a Random map in a bench is generated with the
same seed every time (3992670690 for these scenes; the log says "Map generation seed"). The
`randomMapCode` sets the landscape, tileset and size.

| Scene | Map | Light and weather |
|---|---|---|
| river | River valleys, Arizona, 2 players | day, clear |
| naval | Islands, Arizona, 2 players | day, light rain |
| artillery | Random landscape, Arizona, advanced bases | day, dust storm |
| night | Highlands, Rockies, advanced bases | night, thunderstorm |

## Recording

1. Copy the scene files into the config folder's `tests/`.
2. Run `warzone2100 --visualbench=menu_<name> --resolution=1280x720` (a window; any GPU).
3. `encode.sh <frames dir> data/base/misc/menufootage/<n>-<name>.webm`

On a desktop GPU a 16-second shot records in under a minute; on the software renderer used
to make the first set it took about an hour per scene.
