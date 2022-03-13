#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cglm/cglm.h>

#define N_SIDE 8

#define GEOMETRY_MAX 8
#define MATERIAL_MAX 8
#define TRANSFORM_MAX N_SIDE * N_SIDE * N_SIDE * N_SIDE
#define NODE_MAX TRANSFORM_MAX
#define PART_MAX 8

enum ATTR_LOCATION {
	ATTR_ASSIGN,
	ATTR_POSITION,
	ATTR_TEXCOORD,
	ATTR_NORMAL,
	ATTR_TANGENT,
	ATTR_BITANGENT,
};

typedef struct {
	vec3 position;
	vec2 texCoord;
	vec3 normal;
	vec3 tangent;
	vec3 bitangent;
} Vertex;

typedef struct {
	unsigned int n_index;
	unsigned int base_index;
	unsigned int base_vertex;
	unsigned int material;
} Part;

typedef struct {
	unsigned int vertex_array;
	unsigned int vertex_buffer;
	unsigned int element_buffer;
	unsigned int indirect_buffer;
	unsigned int n_parts;
	Part parts[PART_MAX];
} Geometry;

typedef struct Node {
	struct Node* parent;
	mat4 transform;
	Geometry* geometry;
	unsigned int n_parts;
	unsigned int n_children;
	void* data;
} Node;

typedef struct {
	unsigned int texture;
	uint64_t handle;
} Texture;

typedef struct {
	Texture diffuse;
	Texture specular;
	Texture normal;
	float shininess;
} Material;

typedef struct {
	uint n_index;
	uint n_instance;
	uint base_index;
	uint base_vertex;
	uint base_instance;
} DrawIndirectCommand;

typedef struct {
	Geometry* geometry;
	unsigned int n_commands;
} CacheObject;

typedef struct {
	Part* part;
	Node* node;
} CachePart;

typedef struct {
	unsigned int assign_buffer;

	unsigned int n_materials;
	Material materials[MATERIAL_MAX];
	unsigned int material_buffer;

	unsigned int transform_buffer;
	unsigned int transform_texture;
	uint64_t transform_handle;

	unsigned int n_geometry;
	Geometry geometry[GEOMETRY_MAX];

	unsigned int n_nodes;
	Node* nodes[NODE_MAX];

	unsigned int n_cache;
	CacheObject cache[GEOMETRY_MAX];
} Scene;

void scene_init(Scene* scene);
void scene_destroy(Scene* scene);
void scene_load(Scene* scene, const char* path, mat4 initialTransform, bool flipUVs);
void scene_build_cache(Scene* scene);
void scene_render(Scene* scene);

Node* node_new(unsigned int nParts, unsigned int nChildren);
void node_delete(Node** node);
void node_resize(Node** node, unsigned int nParts, unsigned int nChildren);

inline Part** node_parts(const Node* node) {
	return (Part**)&node->data;
}

inline Node** node_children(const Node* node) {
	return (Node**)(&node->data + sizeof(Part*) * node->n_parts);
}