// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

layout(std140) uniform cbuffer {
	vec4 uvScaleClamp;
	vec4 texelSize; // xy = 1 / texture size
	vec4 params;    // x = sharpening strength (0..1)
};
uniform sampler2D sourceTexture;

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#else
#define texture(tex, uv) texture2D(tex, uv)
#endif

#ifdef NEWGL
in vec2 texCoords;
out vec4 FragColor;
#else
varying vec2 texCoords;
#define FragColor gl_FragColor
#endif

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

