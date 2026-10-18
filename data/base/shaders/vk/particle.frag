#version 450

layout(set = 1, binding = 0) uniform sampler2D particleAtlas;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 colour;
layout(location = 0) out vec4 FragColor;

void main()
{
	float mask = texture(particleAtlas, uv).a;
	float alpha = mask * colour.a;
	if (alpha < 0.002)
	{
		discard;
	}
	FragColor = vec4(pow(max(colour.rgb, vec3(0.0)), vec3(1.0 / 2.2)), alpha);
}
