// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.20 - 1.50 core.)

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
in vec2 vertex;
#else
attribute vec2 vertex;
#endif

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
out vec2 texCoords;
#else
varying vec2 texCoords;
#endif

void main()
{
	// Warzone 2126 sky dome: placed on the far plane, so the depth test keeps it behind everything already drawn
	gl_Position = vec4(vertex.x, vertex.y, 1.0, 1.0);
	texCoords = 0.5 * gl_Position.xy + vec2(0.5);
}
