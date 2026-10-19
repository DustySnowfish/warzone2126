#version 450

#include "bloom_common.glsl"
#include "sky_dome_common.glsl"

layout(std140, set = 0, binding = 0) uniform cbuffer {
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

layout(set = 1, binding = 0) uniform sampler2D sceneTexture;
layout(set = 1, binding = 1) uniform sampler2D bloomTexture;
layout(set = 1, binding = 2) uniform sampler2D gradeLut;
layout(set = 1, binding = 3) uniform sampler2D depthTexture;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

// Vulkan clip space has y pointing down relative to the engine's matrices (see view_position.glsl)
#define CLIP_FROM_TEXCOORD(t) (vec2((t).x, 1.0 - (t).y) * 2.0 - 1.0)

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
	if (haze.z <= 0.0)
	{
		return encoded;
	}
	vec2 fromPivot = abs(rel.xz - hazeShape.zw);
	float squareDistance = max(fromPivot.x, fromPivot.y);
	float amount = smoothstep(haze.x, haze.y, squareDistance) * haze.z;
	if (amount <= 0.0)
	{
		return encoded;
	}
	vec3 dir = normalize(rel);
	vec3 sky = wzSkyDomeColour(dir, hazeZenith.rgb, hazeHorizon.rgb, hazeGround.rgb, hazeSunDir.xyz, hazeSunColour.rgb, hazeShape.x, hazeSunColour.w, hazeShape.y);
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
