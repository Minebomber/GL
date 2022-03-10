#pragma once

#include <stdarg.h>

enum LOG_LEVEL {
	LL_DEBUG,
	LL_INFO,
	LL_WARN,
	LL_ERROR,
};

int plogf(enum LOG_LEVEL level, const char* format, ...);
void gl_log(unsigned int source, unsigned int type, unsigned int id, unsigned int severity, int length, char const* message, void const* userParam);