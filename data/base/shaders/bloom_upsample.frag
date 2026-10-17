// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

layout(std140) uniform cbuffer {
	vec4 params; // x = filter radius in coarser-level texels
};
uniform sampler2D coarserTexture;
uniform sampler2D levelTexture;

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
