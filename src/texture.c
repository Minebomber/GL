#include "texture.h"

#include "stb_image.h"
#include <glad/glad.h>
#include <math.h>
#include "log.h"

bool load_texture(unsigned int* id, const char* path, bool mipmap, int wrapS, int wrapT, int minFilter, int magFilter) {
	int width, height, nrComponents;
	unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);
	if (!data) {
		plogf(LL_ERROR, "Failed to load texture: %s\n", path);
		return false;
	}

	GLenum fmt = 0, ifmt = 0;
	if (nrComponents == 1) {
		fmt = GL_RED;
		ifmt = GL_R8;
	}	else if (nrComponents == 3) {
		fmt = GL_RGB;
		ifmt = GL_RGB8;
	}	else if (nrComponents == 4) {
		fmt = GL_RGBA;
		ifmt = GL_RGBA8;
	}	

	glCreateTextures(GL_TEXTURE_2D, 1, id);
	glTextureParameteri(*id, GL_TEXTURE_WRAP_S, wrapS);
	glTextureParameteri(*id, GL_TEXTURE_WRAP_T, wrapT);
	glTextureParameteri(*id, GL_TEXTURE_MIN_FILTER, minFilter);
	glTextureParameteri(*id, GL_TEXTURE_MAG_FILTER, magFilter);

	int levels = (mipmap) ? 1 + floor(log2(fmax(width, height))) : 1;
	glTextureStorage2D(*id, levels, ifmt, width, height);
	glTextureSubImage2D(*id, 0, 0, 0, width, height, fmt, GL_UNSIGNED_BYTE, data);
	glGenerateTextureMipmap(*id);

	return true;
}