// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

// Warzone 2126 heat shimmer: the lit scene sampled through the heat offset field, plus a faint
// shimmer over distant hot ground

#include "view_position.glsl"
#include "heat_common.glsl"

layout(std140) uniform cbuffer {
	mat4 invProjectionMatrix;
	vec4 uvScaleClamp;       // scene read
	vec4 offsetUvScaleClamp; // offset field read
	vec4 heatParams;         // x = time (s), y = ground shimmer strength (screen uv), z = shimmer start distance, w = full distance
};
uniform sampler2D sceneTexture;
uniform sampler2D heatOffset;
uniform sampler2D prepassDepth;

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

const float SKY_DEPTH_THRESHOLD = 0.9999;

void main()
{
	vec2 uv = clamp(texCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	vec2 offset = texture(heatOffset, clamp(texCoords * offsetUvScaleClamp.xy, vec2(0.0), offsetUvScaleClamp.zw)).rg;
	if (heatParams.y > 0.0)
	{
		float depth = texture(prepassDepth, uv).r;
		if (depth < SKY_DEPTH_THRESHOLD)
		{
			float viewDist = length(wzGetViewPosition(texCoords, depth, invProjectionMatrix));
			float amount = smoothstep(heatParams.z, heatParams.w, viewDist);
			offset += wzHeatWobble(texCoords * vec2(70.0, 26.0), heatParams.x * 0.8) * heatParams.y * amount;
		}
	}
	vec2 bent = clamp(uv + offset * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	FragColor = vec4(texture(sceneTexture, bent).rgb, 1.0);
}
