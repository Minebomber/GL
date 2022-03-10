#pragma once

#include <stdarg.h>

typedef struct {
	unsigned int shader;
	const char* path;
} ShaderArgs;

void create_shader(unsigned int* id, unsigned int count, ...);
