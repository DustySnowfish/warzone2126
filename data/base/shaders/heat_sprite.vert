// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

// Warzone 2126 heat shimmer source: a camera-facing quad rising from its position

layout(std140) uniform cbuffer {
	mat4 viewProjection;
	vec4 cameraRight;
	vec4 cameraUp;
	vec4 heatParams; // x = time (s), y = offset scale (screen uv)
};

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
in vec2 vertex;
in vec4 particlePositionSize;
in vec4 particleColour;     // (heat) x = strength 0..1, y = seed, z = height / width
in vec4 particleParams;
in vec4 particleVelocity;
out vec2 uv;
out vec4 data;
#else
attribute vec2 vertex;
attribute vec4 particlePositionSize;
attribute vec4 particleColour;
attribute vec4 particleParams;
attribute vec4 particleVelocity;
varying vec2 uv;
varying vec4 data;
#endif

void main()
{
	float width = particlePositionSize.w;
	float height = width * max(particleColour.z, 0.5);
	vec3 centre = particlePositionSize.xyz + cameraUp.xyz * (height * 0.5 - width * 0.15);
	vec3 world = centre + cameraRight.xyz * (vertex.x * width) + cameraUp.xyz * (vertex.y * height);
	uv = vertex + vec2(0.5);
	data = particleColour;
	gl_Position = viewProjection * vec4(world, 1.0);
}
