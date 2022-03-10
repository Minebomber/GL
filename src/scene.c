#include "scene.h"

#include <glad/glad.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "texture.h"
#include "log.h"

static Node* node_new(unsigned int numParts, unsigned int numChildren);
static void node_delete(Node** node);
static inline Part** node_parts(const Node* node);
static inline Node** node_children(const Node* node);

static void scene_load_geometry(Scene* scene, const struct aiScene* aiScn, unsigned int materialOffset);
static void scene_load_materials(Scene* scene, const char* path, const struct aiScene* aiScn);
static void scene_load_texture(unsigned int* texture, const char* path, const struct aiMaterial* aiMat, enum aiTextureType type);
static void scene_load_node(Scene* scene, Node** node, const struct aiScene* aiScn, const struct aiNode* aiNd, Node* parent, unsigned int geometryOffset);
static void node_world_transform(Node* node, mat4 dest);

void scene_init(Scene* scene) {
	glCreateBuffers(1, &scene->material_buffer);
	glNamedBufferData(scene->material_buffer, 16 * MATERIAL_MAX, NULL, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 2, scene->material_buffer);

	glCreateBuffers(1, &scene->transform_buffer);
	glNamedBufferData(scene->transform_buffer, sizeof(mat4) * TRANSFORM_MAX, NULL, GL_STATIC_DRAW);
	glCreateTextures(GL_TEXTURE_BUFFER, 1, &scene->transform_texture);
	glTextureBuffer(scene->transform_texture, GL_RGBA32F, scene->transform_buffer);

	glCreateBuffers(1, &scene->assign_buffer);
	glNamedBufferData(scene->assign_buffer, sizeof(ivec2) * TRANSFORM_MAX, NULL, GL_STATIC_DRAW);
}

void scene_destroy(Scene* scene) {
	if (scene->material_buffer) {
		glDeleteBuffers(1, &scene->material_buffer);
		scene->material_buffer = 0;
	}

	if (scene->transform_handle) {
		glMakeTextureHandleNonResidentARB(scene->transform_handle);
		scene->transform_handle = 0;
	}
	if (scene->transform_texture) {
		glDeleteTextures(1, &scene->transform_texture);
		scene->transform_texture = 0;
	}
	if (scene->transform_buffer) {
		glDeleteBuffers(1, &scene->transform_buffer);
		scene->transform_buffer = 0;
	}

	if (scene->assign_buffer) {
		glDeleteBuffers(1, &scene->assign_buffer);
		scene->assign_buffer = 0;
	}

	scene->n_cache = 0;

	for (unsigned int i = 0; i < scene->n_geometry; i++) {
		Geometry* g = &scene->geometry[i];
		glDeleteBuffers(1, &g->vertex_buffer);
		glDeleteBuffers(1, &g->element_buffer);
		glDeleteVertexArrays(1, &g->vertex_array);
		glDeleteBuffers(1, &g->indirect_buffer);
		*g = (Geometry){ 0 };
	}

	for (unsigned int i = 0; i < scene->n_materials; i++) {
		Material* m = &scene->materials[i];
		if (m->diffuse.handle) glMakeTextureHandleNonResidentARB(m->diffuse.handle);
		if (m->diffuse.texture)	glDeleteTextures(1, &m->diffuse.texture);
		if (m->specular.handle) glMakeTextureHandleNonResidentARB(m->specular.handle);
		if (m->specular.texture)	glDeleteTextures(1, &m->specular.texture);
		*m = (Material){ 0 };
	}

	for (unsigned int i = 0; i < scene->n_nodes; i++) {
		node_delete(&scene->nodes[i]);
	}
}

