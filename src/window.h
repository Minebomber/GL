#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdbool.h>

#define EVENT_QUEUE_SIZE 48

typedef enum {
	EVENT_NONE,
	EVENT_KEYBOARD,
	EVENT_MOUSE_MOVE,
	EVENT_MOUSE_BUTTON,
	EVENT_MOUSE_WHEEL,
	EVENT_RESIZE,
} EVENT_TYPE;

typedef struct {
	EVENT_TYPE type;
	union {
		struct {
			int key;
			int scancode;
			int action;
			int mods;
		} keyboard;

		struct {
			double xpos;
			double ypos;
		} mouse_move;
		
		struct {
			int button;
			int action;
			int mods;
		} mouse_button;
		
		struct {
			double xoffset;
			double yoffset;
		} mouse_wheel;

		struct {
			int width;
			int height;
		} resize;
	};
} Event;

typedef struct {
	GLFWwindow* window;
	int width, height;
	Event events[EVENT_QUEUE_SIZE];
	size_t n_events;
} Window;

bool window_init(Window* w, int width, int height, const char* title);
