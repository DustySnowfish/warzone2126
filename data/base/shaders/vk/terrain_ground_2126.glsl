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
// Thickness of the colour bands on cliff faces (world units; a tile is 128)
#define WZ2126_STRATA_THICKNESS 13.0

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

// Smooth ground boundaries (terrain realism A1): the four strongest ground types here and their
// weights, from the blurred per-vertex ground masks (WZ_GROUND_MASK(k, uv) samples mask texture k,
// four ground types per texture). The sample position is warped by a low-frequency noise and each
// ground type gets its own fine noise, so edges meander naturally instead of following the tile
// grid. Returns false when there are no masks (the tile-corner weights are used then).
#ifdef WZ_GROUND_MASK
bool wz2126GroundsFromMasks(vec2 uvMask, vec2 texSize, out uvec4 grounds, out vec4 weights)
{
	vec3 p = WZ_POS_MODEL;
	// Warp of up to about 0.45 tiles, in two octaves
	vec2 warp = vec2(wz2126Noise(p.xz / 220.0 + vec2(3.1, 7.7)), wz2126Noise(p.xz / 220.0 + vec2(11.3, 1.9))) - 0.5;
	warp += (vec2(wz2126Noise(p.xz / 70.0 + vec2(5.3, 2.2)), wz2126Noise(p.xz / 70.0 + vec2(9.1, 4.4))) - 0.5) * 0.45;
	vec2 uv = uvMask + warp * (0.75 / texSize);
	vec4 m0 = WZ_GROUND_MASK(0, uv);
	vec4 m1 = WZ_GROUND_MASK(1, uv);
	vec4 m2 = WZ_GROUND_MASK(2, uv);
	float m[12] = float[12](m0.r, m0.g, m0.b, m0.a, m1.r, m1.g, m1.b, m1.a, m2.r, m2.g, m2.b, m2.a);
	float total = 0.0;
	for (int g = 0; g < 12; ++g)
	{
		total += m[g];
	}
	if (total < 0.5)
	{
		return false; // no masks bound (all zero)
	}
	// Fine per-ground noise roughens the edges: tufts and fingers rather than smooth curves
	for (int g = 0; g < 12; ++g)
	{
		if (m[g] > 0.004)
		{
			float n = wz2126Noise(p.xz / 34.0 + vec2(float(g) * 17.13, float(g) * 5.71)) * 0.6
				+ wz2126Noise(p.xz / 13.0 + vec2(float(g) * 3.37, float(g) * 9.91)) * 0.4;
			m[g] = max(m[g] + (n - 0.5) * 0.45 * min(1.0, m[g] * 4.0), 0.0);
		}
	}
	// The four strongest
	grounds = uvec4(0u);
	weights = vec4(0.0);
	for (int k = 0; k < 4; ++k)
	{
		int bestG = 0;
		float bestM = -1.0;
		for (int g = 0; g < 12; ++g)
		{
			if (m[g] > bestM)
			{
				bestM = m[g];
				bestG = g;
			}
		}
		grounds[k] = uint(bestG);
		weights[k] = max(bestM, 0.0);
		m[bestG] = -2.0;
	}
	float s = weights.x + weights.y + weights.z + weights.w;
	weights /= max(s, 1e-4);
	return true;
}
#endif

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

	vec4 topColour = vec4(0.0); // the strongest ground seen from above (for dust on cliff ledges)
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
		if (i == 0)
		{
			topColour = col;
		}
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
	// Micro relief (terrain realism D2): wind ripples on sand and snow, rills down slopes of natural
	// ground; detail normals only, faded out with distance (they would only shimmer far away)
	{
		int rippleBits = int(groundFlags2126.x + 0.5), rillBits = int(groundFlags2126.y + 0.5);
		float rippleW = 0.0, rillW = 0.0, wsum = 0.0;
		for (int i = 0; i < 4; ++i)
		{
			float w = max(WZ_GROUND_WEIGHT(i), 0.0);
			int g = int(WZ_GROUND_NO(i));
			rippleW += w * float((rippleBits >> g) & 1);
			rillW += w * float((rillBits >> g) & 1);
			wsum += w;
		}
		rippleW /= max(wsum, 1e-4);
		rillW /= max(wsum, 1e-4);
		float nearK = 1.0 - smoothstep(1800.0, 4200.0, length(cameraPos.xyz - p));
		float up = abs(Ng.y);
		vec3 tilt = vec3(0.0);
		if (rippleW > 0.01 && nearK > 0.0)
		{
			// The wind direction drifts slowly across the map; crests wander
			float dirA = wz2126Noise(p.xz / 2600.0) * 1.2 + 0.6;
			vec2 wdir = vec2(cos(dirA), sin(dirA));
			float u = dot(p.xz, wdir) + (wz2126Noise(p.xz / 90.0) - 0.5) * 40.0 + (wz2126Noise(p.xz / 23.0 + 3.3) - 0.5) * 7.0;
			float ph = u * (6.2831853 / 22.0);
			float slopeU = cos(ph) + 0.35 * cos(2.0 * ph); // gentle windward side, steeper lee
			float amt = rippleW * nearK * smoothstep(0.75, 0.95, up) * smoothstep(0.2, 0.55, wz2126Noise(p.xz / 160.0 + 7.1)) * 0.35;
			tilt.xz -= wdir * slopeU * amt;
		}
		if (rillW > 0.01 && nearK > 0.0)
		{
			// Narrow channels running straight down moderate slopes (not flats, not cliffs)
			vec2 down = Ng.xz / max(length(Ng.xz), 1e-4);
			vec2 across = vec2(-down.y, down.x);
			float onSlope = (1.0 - smoothstep(0.93, 0.985, up)) * smoothstep(0.55, 0.68, up);
			float v = dot(p.xz, across) + (wz2126Noise(p.xz / 70.0 + 1.7) - 0.5) * 26.0;
			float ph = v * (6.2831853 / 34.0);
			float sp = max(sin(ph), 0.0);
			float groove = pow(sp, 6.0);
			float dGroove = 6.0 * pow(sp, 5.0) * cos(ph);
			float amt = rillW * nearK * onSlope * smoothstep(0.35, 0.7, wz2126Noise(p.xz / 45.0 + 9.1)) * 0.45;
			tilt.xz += across * dGroove * amt;
			outColour.rgb *= 1.0 - groove * amt * 0.35;
		}
		if (dot(tilt, tilt) > 0.0)
		{
			outN = normalize(outN + ModelTangentMatrix * tilt);
		}
	}
	outColour.rgb *= 1.0 + (macroA - 0.5) * 2.0 * WZ2126_MACRO_VARIATION;

	// Cliffs (terrain realism C1): on steep faces, horizontal strata in slightly different shades
	// with wavering edges, and the ground from above settling on the ledges that face up
	float steep = 1.0 - smoothstep(0.5, 0.85, abs(Ng.y));
	if (steep > 0.0)
	{
		float wave = (wz2126Noise(p.xz / 300.0) - 0.5) * 14.0 + (wz2126Noise(p.xz / 70.0 + vec2(4.7, 1.3)) - 0.5) * 4.0;
		float layer = (p.y + wave) / WZ2126_STRATA_THICKNESS;
		float band = wz2126Noise(vec2(layer, 0.37)) * 0.7 + wz2126Noise(vec2(layer * 2.3, 5.1)) * 0.3;
		vec3 tint = mix(vec3(0.72, 0.7, 0.7), vec3(1.22, 1.12, 1.0), band);
		outColour.rgb *= mix(vec3(1.0), tint, steep);
		// The detail normal in model space: up-facing bumps on a steep face are ledges
		vec3 nModel = normalize(transpose(ModelTangentMatrix) * normalize(outN));
		float ledge = smoothstep(0.45, 0.8, nModel.y) * steep * smoothstep(0.3, 0.6, wz2126Noise(p.xz / 40.0 + vec2(p.y / 30.0, 2.9)));
		outColour = mix(outColour, topColour, ledge * 0.7);
		outGloss = mix(outGloss, 0.1, ledge * 0.5);
	}
}

// Decal opacity factor: 1 on flat and gentle ground, fading to 0 on steep faces
float wz2126DecalSlopeFade()
{
	float up = abs(wz2126GeometricNormal().y);
	return smoothstep(0.45, 0.75, up);
}

#endif // WZ_TERRAIN_GROUND_2126_GLSL