void scene_load(Scene* scene, const char* path, mat4 initialTransform, bool flipUVs) {
	const struct aiScene* aiScn = aiImportFile(
		path,
		(flipUVs ? aiProcess_FlipUVs : 0) | aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType
	);
	if (!aiScn || aiScn->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !aiScn->mRootNode) {
		plogf(LL_ERROR, "Failed to load model: %s. %s\n", path, aiGetErrorString());
	}

	unsigned int geometryOffset = scene->n_geometry;
	unsigned int materialOffset = scene->n_materials;
	scene_load_geometry(scene, aiScn, materialOffset);
	scene_load_materials(scene, path, aiScn);

	scene_load_node(
		scene,
		&scene->nodes[scene->n_nodes],
		aiScn,
		aiScn->mRootNode,
		NULL,
		geometryOffset
	);

	Node* node = scene->nodes[scene->n_nodes++];
	glm_mat4_mul(initialTransform, node->transform, node->transform);
}

void scene_build_cache(Scene* scene) {
	for (unsigned int i = 0; i < scene->n_nodes; i++) {
		Node* node = scene->nodes[i];
		CacheObject* cached = &scene->cache[scene->n_cache++];
		cached->geometry = node->geometry;

		Node* queue[128];
		unsigned int nQueue = 1;
		queue[0] = node;
		while (nQueue) {
			Node* n = queue[--nQueue];
			for (unsigned int j = 0; j < n->n_parts; j++) {
				Part* part = node_parts(n)[j];
				DrawIndirectCommand* cmd = &cached->commands[cached->n_commands++];
				cmd->n_index = part->n_index;
				cmd->n_instance = part->n_instance;
				cmd->base_index = part->base_index;
				cmd->base_vertex = part->base_vertex;

				node_world_transform(node, scene->transform[scene->n_transform]);
				ivec2 assign = { part->material, scene->n_transform };
				glNamedBufferSubData(scene->assign_buffer, scene->n_transform * sizeof(ivec2), sizeof(ivec2), assign);
				cmd->base_instance = scene->n_transform++;
			}
			for (unsigned int j = 0; j < n->n_children; j++)
				queue[nQueue++] = node_children(n)[j];
		}
		
		glCreateBuffers(1, &cached->geometry->indirect_buffer);
		glNamedBufferData(
			cached->geometry->indirect_buffer,
			cached->n_commands * sizeof(DrawIndirectCommand),
			cached->commands,
			GL_STATIC_DRAW
		);
	}

	glNamedBufferSubData(scene->transform_buffer, 0, sizeof(mat4) * scene->n_transform, scene->transform);

	for (unsigned int i = 0; i < scene->n_materials; i++) {
		Material* mat = &scene->materials[i];
		if (mat->diffuse.texture) {
			mat->diffuse.handle = glGetTextureHandleARB(mat->diffuse.texture);
			glMakeTextureHandleResidentARB(mat->diffuse.handle);
			glNamedBufferSubData(scene->material_buffer, i * 16, 8, &mat->diffuse.handle);
		}
		if (mat->specular.texture) {
			mat->specular.handle = glGetTextureHandleARB(mat->specular.texture);
			glMakeTextureHandleResidentARB(mat->specular.handle);
			glNamedBufferSubData(scene->material_buffer, i * 16 + 8, 8, &mat->specular.handle);
		}
	}
}

void scene_render(Scene* scene) {
	for (unsigned int i = 0; i < scene->n_cache; i++) {
		CacheObject* cached = &scene->cache[i];
		glUseProgram(cached->geometry->shader);
		glBindVertexArray(cached->geometry->vertex_array);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cached->geometry->indirect_buffer);
		glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, 0, cached->n_commands, 0);
	}
}

static Node* node_new(unsigned int numParts, unsigned int numChildren) {
	Node* node = calloc(1, offsetof(Node, data) + sizeof(Part*) * numParts + sizeof(Node*) * numChildren);
	if (!node) return NULL;
	node->n_parts = numParts;
	node->n_children = numChildren;
	return node;
}

static void node_delete(Node** node) {
	if (!node || !*node) return;
	for (unsigned int i = 0; i < (*node)->n_children; i++)
		node_delete(&node_children(*node)[i]);
	free(*node);
	*node = NULL;
}

static inline Part** node_parts(const Node* node) {
	return (Part**)&node->data;
}

static inline Node** node_children(const Node* node) {
	return (Node**)(&node->data + sizeof(Part*) * node->n_parts);
}

