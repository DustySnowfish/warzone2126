#version 450

// Warzone 2126 heat shimmer: the lit scene sampled through the heat offset field, plus a faint
// shimmer over distant hot ground

#include "view_position.glsl"
#include "heat_common.glsl"

layout(std140, set = 0, binding = 0) uniform cbuffer {
	mat4 invProjectionMatrix;
	vec4 uvScaleClamp;       // scene read
	vec4 offsetUvScaleClamp; // offset field read
	vec4 heatParams;         // x = time (s), y = ground shimmer strength (screen uv), z = shimmer start distance, w = full distance
};

layout(set = 1, binding = 0) uniform sampler2D sceneTexture;
layout(set = 1, binding = 1) uniform sampler2D heatOffset;
layout(set = 1, binding = 2) uniform sampler2D prepassDepth;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

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
