#include "window.h"

#include "internal.h"
#include "renderer.h"

/* clang-format off */
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
/* clang-format on */

OWL_INTERNAL enum owl_mouse_btn owl_to_mouse_btn_(int type) {
  switch (type) {
  case GLFW_MOUSE_BUTTON_LEFT:
    return OWL_MOUSE_BUTTON_LEFT;

  case GLFW_MOUSE_BUTTON_MIDDLE:
    return OWL_MOUSE_BUTTON_MIDDLE;

  case GLFW_MOUSE_BUTTON_RIGHT:
    return OWL_MOUSE_BUTTON_RIGHT;

  default:
    return 0;
  }
}

OWL_INTERNAL enum owl_btn_state owl_to_mouse_state_(int state) {
  switch (state) {
  case GLFW_PRESS:
    return OWL_BUTTON_STATE_PRESS;

  case GLFW_RELEASE:
    return OWL_BUTTON_STATE_RELEASE;

  case GLFW_REPEAT:
    return OWL_BUTTON_STATE_REPEAT;

  default:
    return OWL_BUTTON_STATE_NONE;
  }
}

OWL_INTERNAL void owl_window_cb_(GLFWwindow *window, int width, int height) {
  struct owl_window *w = glfwGetWindowUserPointer(window);

  w->window_width = width;
  w->window_height = height;
}

OWL_INTERNAL void owl_framebuffer_cb_(GLFWwindow *handle, int width,
                                      int height) {
  struct owl_window *w = glfwGetWindowUserPointer(handle);

  w->framebuffer_width = width;
  w->framebuffer_height = height;
}

OWL_INTERNAL void owl_cursor_pos_cb_(GLFWwindow *handle, double x, double y) {
  struct owl_window *w = glfwGetWindowUserPointer(handle);

  w->input.cur_cursor_pos[0] =
      2.0F * ((float)x / (float)w->window_width) - 1.0F;

  w->input.cur_cursor_pos[1] =
      2.0F * ((float)y / (float)w->window_height) - 1.0F;
}

OWL_INTERNAL void owl_mouse_cb_(GLFWwindow *window, int button, int action,
                                int modifiers) {
  struct owl_window *w = glfwGetWindowUserPointer(window);
  enum owl_mouse_btn b = owl_to_mouse_btn_(button);

  OWL_UNUSED(modifiers);

  w->input.mouse[b] = owl_to_mouse_state_(action);
}

OWL_INTERNAL void owl_keyboard_cb_(GLFWwindow *window, int key, int scancode,
                                   int action, int mods) {
  struct owl_window *w = glfwGetWindowUserPointer(window);

  OWL_UNUSED(scancode);
  OWL_UNUSED(action);
  OWL_UNUSED(mods);

  w->input.keyboard[key] = owl_to_mouse_state_(action);
}

OWL_INTERNAL enum owl_code
owl_init_vk_surface_cb_(struct owl_vk_renderer const *renderer,
                        void const *data, VkSurfaceKHR *out) {
  struct owl_window const *w = data;
  int err = glfwCreateWindowSurface(renderer->instance, w->handle, NULL, out);

  if (VK_SUCCESS != err)
    return OWL_ERROR_BAD_INIT;

  return OWL_SUCCESS;
}

OWL_INTERNAL void owl_init_timer_(struct owl_window *window) {
  window->input.cur_time = 0.0;
  window->input.past_time = 0.0;
  window->input.dt_time = 0.16667;
}

OWL_INTERNAL void owl_update_timer_(struct owl_window *window) {
  window->input.past_time = window->input.cur_time;
  window->input.cur_time = glfwGetTime();
  window->input.dt_time = window->input.cur_time - window->input.past_time;
}

OWL_INTERNAL void owl_update_prev_cursor_(struct owl_window *window) {
  OWL_COPY_V2(window->input.cur_cursor_pos, window->input.prev_cursor_pos);
}

OWL_GLOBAL int g_window_count = 0;

