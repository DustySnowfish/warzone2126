// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

layout(std140) uniform cbuffer {
	vec4 params; // unused
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
	// 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare")
	vec2 texel = 1.0 / vec2(textureSize(sourceTexture, 0));
	vec2 uv = texCoords;
	vec3 a = texture(sourceTexture, uv + texel * vec2(-2.0, -2.0)).rgb;
	vec3 b = texture(sourceTexture, uv + texel * vec2( 0.0, -2.0)).rgb;
	vec3 c = texture(sourceTexture, uv + texel * vec2( 2.0, -2.0)).rgb;
	vec3 d = texture(sourceTexture, uv + texel * vec2(-2.0,  0.0)).rgb;
	vec3 e = texture(sourceTexture, uv).rgb;
	vec3 f = texture(sourceTexture, uv + texel * vec2( 2.0,  0.0)).rgb;
	vec3 g = texture(sourceTexture, uv + texel * vec2(-2.0,  2.0)).rgb;
	vec3 h = texture(sourceTexture, uv + texel * vec2( 0.0,  2.0)).rgb;
	vec3 i = texture(sourceTexture, uv + texel * vec2( 2.0,  2.0)).rgb;
	vec3 j = texture(sourceTexture, uv + texel * vec2(-1.0, -1.0)).rgb;
	vec3 k = texture(sourceTexture, uv + texel * vec2( 1.0, -1.0)).rgb;
	vec3 l = texture(sourceTexture, uv + texel * vec2(-1.0,  1.0)).rgb;
	vec3 m = texture(sourceTexture, uv + texel * vec2( 1.0,  1.0)).rgb;
	vec3 result = e * 0.125;
	result += (a + c + g + i) * 0.03125;
	result += (b + d + f + h) * 0.0625;
	result += (j + k + l + m) * 0.125;
	FragColor = vec4(result, 1.0);
}
