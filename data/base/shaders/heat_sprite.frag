// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

#include "heat_common.glsl"

layout(std140) uniform cbuffer {
	mat4 viewProjection;
	vec4 cameraRight;
	vec4 cameraUp;
	vec4 heatParams;
};

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
in vec2 uv;
in vec4 data;
out vec4 FragColor;
#else
varying vec2 uv;
varying vec4 data;
#define FragColor gl_FragColor
#endif

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
