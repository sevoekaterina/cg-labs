#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_normal;

layout(set = 0, binding = 0) uniform GlobalUniforms {
	mat4 mvp;
	vec4 tint;
} u;

void main() {
	gl_Position = u.mvp * vec4(in_position, 1.0);
	out_color = in_color;
	out_normal = in_normal;
}