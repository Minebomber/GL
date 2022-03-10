#pragma once

#include <cglm/cglm.h>
#include <stdbool.h>

typedef struct {
	float vp_width, vp_height;
	float fov;
	float z_near, z_far;
	bool update_projection;
	mat4 perspective;
	mat4 ortho;

	vec3 position;
	versor rotation;
	vec3 front;
	vec3 right;
	vec3 up;
	bool update_view;
	mat4 view;
} Camera;

void camera_init(Camera* c, float width, float height, float fov, float zn, float zf);
void camera_update_projection(Camera *c);
void camera_update_view(Camera* c);
void camera_rotate_x(Camera* c, float angle);
void camera_rotate_y(Camera* c, float angle);
