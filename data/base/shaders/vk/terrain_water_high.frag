#version 450

#include "terrain_water_high.glsl"

layout (constant_id = 1) const uint WZ_SHADOW_MODE = 1;
layout (constant_id = 2) const uint WZ_SHADOW_FILTER_SIZE = 5;
layout (constant_id = 3) const uint WZ_SHADOW_CASCADES_COUNT = 3;
layout (constant_id = 4) const uint WZ_POINT_LIGHT_ENABLED = 0;

// Set 2 rather than 1, because the point light block takes a set of its own ahead of these
layout(set = 2, binding = 0) uniform sampler2DArray tex;
layout(set = 2, binding = 1) uniform sampler2DArray tex_nm;
layout(set = 2, binding = 2) uniform sampler2DArray tex_sm;
layout(set = 2, binding = 3) uniform sampler2D lightmap_tex;
// depth map
layout(set = 2, binding = 4) uniform sampler2DArrayShadow shadowMap;
// Warzone 2126: map environment for the sky reflection
layout(set = 2, binding = 5) uniform sampler2D TextureEnvironment;

layout(location = 1) in vec4 uv1_uv2;
layout(location = 2) in vec2 uvLightmap;
layout(location = 3) in vec3 lightDir;
layout(location = 4) in vec3 halfVec;
layout(location = 5) in float depth;
layout(location = 6) in vec3 eyeVec;
layout(location = 7) in float fresnel;
layout(location = 8) in float fresnel_alpha;
layout(location = 9) in FragData frag;
layout(location = 11) in float waterDepth;

layout(location = 0) out vec4 FragColor;

#include "shadow_mapping.glsl"
#include "light.glsl"
#include "pointlights.glsl"
#include "water_normals.glsl"
#include "water_2126.glsl"

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return a + b; // (Warzone 2126: not clamped, so sun glints can bloom)
}

vec4 main_bumpMapping()
{
	vec2 uv1 = uv1_uv2.xy;
	vec2 uv2 = uv1_uv2.zw;

	vec3 N = wzWaterModelNormal(tex_nm, uv1, uv2, timeSec, WZ_MIP_LOAD_BIAS);

	float noise = texture(tex, vec3(uv1, 0.0), WZ_MIP_LOAD_BIAS).r * texture(tex, vec3(uv2, 1.0), WZ_MIP_LOAD_BIAS).r;
	vec3 waterColor = vec3(0.18,0.33,0.42);
	// Water is a mirror rather than a rough surface, so a point light on it should be mostly highlight.
	// (But capped to keep the broader shape from washing out.)
	const float waterGloss = 0.5f;

	// Warzone 2126: the surface from the true depth (water_2126.glsl)
	float diffuseFactor = lambertTerm(N, lightDir);
	float visibility = getShadowVisibility(frag.posModelSpace, frag.posViewSpace, max(diffuseFactor, 0.05), 0.0f);
	vec4 finalColor = wz2126WaterShade(N, normalize(eyeVec), normalize(lightDir), waterDepth, frag.posModelSpace, timeSec, visibility,
		clamp(sqrt(max(noise, 0.0)) * 1.4, 0.0, 1.0), envOn, ambientLight.rgb, diffuseLight.rgb, specularLight.rgb, rainOnWater);

	vec4 lightmap = texture(lightmap_tex, uvLightmap, 0.0);
	finalColor.rgb = blendAddEffectLighting(finalColor.rgb, (lightmap.rgb / 1.5f)); // additive color (from environmental point lights / effects)
	finalColor.rgb *= vec3(lightmap.a); // ... * tile brightness / ambient occlusion (stored in lightmap.a);

	if (WZ_POINT_LIGHT_ENABLED == 1)
	{
		// Water needs no tangent frame: its normal is already in model space and the surface it sits on is flat,
		// so the geometric normal is just up.
		vec2 clipSpaceCoord = gl_FragCoord.xy / vec2(float(viewportWidth), float(viewportHeight));
		finalColor.rgb += iterateOverAllPointLights(clipSpaceCoord, frag.posModelSpace, N, vec3(0.f, 1.f, 0.f), normalize(eyeVec), vec4(waterColor, 1.f), waterGloss).rgb;
	}

	return finalColor;
}

void main()
{
	vec4 fragColor = main_bumpMapping();

	FragColor = fragColor;
}
