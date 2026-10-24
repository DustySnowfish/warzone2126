#version 450

#include "taa_common.glsl"

layout(std140, set = 0, binding = 0) uniform cbuffer {
	mat4 currentInvViewProj;  // clip to world, this frame (without jitter)
	mat4 previousViewProj;    // world to clip, last frame (without jitter)
	vec4 uvScaleClamp;
	vec4 texelSize;           // xy = 1 / texture size
	vec4 params;              // x = history weight, y = history valid, z = colour box size (deviations)
};
layout(set = 1, binding = 0) uniform sampler2D currentTexture;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;
layout(set = 1, binding = 2) uniform sampler2D historyTexture;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

// Vulkan clip space has y pointing down relative to the engine's matrices (see view_position.glsl)
#define CLIP_FROM_TEXCOORD(t) (vec2((t).x, 1.0 - (t).y) * 2.0 - 1.0)
#define TEXCOORD_FROM_CLIP(c) (vec2((c).x * 0.5 + 0.5, 0.5 - (c).y * 0.5))

// Warzone 2126: some drivers produce the odd NaN or infinity upstream (far terrain, the sky); left in,
// TAA would keep it in its history and bloom would spread it into black squares
vec3 wzFinite(vec3 c)
{
	return (any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : max(c, vec3(0.0));
}

void main()
{
	vec2 uv = clamp(texCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	vec3 current = wzFinite(texture(currentTexture, uv).rgb);
	if (params.y < 0.5)
	{
		// No usable history (first frame, resize, camera cut): start again from this frame
		FragColor = vec4(current, 1.0);
		return;
	}

	// Colour range of the 3x3 neighbourhood, in YCoCg (mean +/- deviation; tighter than min/max)
	vec3 m1 = vec3(0.0);
	vec3 m2 = vec3(0.0);
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			vec2 sampleUV = clamp(uv + vec2(float(x), float(y)) * texelSize.xy, vec2(0.0), uvScaleClamp.zw);
			vec3 c = wzRGBToYCoCg(wzFinite(texture(currentTexture, sampleUV).rgb));
			m1 += c;
			m2 += c * c;
		}
	}
	vec3 mean = m1 / 9.0;
	vec3 deviation = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
	vec3 boxMin = mean - params.z * deviation;
	vec3 boxMax = mean + params.z * deviation;

	// Where this pixel was last frame: un-project with the (jittered) depth, re-project with last frame's camera
	float depth = texture(depthTexture, uv).r;
	vec2 clipXY = CLIP_FROM_TEXCOORD(texCoords);
	vec4 world = currentInvViewProj * vec4(clipXY, depth * 2.0 - 1.0, 1.0);
	// (Projective transforms compose, so no divide is needed in between. Dividing by world.w broke
	// down at the far plane and in the sky, where w approaches zero.)
	vec4 previousClip = previousViewProj * world;
	vec2 previousCoords = TEXCOORD_FROM_CLIP(previousClip.xy / previousClip.w);
	if (abs(previousClip.w) < 1e-7 || any(isnan(previousCoords)) || any(isinf(previousCoords))
	    || any(lessThan(previousCoords, vec2(0.0))) || any(greaterThan(previousCoords, vec2(1.0))))
	{
		FragColor = vec4(current, 1.0);
		return;
	}
	vec2 historyUV = clamp(previousCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	vec3 history = wzRGBToYCoCg(wzFinite(texture(historyTexture, historyUV).rgb));
	history = wzYCoCgToRGB(wzClipToBox(history, boxMin, boxMax));

	FragColor = vec4(mix(current, history, params.x), 1.0);
}

