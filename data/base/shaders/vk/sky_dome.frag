#version 450

#include "sky_dome_common.glsl"

layout(std140, set = 0, binding = 0) uniform cbuffer {
	mat4 invViewProj;
	vec4 zenith;
	vec4 horizon;
	vec4 ground;
	vec4 sunDir;
	vec4 sunColour;
	vec4 params; // x = horizon sharpness, y = sun glow, z = sun disc, w = brightness
	vec4 fogColour;
};

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

void main()
{
	FragColor = vec4(wzSkyDome(texCoords, invViewProj, zenith.rgb, horizon.rgb, ground.rgb, sunDir.xyz, sunColour.rgb, params, fogColour), 1.0);
}
