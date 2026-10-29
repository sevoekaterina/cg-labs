#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_world_normal;
layout(location = 2) out vec3 out_world_position;

// set = 0: общие данные сцены
layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec3 camera_position;
	uint point_light_count;
	vec3 directional_light_direction;
	uint spot_light_count;
	vec3 directional_light_color;
	vec3 ambient_light;
} scene;

// set = 1: данные модели
layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix;
	vec4 albedo;
	vec3 specular;
	float shininess;
	uint use_vertex_color;
} model_uniforms;

void main() {
	const vec4 world_position = model_uniforms.model * vec4(in_position, 1.0);

	gl_Position = scene.view_projection * world_position;

	// освещение считаем в мировых координатах, поэтому переводим туда
	// и позицию, и нормаль (нормаль — через матрицу нормалей)
	out_world_position = world_position.xyz;
	out_world_normal = mat3(model_uniforms.normal_matrix) * in_normal;
	out_color = in_color;
}