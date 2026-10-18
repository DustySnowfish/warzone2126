// Warzone 2126 sky dome: a gradient sky from the map's environment file, drawn behind
// everything. Shared by the GL and Vulkan shaders.

#ifndef WZ_SKY_DOME_COMMON_GLSL
#define WZ_SKY_DOME_COMMON_GLSL

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

#endif // WZ_SKY_DOME_COMMON_GLSL
