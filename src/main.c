#include "window.h"
#include "log.h"
#include "camera.h"
#include "shader.h"
#include "scene.h"
#include "light.h"
#include "texture.h"
#include "stb_image.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define WINDOW_TITLE "OpenGL Program"

#define CAMERA_FOV M_PI / 3.0f
#define CAMERA_NEAR 0.1f
#define CAMERA_FAR 100.f

#define MOVEMENT_SPEED 5.0f
#define ROTATION_SPEED 0.1f

#define LIGHT_MAX 8

#define N_SIDE 16

enum UBO_BINDING {
	UBO_GLOBAL,
	UBO_CAMERA,
	UBO_MATERIAL,
	UBO_LIGHT,
};

enum SHADER_TYPE {
	SHADER_DEFAULT,
	SHADER_SKYBOX,
	_SHADER_MAX
};

typedef struct {
	Window window;
	Camera camera;
	vec2 mouse;
	
	unsigned int shaders[_SHADER_MAX];
	
	unsigned int global_buffer;
	unsigned int camera_buffer;
	unsigned int light_buffer;
	
	struct {
		unsigned int texture;
		unsigned int handle;
		unsigned int vertex_array;
		unsigned int vertex_buffer;
		unsigned int element_buffer;
	} skybox;

	Scene scene;
} Application;

void on_setup(Application* app);
void on_event(Application* app, Event* e);
void on_update(Application* app, double frameTime);
void on_teardown(Application* app);

int main(const int argc, const char* argv[]) {
	Application app = { 0 };
	if (!window_init(&app.window, WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE)) {
		plogf(LL_ERROR, "Failed to initialize window\n");
		return 1;
	}
	on_setup(&app);

	double lastTime = glfwGetTime();
	while (!glfwWindowShouldClose(app.window.window)) {
		// Time
		double currentTime = glfwGetTime();
		double frameTime = currentTime - lastTime;
		lastTime = currentTime;
		
		// Events
		for (size_t i = 0; i < app.window.n_events; i++)
			on_event(&app, &app.window.events[i]);
		app.window.n_events = 0;
		
		// Update
		on_update(&app, frameTime);
		
		// Render
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glUseProgram(app.shaders[SHADER_DEFAULT]);
		scene_render(&app.scene);
		glUseProgram(app.shaders[SHADER_SKYBOX]);
		glBindVertexArray(app.skybox.vertex_array);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);

		glfwSwapBuffers(app.window.window);
		glfwPollEvents();
	}

	on_teardown(&app);
	glfwTerminate();

	return 0;
}

