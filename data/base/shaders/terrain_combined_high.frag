// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.20 - 1.50 core.)

// Texture arrays are supported in:
// 1. core OpenGL 3.0+ (and earlier with EXT_texture_array)
//#if (!defined(GL_ES) && (__VERSION__ < 130))
//#extension GL_EXT_texture_array : require // only enable this on OpenGL < 3.0
//#endif
// 2. OpenGL ES 3.0+
#if (defined(GL_ES) && (__VERSION__ < 300))
#error Unsupported version of GLES
#endif

#include "terrain_combined.glsl"

// constants overridden by WZ when loading shaders (do not modify here in the shader source!)
#define WZ_SHADOW_MODE 1
#define WZ_SHADOW_FILTER_SIZE 3
#define WZ_SHADOW_CASCADES_COUNT 3
#define WZ_POINT_LIGHT_ENABLED 0
//


#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#define texture2DArray(tex,coord,bias) texture(tex,coord,bias)
#else
#define texture(tex,uv,bias) texture2D(tex,uv,bias)
#endif

uniform sampler2D lightmap_tex;

// ground texture arrays. layer = ground type
uniform sampler2DArray groundTex;
uniform sampler2DArray groundNormal;
uniform sampler2DArray groundSpecular;
uniform sampler2DArray groundHeight;

// array of scales for ground textures, encoded in mat4. scale_i = groundScale[i/4][i%4]

// decal texture arrays. layer = decal tile
uniform sampler2DArray decalTex;
uniform sampler2DArray decalNormal;
uniform sampler2DArray decalSpecular;
uniform sampler2DArray decalHeight;

// shadow map
uniform sampler2DArrayShadow shadowMap;
uniform sampler2D terrainDeform; // Warzone 2126: crater depth
float wzCraterHash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float wzCraterNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(wzCraterHash(i), wzCraterHash(i + vec2(1.0, 0.0)), f.x), mix(wzCraterHash(i + vec2(0.0, 1.0)), wzCraterHash(i + vec2(1.0, 1.0)), f.x), f.y);
}


// sun light colors/intensity:



// fog

in vec2 uvLightmap;
in vec2 uvDecal;
in vec2 uvGround;
flat in int tile;
flat in uvec4 fgrounds;
in vec4 fgroundWeights;
// Light in tangent space:
in vec3 groundLightDir;
in vec3 groundHalfVec;
in mat2 decal2groundMat2;


in mat3 ModelTangentMatrix;
// For Shadows
in vec3 posModelSpace;
in vec3 posViewSpace;

#ifdef NEWGL
out vec4 FragColor;
#else
// Uses gl_FragColor
#endif

#include "shadow_mapping.glsl"
#include "light.glsl"
#include "pointlights.frag"

#define WZ_TEXARR(tex, uvw) texture2DArray(tex, uvw, WZ_MIP_LOAD_BIAS)
#define WZ_GROUND_NO(i) fgrounds[i]
#define WZ_GROUND_WEIGHT(i) fgroundWeights[i]
#define WZ_UV_GROUND uvGround
#define WZ_POS_MODEL posModelSpace
#include "terrain_ground_2126.glsl"

vec3 getGroundUv(int i) {
	uint groundNo = fgrounds[i];
	return vec3(uvGround * groundScale[groundNo/4u][groundNo%4u], float(groundNo));
}

struct BumpData {
	vec4 color;
	vec3 N;
	float gloss;
};

void getGroundBM(int i, inout BumpData res) {
	vec3 uv = getGroundUv(i);
	float w = fgroundWeights[i];
	res.color += texture2DArray(groundTex, uv, WZ_MIP_LOAD_BIAS) * w;
	vec3 N = texture2DArray(groundNormal, uv, WZ_MIP_LOAD_BIAS).xyz;
	N = mix(normalize(N * 2.f - 1.f), vec3(0.f,0.f,1.f), vec3(float(N == vec3(0.f,0.f,0.f))));
	res.N += N * w;
	res.gloss += texture2DArray(groundSpecular, uv, WZ_MIP_LOAD_BIAS).r * w;
}

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return a + b;
}

