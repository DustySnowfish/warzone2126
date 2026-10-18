// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

#include "particle_common.glsl"

layout(std140) uniform cbuffer {
	mat4 viewProjection;
	vec4 cameraRight;
	vec4 cameraUp;
	vec4 cameraForward;
	vec4 atlas;
	vec4 ambient;
	vec4 sunColour;
	vec4 sunDir;
};

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#endif

#ifdef NEWGL
in vec2 vertex;
in vec4 particlePositionSize;
in vec4 particleColour;
in vec4 particleParams;
in vec4 particleVelocity;
out vec2 uv;
out vec4 colour;
#else
attribute vec2 vertex;
attribute vec4 particlePositionSize;
attribute vec4 particleColour;
attribute vec4 particleParams;
attribute vec4 particleVelocity;
varying vec2 uv;
varying vec4 colour;
#endif

void main()
{
	vec3 world = wzParticleCorner(vertex, particlePositionSize, particleParams, particleVelocity, cameraRight.xyz, cameraUp.xyz, cameraForward.xyz);
	uv = wzParticleUV(vertex, particleParams.y, atlas.xy);
	vec3 rgb = particleColour.rgb;
	if (particleParams.w > 0.5)
	{
		// Lit (smoke, dust): sky light plus a soft wrap of the sun
		rgb *= ambient.rgb + sunColour.rgb * 0.6;
	}
	colour = vec4(rgb, particleColour.a);
	gl_Position = viewProjection * vec4(world, 1.0);
}
