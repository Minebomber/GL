#include "shader.h"
#include <glad/glad.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "log.h"

static char* read_file_contents(const char* path);
static bool verify(GLuint id, GLenum status, void (*get_iv)(GLuint, GLenum, GLint*), void (*get_log)(GLuint, GLsizei, GLsizei*, GLchar*));
static unsigned int compile_shader(const char* source, GLenum shader, GLenum status, void (*get_iv)(GLuint, GLenum, GLint*), void (*get_log)(GLuint, GLsizei, GLsizei*, GLchar*));

void create_shader(unsigned int* id, unsigned int count, ...) {
	if (!id) return;
	unsigned int shaders[count];
	unsigned int program = glCreateProgram();
	va_list ptr;
	va_start(ptr, count);
	for (unsigned int i = 0; i < count; i++) {
		ShaderArgs args = va_arg(ptr, ShaderArgs);
		char* source = read_file_contents(args.path);
		if (!source) plogf(LL_ERROR, "Cannot load shader: %s\n", args.path);
		shaders[i] = compile_shader(source, args.shader, GL_COMPILE_STATUS, glGetShaderiv, glGetShaderInfoLog);
		if (!shaders[i]) plogf(LL_ERROR, "Shader:%u compilation failed\n", args.shader);
		free(source);
		glAttachShader(program, shaders[i]);
	}
	va_end(ptr);
	glLinkProgram(program);
	if (!verify(program, GL_LINK_STATUS, glGetProgramiv, glGetProgramInfoLog)) {
		glDeleteProgram(program);
		*id = 0;
	} else {
		*id = program;
	}
	for (unsigned int i = 0; i < count; i++) glDeleteShader(shaders[i]);
}

static char* read_file_contents(const char* path) {
	FILE* fp = fopen(path, "r");
	if (!fp) return NULL;
	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	rewind(fp);

	char* buffer = malloc(size + 1);
	fread(buffer, size, 1, fp);
	fclose(fp);
	buffer[size] = 0;
	return buffer;
}

static bool verify(GLuint id, GLenum status, void (*get_iv)(GLuint, GLenum, GLint*), void (*get_log)(GLuint, GLsizei, GLsizei*, GLchar*)) {
	int success;
	char infoLog[2048];
	get_iv(id, status, &success);
	if (!success) {
		get_log(id, 2048, NULL, infoLog);
		plogf(LL_ERROR, "%s\n", infoLog);
	}
	return success;
}

static unsigned int compile_shader(const char* source, GLenum shader, GLenum status, void (*get_iv)(GLuint, GLenum, GLint*), void (*get_log)(GLuint, GLsizei, GLsizei*, GLchar*)) {
	unsigned int id = glCreateShader(shader);
	glShaderSource(id, 1, &source, NULL);
	glCompileShader(id);
	if (!verify(id, status, get_iv, get_log))
		return 0;
	return id;
}