vec4 doBumpMapping(BumpData b, vec3 groundLightDir, vec3 groundHalfVec) {
	vec3 L = normalize(groundLightDir);
	float diffuseFactor = lambertTerm(b.N, L); // diffuse lighting
	float visibility = getShadowVisibility(posModelSpace, posViewSpace, diffuseFactor, 0.001f);
	diffuseFactor = min(diffuseFactor, visibility*diffuseFactor);

	float specularFactor = blinnTerm(b.N, normalize(groundHalfVec), b.gloss, 16.f);

	vec4 lightmap_vec4 = texture(lightmap_tex, uvLightmap, 0.f);

	float adjustedTileBrightness = pow(lightmap_vec4.a, 2.f-lightmap_vec4.a); // ... * tile brightness / ambient occlusion (stored in lightmap.a)

	vec4 adjustedAmbientLight = ambientLight * adjustedTileBrightness;
	vec4 light = adjustedAmbientLight + diffuseLight * diffuseFactor;
	light.rgb = blendAddEffectLighting(light.rgb, (lightmap_vec4.rgb / 1.4f)); // additive color (from environmental point lights / effects)

	vec4 light_spec = specularLight * specularFactor * diffuseFactor;
	light_spec.rgb = blendAddEffectLighting(light_spec.rgb, (lightmap_vec4.rgb / 2.5f)); // additive color (from environmental point lights / effects)
	light_spec *= (b.gloss * b.gloss);

	vec4 res = (b.color*light) + light_spec;

#if WZ_POINT_LIGHT_ENABLED == 1
	// point lights
	vec2 clipSpaceCoord = gl_FragCoord.xy / vec2(float(viewportWidth), float(viewportHeight));
	// The light loop works in world space, so the tangent frame is inverted once here.
	// The frame is built orthonormal, so its transpose stands in for its inverse, and the third column of that is the vertex normal.
	mat3 tangentToWorld = transpose(ModelTangentMatrix);
	res += iterateOverAllPointLights(clipSpaceCoord, posModelSpace, tangentToWorld * b.N, tangentToWorld[2], tangentToWorld * normalize(groundHalfVec - groundLightDir), b.color, b.gloss);
#endif

	// Calculate water murkiness based on non-constant-density-fog, see https://iquilezles.org/articles/fog/
	vec3 c2p = normalize(posModelSpace - cameraPos.xyz); // camera to point vector
	vec3 c = normalize(cameraPos.xyz);
	float inscatter = 0.007; // in-scattering
	float extinction = 0.001; // extinction
	float murkyFactor = exp(-c.y * extinction) * (1.0 - exp( -(posModelSpace.y+40.0) * c2p.y * extinction)) / (-c2p.y * extinction);
	float murkiness = inscatter * murkyFactor;
	float waterDeep = 0.003 * murkyFactor;
	murkiness = clamp(murkiness, 0.0, 1.0);
	waterDeep = clamp(waterDeep, 0.0, 1.0);
	res.rgb = mix(res.rgb, (vec3(0.315,0.425,0.475)*vec3(1.0-waterDeep)), murkiness);

	return vec4(res.rgb * lightmap_vec4.a, b.color.a);
}

vec4 main_bumpMapping() {
	BumpData bump;
	// Warzone 2126: height blending, cliff side projection and anti-tiling
	wz2126SampleGround(bump.color, bump.N, bump.gloss);

	if (tile >= 0) {
		vec3 uv = vec3(uvDecal, float(tile));
		vec4 decalColor = texture2DArray(decalTex, uv, WZ_MIP_LOAD_BIAS);
		float a = decalColor.a;
		// Warzone 2126: painted decals stretch on steep faces; let the side-projected ground show there
		a *= wz2126DecalSlopeFade();
		// blend color, normal and gloss with ground ones based on alpha
		bump.color = (1.f - a)*bump.color + a*vec4(decalColor.rgb, 1.f);
		vec3 n = texture2DArray(decalNormal, uv, WZ_MIP_LOAD_BIAS).xyz;
		vec3 n_normalized = normalize(n * 2.f - 1.f);
		n = mix(vec3(n_normalized.xy * decal2groundMat2, n_normalized.z), vec3(0.f,0.f,1.f), vec3(float(n == vec3(0.f,0.f,0.f))));
		bump.N = (1.f - a)*bump.N + a*n;
		bump.gloss = (1.f - a)*bump.gloss + a*texture2DArray(decalSpecular, uv, WZ_MIP_LOAD_BIAS).r;
	}
	// Every lighting term below assumes unit length - but a weighted sum of unit normals is shorter than unit when they disagree (so normalize)
	// Warzone 2126: deep craters stay dark, churned and rough after the scorch mark fades
	{
		float crater = texture(terrainDeform, uvLightmap, 0.f).r;
		if (crater > 0.02)
		{
			float k = smoothstep(0.04, 0.45, crater);
			vec2 cp = posModelSpace.xz * 0.045;
			float n = wzCraterNoise(cp);
			float n2 = wzCraterNoise(cp * 2.7 + 3.1);
			float clod = mix(n, n2, 0.5);
			vec3 churned = bump.color.rgb * vec3(0.56, 0.5, 0.46) * (0.72 + 0.5 * clod);
			bump.color.rgb = mix(bump.color.rgb, churned, k * 0.9);
			bump.gloss *= 1.0 - 0.7 * k;
			bump.N = mix(bump.N, normalize(bump.N + vec3(n - 0.5, n2 - 0.5, 0.0) * 0.9), k);
		}
	}
	// Warzone 2126 weather: wet ground darkens and shines (most in hollows); snow settles on flat ground
	if (weatherGround > 0.0)
	{
		float hollow = clamp(texture(terrainDeform, uvLightmap, 0.f).r * 3.0, 0.0, 1.0);
		float wet = weatherGround * (0.75 + 0.25 * hollow);
		bump.color.rgb *= 1.0 - 0.25 * wet;
		bump.gloss = mix(bump.gloss, 1.0, wet * (0.55 + 0.45 * hollow));
	}
	else if (weatherGround < 0.0)
	{
		vec3 worldN = normalize(transpose(ModelTangentMatrix) * normalize(bump.N));
		float drift = wzCraterNoise(posModelSpace.xz * 0.012);
		float cover = clamp(-weatherGround * smoothstep(0.55, 0.9, worldN.y) * (0.6 + 0.6 * drift), 0.0, 1.0);
		bump.color.rgb = mix(bump.color.rgb, vec3(0.84, 0.87, 0.92), cover);
		bump.gloss = mix(bump.gloss, 0.3, cover);
	}
	bump.N = normalize(bump.N);
	return doBumpMapping(bump, groundLightDir, groundHalfVec);
}

void main()
{
	vec4 fragColor = main_bumpMapping();

	#ifdef NEWGL
	FragColor = fragColor;
	#else
	gl_FragColor = fragColor;
	#endif
}
