#version 460 core
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

#define MATERIAL_MAX 32

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

#define LIGHT_MAX 8

in VS_OUT {
	flat ivec2 assign;
	vec3 position;
	vec2 texCoord;
	vec3 normal;
	mat3 TBN;
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
	uint64_t normal;
	float shininess;
};

layout (std140, binding = 2) uniform Materials {
	Material u_materials[MATERIAL_MAX];
};

struct Light {
	uint type;
	vec4 positionConstant;
	vec4 directionLinear;
	vec4 ambientQuadratic;
	vec4 diffuseCutOff;
	vec4 specularOuterCutOff;
};
layout (std140, binding = 3) uniform Lights {
	uint u_lightCount;
	Light u_lights[LIGHT_MAX];
};

vec3 lighting(Light light, vec3 normal, vec3 viewDirection, vec3 diffuseColor, vec3 specularColor, float shininess);

void main() {
	vec3 normal = (u_materials[fs_in.assign.x].normal > 0) ?
		normalize(fs_in.TBN * ((texture(sampler2D(u_materials[fs_in.assign.x].normal), fs_in.texCoord).rgb) * 2.0 - 1.0)) :
		normalize(fs_in.normal);

	vec3 viewDirection = normalize(u_position - fs_in.position);

	vec3 diffuseColor = texture(u_materials[fs_in.assign.x].diffuse, fs_in.texCoord).rgb;
	vec3 specularColor = texture(u_materials[fs_in.assign.x].specular, fs_in.texCoord).rgb;
	float shininess = u_materials[fs_in.assign.x].shininess;
	vec3 result = vec3(0);
	for (uint i = 0; i < u_lightCount; i++) {
		result += lighting(u_lights[i], normal, viewDirection, diffuseColor, specularColor, shininess);
	}

	o_fragColor = vec4(result, 1.0);

	// Display normals
	//o_fragColor = vec4((normal + 1.0) / 2.0, 1.0);
}

vec3 lighting(Light light, vec3 normal, vec3 viewDirection, vec3 diffuseColor, vec3 specularColor, float shininess) {
	vec3 ambient = light.ambientQuadratic.rgb * diffuseColor;

	vec3 lightDirection = (light.type == LIGHT_DIRECTIONAL) ? 
		normalize(-light.directionLinear.xyz) :
		normalize(light.positionConstant.xyz - fs_in.position);

	float diffuseFactor = max(dot(normal, lightDirection), 0.0);
	vec3 diffuse = light.diffuseCutOff.rgb * diffuseFactor * diffuseColor;
	
	vec3 halfwayDirection = normalize(lightDirection + viewDirection);
	float specularFactor = pow(max(dot(normal, halfwayDirection), 0.0), shininess);
	vec3 specular = light.specularOuterCutOff.rgb * specularFactor * specularColor;

	if (light.type == LIGHT_SPOT) {
		float theta = dot(lightDirection, normalize(-(light.directionLinear.xyz)));
		float epsilon = (light.diffuseCutOff.w - light.specularOuterCutOff.w);
		float intensity = clamp((theta - light.specularOuterCutOff.w) / epsilon, 0.0, 1.0);
		diffuse *= intensity;
		specular *= intensity;
	}

	if (light.type > LIGHT_DIRECTIONAL) {
		float d = length(light.positionConstant.xyz - fs_in.position);
		float attenuation = 1.0 / (
			light.positionConstant.w +
			light.directionLinear.w * d +
			light.ambientQuadratic.w * (d * d)
		);
		ambient *= attenuation;
		diffuse *= attenuation;
		specular *= attenuation;
	}

	return (ambient + diffuse + specular);	
}