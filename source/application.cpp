#include "application.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>

#include <imgui.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace application {

namespace {

// ------------------------------------------------------------------
// Geometry
// ------------------------------------------------------------------

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
	glm::vec3 normal;
};

constexpr int cone_segments = 32;
constexpr float cone_radius = 0.5f;
constexpr float cone_height = 1.0f;

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;


// Цвет вершины по её локальной позиции на окружности (доп. задание №5 из ЛР1)
glm::vec3 proceduralColor(float x, float z) {
	return { x / cone_radius * 0.5f + 0.5f,
			 0.5f,
			 z / cone_radius * 0.5f + 0.5f };
}

// Нормаль боковой поверхности в точке под углом angle:
// перпендикулярна образующей -> (h*cos, r, h*sin), нормированная
glm::vec3 sideNormal(float angle) {
	return glm::normalize(glm::vec3(cone_height * std::cos(angle),
									cone_radius,
									cone_height * std::sin(angle)));
}

void generateCone() {
	vertices.clear();
	indices.clear();

	const float half_height = cone_height * 0.5f;

	// 1) кольцо основания для боковой поверхности (нормали смотрят наружу-вверх)
	const uint32_t side_ring_first = uint32_t(vertices.size());
	for (int i = 0; i < cone_segments; ++i) {
		const float angle = (float(i) / float(cone_segments)) * 2.0f * float(M_PI);
		const float x = std::cos(angle) * cone_radius;
		const float z = std::sin(angle) * cone_radius;

		vertices.push_back({
			.position = { x, -half_height, z },
			.color = proceduralColor(x, z),
			.normal = sideNormal(angle),
		});
	}

	// 2) вершина конуса: по копии на каждый сегмент, потому что у каждой
	//    грани своя нормаль; берём нормаль в середине сегмента
	const uint32_t apex_first = uint32_t(vertices.size());
	for (int i = 0; i < cone_segments; ++i) {
		const float mid_angle = ((float(i) + 0.5f) / float(cone_segments)) * 2.0f * float(M_PI);

		vertices.push_back({
			.position = { 0.0f, half_height, 0.0f },
			.color = { 1.0f, 1.0f, 1.0f },
			.normal = sideNormal(mid_angle),
		});
	}

	// боковая поверхность: тот же порядок вершин, что в ЛР1
	for (int i = 0; i < cone_segments; ++i) {
		const uint32_t curr = side_ring_first + uint32_t(i);
		const uint32_t next = side_ring_first + uint32_t((i + 1) % cone_segments);

		indices.push_back(apex_first + uint32_t(i));
		indices.push_back(curr);
		indices.push_back(next);
	}

	// 3) дно: центр + своё кольцо вершин, нормаль везде вниз
	const uint32_t base_center_index = uint32_t(vertices.size());
	vertices.push_back({
		.position = { 0.0f, -half_height, 0.0f },
		.color = { 0.2f, 0.2f, 0.2f },
		.normal = { 0.0f, -1.0f, 0.0f },
	});

	const uint32_t base_ring_first = uint32_t(vertices.size());
	for (int i = 0; i < cone_segments; ++i) {
		const float angle = (float(i) / float(cone_segments)) * 2.0f * float(M_PI);
		const float x = std::cos(angle) * cone_radius;
		const float z = std::sin(angle) * cone_radius;

		vertices.push_back({
			.position = { x, -half_height, z },
			.color = proceduralColor(x, z),
			.normal = { 0.0f, -1.0f, 0.0f },
		});
	}

	// дно: обратный порядок вершин, как в ЛР1
	for (int i = 0; i < cone_segments; ++i) {
		const uint32_t curr = base_ring_first + uint32_t(i);
		const uint32_t next = base_ring_first + uint32_t((i + 1) % cone_segments);

		indices.push_back(base_center_index);
		indices.push_back(next);
		indices.push_back(curr);
	}
}

// GPU resources

VkBuffer vertex_buffer;
VmaAllocation vertex_buffer_allocation;

VkBuffer index_buffer;
VmaAllocation index_buffer_allocation;

VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

