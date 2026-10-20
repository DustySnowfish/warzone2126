# Particle effects

Warzone 2126 draws weapon and explosion effects with a particle system defined by two files in this folder. Both are re-read at the start of every game, so they can be tuned between games by copying them into an `effects` folder in the Warzone 2126 settings folder. Effects are display only: they never change the game, so they are safe in multiplayer.

Distances are game units: 128 per tile, 32 per metre. Times are milliseconds. Colours are 0..1 sRGB.

## particles.json

Named effects. Each effect has `layers` (groups of particles) and an optional `light`.

```json
"cannon_hit": {
	"layers": [ { ...layer... }, { ...layer... } ],
	"light": { "rgb": [1.0, 0.65, 0.3], "range": 380, "intensity": 3.0, "life": 260 }
}
```

Layer keys (a value in `[a, b]` form is a random range):

| Key | Default | Meaning |
|---|---|---|
| texture | glow | `glow`, `core`, `spark`, `ring`, `star`, `dust`, `smoke`, `fire`, `debris`, `scorch`, `tracks`, `tyres` |
| blend | additive | `additive` (light: fire, flashes, sparks) or `alpha` (smoke, dust, debris) |
| count | 1 | Particles spawned |
| life | 300 | Lifetime, ms |
| size | [10, 10] | Size at start and end of life, game units (each varied by `sizeJitter`, default 0.2) |
| speed | 0 | Initial speed, game units per second |
| direction | sphere | `emitter` (the weapon's aim), `up`, `sphere`, `hemisphere` (upper half) |
| spread | 180 | Cone half angle in degrees, for `emitter` and `up` |
| gravity | 0 | Pull downwards, game units per second squared |
| drag | 0 | Slows the particle; higher stops it sooner |
| rise | 0 | Constant upward speed added (hot smoke) |
| offset | 0 | Random spawn radius |
| rotation | [0, 360] | Starting rotation, degrees |
| spin | 0 | Rotation speed, degrees per second (random direction) |
| stretch | 0 | Lengthens the particle along its motion (sparks) |
| orient | camera | `camera` (faces the camera) or `ground` (lies flat on the terrain under the effect; scorch marks) |
| align | false | With `orient: ground`, turns the mark to face along the effect's direction (track and tyre marks) |
| bounce | off | 0 to 1: the particle bounces on the terrain, keeping this fraction of its speed, then rests (debris) |
| lit | alpha layers | Darkened and tinted by the scene light (smoke, dust) |
| colour | white | List of keys `{ "t": 0..1, "rgb": [r, g, b], "intensity": 1, "alpha": 1 }`; `t` defaults to evenly spaced. Intensity above 1 glows (bloom) with HDR on |

## weapon_vfx.json

Which effect each weapon plays. `subclass` covers every weapon of a class (`MACHINE GUN`, `CANNON`, `ROCKET`, `MORTARS`, and so on); `weapons` overrides single weapons by id (such as `MG3Mk1`).

| Key | Meaning |
|---|---|
| muzzle | Effect when the weapon fires (aimed down the barrel) |
| trail | Effect repeated along a projectile's flight path, every `trailSpacing` game units |
| hit / miss / water | Effect where the projectile lands (`miss` defaults to `hit`) |
| replaceImpact | `true` hides the classic impact sprite (when the new effect is complete) |
| replaceMuzzle | `true` hides the classic muzzle flash model |
| scale | Scales all sizes, speeds and ranges of this weapon's effects |

## Vehicle dust and tracks (weapon_vfx.json "propulsion")

Moving vehicles leave dust and track marks behind them, or spray over water. Keys are propulsion types: `wheeled`, `half-tracked`, `tracked`, `hover`, `legged`. Heavy bodies scale the effect by 1.3, light bodies by 0.8.

| Key | Meaning |
|---|---|
| ground | Effect on land |
| water | Effect over water |
| marks | Ground mark laid at every spacing point on land (`marks_tracks`, `marks_halftracks`, `marks_wheels`); emitted at any speed |
| spacing | Game units travelled between puffs |
| minSpeed | Below this speed (game units per second) no dust is emitted |

Ground marks (scorch, tracks) are capped at 7000 at a time; beyond that new ones are skipped until older ones fade.
