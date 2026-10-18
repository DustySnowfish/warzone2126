// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

#include "sky_dome_common.glsl"

layout(std140) uniform cbuffer {
	mat4 invViewProj;
	vec4 zenith;
	vec4 horizon;
	vec4 ground;
	vec4 sunDir;
	vec4 sunColour;
	vec4 params; // x = horizon sharpness, y = sun glow, z = sun disc, w = brightness
	vec4 fogColour;
};

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#endif

#ifdef NEWGL
in vec2 texCoords;
out vec4 FragColor;
#else
varying vec2 texCoords;
#define FragColor gl_FragColor
#endif

void main()
{
	FragColor = vec4(wzSkyDome(texCoords, invViewProj, zenith.rgb, horizon.rgb, ground.rgb, sunDir.xyz, sunColour.rgb, params, fogColour), 1.0);
}
