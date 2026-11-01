// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.20 - 1.50 core.)

// constants overridden by WZ when loading shaders (do not modify here in the shader source!)
#include "terrain_water_high.glsl"
#define WZ_SHADOW_MODE 1
#define WZ_SHADOW_FILTER_SIZE 3
#define WZ_SHADOW_CASCADES_COUNT 3
#define WZ_POINT_LIGHT_ENABLED 0
//


uniform sampler2DArray tex;
uniform sampler2DArray tex_nm;
uniform sampler2DArray tex_sm;
uniform sampler2D lightmap_tex;

// shadow map
uniform sampler2DArrayShadow shadowMap;


// light colors/intensity:



#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#define FRAGMENT_INPUT in
#define texture2DArray(tex,coord,bias) texture(tex,coord,bias)
#else
#define texture(tex,uv,bias) texture2D(tex,uv,bias)
#define FRAGMENT_INPUT varying
#endif

FRAGMENT_INPUT vec4 uv1_uv2;
FRAGMENT_INPUT vec2 uvLightmap;
FRAGMENT_INPUT float depth;
FRAGMENT_INPUT float waterDepth;
// light in modelSpace:
FRAGMENT_INPUT vec3 lightDir;
FRAGMENT_INPUT vec3 eyeVec;
FRAGMENT_INPUT vec3 halfVec;
FRAGMENT_INPUT float fresnel;
FRAGMENT_INPUT float fresnel_alpha;

// For Shadows
FRAGMENT_INPUT vec3 posModelSpace;
FRAGMENT_INPUT vec3 posViewSpace;

#ifdef NEWGL
out vec4 FragColor;
#else
#define FragColor gl_FragColor
#endif

#include "shadow_mapping.glsl"
#include "light.glsl"
#include "pointlights.frag"
#include "water_normals.glsl"
uniform sampler2D TextureEnvironment; // Warzone 2126: map environment (sky reflection)
#include "water_2126.glsl"

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return a + b; // (Warzone 2126: not clamped, so sun glints can bloom)
}

vec4 main_bumpMapping()
{
	vec2 uv1 = uv1_uv2.xy;
	vec2 uv2 = uv1_uv2.zw;

	vec3 N = wzWaterModelNormal(tex_nm, uv1, uv2, timeSec, WZ_MIP_LOAD_BIAS);

	float noise = texture2DArray(tex, vec3(uv1, 0.0), WZ_MIP_LOAD_BIAS).r * texture2DArray(tex, vec3(uv2, 1.0), WZ_MIP_LOAD_BIAS).r;
	vec3 waterColor = vec3(0.18,0.33,0.42);
	// Water is a mirror rather than a rough surface, so a point light on it should be mostly highlight.
	// (But capped to keep the broader shape from washing out.)
	const float waterGloss = 0.5f;

	// Warzone 2126: the surface from the true depth (water_2126.glsl)
	float diffuseFactor = lambertTerm(N, lightDir);
	float visibility = getShadowVisibility(posModelSpace, posViewSpace, max(diffuseFactor, 0.05), 0.0f);
	vec4 finalColor = wz2126WaterShade(N, normalize(eyeVec), normalize(lightDir), waterDepth, posModelSpace, timeSec, visibility,
		clamp(sqrt(max(noise, 0.0)) * 1.4, 0.0, 1.0), envOn, ambientLight.rgb, diffuseLight.rgb, specularLight.rgb);

	vec4 lightmap = texture(lightmap_tex, uvLightmap, 0.0);
	finalColor.rgb = blendAddEffectLighting(finalColor.rgb, (lightmap.rgb / 1.5f)); // additive color (from environmental point lights / effects)
	finalColor.rgb *= lightmap.a; // ... * tile brightness / ambient occlusion (stored in lightmap.a);

#if WZ_POINT_LIGHT_ENABLED == 1
	// Water needs no tangent frame: its normal is already in model space and the surface it sits on is flat,
	// so the geometric normal is just up.
	vec2 clipSpaceCoord = gl_FragCoord.xy / vec2(float(viewportWidth), float(viewportHeight));
	finalColor.rgb += iterateOverAllPointLights(clipSpaceCoord, posModelSpace, N, vec3(0.f, 1.f, 0.f), normalize(eyeVec), vec4(waterColor, 1.f), waterGloss).rgb;
#endif
	return finalColor;
}

void main()
{
	vec4 fragColor = main_bumpMapping();

	FragColor = fragColor;
}
