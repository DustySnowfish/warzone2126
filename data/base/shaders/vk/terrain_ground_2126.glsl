// Warzone 2126 terrain ground sampling, shared by the GL and Vulkan high-quality terrain shaders.
//
// - Height blending: where ground types meet, the "taller" texture (brighter detail such as
//   stones, since the ground sets ship without height maps) wins, so transitions follow the
//   texture detail instead of a soft cross-fade.
// - Cliffs: steep faces are textured from the side (triplanar), not projected from above,
//   so cliff textures no longer smear into vertical streaks.
// - Anti-tiling: a rotated, larger-scale second sample is blended in by a low-frequency noise,
//   and a gentle large-scale brightness variation breaks up visible repetition.
//
// The including shader defines, before including this file:
//   WZ_TEXARR(tex, uvw)     sample a texture array with the terrain mip bias
//   WZ_GROUND_NO(i)         ground type of splat i (0..3)
//   WZ_GROUND_WEIGHT(i)     splat weight of splat i
//   WZ_UV_GROUND            the base ground uv (model space, -z, x)
//   WZ_POS_MODEL            model space position
// and declares ModelTangentMatrix (model -> tangent space), groundScale, groundTex,
// groundNormal and groundSpecular.

#ifndef WZ_TERRAIN_GROUND_2126_GLSL
#define WZ_TERRAIN_GROUND_2126_GLSL

// How far the brightness of a ground texture lifts its blend weight
#define WZ2126_HEIGHT_BLEND_STRENGTH 0.6
// Width of the height-blend transition (smaller is crisper)
#define WZ2126_HEIGHT_BLEND_DEPTH 0.14
// Strongest mix of the anti-tiling sample
#define WZ2126_ANTI_TILE_MAX 0.4
// Large-scale brightness variation (+/-)
#define WZ2126_MACRO_VARIATION 0.07
// Slope (vertical part of the normal) below which the side projections are blended in
#define WZ2126_TRIPLANAR_SHARPNESS 6.0

float wz2126Hash(vec2 p)
{
	p = fract(p * vec2(0.1031, 0.1030));
	p += dot(p, p.yx + 33.33);
	return fract((p.x + p.y) * p.x);
}

float wz2126Noise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = wz2126Hash(i);
	float b = wz2126Hash(i + vec2(1.0, 0.0));
	float c = wz2126Hash(i + vec2(0.0, 1.0));
	float d = wz2126Hash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec3 wz2126UnpackNormal(vec3 n)
{
	// An all-zero texel means "no normal map": flat
	if (n == vec3(0.0))
	{
		return vec3(0.0, 0.0, 1.0);
	}
	return normalize(n * 2.0 - 1.0);
}

// The triangle's own normal (model space). Vertex normals are smoothed across tiles, which
// makes cliffs look gentler than they are, so texturing decisions use this instead.
vec3 wz2126GeometricNormal()
{
	vec3 p = WZ_POS_MODEL;
	vec3 n = normalize(cross(dFdx(p), dFdy(p)));
	// Point it towards the camera (the cross product's sign depends on the backend's screen axes)
	if (dot(n, cameraPos.xyz - p) < 0.0)
	{
		n = -n;
	}
	return n;
}