static void scene_load_geometry(Scene* scene, const struct aiScene* aiScn, unsigned int materialOffset) {
	Geometry* g = &scene->geometry[scene->n_geometry++];
	if (scene->n_geometry > GEOMETRY_MAX) {
		plogf(LL_ERROR, "Geometry out of bounds\n");
		return;
	}
	
	size_t nVertices = 0, nIndices = 0;
	for (unsigned int i = 0; i < aiScn->mNumMeshes; i++) {
		const struct aiMesh* aiMsh = aiScn->mMeshes[i];
		nVertices += aiMsh->mNumVertices;
		for (unsigned int j = 0; j < aiMsh->mNumFaces; j++)
			nIndices += aiMsh->mFaces[j].mNumIndices;
	}

	Vertex* vertices = malloc(sizeof(Vertex) * nVertices);
	unsigned int* indices = malloc(sizeof(unsigned int) * nIndices);

	size_t vIdx = 0, iIdx = 0;

	for (unsigned int i = 0; i < aiScn->mNumMeshes; i++) {
		Part* p = &g->parts[g->n_parts++];
		if (g->n_parts > PART_MAX) {
			plogf(LL_ERROR, "Part out of bounds\n");
			free(vertices); free(indices);
			return;
		}
		p->base_vertex = vIdx;
		p->base_index = iIdx;
		p->n_instance = 1;

		const struct aiMesh* aiMsh = aiScn->mMeshes[i];
		
		p->material = materialOffset + aiMsh->mMaterialIndex;

		for (unsigned int j = 0; j < aiMsh->mNumVertices; j++) {
			Vertex* v = &vertices[vIdx++];
			glm_vec3_copy((vec3){	aiMsh->mVertices[j].x, aiMsh->mVertices[j].y,	aiMsh->mVertices[j].z	}, v->position);
			if (aiMsh->mTextureCoords[0])
				glm_vec2_copy((vec2){ aiMsh->mTextureCoords[0][j].x, aiMsh->mTextureCoords[0][j].y }, v->texCoord);
			glm_vec3_copy((vec3){ aiMsh->mNormals[j].x, aiMsh->mNormals[j].y, aiMsh->mNormals[j].z }, v->normal);
		}
		p->n_index = 0;
		for (unsigned int j = 0; j < aiMsh->mNumFaces; j++) {
			p->n_index += aiMsh->mFaces[j].mNumIndices;
			for (unsigned int k = 0; k < aiMsh->mFaces[j].mNumIndices; k++)
				indices[iIdx++] = aiMsh->mFaces[j].mIndices[k];
		}
	}

	glCreateVertexArrays(1, &g->vertex_array);
	glCreateBuffers(1, &g->vertex_buffer);
	glNamedBufferData(g->vertex_buffer, nVertices * sizeof(Vertex), vertices, GL_STATIC_DRAW);
	glCreateBuffers(1, &g->element_buffer);
	glNamedBufferData(g->element_buffer, nIndices * sizeof(unsigned int), indices, GL_STATIC_DRAW);

	glEnableVertexArrayAttrib(g->vertex_array, ATTR_POSITION);
	glVertexArrayAttribBinding(g->vertex_array, ATTR_POSITION, 0);
	glVertexArrayAttribFormat(g->vertex_array, ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
	
	glEnableVertexArrayAttrib(g->vertex_array, ATTR_TEXCOORD);
	glVertexArrayAttribBinding(g->vertex_array, ATTR_TEXCOORD, 0);
	glVertexArrayAttribFormat(g->vertex_array, ATTR_TEXCOORD, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, texCoord));
	
	glEnableVertexArrayAttrib(g->vertex_array, ATTR_NORMAL);
	glVertexArrayAttribBinding(g->vertex_array, ATTR_NORMAL, 0);
	glVertexArrayAttribFormat(g->vertex_array, ATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));

	glVertexArrayVertexBuffer(g->vertex_array, 0, g->vertex_buffer, 0, sizeof(Vertex));
	glVertexArrayElementBuffer(g->vertex_array, g->element_buffer);
	
	glEnableVertexArrayAttrib(g->vertex_array, ATTR_ASSIGN);
	glVertexArrayAttribBinding(g->vertex_array, ATTR_ASSIGN, 1);
	glVertexArrayAttribIFormat(g->vertex_array, ATTR_ASSIGN, 2, GL_INT, 0);
	glVertexArrayVertexBuffer(g->vertex_array, 1, scene->assign_buffer, 0, sizeof(ivec2));
	glVertexArrayBindingDivisor(g->vertex_array, 1, 1);
}

