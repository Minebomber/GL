#include "scene.h"

#include <glad/glad.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "texture.h"
#include "log.h"

Part** node_parts(const Node* node);
Node** node_children(const Node* node);

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

unsigned long long strhash(const char* str) {
	unsigned long long hash = 0;
	while (*str) {
		hash += *str + 15 * hash;
		str++;
	}
	return hash;
}

static void scene_load_geometry(Scene* scene, unsigned int index, const struct aiScene* aiScn, unsigned int materialOffset);
static void scene_load_materials(Scene* scene, const char* path, const struct aiScene* aiScn);
static void scene_load_texture(Scene* scene, Texture** texture, const char* path, const struct aiMaterial* aiMat, enum aiTextureType type);
static void scene_load_node(Scene* scene, Node** node, const struct aiScene* aiScn, const struct aiNode* aiNd, Node* parent, unsigned int geometryIdx, unsigned int partOffset);
static void node_world_transform(Node* node, mat4 dest);

void scene_init(Scene* scene) {
	glCreateBuffers(1, &scene->material_buffer);
	glNamedBufferData(scene->material_buffer, 32 * MATERIAL_MAX, NULL, GL_STATIC_DRAW);
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

	for (unsigned int i = 0; i < TEXTURE_MAX; i++) {
		Texture* t = &scene->textures[i];
		if (!t->key) continue;
		if (t->handle) glMakeTextureHandleNonResidentARB(t->handle);
		if (t->texture) glDeleteTextures(1, &t->texture);
		*t = (Texture){ 0 };
	}
	for (unsigned int i = 0; i < scene->n_materials; i++) {
		scene->materials[i] = (Material){ 0 };
	}

	for (unsigned int i = 0; i < scene->n_nodes; i++) {
		node_delete(&scene->nodes[i]);
	}
}

void scene_load(Scene* scene, const char* path, unsigned int geometryIdx, mat4 initialTransform, bool flipUVs) {
	const struct aiScene* aiScn = aiImportFile(
		path,
		(flipUVs ? aiProcess_FlipUVs : 0) | aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType
	);
	if (!aiScn || aiScn->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !aiScn->mRootNode) {
		plogf(LL_ERROR, "Failed to load model: %s. %s\n", path, aiGetErrorString());
	}

	unsigned int partOffset = scene->geometry[geometryIdx].n_parts;
	scene_load_geometry(scene, geometryIdx, aiScn, scene->n_materials);
	scene_load_materials(scene, path, aiScn);
	Node** node = &scene->nodes[scene->n_nodes++];
	scene_load_node(
		scene,
		node,
		aiScn,
		aiScn->mRootNode,
		NULL,
		geometryIdx,
		partOffset
	);
	plogf(LL_INFO, "Applying transform\n");
	glm_mat4_copy(initialTransform, (*node)->transform);
}

static int part_compare(const void* a, const void* b) {
	if (!a) return -1;
	if (!b) return 1;
	const Part *p = a, *q = b;
	int n_index = p->n_index - q->n_index;
	int base_index = p->base_index - q->base_index;
	int base_vertex = p->base_vertex - q->base_vertex;
	if (!n_index && !base_index && !base_vertex) return 0;
	return p - q;
}

static int cache_part_compare(const void* a, const void* b) {
	if (!a) return -1;
	if (!b) return 1;
	const CachePart *p = a, *q = b;
	int geometry = p->node->geometry - q->node->geometry;
	if (geometry) return geometry;
	return part_compare(p->part, q->part);
}

