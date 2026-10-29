#version 450

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec3 in_world_normal;
layout(location = 2) in vec3 in_world_position;

layout(location = 0) out vec4 out_frag_color;

// set = 0: общие данные сцены
layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec3 camera_position;
	uint point_light_count;
	vec3 directional_light_direction; // направление, КУДА светит источник
	uint spot_light_count;
	vec3 directional_light_color;     // цвет * интенсивность
	vec3 ambient_light;               // цвет * интенсивность
} scene;

// set = 1: данные модели
layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix;
	vec4 albedo;      // rho_d — цвет диффузной составляющей (Tint из ЛР1)
	vec3 specular;    // rho_s — цвет блика
	float shininess;  // n — коэффициент блеска
	uint use_vertex_color;
} model_uniforms;


// Точечный источник (доп. задание 2). Раскладка std430 совпадает с C++.
struct PointLight {
	vec3 position;
	float intensity;
	vec3 color;
};

// storage-буфер: размер массива в шейдере не задан,
// реальное количество — scene.point_light_count
layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight point_lights[];
};

// Модель Блинн-Фонга для одного источника:
//   L = (rho_d + rho_s * max(n·h, 0)^shininess) * max(n·l, 0) * E_L
// n — нормаль, v — направление на камеру, l — направление на источник,
// h — половинчатый вектор, radiance — свет, пришедший от источника
vec3 blinnPhong(vec3 base_color, vec3 n, vec3 v, vec3 l, vec3 radiance) {
	const float n_dot_l = dot(n, l);
	if (n_dot_l <= 0.0) {
		return vec3(0.0); // источник позади поверхности
	}

	const vec3 h = normalize(v + l);
	const float n_dot_h = max(dot(n, h), 0.0);

	const vec3 diffuse = base_color;
	const vec3 specular = model_uniforms.specular * pow(n_dot_h, model_uniforms.shininess);

	return (diffuse + specular) * n_dot_l * radiance;
}

void main() {
	const vec3 n = normalize(in_world_normal);
	const vec3 v = normalize(scene.camera_position - in_world_position);

	// альбедо = цвет материала, при желании умноженный на процедурный цвет из ЛР1
	vec3 base_color = model_uniforms.albedo.rgb;
	if (model_uniforms.use_vertex_color != 0u) {
		base_color *= in_color;
	}

	// рассеянный свет: равномерно со всех сторон
	vec3 color = scene.ambient_light * base_color;

	// направленный источник: для всех точек одно направление, без затухания
	const vec3 l = normalize(-scene.directional_light_direction);
	color += blinnPhong(base_color, n, v, l, scene.directional_light_color);

	// точечные источники: затухание по закону обратных квадратов 1/d^2
	for (uint i = 0; i < scene.point_light_count; ++i) {
		const vec3 to_light = point_lights[i].position - in_world_position;
		const float distance_squared = max(dot(to_light, to_light), 0.0001);
		const vec3 l_point = to_light / sqrt(distance_squared);

		const vec3 radiance = point_lights[i].color * point_lights[i].intensity / distance_squared;
		color += blinnPhong(base_color, n, v, l_point, radiance);
	}


	out_frag_color = vec4(color, model_uniforms.albedo.a);

}