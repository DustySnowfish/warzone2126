// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

#include "bloom_common.glsl"
#include "sky_dome_common.glsl"

layout(std140) uniform cbuffer {
	vec4 uvScaleClamp;
	vec4 params; // x = bloom intensity, y = exposure, z = shoulder knee, w = bloom levels summed
	vec4 grade;  // x = colour grade strength (0 = off), y = LUT size
	mat4 invViewProj;
	vec4 hazeZenith;
	vec4 hazeHorizon;
	vec4 hazeGround;
	vec4 hazeSunDir;
	vec4 hazeSunColour; // w = sun glow
	vec4 hazeShape;     // x = horizon sharpness, y = sky brightness, zw = pivot (render x, z) relative to the camera
	vec4 haze;          // x = start, y = end (square distance from the pivot), z = strength
	vec4 cloud;         // x = coverage, y = strength (0 = off), z = 1 / size, w = height above the camera
	vec4 cloudOffset;   // xy = camera (render x, z) plus wind drift, z = edge softness
};
uniform sampler2D sceneTexture;
uniform sampler2D bloomTexture;
uniform sampler2D gradeLut;
uniform sampler2D depthTexture;

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#else
#define texture(tex, uv) texture2D(tex, uv)
#endif

#ifdef NEWGL
in vec2 texCoords;
out vec4 FragColor;
#else
varying vec2 texCoords;
#define FragColor gl_FragColor
#endif

#define CLIP_FROM_TEXCOORD(t) ((t) * 2.0 - 1.0)

// Volumetric mist: density per render unit at sea level, how fast it thins with height, how far
// it is marched
#define WZ2126_MIST_DENSITY 0.00011
#define WZ2126_MIST_HEIGHT 190.0
#define WZ2126_MIST_REACH 9000.0

// Aerial perspective: how far (render units, 1/e) and how strong (at most) it gets
#define WZ2126_AERIAL_DISTANCE 13000.0
#define WZ2126_AERIAL_STRENGTH 0.45