// ЛР2 (доп. задание 1): данные разделены на два набора дескрипторов.
// set = 0 — общие данные сцены (одинаковые для всех объектов)
struct SceneUniforms {
	glm::mat4 view_projection;
};

// set = 1 — данные конкретной модели
struct ModelUniforms {
	glm::mat4 model;
	glm::vec4 tint;
};

VkBuffer scene_uniform_buffer;
VmaAllocation scene_uniform_buffer_allocation;
void* scene_uniform_buffer_mapped;

VkBuffer model_uniform_buffer;
VmaAllocation model_uniform_buffer_allocation;
void* model_uniform_buffer_mapped;

VkDescriptorSetLayout scene_set_layout;
VkDescriptorSetLayout model_set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet scene_descriptor_set;
VkDescriptorSet model_descriptor_set;

std::vector<char> readFile(const char* path) {
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << path << '\n';
		return {};
	}

	const size_t size = size_t(file.tellg());
	std::vector<char> buffer(size);

	file.seekg(0);
	file.read(buffer.data(), std::streamsize(size));

	return buffer;
}

VkShaderModule loadShaderModule(VkDevice device, const char* path) {
	const std::vector<char> code = readFile(path);
	if (code.empty()) {
		return VK_NULL_HANDLE;
	}

	const VkShaderModuleCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size(),
		.pCode = reinterpret_cast<const uint32_t*>(code.data()),
	};

	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
		std::cerr << "Failed to create shader module: " << path << '\n';
		return VK_NULL_HANDLE;
	}

	return module;
}

// data == nullptr создаёт буфер и оставляет его смаппленным (через
// out_mapped), чтобы можно было писать в него позже, каждый кадр —
// это нужно для uniform-буфера. Если data не nullptr, буфер сразу
// заполняется этими данными один раз (вершины/индексы конуса).
bool createBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
				   VkBuffer& out_buffer, VmaAllocation& out_allocation, const void* data,
				   void** out_mapped = nullptr) {
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
				| VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo info;
	if (vmaCreateBuffer(allocator, &buffer_info, &allocation_info, &out_buffer,
						 &out_allocation, &info) != VK_SUCCESS) {
		std::cerr << "Failed to create buffer\n";
		return false;
	}

	if (data != nullptr) {
		std::memcpy(info.pMappedData, data, size_t(size));
	}

	if (out_mapped != nullptr) {
		*out_mapped = info.pMappedData;
	}

	return true;
}

// UI-controlled state

bool use_perspective_projection = true;

float object_position[3] = { 0.0f, 0.0f, 0.0f };
float object_rotation[3] = { 0.0f, 0.0f, 0.0f };
float object_scale[3] = { 1.0f, 1.0f, 1.0f };

float perspective_fov_degrees = 60.0f;
float orthographic_size = 1.5f;

float object_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

// доп. задание №3: движение/поворот по траектории
bool animation_enabled = false;
bool animation_playing = true;
float animation_time = 0.0f;
float animation_speed = 1.0f;
float trajectory_radius = 1.0f;
float trajectory_wave_amplitude = 0.3f;

double last_frame_time = 0.0;

// ЛР2: камера.
// (lookAt из точки (2, 1.5, 2) в начало координат)
const glm::vec3 default_camera_position = { 2.0f, 1.5f, 2.0f };
constexpr float default_camera_yaw = 45.0f;    // поворот вокруг оси Y, градусы
constexpr float default_camera_pitch = -27.9f; // наклон вверх/вниз, градусы

glm::vec3 camera_position = default_camera_position;
float camera_yaw = default_camera_yaw;
float camera_pitch = default_camera_pitch;
float camera_speed = 2.0f;

// Матрица "модели" камеры: камера — такой же объект в мире.
// Сначала поворот (yaw, потом pitch), потом перенос в позицию.
// Камера смотрит вдоль своей локальной оси -Z.
glm::mat4 cameraMatrix() {
	glm::mat4 m(1.0f);
	m = glm::translate(m, camera_position);
	m = glm::rotate(m, glm::radians(camera_yaw), glm::vec3(0.0f, 1.0f, 0.0f));
	m = glm::rotate(m, glm::radians(camera_pitch), glm::vec3(1.0f, 0.0f, 0.0f));
	return m;
}

