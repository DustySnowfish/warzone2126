// Warzone 2126 sky dome: a gradient sky from the map's environment file, drawn behind
// everything. Shared by the GL and Vulkan shaders.

#ifndef WZ_SKY_DOME_COMMON_GLSL
#define WZ_SKY_DOME_COMMON_GLSL

// Sky colour (display-encoded) seen in direction d, without the sun disc.
vec3 wzSkyDomeColour(vec3 d, vec3 zenith, vec3 horizon, vec3 ground, vec3 sunDir, vec3 sunColour,
	float horizonSharpness, float sunGlow, float brightness)
{
	vec3 c;
	if (d.y >= 0.0)
	{
		c = mix(zenith, horizon, pow(1.0 - d.y, horizonSharpness));
	}
	else
	{
		c = mix(horizon, ground, min(1.0, -d.y * 4.0));
		// Looking steeply down past the map edge: dim ground (like the out-of-bounds terrain), not bright sky
		c = mix(c, ground * 0.45, smoothstep(0.15, 0.55, -d.y));
	}
	float s = max(dot(d, sunDir), 0.0);
	c += sunColour * (sunGlow * pow(s, 8.0));
	return pow(max(c * brightness, vec3(0.0)), vec3(1.0 / 2.2));
}

// Returns display-encoded colour (gamma 2.2), unclamped: the sun can exceed 1.0 and bloom.
vec3 wzSkyDome(vec2 texCoords, mat4 invViewProj, vec3 zenith, vec3 horizon, vec3 ground,
	vec3 sunDir, vec3 sunColour, vec4 params, vec4 fogColour)
{
	vec4 farPoint = invViewProj * vec4(texCoords * 2.0 - 1.0, 1.0, 1.0);
	vec3 d = normalize(farPoint.xyz / farPoint.w);
	vec3 c;
	if (d.y >= 0.0)
	{
		c = mix(zenith, horizon, pow(1.0 - d.y, params.x));
	}
	else
	{
		c = mix(horizon, ground, min(1.0, -d.y * 4.0));
		// Looking steeply down past the map edge: dim ground (like the out-of-bounds terrain), not bright sky
		c = mix(c, ground * 0.45, smoothstep(0.15, 0.55, -d.y));
	}
	float s = max(dot(d, sunDir), 0.0);
	c += sunColour * (params.y * pow(s, 8.0));
	// Sun disc with a soft edge (about 1.5 degrees across)
	c += sunColour * (params.z * 6.0 * smoothstep(0.99955, 0.99985, s));
	c *= params.w;
	vec3 encoded = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
	if (fogColour.w > 0.5)
	{
		// Below the horizon the classic fog colour takes over, as the old sky did
		encoded = mix(encoded, fogColour.rgb, clamp(-d.y * 8.0, 0.0, 1.0));
	}
	return encoded;
}


// Warzone 2126 cloud shadows. rel is the camera-relative position, sunDir points to the sun.
// cloud: x = coverage, y = strength, z = 1 / size, w = cloud height above the camera.
// offset: xy = camera position (render x, z) plus wind drift, z = edge softness.
float wzCloudHash(vec2 p)
{
	p = fract(p * vec2(0.1031, 0.1030));
	p += dot(p, p.yx + 33.33);
	return fract((p.x + p.y) * p.x);
}

float wzCloudValueNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = wzCloudHash(i);
	float b = wzCloudHash(i + vec2(1.0, 0.0));
	float c = wzCloudHash(i + vec2(0.0, 1.0));
	float d = wzCloudHash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float wzCloudShadow(vec3 rel, vec3 sunDir, vec4 cloud, vec4 offset)
{
	if (cloud.y <= 0.0)
	{
		return 1.0;
	}
	// Follow the ray towards the sun up to the cloud layer
	float t = (cloud.w - rel.y) / max(sunDir.y, 0.2);
	vec2 p = (rel.xz + offset.xy + sunDir.xz * t) * cloud.z;
	float n = 0.0;
	float amplitude = 0.5;
	for (int i = 0; i < 4; ++i)
	{
		n += wzCloudValueNoise(p) * amplitude;
		p = p * 2.03 + vec2(17.3, 5.1);
		amplitude *= 0.5;
	}
	n /= 0.9375;
	float threshold = 1.0 - cloud.x;
	float density = smoothstep(threshold - offset.z, threshold + offset.z, n);
	return 1.0 - cloud.y * density;
}

#endif // WZ_SKY_DOME_COMMON_GLSL