// Distance haze: far terrain fades into the sky colour in the same direction, so the land
// runs into the sky instead of ending in a hard (or black) edge. Display-encoded in and out.
vec3 wzApplyHaze(vec3 encoded, vec2 t, float depth)
{
	if ((haze.z <= 0.0 && cloud.y <= 0.0) || depth >= 0.9999)
	{
		return encoded;
	}
	vec4 p = invViewProj * vec4(CLIP_FROM_TEXCOORD(t), depth * 2.0 - 1.0, 1.0);
	vec3 rel = p.xyz / p.w; // camera-relative position
	// Cloud shadows darken the lit scene (display-encoded, so scale by the encoded factor)
	encoded *= pow(wzCloudShadow(rel, hazeSunDir.xyz, cloud, cloudOffset), 1.0 / 2.2);
	// Warzone 2126 volumetric haze (terrain realism E2): march the view ray through a mist that
	// thins with height, scattering sunlight forward (the mist glows towards the sun) and sky light,
	// with the cloud shadows cutting the sunlit part into shafts
	if (haze.w > 0.0 && grade.w > 0.5)
	{
		int steps = int(grade.w + 0.5);
		float len = length(rel);
		vec3 dir = rel / max(len, 1e-3);
		float marchLen = min(len, WZ2126_MIST_REACH);
		float stepLen = marchLen / float(steps);
		// Per-pixel offset of the samples (interleaved gradient noise) hides the steps
		float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
		float g = 0.55;
		float cosT = dot(dir, hazeSunDir.xyz);
		float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cosT, 1.5) * 0.25;
		vec3 sunLight = hazeSunColour.rgb * phase * step(0.0, hazeSunDir.y);
		vec3 skyLight = hazeHorizon.rgb * 0.45;
		float T = 1.0;
		vec3 inscatter = vec3(0.0);
		for (int i = 0; i < 16; ++i)
		{
			if (i >= steps) { break; }
			vec3 q = dir * ((float(i) + jitter) * stepLen);
			float y = grade.z + q.y;
			float density = haze.w * WZ2126_MIST_DENSITY * exp(-max(y, 0.0) / WZ2126_MIST_HEIGHT);
			float lit = cloud.y > 0.0 ? wzCloudShadow(q, hazeSunDir.xyz, cloud, cloudOffset) : 1.0;
			float a = density * stepLen;
			inscatter += T * a * (sunLight * lit + skyLight);
			T *= exp(-a);
		}
		encoded = encoded * T + inscatter;
	}
	if (haze.z <= 0.0)
	{
		return encoded;
	}
	vec2 fromPivot = abs(rel.xz - hazeShape.zw);
	float squareDistance = max(fromPivot.x, fromPivot.y);
	float amount = smoothstep(haze.x, haze.y, squareDistance) * haze.z;
	vec3 dir = normalize(rel);
	// Warzone 2126 aerial perspective (terrain realism E3): the air between the camera and the
	// ground scatters the sky's own colour in (blue on clear days, ochre in dust, grey in rain), more
	// with distance, so far hills read as far instead of only the map edge fading
	float aerial = (1.0 - exp(-length(rel) / WZ2126_AERIAL_DISTANCE)) * WZ2126_AERIAL_STRENGTH * min(haze.z, 1.0);
	vec3 sky = wzSkyDomeColour(dir, hazeZenith.rgb, hazeHorizon.rgb, hazeGround.rgb, hazeSunDir.xyz, hazeSunColour.rgb, hazeShape.x, hazeSunColour.w, hazeShape.y);
	// The scattered light is the sky near the horizon in that direction (looking down at the ground
	// would otherwise pick the dark ground colour)
	vec3 airDir = normalize(vec3(dir.x, 0.0, dir.z) + vec3(0.0, 0.05, 0.0));
	vec3 air = wzSkyDomeColour(airDir, hazeZenith.rgb, hazeHorizon.rgb, hazeGround.rgb, hazeSunDir.xyz, hazeSunColour.rgb, hazeShape.x, hazeSunColour.w, hazeShape.y);
	encoded = mix(encoded, air, aerial);
	return mix(encoded, sky, amount);
}

// Colour grade: a LUT x LUT x LUT colour cube stored as a horizontal strip of LUT slices
// (blue picks the slice). Input and output are display-encoded colour in 0..1.
vec3 wzApplyGradeLut(vec3 c, float size)
{
	c = clamp(c, vec3(0.0), vec3(1.0));
	float maxIndex = size - 1.0;
	float b = c.b * maxIndex;
	float slice0 = floor(b);
	float slice1 = min(slice0 + 1.0, maxIndex);
	float f = b - slice0;
	vec2 texel = vec2(1.0 / (size * size), 1.0 / size);
	float u = c.r * maxIndex + 0.5;
	float v = (c.g * maxIndex + 0.5) * texel.y;
	vec3 a = texture(gradeLut, vec2((slice0 * size + u) * texel.x, v)).rgb;
	vec3 d = texture(gradeLut, vec2((slice1 * size + u) * texel.x, v)).rgb;
	return mix(a, d, f);
}

void main()
{
	vec2 uv = clamp(texCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	vec3 sceneEncoded = wzApplyHaze(texture(sceneTexture, uv).rgb, texCoords, texture(depthTexture, uv).r);
	vec3 colour = wzHdrDecode(sceneEncoded);
	// The bloom chain covers the scene's used area across its whole extent
	vec3 bloom = texture(bloomTexture, texCoords).rgb / max(params.w, 1.0);
	colour += bloom * params.x;
	colour *= params.y;
	colour = wzTonemapShoulder(colour, params.z);
	vec3 display = wzHdrEncode(colour);
	if (grade.x > 0.0)
	{
		display = mix(display, wzApplyGradeLut(display, grade.y), grade.x);
	}
	FragColor = vec4(display, 1.0);
}
