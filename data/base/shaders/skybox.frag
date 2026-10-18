// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

layout(std140) uniform cbuffer
{
	mat4 posMatrix;
	vec4 color;
	vec4 fog_color;
	int fog_enabled;
	float top_fade; // Warzone 2126: fade the top of the band into the sky dome
};

uniform sampler2D theTexture;

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
in vec2 uv;
in vec4 fog;
#else
varying vec2 uv;
varying vec4 fog;
#endif

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
out vec4 FragColor;
#else
// Uses gl_FragColor
#endif

void main()
{
	#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
	vec4 resultingColor = texture(theTexture, uv) * color;
	#else
	vec4 resultingColor = texture2D(theTexture, uv) * color;
	#endif

	if (top_fade > 0.0)
	{
		resultingColor.a *= smoothstep(0.01, top_fade, uv.y);
	}

	if(fog_enabled > 0)
	{
		resultingColor = mix(resultingColor, vec4(fog.xyz, 1), fog.w);
	}

	#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
	FragColor = resultingColor;
	#else
	gl_FragColor = resultingColor;
	#endif
}