// Матрица вида — обратная к матрице камеры 
// V = (T * Ry * Rx)^-1 = Rx^-1 * Ry^-1 * T^-1
glm::mat4 viewMatrix() {
	glm::mat4 v(1.0f);
	v = glm::rotate(v, glm::radians(-camera_pitch), glm::vec3(1.0f, 0.0f, 0.0f));
	v = glm::rotate(v, glm::radians(-camera_yaw), glm::vec3(0.0f, 1.0f, 0.0f));
	v = glm::translate(v, -camera_position);
	return v;
}

} // namespace

bool initialize() {
	auto& context = graphics::internal::context;

	generateCone();

	if (!createBuffer(context.allocator, vertices.size() * sizeof(Vertex),
					   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer,
					   vertex_buffer_allocation, vertices.data())) {
		return false;
	}

	if (!createBuffer(context.allocator, indices.size() * sizeof(uint32_t),
					   VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer,
					   index_buffer_allocation, indices.data())) {
		return false;
	}

		// два uniform-буфера: для сцены и для модели. Данные не задаём сразу,
	// будем перезаписывать их каждый кадр в render()
	if (!createBuffer(context.allocator, sizeof(SceneUniforms),
					   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, scene_uniform_buffer,
					   scene_uniform_buffer_allocation, nullptr, &scene_uniform_buffer_mapped)) {
		return false;
	}

	if (!createBuffer(context.allocator, sizeof(ModelUniforms),
					   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, model_uniform_buffer,
					   model_uniform_buffer_allocation, nullptr, &model_uniform_buffer_mapped)) {
		return false;
	}

	// раскладка set = 0 (сцена): binding 0 — uniform-буфер сцены
	const VkDescriptorSetLayoutBinding scene_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo scene_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &scene_binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &scene_layout_info, nullptr,
									&scene_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create scene descriptor set layout\n";
		return false;
	}

	// раскладка set = 1 (модель): binding 0 — uniform-буфер модели
	const VkDescriptorSetLayoutBinding model_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo model_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &model_binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &model_layout_info, nullptr,
									&model_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create model descriptor set layout\n";
		return false;
	}

	// пул: 2 набора, в них всего 2 uniform-буфера
	const VkDescriptorPoolSize descriptor_pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 2,
	};

	const VkDescriptorPoolCreateInfo descriptor_pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 2,
		.poolSizeCount = 1,
		.pPoolSizes = &descriptor_pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &descriptor_pool_info, nullptr,
							   &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	// выделяем сразу оба набора: каждый по своей раскладке
	const VkDescriptorSetLayout set_layouts[] = { scene_set_layout, model_set_layout };
	VkDescriptorSet allocated_sets[2];

	const VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 2,
		.pSetLayouts = set_layouts,
	};

	if (vkAllocateDescriptorSets(context.device, &descriptor_set_alloc_info,
								 allocated_sets) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor sets\n";
		return false;
	}

	scene_descriptor_set = allocated_sets[0];
	model_descriptor_set = allocated_sets[1];

	// привязываем буферы к наборам (один раз: меняться будет только содержимое)
	const VkDescriptorBufferInfo scene_buffer_info = {
		.buffer = scene_uniform_buffer,
		.offset = 0,
		.range = sizeof(SceneUniforms),
	};

	const VkDescriptorBufferInfo model_buffer_info = {
		.buffer = model_uniform_buffer,
		.offset = 0,
		.range = sizeof(ModelUniforms),
	};

	const VkWriteDescriptorSet descriptor_writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = scene_descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &scene_buffer_info,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = model_descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &model_buffer_info,
		},
	};

	vkUpdateDescriptorSets(context.device, 2, descriptor_writes, 0, nullptr);

	const VkShaderModule vert_module = loadShaderModule(context.device, "shaders/cone.vert.spv");
	const VkShaderModule frag_module = loadShaderModule(context.device, "shaders/cone.frag.spv");
	if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
		return false;
	}

	const VkPipelineShaderStageCreateInfo shader_stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vert_module,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = frag_module,
			.pName = "main",
		},
	};

	const VkVertexInputBindingDescription vertex_binding = {
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

		const VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, color),
		},
		{
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, normal),
		},
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = sizeof(vertex_attributes) / sizeof(vertex_attributes[0]),
		.pVertexAttributeDescriptions = vertex_attributes,
	};

	const VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	const VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo rasterization_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	const VkPipelineMultisampleStateCreateInfo multisample_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	const VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	const VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
						  | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo color_blend_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
		.pDynamicStates = dynamic_states,
	};

	const VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2,
		.pSetLayouts = set_layouts, // set 0 — сцена, set 1 — модель
	};

	if (vkCreatePipelineLayout(context.device, &pipeline_layout_info, nullptr,
							   &pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		return false;
	}

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(shader_stages) / sizeof(shader_stages[0]),
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_state,
		.pInputAssemblyState = &input_assembly_state,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization_state,
		.pMultisampleState = &multisample_state,
		.pDepthStencilState = &depth_stencil_state,
		.pColorBlendState = &color_blend_state,
		.pDynamicState = &dynamic_state,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	const VkResult pipeline_result = vkCreateGraphicsPipelines(
		context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);

	vkDestroyShaderModule(context.device, vert_module, nullptr);
	vkDestroyShaderModule(context.device, frag_module, nullptr);

	if (pipeline_result != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline\n";
		return false;
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

	// уничтожение пула дескрипторов освобождает и выделенный из него
	// descriptor_set — отдельно его уничтожать не нужно
		vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);

	vmaDestroyBuffer(context.allocator, model_uniform_buffer, model_uniform_buffer_allocation);
	vmaDestroyBuffer(context.allocator, scene_uniform_buffer, scene_uniform_buffer_allocation);
	vmaDestroyBuffer(context.allocator, index_buffer, index_buffer_allocation);
	vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_buffer_allocation);
}

