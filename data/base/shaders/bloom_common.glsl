// Warzone 2126 HDR bloom / tonemap helpers (shared by the GL and Vulkan shaders).

#ifndef WZ_BLOOM_COMMON_GLSL
#define WZ_BLOOM_COMMON_GLSL

// The scene colour is stored display-encoded (gamma 2.2), as the classic renderer always
// did; in HDR mode values may exceed 1.0. Bloom and tonemapping work in linear light.
vec3 wzHdrDecode(vec3 c)
{
	return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 wzHdrEncode(vec3 c)
{
	return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}

float wzLuma(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Soft-knee bright pass: keeps only light above `threshold`, easing in over `knee`.
vec3 wzBloomBrightPass(vec3 linearColour, float threshold, float knee)
{
	float brightness = max(linearColour.r, max(linearColour.g, linearColour.b));
	float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
	soft = (soft * soft) / (4.0 * knee + 1e-4);
	float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
	return linearColour * contribution;
}

// Shoulder tonemap: identity below `knee`, then rolls smoothly towards 1.0.
// Scales all channels by the same factor (from the brightest channel) so hues are kept.
vec3 wzTonemapShoulder(vec3 linearColour, float knee)
{
	float peak = max(linearColour.r, max(linearColour.g, linearColour.b));
	if (peak <= knee)
	{
		return linearColour;
	}
	float range = 1.0 - knee;
	float mapped = knee + range * (1.0 - exp(-(peak - knee) / range));
	return linearColour * (mapped / peak);
}

#endif // WZ_BLOOM_COMMON_GLSL
