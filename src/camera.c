#include "camera.h"

void camera_init(Camera* c, float width, float height, float fov, float near, float far) {
	c->vp_width = width;
	c->vp_height = height;
	c->fov = fov;
	c->z_near = near;
	c->z_far = far;
	c->update_projection = true;

	glm_vec3_copy((vec3){0, 0, 0}, c->position);
	glm_quat_identity(c->rotation);
	glm_vec3_copy((vec3){0, 0, -1}, c->front);
	glm_vec3_copy((vec3){1, 0, 0}, c->right);
	glm_vec3_copy((vec3){0, 1, 0}, c->up);
	c->update_view = true;
}

void camera_update_projection(Camera *c) {
	glm_perspective(c->fov, (float)c->vp_width / (float)c->vp_height, c->z_near, c->z_far, c->perspective);
	c->update_projection = false;
}

void camera_update_view(Camera* c) {
	glm_quat_rotatev(c->rotation, (vec3){0, 0, -1}, c->front);
	glm_vec3_crossn(c->front, c->up, c->right);
	glm_look(c->position, c->front, c->up, c->view);
	c->update_view = false;
}

void camera_rotate_x(Camera* c, float angle) {
	versor q;
	glm_quatv(q, angle, c->right);
	glm_quat_mul(q, c->rotation, c->rotation);
	glm_quat_normalize(c->rotation);
}

void camera_rotate_y(Camera* c, float angle) {
	versor q;
	glm_quatv(q, angle, c->up);
	glm_quat_mul(q, c->rotation, c->rotation);
	glm_quat_normalize(c->rotation);
}
