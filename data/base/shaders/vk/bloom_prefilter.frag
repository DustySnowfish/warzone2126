#version 450

#include "bloom_common.glsl"

layout(std140, set = 0, binding = 0) uniform cbuffer {
	vec4 uvScaleClamp;
	vec4 params; // x = threshold (linear), y = knee
};

layout(set = 1, binding = 0) uniform sampler2D sceneTexture;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

vec3 fetchBright(vec2 uv)
{
	uv = clamp(uv, vec2(0.0), uvScaleClamp.zw);
	vec3 c = texture(sceneTexture, uv).rgb;
	// Warzone 2126: a stray NaN or infinity would otherwise smear into a black square across the bloom chain
	c = (any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c;
	return wzBloomBrightPass(wzHdrDecode(c), params.x, params.y);
}

void main()
{
	// 4 bilinear taps (16 texels) with a Karis average, which stops single very bright
	// pixels from flickering as they move.
	vec2 uv = texCoords * uvScaleClamp.xy;
	vec2 texel = 1.0 / vec2(textureSize(sceneTexture, 0));
	vec3 a = fetchBright(uv + texel * vec2(-1.0, -1.0));
	vec3 b = fetchBright(uv + texel * vec2( 1.0, -1.0));
	vec3 c = fetchBright(uv + texel * vec2(-1.0,  1.0));
	vec3 d = fetchBright(uv + texel * vec2( 1.0,  1.0));
	float wa = 1.0 / (1.0 + wzLuma(a));
	float wb = 1.0 / (1.0 + wzLuma(b));
	float wc = 1.0 / (1.0 + wzLuma(c));
	float wd = 1.0 / (1.0 + wzLuma(d));
	vec3 result = (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
	FragColor = vec4(result, 1.0);
}
