#version 450

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec4 out_frag_color;

layout(set = 0, binding = 0) uniform GlobalUniforms {
	mat4 mvp;
	vec4 tint;
} u;

void main() {
	// out_frag_color = vec4(in_color * u.tint.rgb, u.tint.a);   // вернётся на шаге 4
	out_frag_color = vec4(normalize(in_normal) * 0.5 + 0.5, 1.0); // временно: проверка нормалей
}