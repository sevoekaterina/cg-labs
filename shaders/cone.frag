#version 450

layout(location = 0) in vec3 in_color;

layout(location = 0) out vec4 out_frag_color;

layout(set = 0, binding = 0) uniform GlobalUniforms {
	mat4 mvp;
	vec4 tint;
} u;

void main() {
	// доп. задание №5: выбранный пользователем цвет умножается на процедурный
	// цвет вершины, заданный по локальной позиции в generateCone()
	out_frag_color = vec4(in_color * u.tint.rgb, u.tint.a);
}