enum owl_code owl_init_window(struct owl_window_desc const *desc,
                              struct owl_input_state **input,
                              struct owl_window *window) {
  if (!g_window_count)
    if (GLFW_FALSE == glfwInit())
      return OWL_ERROR_BAD_INIT;

  if (!input)
    return OWL_ERROR_BAD_PTR;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window->handle =
      glfwCreateWindow(desc->width, desc->height, desc->title, NULL, NULL);

  ++g_window_count;

  glfwSetWindowUserPointer(window->handle, window);
  glfwGetWindowSize(window->handle, &window->window_width,
                    &window->window_height);
  glfwGetFramebufferSize(window->handle, &window->framebuffer_width,
                         &window->framebuffer_height);
  glfwSetWindowSizeCallback(window->handle, owl_window_cb_);
  glfwSetFramebufferSizeCallback(window->handle, owl_framebuffer_cb_);
  glfwSetCursorPosCallback(window->handle, owl_cursor_pos_cb_);
  glfwSetMouseButtonCallback(window->handle, owl_mouse_cb_);
  glfwSetKeyCallback(window->handle, owl_keyboard_cb_);

  OWL_ZERO_V2(window->input.prev_cursor_pos);
  OWL_ZERO_V2(window->input.cur_cursor_pos);

  {
    int i;
    for (i = 0; i < OWL_MOUSE_BUTTON_COUNT; ++i)
      window->input.mouse[i] = OWL_BUTTON_STATE_NONE;

    for (i = 0; i < OWL_KEYBOARD_KEY_LAST; ++i)
      window->input.keyboard[i] = OWL_BUTTON_STATE_NONE;
  }

  owl_init_timer_(window);

  *input = &window->input;

  return OWL_SUCCESS;
}

void owl_deinit_window(struct owl_window *window) {
  glfwDestroyWindow(window->handle);

  if (!--g_window_count)
    glfwTerminate();
}

#ifndef NDEBUG
#define OWL_MAX_EXTENSIONS 64
enum owl_code owl_fill_vk_plataform(struct owl_window const *window,
                                    struct owl_vk_plataform *plataform) {
  OwlU32 count;
  OWL_LOCAL_PERSIST char const *names[OWL_MAX_EXTENSIONS];

  plataform->framebuffer_width = (OwlU32)window->framebuffer_width;
  plataform->framebuffer_height = (OwlU32)window->framebuffer_height;

  plataform->surface_user_data = window;
  plataform->create_surface = owl_init_vk_surface_cb_;
  plataform->instance_extensions = glfwGetRequiredInstanceExtensions(&count);

  OWL_ASSERT(OWL_MAX_EXTENSIONS > count);

  OWL_MEMCPY(names, plataform->instance_extensions,
             count * sizeof(char const *));
  names[count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

  plataform->instance_extensions = names;
  plataform->instance_extension_count = count;

  return OWL_SUCCESS;
}
#undef OWL_MAX_EXTENSIONS
#else
enum owl_code owl_fill_vk_plataform(struct owl_window const *window,
                                    struct owl_vk_plataform *config) {
  OwlU32 count;

  config->framebuffer_width = (OwlU32)window->framebuffer_width;
  config->framebuffer_height = (OwlU32)window->framebuffer_height;

  config->surface_user_data = window;
  config->create_surface = owl_init_vk_surface_cb_;

  config->instance_extensions = glfwGetRequiredInstanceExtensions(&count);
  config->instance_extension_count = count;

  return OWL_SUCCESS;
}
#endif

int owl_is_window_done(struct owl_window *window) {
  return glfwWindowShouldClose(window->handle);
}

void owl_poll_window_input(struct owl_window *window) {
  owl_update_prev_cursor_(window);
  glfwPollEvents();
  owl_update_timer_(window);
}

enum owl_code owl_create_window(struct owl_window_desc const *desc,
                                struct owl_input_state **input,
                                struct owl_window **window) {
  enum owl_code err = OWL_SUCCESS;

  if (!(*window = OWL_MALLOC(sizeof(**window))))
    return OWL_ERROR_BAD_ALLOC;

  err = owl_init_window(desc, input, *window);

  if (OWL_SUCCESS != err)
    OWL_FREE(*window);

  return err;
}

void owl_destroy_window(struct owl_window *window) {
  owl_deinit_window(window);
  OWL_FREE(window);
}