void scene_build_cache(Scene* scene) {
	// Count total parts in scene to allocate cache
	unsigned int partCount = 0;
	for (unsigned int i = 0; i < scene->n_nodes; i++) {
		// Traverse each tree
		Node* node = scene->nodes[i];
		Node* queue[128];
		unsigned int nQueue = 1;
		queue[0] = node;
		while (nQueue) {
			Node* n = queue[--nQueue];
			// Increment part total
			partCount += n->n_parts;
			// Traverse children
			for (unsigned int j = 0; j < n->n_children; j++)
				queue[nQueue++] = node_children(n)[j];
		}
	}
	
	// Build parts list
	// Same loop as in counting, this time copy the data
	unsigned int n_parts = 0;
	CachePart* parts = malloc(sizeof(CachePart) * partCount);
	for (unsigned int i = 0; i < scene->n_nodes; i++) {
		Node* node = scene->nodes[i];
		Node* queue[128];
		unsigned int nQueue = 1;
		queue[0] = node;
		while (nQueue) {
			Node* n = queue[--nQueue];
			for (unsigned int j = 0; j < n->n_parts; j++) {
				CachePart* cached = &parts[n_parts++];
				cached->part = node_parts(n)[j];
				cached->node = n;
			}
			for (unsigned int j = 0; j < n->n_children; j++)
				queue[nQueue++] = node_children(n)[j];
		}
	}
	// Sort parts by geometry and part to instance identical parts
	qsort(parts, n_parts, sizeof(CachePart), cache_part_compare);

	DrawIndirectCommand* commands = malloc(sizeof(DrawIndirectCommand) * TRANSFORM_MAX);
	mat4* transform = malloc(sizeof(mat4) * TRANSFORM_MAX);
	unsigned int nTransform = 0;

	// Build render cache
	// New cacheobject when geometry changes
	// New cachepart when part changes, same parts increment instance
	Geometry* currentGeometry = NULL;
	CacheObject* currentCache = NULL;
	Part* currentPart = NULL;
	DrawIndirectCommand* command = NULL;
	for (unsigned int i = 0; i < n_parts; i++) {
		CachePart* cachePart = &parts[i];
		// Switch object if geometry changes
		if (cachePart->node->geometry != currentGeometry) {
			plogf(LL_INFO, "Switching geometry\n");
			// Write commands for old geometry
			if (currentGeometry) {
				plogf(LL_INFO, "Writing indirect buffer\n");
				glCreateBuffers(1, &currentGeometry->indirect_buffer);
				glNamedBufferData(
					currentGeometry->indirect_buffer,
					currentCache->n_commands * sizeof(DrawIndirectCommand),
					commands,
					GL_STATIC_DRAW
				);
			}
			// Setup new geometry
			currentGeometry = cachePart->node->geometry;
			currentCache = &scene->cache[scene->n_cache++];
			currentCache->geometry = currentGeometry;
		}
		// Switch command if part changes (vertices/indices, not on material change)
		if (part_compare(cachePart->part, currentPart)) {
			currentPart = cachePart->part;
			command = &commands[currentCache->n_commands++];
			// Initialize new command
			command->n_index = currentPart->n_index;
			command->n_instance = 0;
			command->base_index = currentPart->base_index;
			command->base_vertex = currentPart->base_vertex;
			command->base_instance = nTransform;
		}
		// Setup instance transform and assign
		command->n_instance++;
		node_world_transform(cachePart->node, transform[nTransform]);
		ivec2 assign = { cachePart->part->material, nTransform };
		glNamedBufferSubData(scene->assign_buffer, nTransform * sizeof(ivec2), sizeof(ivec2), assign);
		nTransform++;
	}
	free(parts);
	// Last processed geometry didn't get switched, save it (if parts > 0)
	if (currentGeometry) {
		plogf(LL_INFO, "Writing indirect buffer\n");
		glCreateBuffers(1, &currentGeometry->indirect_buffer);
		glNamedBufferData(
			currentGeometry->indirect_buffer,
			currentCache->n_commands * sizeof(DrawIndirectCommand),
			commands,
			GL_STATIC_DRAW
		);
	}
	free(commands);
	// Buffer transforms
	glNamedBufferSubData(scene->transform_buffer, 0, sizeof(mat4) * nTransform, transform);
	free(transform);
	
	// Buffer materials
	for (unsigned int i = 0; i < scene->n_materials; i++) {
		Material* mat = &scene->materials[i];
		if (mat->diffuse && mat->diffuse->texture) {
			if (!mat->diffuse->handle) {
				mat->diffuse->handle = glGetTextureHandleARB(mat->diffuse->texture);
				glMakeTextureHandleResidentARB(mat->diffuse->handle);
			}
			glNamedBufferSubData(scene->material_buffer, i * 32, 8, &mat->diffuse->handle);
		}
		if (mat->specular && mat->specular->texture) {
			if (!mat->specular->handle) {
				mat->specular->handle = glGetTextureHandleARB(mat->specular->texture);
				glMakeTextureHandleResidentARB(mat->specular->handle);
				glNamedBufferSubData(scene->material_buffer, i * 32 + 8, 8, &mat->specular->handle);
			}
		}
		if (mat->normal && mat->normal->texture) {
			if (!mat->normal->handle) {
				mat->normal->handle = glGetTextureHandleARB(mat->normal->texture);
				glMakeTextureHandleResidentARB(mat->normal->handle);
			}
			glNamedBufferSubData(scene->material_buffer, i * 32 + 16, 8, &mat->normal->handle);
		}
		glNamedBufferSubData(scene->material_buffer, i * 32 + 24, sizeof(float), &mat->shininess);
	}
}