void on_setup(Application* app) {
	void load_skybox(Application* app);

	// GL setup
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(gl_log, NULL);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	create_shader(
		&app->shaders[SHADER_DEFAULT], 2,
		(ShaderArgs) { GL_VERTEX_SHADER, "res/shaders/default.vert" },
		(ShaderArgs) { GL_FRAGMENT_SHADER, "res/shaders/default.frag" }
	);
	
	create_shader(
		&app->shaders[SHADER_SKYBOX], 2,
		(ShaderArgs) { GL_VERTEX_SHADER, "res/shaders/skybox.vert" },
		(ShaderArgs) { GL_FRAGMENT_SHADER, "res/shaders/skybox.frag" }
	);

	glCreateBuffers(1, &app->global_buffer);
	glNamedBufferData(app->global_buffer, 16, NULL, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_GLOBAL, app->global_buffer);

	glCreateBuffers(1, &app->camera_buffer);
	glNamedBufferData(app->camera_buffer, 144, NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_CAMERA, app->camera_buffer);
	camera_init(&app->camera, app->window.width, app->window.height, CAMERA_FOV, CAMERA_NEAR, CAMERA_FAR);

	glCreateBuffers(1, &app->light_buffer);
	glNamedBufferData(app->light_buffer, 16 + LIGHT_SIZE * LIGHT_MAX, NULL, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_LIGHT, app->light_buffer);

	uint lightCount = 1;
	glNamedBufferSubData(app->light_buffer, 0, sizeof(uint), &lightCount);

	Light l = {
		.type = LIGHT_DIRECTIONAL,
		.directionLinear = { 0.6, -1.0, 0.3 },
		.ambientQuadratic = { 0.3, 0.3, 0.3 },
		.diffuseCutOff = { 0.8, 0.8, 0.8 },
		.specularOuterCutOff = { 1.0, 1.0, 1.0 }
	};

	glNamedBufferSubData(app->light_buffer, 16, sizeof(uint), &l.type);
	glNamedBufferSubData(app->light_buffer, 32, sizeof(vec4) * 5, &l.positionConstant);
	
	scene_init(&app->scene);

	app->scene.transform_handle = glGetTextureHandleARB(app->scene.transform_texture);
	glMakeTextureHandleResidentARB(app->scene.transform_handle);
	glNamedBufferSubData(app->global_buffer, 0, 8, &app->scene.transform_handle);
	// Load cube model
	mat4 modelMatrix; glm_mat4_identity(modelMatrix);
	glm_translate(modelMatrix, (vec3){ 5, 0, 0 });
	scene_load(&app->scene, "res/models/cube/cube.obj", 0, modelMatrix, false);
	// Load floor material
	Material* floorMat = &app->scene.materials[app->scene.n_materials++];
	unsigned int floorDiffuse = 0;
	load_texture_color(&floorDiffuse, (unsigned char[3]){ 85, 170, 255 });
	floorMat->diffuse = scene_insert_texture(&app->scene, strhash("floorDiffuse"), floorDiffuse);

	unsigned int floorSpecular = 0;
	load_texture_color(&floorSpecular, (unsigned char[3]){ 64, 64, 64 });
	floorMat->specular = scene_insert_texture(&app->scene, strhash("floorSpecular"), floorSpecular);

	floorMat->shininess = 1.0f;
	// Insert floor part into cube geometry (same mesh, different material)
	Geometry* cubeGeometry = &app->scene.geometry[0];
	Part* cubePart = &cubeGeometry->parts[0];
	Part* floorPart = &cubeGeometry->parts[cubeGeometry->n_parts++];
	floorPart->n_index = cubePart->n_index;
	floorPart->base_index = cubePart->base_index;
	floorPart->base_vertex = cubePart->base_vertex;
	floorPart->material = 2;
	
	for (unsigned int i = 0; i < N_SIDE; i++) {
		for (unsigned int j = 0; j < N_SIDE; j++) {
			float x = (2.0f * i) - (N_SIDE);
			float y = -2.0f;
			float z = (2.0f * j) - (N_SIDE);
			Node* iCubeNode = node_new(1, 0);
			iCubeNode->geometry = &app->scene.geometry[0];
			glm_translate_make(iCubeNode->transform, (vec3) { x, y, z });
			node_parts(iCubeNode)[0] = floorPart;
			app->scene.nodes[app->scene.n_nodes++] = iCubeNode;
		}
	}

	glm_mat4_identity(modelMatrix);
	glm_translate(modelMatrix, (vec3){ 0, 1, 0 });
	scene_load(&app->scene, "res/models/backpack/backpack.obj", 0, modelMatrix, false);
	glm_mat4_identity(modelMatrix);
	glm_translate(modelMatrix, (vec3){ 0, 0, 5 });
	scene_load(&app->scene, "res/models/cube/cube.obj", 0, modelMatrix, false);

	scene_build_cache(&app->scene);

	load_skybox(app);
}

