// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

#include "taa_common.glsl"

layout(std140) uniform cbuffer {
	mat4 currentInvViewProj;  // clip to world, this frame (without jitter)
	mat4 previousViewProj;    // world to clip, last frame (without jitter)
	vec4 uvScaleClamp;
	vec4 texelSize;           // xy = 1 / texture size
	vec4 params;              // x = history weight, y = history valid, z = colour box size (deviations)
};
uniform sampler2D currentTexture;
uniform sampler2D depthTexture;
uniform sampler2D historyTexture;

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#else
#define texture(tex, uv) texture2D(tex, uv)
#endif

#ifdef NEWGL
in vec2 texCoords;
out vec4 FragColor;
#else
varying vec2 texCoords;
#define FragColor gl_FragColor
#endif

#define CLIP_FROM_TEXCOORD(t) ((t) * 2.0 - 1.0)
#define TEXCOORD_FROM_CLIP(c) ((c) * 0.5 + 0.5)

void main()
{
	vec2 uv = clamp(texCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	vec3 current = texture(currentTexture, uv).rgb;
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
			vec3 c = wzRGBToYCoCg(texture(currentTexture, sampleUV).rgb);
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
	world /= world.w;
	vec4 previousClip = previousViewProj * world;
	vec2 previousCoords = TEXCOORD_FROM_CLIP(previousClip.xy / previousClip.w);
	if (any(lessThan(previousCoords, vec2(0.0))) || any(greaterThan(previousCoords, vec2(1.0))))
	{
		FragColor = vec4(current, 1.0);
		return;
	}
	vec2 historyUV = clamp(previousCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	vec3 history = wzRGBToYCoCg(texture(historyTexture, historyUV).rgb);
	history = wzYCoCgToRGB(wzClipToBox(history, boxMin, boxMax));

	FragColor = vec4(mix(current, history, params.x), 1.0);
}

