#include "window.h"
#include "log.h"

static void cb_keyboard(GLFWwindow* window, int key, int scancode, int action, int mods);
static void cb_mouse_move(GLFWwindow* window, double xpos, double ypos);
static void cb_mouse_button(GLFWwindow* window, int button, int action, int mods);
static void cb_mouse_wheel(GLFWwindow* window, double xoffset, double yoffset);
static void cb_resize(GLFWwindow* window, int width, int height);

bool window_init(Window* w, int width, int height, const char* title) {
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(width, height, title, NULL, NULL);
	if (window == NULL)	{
		plogf(LL_ERROR, "Failed to create GLFW window\n");
		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(window);

	*w = (Window) {
		.window = window,
		.width = width,
		.height = height,
		.n_events = 0
	};

	glfwSetWindowUserPointer(window, w);

	glfwSetKeyCallback(window, cb_keyboard);
	glfwSetCursorPosCallback(window, cb_mouse_move);
	glfwSetMouseButtonCallback(window, cb_mouse_button);
	glfwSetScrollCallback(window, cb_mouse_wheel);
	glfwSetFramebufferSizeCallback(window, cb_resize);

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		plogf(LL_ERROR, "Failed to initialize GLAD\n");
		glfwTerminate();
		return false;
	}

	return true;
}

static void cb_keyboard(GLFWwindow* window, int key, int scancode, int action, int mods) {
	Window* w = glfwGetWindowUserPointer(window);
	if (w->n_events == EVENT_QUEUE_SIZE) return;
	w->events[w->n_events++] = (Event) {
		.type = EVENT_KEYBOARD,
		.keyboard = {
			.key = key,
			.scancode = scancode,
			.action = action,
			.mods = mods
		}
	};
}

static void cb_mouse_move(GLFWwindow* window, double xpos, double ypos) {
	Window* w = glfwGetWindowUserPointer(window);
	if (w->n_events == EVENT_QUEUE_SIZE) return;
	w->events[w->n_events++] = (Event) {
		.type = EVENT_MOUSE_MOVE,
		.mouse_move = {
			.xpos = xpos,
			.ypos = ypos
		}
	};
}

static void cb_mouse_button(GLFWwindow* window, int button, int action, int mods) {
	Window* w = glfwGetWindowUserPointer(window);
	if (w->n_events == EVENT_QUEUE_SIZE) return;
	w->events[w->n_events++] = (Event) {
		.type = EVENT_MOUSE_BUTTON,
		.mouse_button = {
			.button = button,
			.action = action,
			.mods = mods
		}
	};
}

static void cb_mouse_wheel(GLFWwindow* window, double xoffset, double yoffset) {
	Window* w = glfwGetWindowUserPointer(window);
	if (w->n_events == EVENT_QUEUE_SIZE) return;
	w->events[w->n_events++] = (Event) {
		.type = EVENT_MOUSE_WHEEL,
		.mouse_wheel = {
			.xoffset = xoffset,
			.yoffset = yoffset
		}
	};
}

static void cb_resize(GLFWwindow* window, int width, int height) {
	Window* w = glfwGetWindowUserPointer(window);
	if (w->n_events == EVENT_QUEUE_SIZE) return;
	w->events[w->n_events++] = (Event) {
		.type = EVENT_RESIZE,
		.resize = {
			.width = width,
			.height = height
		}
	};
}
