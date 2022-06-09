#ifndef OWL_VK_RENDERER_H_
#define OWL_VK_RENDERER_H_

#include "owl_definitions.h"
#include "owl_vector.h"
#include "owl_vk_font.h"
#include "owl_vk_texture.h"

#include <vulkan/vulkan.h>

OWL_BEGIN_DECLS

#define OWL_MAX_SWAPCHAIN_IMAGES 8
#define OWL_NUM_IN_FLIGHT_FRAMES 3
#define OWL_MAX_GARBAGE_ITEMS 8
#define OWL_FONT_FIRST_CHAR ((int)(' '))
#define OWL_FONT_NUM_CHARS ((int)('~' - ' '))

enum owl_vk_pipeline {
  OWL_VK_PIPELINE_BASIC,
  OWL_VK_PIPELINE_WIRES,
  OWL_VK_PIPELINE_TEXT,
  OWL_VK_PIPELINE_MODEL,
  OWL_VK_PIPELINE_SKYBOX,
  OWL_VK_PIPELINE_NONE
};
#define OWL_VK_NUM_PIPELINES OWL_VK_PIPELINE_NONE

struct owl_plataform;

struct owl_vk_attachment {
  VkImage image;
  VkDeviceMemory memory;
  VkImageView image_view;
};

struct owl_vk_renderer {
  struct owl_plataform *plataform;

  owl_m4 projection;
  owl_m4 view;

  uint32_t width;
  uint32_t height;

  VkClearValue clear_values[2];

  VkInstance instance;
  VkDebugUtilsMessengerEXT debug;

  VkSurfaceKHR surface;
  VkSurfaceFormatKHR surface_format;

  VkPhysicalDevice physical_device;
  VkDevice device;
  uint32_t graphics_queue_family;
  uint32_t present_queue_family;
  VkQueue graphics_queue;
  VkQueue present_queue;

  VkSampleCountFlagBits msaa;
  VkFormat depth_format;

  struct owl_vk_attachment color_attachment;
  struct owl_vk_attachment depth_attachment;

  VkRenderPass main_render_pass;
  VkRenderPass offscreen_render_pass;

  VkCommandBuffer im_command_buffer;

  VkPresentModeKHR present_mode;

  VkSwapchainKHR swapchain;
  uint32_t image;
  uint32_t num_images;
  VkImage images[OWL_MAX_SWAPCHAIN_IMAGES];
  VkImageView image_views[OWL_MAX_SWAPCHAIN_IMAGES];
  VkFramebuffer framebuffers[OWL_MAX_SWAPCHAIN_IMAGES];

  VkCommandPool command_pool;
  VkDescriptorPool descriptor_pool;

  VkShaderModule basic_vertex_shader;
  VkShaderModule basic_fragment_shader;
  VkShaderModule text_fragment_shader;
  VkShaderModule model_vertex_shader;
  VkShaderModule model_fragment_shader;
  VkShaderModule skybox_vertex_shader;
  VkShaderModule skybox_fragment_shader;

  VkDescriptorSetLayout ubo_vertex_set_layout;
  VkDescriptorSetLayout ubo_fragment_set_layout;
  VkDescriptorSetLayout ubo_both_set_layout;
  VkDescriptorSetLayout ssbo_vertex_set_layout;
  VkDescriptorSetLayout image_fragment_set_layout;

  VkPipelineLayout common_pipeline_layout;
  VkPipelineLayout model_pipeline_layout;

  VkPipeline basic_pipeline;
  VkPipeline wires_pipeline;
  VkPipeline text_pipeline;
  VkPipeline model_pipeline;
  VkPipeline skybox_pipeline;

  int upload_buffer_in_use;
  void *upload_buffer_data;
  VkDeviceSize upload_buffer_size;
  VkBuffer upload_buffer;
  VkDeviceMemory upload_buffer_memory;

  VkSampler linear_sampler;

  int skybox_loaded;
  VkImage skybox_image;
  VkDeviceMemory skybox_memory;
  VkImageView skybox_image_view;
  VkDescriptorSet skybox_set;

  int font_loaded;
  struct owl_vk_texture font_atlas;
  struct owl_vk_packed_char font_chars[OWL_FONT_NUM_CHARS];

  uint32_t frame;
  uint32_t num_frames;

  VkCommandPool frame_command_pools[OWL_NUM_IN_FLIGHT_FRAMES];
  VkCommandBuffer frame_command_buffers[OWL_NUM_IN_FLIGHT_FRAMES];
  VkFence frame_in_flight_fences[OWL_NUM_IN_FLIGHT_FRAMES];
  VkSemaphore frame_acquire_semaphores[OWL_NUM_IN_FLIGHT_FRAMES];
  VkSemaphore frame_render_done_semaphores[OWL_NUM_IN_FLIGHT_FRAMES];

  VkDeviceSize render_buffer_size;
  VkDeviceSize render_buffer_alignment;
  VkDeviceSize render_buffer_offset;

  void *render_buffer_data[OWL_NUM_IN_FLIGHT_FRAMES];
  VkBuffer render_buffers[OWL_NUM_IN_FLIGHT_FRAMES];
  VkDeviceMemory render_buffer_memories[OWL_NUM_IN_FLIGHT_FRAMES];
  VkDescriptorSet render_buffer_pvm_sets[OWL_NUM_IN_FLIGHT_FRAMES];
  VkDescriptorSet render_buffer_model1_sets[OWL_NUM_IN_FLIGHT_FRAMES];
  VkDescriptorSet render_buffer_model2_sets[OWL_NUM_IN_FLIGHT_FRAMES];

  owl_vector(VkBuffer) garbage_buffers[OWL_NUM_IN_FLIGHT_FRAMES];
  owl_vector(VkDeviceMemory) garbage_memories[OWL_NUM_IN_FLIGHT_FRAMES];
  owl_vector(VkDescriptorSet) garbage_sets[OWL_NUM_IN_FLIGHT_FRAMES];

  PFN_vkCreateDebugUtilsMessengerEXT vk_create_debug_utils_messenger_ext;
  PFN_vkDestroyDebugUtilsMessengerEXT vk_destroy_debug_utils_messenger_ext;
};

/**
 * @brief init a renderer instance
 *
 * @param vk
 * @param plataform
 * @return owl_code
 */
owl_public owl_code
owl_vk_renderer_init(struct owl_vk_renderer *vk,
                     struct owl_plataform *plataform);

/**
 * @brief deinit a renderer instance
 *
 * @param vk
 * @return void
 */
owl_public void
owl_vk_renderer_deinit(struct owl_vk_renderer *vk);

/**
 * @brief
 *
 * @param vk
 * @param size
 * @return owl_code
 */
owl_public owl_code
owl_vk_renderer_init_render_buffers(struct owl_vk_renderer *vk, uint64_t size);

/**
 * @brief
 *
 * @param vk
 * @return void
 */
owl_public void
owl_vk_renderer_deinit_render_buffers(struct owl_vk_renderer *vk);

/**
 * @brief
 *
 * @param vk
 * @param size
 * @return owl_code
 */
owl_public owl_code
owl_vk_renderer_init_upload_buffer(struct owl_vk_renderer *vk, uint64_t size);

/**
 * @brief
 *
 * @param vk
 * @return void
 */
owl_public void
owl_vk_renderer_deinit_upload_buffer(struct owl_vk_renderer *vk);

/**
 * @brief
 *
 * @param vk
 * @return owl_code
 */
owl_public owl_code
owl_vk_renderer_resize_swapchain(struct owl_vk_renderer *vk);

OWL_END_DECLS

#endif
