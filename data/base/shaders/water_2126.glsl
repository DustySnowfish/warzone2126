// Warzone 2126 water surface additions, shared by the GL and Vulkan high-quality water shaders.
//
// - Sky reflection: the map's environment (the same sky and ground the image-based lighting
//   uses) reflected in the waves, weighted by a Fresnel term so the water mirrors the sky at
//   glancing angles and shows its own colour looking straight down.
// - Shoreline foam: a broken, moving band of foam where the water is shallow.
//
// The including shader declares TextureEnvironment (the RGBM lat-long environment, see
// pbr.glsl) before including this file.

#ifndef WZ_WATER_2126_GLSL
#define WZ_WATER_2126_GLSL

#define WZ2126_WATER_PI 3.14159265
#define WZ2126_WATER_ENV_LEVELS 7.0
#define WZ2126_WATER_ENV_LEVEL_HEIGHT 32.0
#define WZ2126_WATER_ENV_RGBM_RANGE 8.0
// Environment level used for the reflection (0 = sharpest; waves blur it a little anyway)
#define WZ2126_WATER_ENV_LEVEL 1.0
// Reflection strength at grazing angles
#define WZ2126_WATER_REFLECTION 0.9

vec3 wz2126WaterEnv(vec3 d)
{
	float u = atan(d.z, d.x) * (0.5 / WZ2126_WATER_PI) + 0.5;
	float v = acos(clamp(d.y, -1.0, 1.0)) / WZ2126_WATER_PI;
	float halfTexel = 0.5 / WZ2126_WATER_ENV_LEVEL_HEIGHT;
	v = clamp(v, halfTexel, 1.0 - halfTexel);
	vec4 t = texture(TextureEnvironment, vec2(u, (WZ2126_WATER_ENV_LEVEL + v) / WZ2126_WATER_ENV_LEVELS), 0.0);
	return t.rgb * t.a * WZ2126_WATER_ENV_RGBM_RANGE;
}

// colour: the classic water colour and alpha (display-encoded). N: water normal (model space,
// y up). V: direction to the camera. shallow: the classic depth term (0.5 at the shore line,
// lower in deep water, higher on the land side). foamNoise: 0..1 noise from the water textures.
vec4 wz2126WaterSurface(vec4 colour, vec3 N, vec3 V, float shallow, float foamNoise, float envOn, vec3 lightColour)
{
	if (envOn > 0.5)
	{
		vec3 R = reflect(-V, N);
		R.y = abs(R.y); // waves can tip the reflection below the horizon; keep it on the sky
		float NdotV = clamp(dot(N, V), 0.0, 1.0);
		// Slightly stronger than physical (0.02, power 5) so the sky reads at RTS camera angles
		float fresnel = 0.06 + 0.94 * pow(1.0 - NdotV, 3.0);
		float amount = clamp(fresnel * WZ2126_WATER_REFLECTION, 0.0, 1.0);
		vec3 sky = pow(max(wz2126WaterEnv(R), vec3(0.0)), vec3(1.0 / 2.2));
		colour.rgb = mix(colour.rgb, sky, amount);
		// Reflections show even where the classic water is nearly transparent (shallows)
		colour.a = max(colour.a, amount * 0.85);
	}
	// Foam along the shore line, broken up by the moving water texture
	float band = smoothstep(0.36, 0.5, shallow) * (1.0 - smoothstep(0.6, 0.78, shallow));
	float foam = band * smoothstep(0.25, 0.7, foamNoise);
	colour.rgb = mix(colour.rgb, lightColour, foam * 0.7);
	colour.a = max(colour.a, foam * 0.75);
	return colour;
}

#endif // WZ_WATER_2126_GLSL
