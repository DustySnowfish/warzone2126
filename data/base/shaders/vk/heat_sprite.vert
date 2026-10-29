#version 450

// Warzone 2126 heat shimmer source: a camera-facing quad rising from its position

layout(std140, set = 0, binding = 0) uniform cbuffer {
	mat4 viewProjection;
	vec4 cameraRight;
	vec4 cameraUp;
	vec4 heatParams; // x = time (s), y = offset scale (screen uv)
};

layout(location = 0) in vec2 vertex;
layout(location = 5) in vec4 particlePositionSize;
layout(location = 6) in vec4 particleColour;     // (heat) x = strength 0..1, y = seed, z = height / width
layout(location = 7) in vec4 particleParams;
layout(location = 8) in vec4 particleVelocity;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 data;

void main()
{
	float width = particlePositionSize.w;
	float height = width * max(particleColour.z, 0.5);
	vec3 centre = particlePositionSize.xyz + cameraUp.xyz * (height * 0.5 - width * 0.15);
	vec3 world = centre + cameraRight.xyz * (vertex.x * width) + cameraUp.xyz * (vertex.y * height);
	uv = vertex + vec2(0.5);
	data = particleColour;
	gl_Position = viewProjection * vec4(world, 1.0);
	gl_Position.y *= -1.0;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
