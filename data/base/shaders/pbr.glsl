// Warzone 2126: physically based shading for glTF (PBR) models.
//
// Metal/roughness model (GGX distribution, Smith-Schlick geometry, Schlick
// Fresnel), as used by glTF 2.0. Lighting is done in linear space and the
// result is returned in linear space; the caller converts back for output.
//
// Material inputs:
//   base colour : diffuse texture (sRGB), team colour applied by multiplication
//                 under the team colour mask (artists paint those areas mid-grey)
//   ORM         : TextureSpecular slot. R = ambient occlusion, G = roughness,
//                 B = metallic (glTF packing)
//
// Requires (from the including shader): the globaluniforms block (diffuse,
// ambient, cameraPos), the meshuniforms block (emissiveFactor), lightDir,
// posModelSpace, posViewSpace, uvLightmap, getShadowVisibility(), lightmap_tex,
// TextureEnvironment.
//
// Image-based lighting: TextureEnvironment holds the map's environment (sky and
// ground) as lat-long maps, RGBM-encoded: WZ_ENV_SPECULAR_LEVELS levels prefiltered
// for roughness 0..1, then one diffuse irradiance level, each WZ_ENV_LEVEL_HEIGHT
// texels high. emissiveFactor.w is 1 when it is bound.

#ifndef WZ_PBR_GLSL
#define WZ_PBR_GLSL

#define WZ_PI 3.14159265
#define WZ_ENV_SPECULAR_LEVELS 6.0
#define WZ_ENV_LEVELS 7.0
#define WZ_ENV_LEVEL_HEIGHT 32.0
#define WZ_ENV_RGBM_RANGE 8.0

// Direction (world space, +Y up) to the texture coordinate inside one level.
vec2 wzEnvUV(vec3 d, float level)
{
	float u = atan(d.z, d.x) * (0.5 / WZ_PI) + 0.5;
	float v = acos(clamp(d.y, -1.0, 1.0)) / WZ_PI;
	float halfTexel = 0.5 / WZ_ENV_LEVEL_HEIGHT;
	v = clamp(v, halfTexel, 1.0 - halfTexel);
	return vec2(u, (level + v) / WZ_ENV_LEVELS);
}

vec3 wzEnvSample(vec3 d, float level)
{
	vec4 t = texture(TextureEnvironment, wzEnvUV(d, level), 0.0);
	return t.rgb * t.a * WZ_ENV_RGBM_RANGE;
}

vec3 wzEnvSpecular(vec3 R, float roughness)
{
	float level = clamp(roughness, 0.0, 1.0) * (WZ_ENV_SPECULAR_LEVELS - 1.0);
	float l0 = floor(level);
	float l1 = min(l0 + 1.0, WZ_ENV_SPECULAR_LEVELS - 1.0);
	return mix(wzEnvSample(R, l0), wzEnvSample(R, l1), level - l0);
}

vec3 wzEnvIrradiance(vec3 N)
{
	return wzEnvSample(N, WZ_ENV_SPECULAR_LEVELS);
}

// Split-sum environment BRDF, analytic fit (Karis 2014), so no lookup texture is needed.
vec3 wzEnvBRDF(vec3 F0, float roughness, float NdotV)
{
	vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
	vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
	vec4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
	vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
	return F0 * AB.x + AB.y;
}

vec3 wzSRGBToLinear(vec3 c)
{
	return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 wzLinearToSRGB(vec3 c)
{
	// Not clamped above: with HDR rendering, values over 1.0 are kept (and tonemapped later);
	// without it the 8/10-bit render target clamps on write.
	return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}

float wzDistributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / max(WZ_PI * d * d, 1e-6);
}

float wzGeometrySchlickGGX(float NdotX, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return NdotX / (NdotX * (1.0 - k) + k);
}

vec3 wzFresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 wzFresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Base colour in sRGB with team colour applied.
vec3 wzPBRBaseColour(vec3 baseSRGB, float teamMask, vec3 teamColourSRGB)
{
	// Mid-grey (0.5) under the mask becomes exactly the team colour; wear and
	// dirt painted into those areas still darken it.
	return baseSRGB * mix(vec3(1.0), teamColourSRGB * 2.0, teamMask);
}

