#version 460 core

layout (location = 1) in vec3 i_position;

out VS_OUT {
	vec3 texCoord;
} vs_out;

layout (std140, binding = 1) uniform Camera {
	mat4 u_projection;
	mat4 u_view;
	vec3 u_position;
};

void main() {
	vs_out.texCoord = i_position;
	gl_Position = (u_projection * mat4(mat3(u_view)) * vec4(i_position, 1.0)).xyww;
}
