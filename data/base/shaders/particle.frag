// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

uniform sampler2D particleAtlas;

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#else
#define texture(tex, uv) texture2D(tex, uv)
#endif

#ifdef NEWGL
in vec2 uv;
in vec4 colour;
out vec4 FragColor;
#else
varying vec2 uv;
varying vec4 colour;
#define FragColor gl_FragColor
#endif

void main()
{
	float mask = texture(particleAtlas, uv).a;
	float alpha = mask * colour.a;
	if (alpha < 0.002)
	{
		discard;
	}
	// The scene colour is display-encoded (gamma 2.2); with HDR on it may exceed 1 and bloom
	FragColor = vec4(pow(max(colour.rgb, vec3(0.0)), vec3(1.0 / 2.2)), alpha);
}