// Sun + ambient (+ lightmap) lighting for one fragment, linear output.
//   N      : shading normal (world space, normalised)
//   orm    : sampled ORM texel
vec3 wzShadePBR(vec3 baseSRGB, vec3 orm, vec3 N)
{
	vec3 albedo = wzSRGBToLinear(baseSRGB);
	float ao = orm.r;
	float roughness = clamp(orm.g, 0.045, 1.0);
	float metallic = clamp(orm.b, 0.0, 1.0);

	vec3 L = normalize(lightDir);
	vec3 V = normalize(cameraPos.xyz - posModelSpace);
	vec3 H = normalize(L + V);
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 1e-4);
	float NdotH = max(dot(N, H), 0.0);
	float HdotV = max(dot(H, V), 0.0);

	vec3 F0 = mix(vec3(0.04), albedo, metallic);

	// Direct sun light. The engine's sun colour is scaled by pi so a white
	// diffuse surface facing the sun matches the classic shading brightness.
	float visibility = getShadowVisibility(posModelSpace, posViewSpace, NdotL, 0.0002f);
	vec3 sunRadiance = wzSRGBToLinear(diffuse.rgb) * WZ_PI;
	vec3 F = wzFresnelSchlick(HdotV, F0);
	float D = wzDistributionGGX(NdotH, roughness);
	float G = wzGeometrySchlickGGX(NdotV, roughness) * wzGeometrySchlickGGX(NdotL, roughness);
	vec3 specularBRDF = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
	vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
	vec3 direct = (kD * albedo / WZ_PI + specularBRDF) * sunRadiance * NdotL * visibility;

	// Ambient: the engine's ambient colour plus the terrain lightmap near the
	// ground, used as a uniform environment until image-based lighting arrives.
	vec4 lightmapTexel = texture(lightmap_tex, uvLightmap.xy, 0.f);
	float lightmapFactor = 1.0 - (clamp(uvLightmap.z, 0.0, 300.0) / 300.0);
	vec3 ambientSRGB = min(ambient.rgb + (lightmapTexel.rgb * lightmapFactor) / 3.0, vec3(1.0));
	vec3 ambientLinear = wzSRGBToLinear(ambientSRGB);
	vec3 ambientTerm;
	if (emissiveFactor.w > 0.5)
	{
		// Image-based lighting. The environment is normalised so that light from straight
		// up has unit brightness; the engine's ambient level (with the terrain lightmap
		// near the ground) sets the overall strength, so it matches the classic shading.
		float ambientLevel = dot(ambientLinear, vec3(0.2126, 0.7152, 0.0722)) * 2.0;
		vec3 R = reflect(-V, N);
		vec3 kDa = (vec3(1.0) - wzFresnelSchlickRoughness(NdotV, F0, roughness)) * (1.0 - metallic);
		vec3 ambientDiffuse = kDa * albedo * wzEnvIrradiance(N);
		vec3 ambientSpecular = wzEnvSpecular(R, roughness) * wzEnvBRDF(F0, roughness, NdotV);
		ambientTerm = (ambientDiffuse + ambientSpecular) * ambientLevel * ao;
	}
	else
	{
		// No environment: a flat ambient plus a brighter sky term for reflections
		vec3 skyLinear = ambientLinear + wzSRGBToLinear(diffuse.rgb) * 0.35;
		vec3 Fa = wzFresnelSchlickRoughness(NdotV, F0, roughness);
		vec3 kDa = (vec3(1.0) - Fa) * (1.0 - metallic);
		vec3 ambientDiffuse = kDa * albedo * ambientLinear * 2.0;
		vec3 ambientSpecular = Fa * skyLinear * (1.0 - roughness * 0.7);
		ambientTerm = (ambientDiffuse + ambientSpecular) * ao;
	}

	return direct + ambientTerm;
}

#endif // WZ_PBR_GLSL
