#version 460 core
#extension GL_ARB_bindless_texture : require

layout (location = 0) in ivec2 i_assign;
layout (location = 1) in vec3 i_position;
layout (location = 2) in vec2 i_texCoord;
layout (location = 3) in vec3 i_normal;

out VS_OUT {
	flat ivec2 assign;
	vec3 position;
	vec2 texCoord;
	vec3 normal;
} vs_out;

layout (std140, binding = 0) uniform Global {
	samplerBuffer u_transforms;
};

layout (std140, binding = 1) uniform Camera {
	mat4 u_projection;
	mat4 u_view;
	vec3 u_position;
};

void main() {
	//ivec2 assign = ivec2(gl_BaseInstance >> 16, gl_BaseInstance & 0xFFFF);

	mat4 model = mat4(
		texelFetch(u_transforms, i_assign.y * 4 + 0),
		texelFetch(u_transforms, i_assign.y * 4 + 1),
		texelFetch(u_transforms, i_assign.y * 4 + 2),
		texelFetch(u_transforms, i_assign.y * 4 + 3)
	);

	vs_out.assign = i_assign;
	vs_out.position = vec3(model * vec4(i_position, 1.0));
	vs_out.texCoord = i_texCoord;
	vs_out.normal = mat3(transpose(inverse(model))) * i_normal;

	gl_Position = u_projection * u_view * model * vec4(i_position, 1.0);
}