#pragma once
#include <stdbool.h>

bool load_texture(unsigned int* id, const char* path, bool mipmap, int wrapS, int wrapT, int minFilter, int magFilter);