#version 450

#include "heat_common.glsl"

layout(std140, set = 0, binding = 0) uniform cbuffer {
	mat4 viewProjection;
	vec4 cameraRight;
	vec4 cameraUp;
	vec4 heatParams;
};

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 data;
layout(location = 0) out vec4 FragColor;

void main()
{
	// Strongest low in the middle, fading out to the sides and towards the top
	float side = 1.0 - smoothstep(0.18, 0.5, abs(uv.x - 0.5));
	float rise = smoothstep(0.0, 0.12, uv.y) * (1.0 - smoothstep(0.45, 1.0, uv.y));
	float weight = side * rise * data.x;
	if (weight < 0.002)
	{
		discard;
	}
	vec2 wobble = wzHeatWobble(vec2(uv.x * 3.0, uv.y * 4.0) + data.y * 7.13, heatParams.x);
	FragColor = vec4(wobble * heatParams.y, 0.0, weight);
}
