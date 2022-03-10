#version 460 core
#extension GL_ARB_bindless_texture : require

in VS_OUT {
	flat ivec2 assign;
	vec3 position;
	vec2 texCoord;
	vec3 normal;
} fs_in;

out vec4 o_fragColor;

layout (std140, binding = 1) uniform Camera {
	mat4 u_projection;
	mat4 u_view;
	vec3 u_position;
};

struct Material {
	sampler2D diffuse;
	sampler2D specular;
};

layout (std140, binding = 2) uniform Materials {
	Material u_materials[8];
};

void main() {
	vec3 diffuseColor = texture(u_materials[fs_in.assign.x].diffuse, fs_in.texCoord).rgb;
	vec3 specularColor = texture(u_materials[fs_in.assign.x].specular, fs_in.texCoord).rgb;
	
	vec3 ambient = vec3(0.2) * diffuseColor;
	
	vec3 normal = normalize(fs_in.normal);
	vec3 lightDirection = normalize(-vec3(0.5, -1.0, 0.25));
	float diffuseFactor = max(dot(normal, lightDirection), 0.0);
	vec3 diffuse = diffuseFactor * diffuseColor;
	
	vec3 viewDirection = normalize(u_position - fs_in.position);
	vec3 reflectDirection = reflect(-lightDirection, normal);
	float specularFactor = pow(max(dot(viewDirection, reflectDirection), 0.0), 32.0);
	vec3 specular = specularFactor * specularColor;
	
	o_fragColor = vec4(ambient + diffuse + specular, 1.0);
}