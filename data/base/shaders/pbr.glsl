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
// ambient, cameraPos), lightDir, posModelSpace, posViewSpace, uvLightmap,
// getShadowVisibility(), lightmap_tex.

#ifndef WZ_PBR_GLSL
#define WZ_PBR_GLSL

#define WZ_PI 3.14159265

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
	// A brighter sky term for reflections, so metals are not black without an environment map
	vec3 skyLinear = ambientLinear + wzSRGBToLinear(diffuse.rgb) * 0.35;
	vec3 Fa = wzFresnelSchlickRoughness(NdotV, F0, roughness);
	vec3 kDa = (vec3(1.0) - Fa) * (1.0 - metallic);
	vec3 ambientDiffuse = kDa * albedo * ambientLinear * 2.0;
	vec3 ambientSpecular = Fa * skyLinear * (1.0 - roughness * 0.7);
	vec3 ambientTerm = (ambientDiffuse + ambientSpecular) * ao;

	return direct + ambientTerm;
}

#endif // WZ_PBR_GLSL