void load_skybox(Application* app) {
	const char* skyboxFaces[] = {
		"res/skybox/right.jpg",
		"res/skybox/left.jpg",
		"res/skybox/top.jpg",
		"res/skybox/bottom.jpg",
		"res/skybox/front.jpg",
		"res/skybox/back.jpg"
	};
	glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &app->skybox.texture);
	glTextureParameteri(app->skybox.texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(app->skybox.texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(app->skybox.texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(app->skybox.texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(app->skybox.texture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	int width, height, nChannels;
	unsigned char* data = stbi_load(skyboxFaces[0], &width, &height, &nChannels, 0);
	stbi_image_free(data);

	glTextureStorage2D(app->skybox.texture, 1, GL_RGB8, width, height);
	for (unsigned int i = 0; i < 6; i++) {
		data = stbi_load(skyboxFaces[i], &width, &height, &nChannels, 0);
		if (!data) {
			plogf(LL_ERROR, "Cannot load image: %s\n", skyboxFaces[i]);
		} else {
			glTextureSubImage3D(app->skybox.texture, 0, 0, 0, i, width, height, 1, GL_RGB, GL_UNSIGNED_BYTE, data);
			stbi_image_free(data);
		}
	}
	app->skybox.handle = glGetTextureHandleARB(app->skybox.texture);
	glMakeTextureHandleResidentARB(app->skybox.handle);
	glNamedBufferSubData(app->global_buffer, 8, 8, &app->skybox.handle); 
	
	vec3 vertices[] = {
		{ -1.0f, -1.0f, -1.0f },
		{  1.0f, -1.0f, -1.0f },
		{  1.0f,  1.0f, -1.0f },
		{ -1.0f,  1.0f, -1.0f },

		{ -1.0f, -1.0f,  1.0f },
		{  1.0f, -1.0f,  1.0f },
		{  1.0f,  1.0f,  1.0f },
		{ -1.0f,  1.0f,  1.0f },

		{ -1.0f,  1.0f,  1.0f },
		{ -1.0f,  1.0f, -1.0f },
		{ -1.0f, -1.0f, -1.0f },
		{ -1.0f, -1.0f,  1.0f },

		{  1.0f,  1.0f,  1.0f },
		{  1.0f,  1.0f, -1.0f },
		{  1.0f, -1.0f, -1.0f },
		{  1.0f, -1.0f,  1.0f },

		{ -1.0f, -1.0f, -1.0f },
		{  1.0f, -1.0f, -1.0f },
		{  1.0f, -1.0f,  1.0f },
		{ -1.0f, -1.0f,  1.0f },

		{ -1.0f,  1.0f, -1.0f },
		{  1.0f,  1.0f, -1.0f },
		{  1.0f,  1.0f,  1.0f },
		{ -1.0f,  1.0f,  1.0f },
	};

	unsigned int indices[] = {
			0,  1,  2,  2,  3,  0,
			4,  6,  5,  6,  4,  7,
			8, 10,  9, 10,  8, 11,
		12, 13, 14, 14, 15, 12,
		16, 18, 17, 18, 16, 19,
		20, 21, 22, 22, 23, 20
	};
	glCreateVertexArrays(1, &app->skybox.vertex_array);
	glCreateBuffers(1, &app->skybox.vertex_buffer);
	glNamedBufferData(app->skybox.vertex_buffer, sizeof(vec3) * 24, vertices, GL_STATIC_DRAW);
	glCreateBuffers(1, &app->skybox.element_buffer);
	glNamedBufferData(app->skybox.element_buffer, sizeof(unsigned int) * 36, indices, GL_STATIC_DRAW);
	
	glEnableVertexArrayAttrib(app->skybox.vertex_array, ATTR_POSITION);
	glVertexArrayAttribBinding(app->skybox.vertex_array, ATTR_POSITION, 0);
	glVertexArrayAttribFormat(app->skybox.vertex_array, ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, 0);

	glVertexArrayVertexBuffer(app->skybox.vertex_array, 0, app->skybox.vertex_buffer, 0, sizeof(vec3));
	glVertexArrayElementBuffer(app->skybox.vertex_array, app->skybox.element_buffer);
}

void on_event(Application* app, Event* e) {
	switch (e->type) {
	case EVENT_RESIZE:
		glViewport(0, 0, e->resize.width, e->resize.height);
		app->camera.vp_width = e->resize.width;
		app->camera.vp_height = e->resize.height;
		app->camera.update_projection = true;
		break;
	case EVENT_KEYBOARD:
		if (e->keyboard.key == GLFW_KEY_ESCAPE && e->keyboard.action)
			glfwSetWindowShouldClose(app->window.window, true);
		break;
	case EVENT_MOUSE_MOVE:
	{
		static vec2 pos;
		static bool first;
		if (!first) glm_vec2_subadd((vec2) { e->mouse_move.xpos, e->mouse_move.ypos }, pos, app->mouse);
		else first = false;
		pos[0] = (float)e->mouse_move.xpos;
		pos[1] = (float)e->mouse_move.ypos;
	}
		break;
	default:
		break;
	}
}

void on_update(Application* app, double frameTime) {
	if (glfwGetKey(app->window.window, GLFW_KEY_W) == GLFW_PRESS) {
		glm_vec3_muladds(app->camera.front, (float)frameTime * MOVEMENT_SPEED, app->camera.position);
		app->camera.update_view = true;
	}
	if (glfwGetKey(app->window.window, GLFW_KEY_S) == GLFW_PRESS) {
		glm_vec3_muladds(app->camera.front, -(float)frameTime * MOVEMENT_SPEED, app->camera.position);
		app->camera.update_view = true;
	}
	if (glfwGetKey(app->window.window, GLFW_KEY_D) == GLFW_PRESS) {
		glm_vec3_muladds(app->camera.right, (float)frameTime * MOVEMENT_SPEED, app->camera.position);
		app->camera.update_view = true;
	}
	if (glfwGetKey(app->window.window, GLFW_KEY_A) == GLFW_PRESS) {
		glm_vec3_muladds(app->camera.right, -(float)frameTime * MOVEMENT_SPEED, app->camera.position);
		app->camera.update_view = true;
	}
	if (glfwGetKey(app->window.window, GLFW_KEY_SPACE) == GLFW_PRESS) {
		glm_vec3_muladds(app->camera.up, (float)frameTime * MOVEMENT_SPEED, app->camera.position);
		app->camera.update_view = true;
	}
	if (glfwGetKey(app->window.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
		glm_vec3_muladds(app->camera.up, -(float)frameTime * MOVEMENT_SPEED, app->camera.position);
		app->camera.update_view = true;
	}
	
	if (app->mouse[0] != 0) {
		camera_rotate_y(&app->camera, -app->mouse[0] * frameTime * ROTATION_SPEED);
		app->mouse[0] = 0;
		app->camera.update_view = true;
	}

	if (app->mouse[1] != 0) {
		camera_rotate_x(&app->camera, -app->mouse[1] * frameTime * ROTATION_SPEED);
		app->mouse[1] = 0;
		app->camera.update_view = true;
	}

	if (app->camera.update_projection || app->camera.update_view) {
		if (app->camera.update_projection) {
			camera_update_projection(&app->camera);
			glNamedBufferSubData(app->camera_buffer, 0, sizeof(mat4), app->camera.perspective[0]);
		}
		if (app->camera.update_view) {
			camera_update_view(&app->camera);
			glNamedBufferSubData(app->camera_buffer, sizeof(mat4), sizeof(mat4), app->camera.view[0]);
			glNamedBufferSubData(app->camera_buffer, 2 * sizeof(mat4), sizeof(vec4), app->camera.position);
		}
	}
}

void on_teardown(Application* app) {
	glDeleteProgram(app->shaders[SHADER_DEFAULT]);
	glDeleteProgram(app->shaders[SHADER_SKYBOX]);
	
	glDeleteBuffers(1, &app->global_buffer);
	glDeleteBuffers(1, &app->camera_buffer);

	scene_destroy(&app->scene);
}
