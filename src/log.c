#include "log.h"

#include <time.h>
#include <stdio.h>
#include <glad/glad.h>

int plogf(enum LOG_LEVEL level, const char* format, ...) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	char buffer[32];
	time_t t = time(NULL);
	strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", localtime(&t));
	printf("%s.%03ld", buffer, ts.tv_nsec / 1000000);
	switch (level) {
	case LL_DEBUG:
		printf("\x1b[92m [DEBUG] \x1b[0m");
		break;
	case LL_INFO:
		printf("\x1b[94m [INFO] \x1b[0m");
		break;
	case LL_WARN:
		printf("\x1b[93m [WARNING] \x1b[0m");
		break;
	case LL_ERROR:
		printf("\x1b[31m [ERROR] \x1b[0m");
		break;
	}
	va_list ap;
	int res = 0;
	va_start(ap, format);
	res = vprintf(format, ap);
	va_end(ap);
	return res;
}

static const char* source_str(unsigned int source) {
	switch (source) {
		case GL_DEBUG_SOURCE_API: return "API";
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "WINDOW SYSTEM";
		case GL_DEBUG_SOURCE_SHADER_COMPILER: return "SHADER COMPILER";
		case GL_DEBUG_SOURCE_THIRD_PARTY: return "THIRD PARTY";
		case GL_DEBUG_SOURCE_APPLICATION: return "APPLICATION";
		case GL_DEBUG_SOURCE_OTHER: return "OTHER";
	}
	return "UNKNOWN";
}

static const char* type_str(unsigned int type) {
	switch (type)	{
		case GL_DEBUG_TYPE_ERROR: return "ERROR";
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "DEPRECATED_BEHAVIOR";
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "UNDEFINED_BEHAVIOR";
		case GL_DEBUG_TYPE_PORTABILITY: return "PORTABILITY";
		case GL_DEBUG_TYPE_PERFORMANCE: return "PERFORMANCE";
		case GL_DEBUG_TYPE_MARKER: return "MARKER";
		case GL_DEBUG_TYPE_OTHER: return "OTHER";
	}
	return "UNKNOWN";
}

static enum LOG_LEVEL severity_lvl(unsigned int severity) {
	switch (severity) {
		case GL_DEBUG_SEVERITY_NOTIFICATION: return LL_DEBUG;
		case GL_DEBUG_SEVERITY_LOW: return LL_INFO;
		case GL_DEBUG_SEVERITY_MEDIUM: return LL_WARN;
		case GL_DEBUG_SEVERITY_HIGH: return LL_ERROR;
	}
	return LL_INFO;
}

void gl_log(unsigned int source, unsigned int type, unsigned int id, unsigned int severity, int length, char const* message, void const* userParam) {
	plogf(severity_lvl(severity), "%s:%s (%u): %s\n", source_str(source), type_str(type), id, message);
}
