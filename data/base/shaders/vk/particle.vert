#version 450

#include "particle_common.glsl"

layout(std140, set = 0, binding = 0) uniform cbuffer {
	mat4 viewProjection;
	vec4 cameraRight;
	vec4 cameraUp;
	vec4 cameraForward;
	vec4 atlas;
	vec4 ambient;
	vec4 sunColour;
	vec4 sunDir;
};

layout(location = 0) in vec2 vertex;
layout(location = 5) in vec4 particlePositionSize;
layout(location = 6) in vec4 particleColour;
layout(location = 7) in vec4 particleParams;
layout(location = 8) in vec4 particleVelocity;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 colour;

void main()
{
	vec3 world = wzParticleCorner(vertex, particlePositionSize, particleParams, particleVelocity, cameraRight.xyz, cameraUp.xyz, cameraForward.xyz);
	uv = wzParticleUV(vertex, particleParams.y, atlas.xy);
	vec3 rgb = particleColour.rgb;
	if (mod(particleParams.w, 2.0) > 0.5)
	{
		rgb *= ambient.rgb + sunColour.rgb * 0.6;
	}
	colour = vec4(rgb, particleColour.a);
	gl_Position = viewProjection * vec4(world, 1.0);
	gl_Position.y *= -1.0;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
