#pragma once

#include <cglm/cglm.h>

#define LIGHT_SIZE 96

enum LIGHT_TYPE {
	LIGHT_DIRECTIONAL,
	LIGHT_POINT,
	LIGHT_SPOT
};

typedef struct {
	unsigned int type;
	vec4 positionConstant;
	vec4 directionLinear;
	vec4 ambientQuadratic;
	vec4 diffuseCutOff;
	vec4 specularOuterCutOff;
} Light;