#version 450

#include "bloom_common.glsl"

layout(std140, set = 0, binding = 0) uniform cbuffer {
	vec4 uvScaleClamp;
	vec4 params; // x = bloom intensity, y = exposure, z = shoulder knee, w = bloom levels summed
	vec4 grade;  // x = colour grade strength (0 = off), y = LUT size
};

layout(set = 1, binding = 0) uniform sampler2D sceneTexture;
layout(set = 1, binding = 1) uniform sampler2D bloomTexture;
layout(set = 1, binding = 2) uniform sampler2D gradeLut;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

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
	vec3 colour = wzHdrDecode(texture(sceneTexture, uv).rgb);
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