void scene_render(Scene* scene) {
	for (unsigned int i = 0; i < scene->n_cache; i++) {
		CacheObject* cached = &scene->cache[i];
		glBindVertexArray(cached->geometry->vertex_array);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cached->geometry->indirect_buffer);
		glMultiDrawElementsIndirect(cached->geometry->primitive, GL_UNSIGNED_INT, 0, cached->n_commands, 0);
	}
}

Node* node_new(unsigned int nParts, unsigned int nChildren) {
	Node* node = calloc(1, offsetof(Node, data) + sizeof(Part*) * nParts + sizeof(Node*) * nChildren);
	if (!node) return NULL;
	node->n_parts = nParts;
	node->n_children = nChildren;
	return node;
}

void node_delete(Node** node) {
	if (!node || !*node) return;
	for (unsigned int i = 0; i < (*node)->n_children; i++)
		node_delete(&node_children(*node)[i]);
	free(*node);
	*node = NULL;
}

void node_resize(Node** node, unsigned int nParts, unsigned int nChildren) {
	if (!node || !*node) return;
	Node* new = node_new(nParts, nChildren);
	if (!new) return;
	memcpy(node_parts(new), node_parts(*node), MIN(nParts, (*node)->n_parts) * sizeof(Part*));
	memcpy(node_children(new), node_children(*node), MIN(nChildren, (*node)->n_children) * sizeof(Node*));
	// Delete children that dont fit in new size
	for (unsigned int i = nChildren; i < (*node)->n_children; i++)
		node_delete(&node_children(*node)[i]);
	free(*node);
	*node = new;
}

