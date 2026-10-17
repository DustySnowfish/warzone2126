#version 450

layout(std140, set = 0, binding = 0) uniform cbuffer {
	vec4 params; // x = filter radius in coarser-level texels
};

layout(set = 1, binding = 0) uniform sampler2D coarserTexture;
layout(set = 1, binding = 1) uniform sampler2D levelTexture;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

void main()
{
	// 9-tap tent filter on the coarser level, plus this level's own downsample
	vec2 texel = params.x / vec2(textureSize(coarserTexture, 0));
	vec2 uv = texCoords;
	vec3 sum = texture(coarserTexture, uv + texel * vec2(-1.0, -1.0)).rgb;
	sum += texture(coarserTexture, uv + texel * vec2( 0.0, -1.0)).rgb * 2.0;
	sum += texture(coarserTexture, uv + texel * vec2( 1.0, -1.0)).rgb;
	sum += texture(coarserTexture, uv + texel * vec2(-1.0,  0.0)).rgb * 2.0;
	sum += texture(coarserTexture, uv).rgb * 4.0;
	sum += texture(coarserTexture, uv + texel * vec2( 1.0,  0.0)).rgb * 2.0;
	sum += texture(coarserTexture, uv + texel * vec2(-1.0,  1.0)).rgb;
	sum += texture(coarserTexture, uv + texel * vec2( 0.0,  1.0)).rgb * 2.0;
	sum += texture(coarserTexture, uv + texel * vec2( 1.0,  1.0)).rgb;
	vec3 result = sum / 16.0 + texture(levelTexture, uv).rgb;
	FragColor = vec4(result, 1.0);
}
