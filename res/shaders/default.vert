#version 460 core
#extension GL_ARB_bindless_texture : require

layout (location = 0) in ivec2 i_assign;
layout (location = 1) in vec3 i_position;
layout (location = 2) in vec2 i_texCoord;
layout (location = 3) in vec3 i_normal;
layout (location = 4) in vec3 i_tangent;
layout (location = 5) in vec3 i_bitangent;

out VS_OUT {
	flat ivec2 assign;
	vec3 position;
	vec2 texCoord;
	vec3 normal;
	mat3 TBN;
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
	mat4 model = mat4(
		texelFetch(u_transforms, i_assign.y * 4 + 0),
		texelFetch(u_transforms, i_assign.y * 4 + 1),
		texelFetch(u_transforms, i_assign.y * 4 + 2),
		texelFetch(u_transforms, i_assign.y * 4 + 3)
	);

	vs_out.assign = i_assign;
	vs_out.position = vec3(model * vec4(i_position, 1.0));
	vs_out.texCoord = i_texCoord;
	mat3 normalMatrix = transpose(inverse(mat3(model)));
	vec3 N = normalize(normalMatrix * i_normal);
	vec3 T = normalize(normalMatrix * i_tangent);
	T = normalize(T - dot(T, N) * N);
	vec3 B = normalize(normalMatrix * i_bitangent);
	vs_out.normal = N;
	vs_out.TBN = mat3(T, B, N);

	gl_Position = u_projection * u_view * model * vec4(i_position, 1.0);
}