static void scene_load_geometry(Scene* scene, unsigned int geometryIndex, const struct aiScene* aiScn, unsigned int materialOffset) {
	if (geometryIndex > GEOMETRY_MAX) {
		plogf(LL_ERROR, "Geometry out of bounds\n");
		return;
	}
	Geometry* g = &scene->geometry[geometryIndex];

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
		p->base_vertex = vIdx + g->n_vertices;
		p->base_index = iIdx + g->n_indices;

		const struct aiMesh* aiMsh = aiScn->mMeshes[i];
		
		p->material = materialOffset + aiMsh->mMaterialIndex;

		for (unsigned int j = 0; j < aiMsh->mNumVertices; j++) {
			Vertex* v = &vertices[vIdx++];
			glm_vec3_copy((vec3){	aiMsh->mVertices[j].x, aiMsh->mVertices[j].y,	aiMsh->mVertices[j].z	}, v->position);
			glm_vec2_copy((vec2){ aiMsh->mTextureCoords[0][j].x, aiMsh->mTextureCoords[0][j].y }, v->texCoord);
			glm_vec3_copy((vec3){ aiMsh->mNormals[j].x, aiMsh->mNormals[j].y, aiMsh->mNormals[j].z }, v->normal);
			glm_vec3_copy((vec3){ aiMsh->mTangents[j].x, aiMsh->mTangents[j].y, aiMsh->mTangents[j].z }, v->tangent);
			glm_vec3_copy((vec3){ aiMsh->mBitangents[j].x, aiMsh->mBitangents[j].y, aiMsh->mBitangents[j].z }, v->bitangent);
		}
		p->n_index = 0;
		for (unsigned int j = 0; j < aiMsh->mNumFaces; j++) {
			p->n_index += aiMsh->mFaces[j].mNumIndices;
			for (unsigned int k = 0; k < aiMsh->mFaces[j].mNumIndices; k++)
				indices[iIdx++] = aiMsh->mFaces[j].mIndices[k];
		}
	}

	if (g->vertex_array) {
		// Resize
		plogf(LL_INFO, "Resizing existing Geometry buffers\n");
		size_t newVertices = g->n_vertices + nVertices;
		size_t newIndices = g->n_indices + nIndices;

		unsigned int vertexBuffer = 0;
		glCreateBuffers(1, &vertexBuffer);
		glNamedBufferData(vertexBuffer, newVertices * sizeof(Vertex), NULL, GL_STATIC_DRAW);
		glCopyNamedBufferSubData(g->vertex_buffer, vertexBuffer, 0, 0, sizeof(Vertex) * g->n_vertices);
		glNamedBufferSubData(vertexBuffer, sizeof(Vertex) * g->n_vertices, sizeof(Vertex) * nVertices, vertices);
		g->n_vertices = newVertices;
		glDeleteBuffers(1, &g->vertex_buffer);
		g->vertex_buffer = vertexBuffer;

		unsigned int elementBuffer = 0;
		glCreateBuffers(1, &elementBuffer);
		glNamedBufferData(elementBuffer, newIndices * sizeof(unsigned int), NULL, GL_STATIC_DRAW);
		glCopyNamedBufferSubData(g->element_buffer, elementBuffer, 0, 0, sizeof(unsigned int) * g->n_indices);
		glNamedBufferSubData(elementBuffer, sizeof(unsigned int) * g->n_indices, sizeof(unsigned int) * nIndices, indices);
		g->n_indices = newIndices;
		glDeleteBuffers(1, &g->element_buffer);
		g->element_buffer = elementBuffer;
	} else {
		plogf(LL_INFO, "Creating new Geometry buffers\n");
		glCreateVertexArrays(1, &g->vertex_array);
		glCreateBuffers(1, &g->vertex_buffer);
		glNamedBufferData(g->vertex_buffer, nVertices * sizeof(Vertex), vertices, GL_STATIC_DRAW);
		g->n_vertices = nVertices;
		glCreateBuffers(1, &g->element_buffer);
		glNamedBufferData(g->element_buffer, nIndices * sizeof(unsigned int), indices, GL_STATIC_DRAW);
		g->n_indices = nIndices;
		
		scene->n_geometry++;
		g->primitive = GL_TRIANGLES;

		glEnableVertexArrayAttrib(g->vertex_array, ATTR_POSITION);
		glVertexArrayAttribBinding(g->vertex_array, ATTR_POSITION, 0);
		glVertexArrayAttribFormat(g->vertex_array, ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
		
		glEnableVertexArrayAttrib(g->vertex_array, ATTR_TEXCOORD);
		glVertexArrayAttribBinding(g->vertex_array, ATTR_TEXCOORD, 0);
		glVertexArrayAttribFormat(g->vertex_array, ATTR_TEXCOORD, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, texCoord));
		
		glEnableVertexArrayAttrib(g->vertex_array, ATTR_NORMAL);
		glVertexArrayAttribBinding(g->vertex_array, ATTR_NORMAL, 0);
		glVertexArrayAttribFormat(g->vertex_array, ATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
		
		glEnableVertexArrayAttrib(g->vertex_array, ATTR_TANGENT);
		glVertexArrayAttribBinding(g->vertex_array, ATTR_TANGENT, 0);
		glVertexArrayAttribFormat(g->vertex_array, ATTR_TANGENT, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, tangent));
		
		glEnableVertexArrayAttrib(g->vertex_array, ATTR_BITANGENT);
		glVertexArrayAttribBinding(g->vertex_array, ATTR_BITANGENT, 0);
		glVertexArrayAttribFormat(g->vertex_array, ATTR_BITANGENT, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, bitangent));

		glEnableVertexArrayAttrib(g->vertex_array, ATTR_ASSIGN);
		glVertexArrayAttribBinding(g->vertex_array, ATTR_ASSIGN, 1);
		glVertexArrayAttribIFormat(g->vertex_array, ATTR_ASSIGN, 2, GL_INT, 0);
		glVertexArrayVertexBuffer(g->vertex_array, 1, scene->assign_buffer, 0, sizeof(ivec2));
		glVertexArrayBindingDivisor(g->vertex_array, 1, 1);
	}

	glVertexArrayVertexBuffer(g->vertex_array, 0, g->vertex_buffer, 0, sizeof(Vertex));
	glVertexArrayElementBuffer(g->vertex_array, g->element_buffer);

	plogf(LL_INFO, "Created geometry[%u] { vao:%u, vbo:%u, ebo:%u }; %lu vertices, %lu indices\n",
		geometryIndex, g->vertex_array, g->vertex_buffer, g->element_buffer, vIdx, iIdx);
}

