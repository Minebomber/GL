#version 460 core
#extension GL_ARB_bindless_texture : require

in VS_OUT {
	vec3 texCoord;
} fs_in;

out vec4 o_fragColor;

layout (std140, binding = 0) uniform Global {
	samplerBuffer u_transforms;
	samplerCube u_skybox;
};

void main() {
	o_fragColor = texture(u_skybox, fs_in.texCoord);
}