// Sets colour, tangent-space normal and gloss of the blended ground.
void wz2126SampleGround(out vec4 outColour, out vec3 outN, out float outGloss)
{
	// The vertex normal in model space is the third row of the model -> tangent matrix
	vec3 Nw = normalize(vec3(ModelTangentMatrix[0][2], ModelTangentMatrix[1][2], ModelTangentMatrix[2][2]));
	vec3 Ng = wz2126GeometricNormal();
	vec3 tw = pow(abs(Ng), vec3(WZ2126_TRIPLANAR_SHARPNESS));
	tw /= max(tw.x + tw.y + tw.z, 1e-5);
	bool sides = tw.y < 0.98;
	vec3 axisSign = vec3(Ng.x < 0.0 ? -1.0 : 1.0, Ng.y < 0.0 ? -1.0 : 1.0, Ng.z < 0.0 ? -1.0 : 1.0);

	vec3 p = WZ_POS_MODEL;
	float macroA = wz2126Noise(p.xz / 1100.0);
	float macroB = wz2126Noise(p.xz / 330.0 + vec2(13.7, 4.1));
	float antiTile = smoothstep(0.25, 0.75, macroA * 0.65 + macroB * 0.35) * WZ2126_ANTI_TILE_MAX;
	const mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);

	vec4 colours[4];
	vec3 normals[4];
	float glosses[4];
	float scores[4];
	float best = -10.0;
	for (int i = 0; i < 4; ++i)
	{
		uint groundNo = WZ_GROUND_NO(i);
		float layer = float(groundNo);
		float s = groundScale[groundNo / 4u][groundNo % 4u];
		vec3 uv = vec3(WZ_UV_GROUND * s, layer);
		vec4 col = WZ_TEXARR(groundTex, uv);
		vec3 uv2 = vec3(rot * (WZ_UV_GROUND * s) * 0.43 + vec2(0.37, 0.71), layer);
		col = mix(col, WZ_TEXARR(groundTex, uv2), antiTile);
		vec3 n = wz2126UnpackNormal(WZ_TEXARR(groundNormal, uv).xyz);
		float gloss = WZ_TEXARR(groundSpecular, uv).r;
		if (sides)
		{
			vec3 uvx = vec3(p.zy * s, layer);
			vec3 uvz = vec3(p.xy * s, layer);
			col = col * tw.y + WZ_TEXARR(groundTex, uvx) * tw.x + WZ_TEXARR(groundTex, uvz) * tw.z;
			vec3 nx = wz2126UnpackNormal(WZ_TEXARR(groundNormal, uvx).xyz);
			vec3 nz = wz2126UnpackNormal(WZ_TEXARR(groundNormal, uvz).xyz);
			nx.x *= axisSign.x;
			nz.x *= -axisSign.z;
			// Whiteout blend of the side normals into model space
			vec3 wx = vec3(nx.xy + Nw.zy, abs(nx.z) * Nw.x).zyx;
			vec3 wz = vec3(nz.xy + Nw.xy, abs(nz.z) * Nw.z);
			vec3 sideN = ModelTangentMatrix * normalize(wx * tw.x + wz * tw.z);
			n = normalize(n * tw.y + sideN * (tw.x + tw.z));
			gloss = gloss * tw.y + WZ_TEXARR(groundSpecular, uvx).r * tw.x + WZ_TEXARR(groundSpecular, uvz).r * tw.z;
		}
		colours[i] = col;
		normals[i] = n;
		glosses[i] = gloss;
		float w = WZ_GROUND_WEIGHT(i);
		float height = dot(col.rgb, vec3(0.299, 0.587, 0.114));
		scores[i] = (w > 0.001) ? w + height * WZ2126_HEIGHT_BLEND_STRENGTH : -10.0;
		best = max(best, scores[i]);
	}

	outColour = vec4(0.0);
	outN = vec3(0.0);
	outGloss = 0.0;
	float total = 0.0;
	for (int i = 0; i < 4; ++i)
	{
		float w = max(scores[i] - (best - WZ2126_HEIGHT_BLEND_DEPTH), 0.0);
		outColour += colours[i] * w;
		outN += normals[i] * w;
		outGloss += glosses[i] * w;
		total += w;
	}
	total = max(total, 1e-5);
	outColour /= total;
	outN /= total;
	outGloss /= total;
	outColour.rgb *= 1.0 + (macroA - 0.5) * 2.0 * WZ2126_MACRO_VARIATION;
}

// Decal opacity factor: 1 on flat and gentle ground, fading to 0 on steep faces
float wz2126DecalSlopeFade()
{
	float up = abs(wz2126GeometricNormal().y);
	return smoothstep(0.45, 0.75, up);
}

#endif // WZ_TERRAIN_GROUND_2126_GLSL