static void scene_load_materials(Scene* scene, const char* path, const struct aiScene* aiScn) {
	for (unsigned int i = 0; i < aiScn->mNumMaterials; i++) {
		Material* mat = &scene->materials[scene->n_materials++];
		const struct aiMaterial* aiMat = aiScn->mMaterials[i];
		if (aiGetMaterialTextureCount(aiMat, aiTextureType_DIFFUSE)) {
			scene_load_texture(scene, &mat->diffuse, path, aiMat, aiTextureType_DIFFUSE);
		}
		if (aiGetMaterialTextureCount(aiMat, aiTextureType_SPECULAR)) {
			scene_load_texture(scene, &mat->specular, path, aiMat, aiTextureType_SPECULAR);
		}
		if (aiGetMaterialTextureCount(aiMat, aiTextureType_HEIGHT)) {
			scene_load_texture(scene, &mat->normal, path, aiMat, aiTextureType_HEIGHT);
		}
		ai_real shininess = 32.0f;
		mat->shininess = shininess;
		plogf(LL_INFO, "Created material[%u] { %u, %u, %u }\n", i, 
			mat->diffuse ? mat->diffuse->texture : 0,
			mat->specular ? mat->specular->texture : 0,
			mat->normal ? mat->normal->texture : 0
		);
	}
}

Texture* scene_find_texture(Scene* scene, unsigned long long key) {
	unsigned long long index = key % TEXTURE_MAX;
	for (unsigned long long i = 0; i < TEXTURE_MAX; i++) {
		Texture* texture = &scene->textures[(index + i) % TEXTURE_MAX];
		if (texture->key == key) return texture;
		if (texture->key == 0) return NULL;
	}
	return NULL;
}

Texture* scene_insert_texture(Scene* scene, unsigned long long key, unsigned int texture) {
	unsigned long long index = key % TEXTURE_MAX;
	for (unsigned long long i = 0; i < TEXTURE_MAX; i++) {
		Texture* cached = &scene->textures[(index + i) % TEXTURE_MAX];
		if (cached->key == key) return cached;
		if (cached->key == 0) {
			cached->key = key;
			cached->texture = texture;
			return cached;
		} 
	}
	return NULL;
}

static void scene_load_texture(Scene* scene, Texture** texture, const char* path, const struct aiMaterial* aiMat, enum aiTextureType type) {
	struct aiString name;
	aiGetMaterialTexture(aiMat, type, 0, &name, NULL, NULL, NULL, NULL, NULL, NULL);

	size_t bufferSize = strlen(path) + name.length + 2;
	char buffer[bufferSize];
	strcpy(buffer, path);
	char* dirMark = strrchr(buffer, '/');
	strcpy(dirMark + 1, name.data);

	unsigned long long key = strhash(buffer);
	Texture* cached = scene_find_texture(scene, key);
	if (cached) {
		*texture = cached;
		return;
	}
	unsigned int id = 0;
	if (!load_texture(&id, buffer, true, GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR)) {
		plogf(LL_ERROR, "Failed to load material texture: %s\n", buffer);
	}
	*texture = scene_insert_texture(scene, key, id);
	plogf(LL_INFO, "Loaded texture: %s : %llu\n", buffer, key);
}

static void scene_load_node(Scene* scene, Node** node, const struct aiScene* aiScn, const struct aiNode* aiNd, Node* parent, unsigned int geometryIdx, unsigned int partOffset) {
	Node* nd = node_new(aiNd->mNumMeshes, aiNd->mNumChildren);
	plogf(LL_INFO, "Created node: %s\n", aiNd->mName.data);
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

	nd->geometry = &scene->geometry[geometryIdx];

	for (unsigned int i = 0; i < aiNd->mNumMeshes; i++) {
		node_parts(nd)[i] = &nd->geometry->parts[partOffset + aiNd->mMeshes[i]];
	}
	for (unsigned int i = 0; i < aiNd->mNumChildren; i++) {
		scene_load_node(
			scene,
			&node_children(nd)[i],
			aiScn,
			aiNd->mChildren[i],
			nd,
			geometryIdx,
			partOffset
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
