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
	// Vulkan clip space has y pointing down (the scene shaders flip y), so flip back before un-projecting
	vec2 clipUV = vec2(texCoords.x, 1.0 - texCoords.y);
	FragColor = vec4(wzSkyDome(clipUV, invViewProj, zenith.rgb, horizon.rgb, ground.rgb, sunDir.xyz, sunColour.rgb, params, fogColour), 1.0);
}
