#version 450

layout(std140, set = 0, binding = 0) uniform cbuffer {
	vec4 params; // unused
};

layout(set = 1, binding = 0) uniform sampler2D sourceTexture;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

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
