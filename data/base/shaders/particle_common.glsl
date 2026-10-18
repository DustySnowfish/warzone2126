// Warzone 2126 particles: shared vertex logic (GL and Vulkan).

#ifndef WZ_PARTICLE_COMMON_GLSL
#define WZ_PARTICLE_COMMON_GLSL

// Corner in [-0.5, 0.5]^2 to a render-space position. Stretched particles (sparks) lie along
// their direction of travel as seen by the camera; the others turn by their rotation.
vec3 wzParticleCorner(vec2 corner, vec4 positionSize, vec4 params, vec4 velocity,
	vec3 right, vec3 up, vec3 forward)
{
	float size = positionSize.w;
	vec3 axisX = right;
	vec3 axisY = up;
	float lengthScale = 1.0;
	if (params.z > 0.0)
	{
		vec3 v = velocity.xyz - forward * dot(velocity.xyz, forward);
		float vl = length(v);
		if (vl > 1e-4)
		{
			axisX = v / vl;
			axisY = normalize(cross(forward, axisX));
			lengthScale = 1.0 + params.z * vl;
		}
	}
	else
	{
		float c = cos(params.x);
		float s = sin(params.x);
		axisX = right * c + up * s;
		axisY = up * c - right * s;
	}
	return positionSize.xyz + axisX * (corner.x * size * lengthScale) + axisY * (corner.y * size);
}

vec2 wzParticleUV(vec2 corner, float frame, vec2 atlasSize)
{
	float column = mod(frame, atlasSize.x);
	float row = floor(frame / atlasSize.x);
	return (vec2(column, row) + vec2(corner.x + 0.5, 0.5 - corner.y)) / atlasSize;
}

#endif // WZ_PARTICLE_COMMON_GLSL
