#version 450

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec4 out_frag_color;

// set = 1: данные модели (цвет tint понадобится на шаге 4)
layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	vec4 tint;
} model_uniforms;

void main() {
	// out_frag_color = vec4(in_color * model_uniforms.tint.rgb, model_uniforms.tint.a); // вернётся на шаге 4
	out_frag_color = vec4(normalize(in_normal) * 0.5 + 0.5, 1.0); // временно: проверка нормалей
}