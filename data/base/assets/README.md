# Warzone 2126 assets

New glTF 2.0 models and their textures live here, laid out as described in the
art specification (units/, structures/, features/).

`model_overrides.json` maps an original PIE model file name to a new glTF model.
The original PIE is still loaded for gameplay calculations (so balance and
multiplayer sync are unchanged); the glTF model only replaces what is drawn.

```json
{
	"version": 1,
	"overrides": {
		"drmbod05.pie": { "model": "assets/units/bodies/cobra.glb" },
		"blhq.pie": { "model": "assets/structures/hq.glb", "scale": 32 }
	}
}
```

`scale` is game units per glTF metre (default 32; one tile is 128 units, 4 m).
Paths are from the data root. Textures are referenced by the .glb and must be
PNG files next to it; the team colour mask is named in the material's
`wz_tcmask` custom property.
