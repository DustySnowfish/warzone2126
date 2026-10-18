// Warzone 2126 temporal anti-aliasing helpers (shared by the GL and Vulkan shaders).

#ifndef WZ_TAA_COMMON_GLSL
#define WZ_TAA_COMMON_GLSL

vec3 wzRGBToYCoCg(vec3 c)
{
	return vec3(
		 0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
		 0.5  * c.r             - 0.5  * c.b,
		-0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 wzYCoCgToRGB(vec3 c)
{
	return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Pull `history` towards the centre of the box [boxMin, boxMax] until it lies inside it
// (clipping, rather than clamping per channel, avoids colour shifts).
vec3 wzClipToBox(vec3 history, vec3 boxMin, vec3 boxMax)
{
	vec3 centre = 0.5 * (boxMax + boxMin);
	vec3 extent = 0.5 * (boxMax - boxMin) + vec3(1e-4);
	vec3 offset = history - centre;
	vec3 ratio = abs(offset / extent);
	float maxRatio = max(ratio.x, max(ratio.y, ratio.z));
	return maxRatio > 1.0 ? centre + offset / maxRatio : history;
}

#endif // WZ_TAA_COMMON_GLSL