void update(double time) {
	const double delta = (last_frame_time == 0.0) ? 0.0 : (time - last_frame_time);
	last_frame_time = time;
		// ЛР2: управление камерой мышью и клавиатурой
	// (только когда курсор/клавиатура не заняты окном ImGui)
	const ImGuiIO& io = ImGui::GetIO();

	if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		camera_yaw -= io.MouseDelta.x * 0.15f;
		camera_pitch -= io.MouseDelta.y * 0.15f;
	}
	camera_pitch = std::clamp(camera_pitch, -89.0f, 89.0f);

	if (!io.WantCaptureKeyboard) {
		const glm::mat4 cam = cameraMatrix();
		const glm::vec3 forward = -glm::vec3(cam[2]); // камера смотрит вдоль -Z
		const glm::vec3 right = glm::vec3(cam[0]);
		const glm::vec3 up = { 0.0f, 1.0f, 0.0f };

		glm::vec3 move(0.0f);
		if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
		if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
		if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
		if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
		if (ImGui::IsKeyDown(ImGuiKey_E)) move += up;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= up;

		if (glm::length(move) > 0.0f) {
			camera_position += glm::normalize(move) * camera_speed * float(delta);
		}
	}

	if (animation_enabled && animation_playing) {
		animation_time += float(delta) * animation_speed;
	}

	ImGui::Begin("Cone (variant 9)");

	ImGui::SeparatorText("Projection");
	ImGui::RadioButton("Perspective", (int*)&use_perspective_projection, 1);
	ImGui::SameLine();
	ImGui::RadioButton("Orthographic", (int*)&use_perspective_projection, 0);

	if (use_perspective_projection) {
		ImGui::SliderFloat("FOV", &perspective_fov_degrees, 20.0f, 120.0f);
	} else {
		ImGui::SliderFloat("Ortho size", &orthographic_size, 0.2f, 5.0f);
	}

	ImGui::SeparatorText("Color");
	ImGui::ColorEdit4("Tint", object_tint);

	ImGui::SeparatorText("Transform");
	ImGui::BeginDisabled(animation_enabled);
	ImGui::SliderFloat3("Position", object_position, -2.0f, 2.0f);
	ImGui::EndDisabled();
	ImGui::SliderFloat3("Rotation (deg)", object_rotation, -180.0f, 180.0f);
	ImGui::SliderFloat3("Scale", object_scale, 0.1f, 3.0f);

	ImGui::SeparatorText("Camera");
	ImGui::TextDisabled("RMB + mouse: look, WASD/QE: move");
	ImGui::DragFloat3("Cam position", &camera_position.x, 0.02f);
	ImGui::SliderFloat("Yaw", &camera_yaw, -180.0f, 180.0f);
	ImGui::SliderFloat("Pitch", &camera_pitch, -89.0f, 89.0f);
	ImGui::SliderFloat("Cam speed", &camera_speed, 0.5f, 10.0f);
	if (ImGui::Button("Reset camera")) {
		camera_position = default_camera_position;
		camera_yaw = default_camera_yaw;
		camera_pitch = default_camera_pitch;
	}

	ImGui::SeparatorText("Animation");
	ImGui::Checkbox("Enable trajectory", &animation_enabled);
	ImGui::BeginDisabled(!animation_enabled);
	if (ImGui::Button(animation_playing ? "Pause" : "Play")) {
		animation_playing = !animation_playing;
	}
	ImGui::SliderFloat("Speed", &animation_speed, 0.0f, 5.0f);
	ImGui::SliderFloat("Radius", &trajectory_radius, 0.1f, 3.0f);
	ImGui::SliderFloat("Wave amplitude", &trajectory_wave_amplitude, 0.0f, 1.5f);
	ImGui::EndDisabled();

	ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	vkBeginCommandBuffer(fd.command_buffer, &begin_info);

	const VkClearValue clear_values[] = {
		{ .color = { { 0.02f, 0.02f, 0.05f, 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	const float aspect = float(context.swapchain_extent.width)
						  / float(context.swapchain_extent.height);

	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

	const VkRect2D scissor = {
		.extent = context.swapchain_extent,
	};
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	const VkDeviceSize vertex_buffer_offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

	glm::mat4 model(1.0f);

	if (animation_enabled) {
		// доп. задание №3: движение по "сложной" траектории (окружность в
		// плоскости XZ + вертикальная волна) и непрерывный поворот вокруг Y
		const float x = std::cos(animation_time) * trajectory_radius;
		const float z = std::sin(animation_time) * trajectory_radius;
		const float y = std::sin(animation_time * 2.0f) * trajectory_wave_amplitude;

		model = glm::translate(model, glm::vec3(x, y, z));
		model = glm::rotate(model, animation_time, glm::vec3(0.0f, 1.0f, 0.0f));
	} else {
		model = glm::translate(model, glm::vec3(object_position[0], object_position[1], object_position[2]));
		model = glm::rotate(model, glm::radians(object_rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	model = glm::rotate(model, glm::radians(object_rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::rotate(model, glm::radians(object_rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
	model = glm::scale(model, glm::vec3(object_scale[0], object_scale[1], object_scale[2]));

	const glm::mat4 view = viewMatrix();

	glm::mat4 projection;
	if (use_perspective_projection) {
		projection = glm::perspective(glm::radians(perspective_fov_degrees), aspect, 0.1f, 100.0f);
	} else {
		const float size = orthographic_size;
		projection = glm::ortho(-size * aspect, size * aspect, -size, size, 0.1f, 100.0f);
	}
	// Vulkan's clip space has Y pointing down, unlike OpenGL's.
	projection[1][1] *= -1.0f;

	
	// каждый кадр обновляем оба uniform-буфера
	const SceneUniforms scene_uniforms = {
		.view_projection = projection * view,
	};
	std::memcpy(scene_uniform_buffer_mapped, &scene_uniforms, sizeof(SceneUniforms));

	const ModelUniforms model_uniforms = {
		.model = model,
		.tint = { object_tint[0], object_tint[1], object_tint[2], object_tint[3] },
	};
	std::memcpy(model_uniform_buffer_mapped, &model_uniforms, sizeof(ModelUniforms));

	// привязываем оба набора сразу: set 0 — сцена, set 1 — модель
	const VkDescriptorSet descriptor_sets[] = { scene_descriptor_set, model_descriptor_set };
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
							0, 2, descriptor_sets, 0, nullptr);
	vkCmdDrawIndexed(fd.command_buffer, uint32_t(indices.size()), 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application