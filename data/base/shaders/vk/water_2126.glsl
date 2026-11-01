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


// Warzone 2126 water, look pass 2: the whole surface from the true depth below it.
//   N: wave normal (model space, y up), V: to the camera, L: to the sun, d: water depth here
//   (world units; a tile is 128), pos: model-space position, t: seconds, sunVis: shadow-map sun
//   visibility, texNoise: 0..1 noise from the water textures. Returns straight-alpha colour.
// - Body: light scattered back out of the water, tinted by absorption (red goes first), so the
//   shallows are clear and turquoise and the deeps dark blue-green; the bed shows through in the
//   shallows.
// - Reflection: Schlick Fresnel with the map's sky; the waves calm with distance (no shimmer).
// - Sun glint: a hot core and a wider sheen, carried by slow swells.
// - Shore foam: lines lapping in and out at the waterline, broken up by the water texture, with
//   no hard edge where the water meets the shore.
float wz2126WaterHash(vec2 p)
{
	p = fract(p * vec2(0.1031, 0.1030));
	p += dot(p, p.yx + 33.33);
	return fract((p.x + p.y) * p.x);
}
float wz2126WaterNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(wz2126WaterHash(i), wz2126WaterHash(i + vec2(1.0, 0.0)), u.x), mix(wz2126WaterHash(i + vec2(0.0, 1.0)), wz2126WaterHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Raindrop rings: each cell of a grid gets a drop at a random moment in a short cycle; its ring
// spreads and fades. Returns a model-space normal tilt (xz).
float wz2126RainRingLight = 0.0; // brightness of the ring crests (set by wz2126RainRipples)
vec2 wz2126RainRipples(vec2 p, float t, float rain)
{
	vec2 tilt = vec2(0.0);
	wz2126RainRingLight = 0.0;
	for (int layer = 0; layer < 2; ++layer)
	{
		float cellSize = layer == 0 ? 22.0 : 15.0;
		vec2 q = p / cellSize + vec2(float(layer) * 0.37, float(layer) * 0.71);
		vec2 cell = floor(q);
		for (int j = -1; j <= 1; ++j)
		{
			for (int i = -1; i <= 1; ++i)
			{
				vec2 c = cell + vec2(float(i), float(j));
				vec2 centre = c + vec2(wz2126WaterHash(c), wz2126WaterHash(c + 13.7)) * 0.8 + 0.1;
				float period = 1.1 + wz2126WaterHash(c + 5.3) * 0.6;
				float phaseT = fract(t / period + wz2126WaterHash(c + 2.9));
				// only some cells drop each cycle, more in heavier rain
				if (wz2126WaterHash(c + floor(t / period) * 0.17) > rain)
				{
					continue;
				}
				vec2 d = q - centre;
				float r = length(d);
				float ring = phaseT * 0.9;
				float w = r - ring;
				float wave = sin(w * 40.0) * exp(-w * w * 120.0) * (1.0 - phaseT);
				tilt += (d / max(r, 1e-3)) * wave * 0.35;
				wz2126RainRingLight += max(wave, 0.0);
			}
		}
	}
	return tilt;
}

vec4 wz2126WaterShade(vec3 N, vec3 V, vec3 L, float d, vec3 pos, float t, float sunVis, float texNoise, float envOn, vec3 ambient, vec3 diffuse, vec3 specular, float rain)
{
	float dist = length(cameraPos.xyz - pos);
	// Slow swells under the ripples
	vec2 swell = vec2(sin(pos.x * 0.0061 + pos.z * 0.0023 + t * 0.9), sin(pos.z * 0.0053 - pos.x * 0.0031 - t * 0.7)) * 0.07;
	N = normalize(N + vec3(swell.x, 0.0, swell.y));
	// Wind patches: gusts roughen the water in drifting patches, with calm, mirror-like streaks
	// between them (the ripple textures alone tile visibly)
	vec2 windP = pos.xz / 1700.0 + vec2(t * 0.018, t * 0.011);
	float gust = wz2126WaterNoise(windP) * 0.65 + wz2126WaterNoise(windP * 2.7 + vec2(5.2, 1.7)) * 0.35;
	float rough = mix(0.3, 1.15, smoothstep(0.25, 0.75, gust));
	N = normalize(vec3(N.x * rough, N.y, N.z * rough));
	N = normalize(mix(N, vec3(0.0, 1.0, 0.0), smoothstep(2500.0, 14000.0, dist) * 0.7));
	if (rain > 0.02 && dist < 5000.0)
	{
		vec2 drops = wz2126RainRipples(pos.xz, t, rain) * (1.0 - smoothstep(2500.0, 5000.0, dist));
		N = normalize(N + vec3(drops.x, 0.0, drops.y));
	}
	float NdotV = clamp(dot(N, V), 0.0, 1.0);
	float fres = clamp((0.05 + 0.95 * pow(1.0 - NdotV, 3.0)) * 1.1, 0.0, 1.0); // (stronger than physical: reads at RTS camera angles)

	float dd = max(d, 0.0);
	vec3 T = exp(-dd * vec3(0.016, 0.0075, 0.0055)); // down to the bed and back up
	vec3 lightIn = ambient * 0.9 + diffuse * (0.35 + 0.65 * sunVis) * max(L.y, 0.15);
	vec3 deep = vec3(0.035, 0.13, 0.17);
	vec3 shallowTint = vec3(0.2, 0.5, 0.46);
	vec3 body = mix(deep, shallowTint, T.g) * lightIn;
	// Light through the wave slopes facing away from the camera towards the sun
	body += shallowTint * diffuse * sunVis * clamp(1.0 - N.y, 0.0, 1.0) * 2.0 * clamp(dot(-V, L) * 0.5 + 0.5, 0.0, 1.0);
	float alpha = 1.0 - (T.r + T.g + T.b) * (0.92 / 3.0);

	vec3 R = reflect(-V, N);
	R.y = abs(R.y);
	vec3 sky = envOn > 0.5 ? pow(max(wz2126WaterEnv(R), vec3(0.0)), vec3(1.0 / 2.2)) : mix(ambient, vec3(0.62, 0.72, 0.82), 0.5);
	vec3 col = mix(body, sky, fres);
	alpha += (1.0 - alpha) * fres;

	// Raindrop ring crests catch the light
	col += (ambient + diffuse * 0.5) * min(wz2126RainRingLight, 1.0) * 0.4 * rain;
	vec3 H = normalize(L + V);
	float NdotH = max(dot(N, H), 0.0);
	float glint = (pow(NdotH, 1200.0) * 3.5 + pow(NdotH, 140.0) * 0.18) * sunVis * step(0.0, L.y);
	col += (specular + diffuse) * 0.5 * glint;
	alpha = max(alpha, clamp(glint, 0.0, 1.0));

	float shore = 1.0 - smoothstep(0.0, 34.0, dd);
	float wave = sin(dd * 0.22 - t * 1.6 + texNoise * 3.0) * 0.5 + 0.5;
	float lap = smoothstep(0.74, 0.96, wave) * shore;
	float fizz = 1.0 - smoothstep(0.0, 5.0, dd);
	float foam = clamp((lap * 0.8 + fizz * 0.9) * smoothstep(0.2, 0.7, texNoise + 0.2), 0.0, 1.0) * step(0.001, d);
	vec3 foamCol = min(ambient * 0.8 + diffuse * (0.4 + 0.6 * sunVis), vec3(1.15));
	col = mix(col, foamCol, foam * 0.85);
	alpha = max(alpha * smoothstep(0.0, 6.0, dd), foam * 0.9);
	return vec4(col, clamp(alpha, 0.0, 1.0));
}

#endif // WZ_WATER_2126_GLSL
