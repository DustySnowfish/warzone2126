// Warzone 2126 heat shimmer: shared noise (GL and Vulkan).

#ifndef WZ_HEAT_COMMON_GLSL
#define WZ_HEAT_COMMON_GLSL

float wzHeatHash(vec2 p)
{
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

// Smooth value noise, 0..1
float wzHeatNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = wzHeatHash(i);
	float b = wzHeatHash(i + vec2(1.0, 0.0));
	float c = wzHeatHash(i + vec2(0.0, 1.0));
	float d = wzHeatHash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// A wobbling 2D offset in -1..1: two noise fields rising over time (heat rises)
vec2 wzHeatWobble(vec2 p, float time)
{
	vec2 q = p + vec2(0.0, -time * 1.7);
	float x = wzHeatNoise(q) * 0.65 + wzHeatNoise(q * 2.1 + vec2(3.7, -time * 0.9)) * 0.35;
	float y = wzHeatNoise(q + vec2(17.3, 5.1)) * 0.65 + wzHeatNoise(q * 2.3 + vec2(-8.2, -time * 1.1)) * 0.35;
	return vec2(x, y) * 2.0 - 1.0;
}

#endif // WZ_HEAT_COMMON_GLSL