static void scene_load_materials(Scene* scene, const char* path, const struct aiScene* aiScn) {
	for (unsigned int i = 0; i < aiScn->mNumMaterials; i++) {
		Material* mat = &scene->materials[scene->n_materials++];
		const struct aiMaterial* aiMat = aiScn->mMaterials[i];
		if (aiGetMaterialTextureCount(aiMat, aiTextureType_DIFFUSE)) {
			scene_load_texture(&mat->diffuse.texture, path, aiMat, aiTextureType_DIFFUSE);
		}
		if (aiGetMaterialTextureCount(aiMat, aiTextureType_SPECULAR)) {
			scene_load_texture(&mat->specular.texture, path, aiMat, aiTextureType_SPECULAR);
		}
		ai_real shininess = 32.0f;
		mat->shininess = shininess;
	}
}

static void scene_load_texture(unsigned int* texture, const char* path, const struct aiMaterial* aiMat, enum aiTextureType type) {
	struct aiString name;
	aiGetMaterialTexture(aiMat, type, 0, &name, NULL, NULL, NULL, NULL, NULL, NULL);
	size_t bufferSize = strlen(path) + name.length + 2;
	char buffer[bufferSize];
	strcpy(buffer, path);
	char* dirMark = strrchr(buffer, '/');
	strcpy(dirMark + 1, name.data);
	if (!load_texture(texture, buffer, true, GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR)) {
		plogf(LL_ERROR, "Failed to load material texture: %s\n", buffer);
	}
}

static void scene_load_node(Scene* scene, Node** node, const struct aiScene* aiScn, const struct aiNode* aiNd, Node* parent, unsigned int geometryOffset) {
	Node* nd = node_new(aiNd->mNumMeshes, aiNd->mNumChildren);
	if (!nd) {
		plogf(LL_ERROR, "Node allocation failed\n");
		return;
	}
	*node = nd;
	nd->parent = parent;

	glm_mat4_copy((mat4) {
		{ aiNd->mTransformation.a1, aiNd->mTransformation.b1, aiNd->mTransformation.c1, aiNd->mTransformation.d1 }, 
		{ aiNd->mTransformation.a2, aiNd->mTransformation.b2, aiNd->mTransformation.c2, aiNd->mTransformation.d2 }, 
		{ aiNd->mTransformation.a3, aiNd->mTransformation.b3, aiNd->mTransformation.c3, aiNd->mTransformation.d3 }, 
		{ aiNd->mTransformation.a4, aiNd->mTransformation.b4, aiNd->mTransformation.c4, aiNd->mTransformation.d4 } 
	}, nd->transform);

	nd->geometry = &scene->geometry[geometryOffset];

	for (unsigned int i = 0; i < aiNd->mNumMeshes; i++) {
		node_parts(nd)[i] = &scene->geometry[scene->n_geometry - 1].parts[aiNd->mMeshes[i]];
	}
	for (unsigned int i = 0; i < aiNd->mNumChildren; i++) {
		scene_load_node(
			scene,
			&node_children(nd)[i],
			aiScn,
			aiNd->mChildren[i],
			nd,
			geometryOffset
		);
	}
}

static void node_world_transform(Node* node, mat4 dest) {
	glm_mat4_copy(node->transform, dest);
	Node* parent = node->parent;
	while (parent) {
		glm_mat4_mul(parent->transform, dest, dest);
		parent = parent->parent;
	}
}
