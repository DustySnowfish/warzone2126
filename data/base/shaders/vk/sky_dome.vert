#version 450

layout(location = 0) in vec2 vertexPos;

layout(location = 0) out vec2 texCoords;

void main()
{
	// Warzone 2126 sky dome: placed on the far plane, so the depth test keeps it behind everything already drawn
	gl_Position = vec4(vertexPos.x, vertexPos.y, 1.0, 1.0);
	texCoords = 0.5 * gl_Position.xy + vec2(0.5);
}
