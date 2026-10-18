#version 450

layout(std140, set = 0, binding = 0) uniform cbuffer {
	vec4 uvScaleClamp;
};
layout(set = 1, binding = 0) uniform sampler2D sourceTexture;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 FragColor;

void main()
{
	FragColor = texture(sourceTexture, clamp(texCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw));
}
