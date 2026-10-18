#version 450

layout(std140, set = 0, binding = 0) uniform cbuffer {
	vec4 uvScaleClamp;
	vec4 texelSize; // xy = 1 / texture size
	vec4 params;    // x = sharpening strength (0..1)
};
layout(set = 1, binding = 0) uniform sampler2D sourceTexture;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

void main()
{
	vec2 uv = clamp(texCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	vec2 dx = vec2(texelSize.x, 0.0);
	vec2 dy = vec2(0.0, texelSize.y);
	vec3 c = texture(sourceTexture, uv).rgb;
	vec3 n = texture(sourceTexture, clamp(uv - dy, vec2(0.0), uvScaleClamp.zw)).rgb;
	vec3 s = texture(sourceTexture, clamp(uv + dy, vec2(0.0), uvScaleClamp.zw)).rgb;
	vec3 w = texture(sourceTexture, clamp(uv - dx, vec2(0.0), uvScaleClamp.zw)).rgb;
	vec3 e = texture(sourceTexture, clamp(uv + dx, vec2(0.0), uvScaleClamp.zw)).rgb;
	// Unsharp mask, limited to the local range so it cannot ring or overshoot
	vec3 minC = min(c, min(min(n, s), min(w, e)));
	vec3 maxC = max(c, max(max(n, s), max(w, e)));
	vec3 sharpened = c + (c * 4.0 - n - s - w - e) * (0.25 * params.x);
	FragColor = vec4(clamp(sharpened, minC, maxC), 1.0);
}

