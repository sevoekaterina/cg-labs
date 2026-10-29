#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_normal;

// set = 0: общие данные сцены
layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
} scene;

// set = 1: данные модели
layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	vec4 tint;
} model_uniforms;

void main() {
	gl_Position = scene.view_projection * model_uniforms.model * vec4(in_position, 1.0);
	out_color = in_color;
	out_normal = in_normal;
}