#include "owl-model.h"

#include "cgltf.h"
#include "owl-internal.h"
#include "owl-vector-math.h"
#include "owl-vk-frame.h"
#include "owl-vk-misc.h"
#include "owl-vk-renderer.h"
#include "owl-vk-types.h"
#include "owl-vk-upload.h"

#include <float.h>
#include <stdio.h>

#if !defined(NDEBUG)

#define OWL_VK_CHECK(e)                                                       \
  do {                                                                        \
    VkResult const result_ = e;                                               \
    if (VK_SUCCESS != result_)                                                \
      OWL_DEBUG_LOG("OWL_VK_CHECK(%s) result = %i\n", #e, result_);           \
    owl_assert(VK_SUCCESS == result_);                                        \
  } while (0)

#else /* NDEBUG */

#define OWL_VK_CHECK(e) e

#endif /* NDEBUG */

struct owl_model_uri {
  char path[OWL_MODEL_MAX_NAME_LENGTH];
};

owl_private void const *
owl_resolve_gltf_accessor(struct cgltf_accessor const *accessor)
{
  struct cgltf_buffer_view const *view = accessor->buffer_view;
  owl_byte const *data = view->buffer->data;
  return &data[accessor->offset + view->offset];
}

owl_private enum owl_code
owl_model_uri_init(char const *src, struct owl_model_uri *uri)
{
  enum owl_code code = OWL_OK;

  snprintf(uri->path, OWL_MODEL_MAX_NAME_LENGTH, "../../assets/%s", src);

  return code;
}

owl_private enum owl_code
owl_model_images_load(struct owl_model *model, struct owl_vk_renderer *vk,
                      struct cgltf_data const *gltf)
{
  owl_i32 i;
  enum owl_code code = OWL_OK;

  for (i = 0; i < (owl_i32)gltf->images_count; ++i) {
    struct owl_model_uri uri;
    struct owl_vk_texture_desc desc;
    struct owl_model_image *image = &model->images[i];

    code = owl_model_uri_init(gltf->images[i].uri, &uri);
    if (code) {
      goto out;
    }

    desc.src = OWL_TEXTURE_SRC_FILE;
    desc.src_file_path = uri.path;

    code = owl_vk_texture_init(&image->image, vk, &desc);
    if (code) {
      OWL_DEBUG_LOG("Failed to load texture %s!\n", desc.src_file_path);
      goto out;
    }
  }

  model->image_count = (owl_i32)gltf->images_count;

out:
  return code;
}

owl_private enum owl_code
owl_model_textures_load(struct owl_model *model, struct cgltf_data const *gltf)
{
  owl_i32 i;
  enum owl_code code = OWL_OK;

  model->texture_count = (owl_i32)gltf->textures_count;

  for (i = 0; i < (owl_i32)gltf->textures_count; ++i) {
    struct cgltf_texture const *gt = &gltf->textures[i];
    model->textures[i].image = (owl_i32)(gt->image - gltf->images);
  }

  return code;
}

owl_private enum owl_code
owl_model_materials_load(struct owl_model *model,
                         struct cgltf_data const *gltf)
{
  owl_i32 i;
  enum owl_code code = OWL_OK;

  if (OWL_MODEL_MAX_NAME_LENGTH <= (owl_i32)gltf->materials_count) {
    code = OWL_ERROR_FATAL;
    goto out;
  }

  model->material_count = (owl_i32)gltf->materials_count;

  for (i = 0; i < (owl_i32)gltf->materials_count; ++i) {
    struct cgltf_material const *gm = &gltf->materials[i];
    struct owl_model_material *material = &model->materials[i];

    owl_assert(gm->has_pbr_metallic_roughness);

    owl_v4_copy(gm->pbr_metallic_roughness.base_color_factor,
                material->base_color_factor);

    material->base_color_texture =
        (owl_model_texture_id)(gm->pbr_metallic_roughness.base_color_texture
                                   .texture -
                               gltf->textures);

#if 0
    owl_assert(src_material->normal_texture.texture);

    material_data->normal_texture.slot =
        (owl_i32)(src_material->normal_texture.texture - gltf->textures);
#else
    material->normal_texture = material->base_color_texture;
    material->physical_desc_texture = material->base_color_texture;
    material->occlusion_texture = OWL_MODEL_TEXTURE_NONE;
    material->emissive_texture = OWL_MODEL_TEXTURE_NONE;
#endif

    /* FIXME(samuel): make sure this is right */
    material->alpha_mode = (enum owl_model_alpha_mode)gm->alpha_mode;
    material->alpha_cutoff = gm->alpha_cutoff;
    material->double_sided = gm->double_sided;
  }

out:
  return code;
}

struct owl_model_load {
  owl_i32 vertex_capacity;
  owl_i32 vertex_count;
  struct owl_pnuujw_vertex *vertices;

  owl_i32 index_capacity;
  owl_i32 index_count;
  owl_u32 *indices;
};

owl_private struct cgltf_attribute const *
owl_find_gltf_attribute(struct cgltf_primitive const *p, char const *name)
{
  owl_i32 i;
  struct cgltf_attribute const *attr = NULL;

  for (i = 0; i < (owl_i32)p->attributes_count; ++i) {
    struct cgltf_attribute const *current = &p->attributes[i];

    if (!owl_strncmp(current->name, name, OWL_MODEL_MAX_NAME_LENGTH)) {
      attr = current;
      goto out;
    }
  }

out:
  return attr;
}

owl_private void
owl_model_load_find_capacities(struct owl_model_load *load,
                               struct cgltf_data const *gltf)
{
  owl_i32 i;
  for (i = 0; i < (owl_i32)gltf->nodes_count; ++i) {
    owl_i32 j;
    struct cgltf_node const *gn = &gltf->nodes[i];

    if (!gn->mesh) {
      continue;
    }

    for (j = 0; j < (owl_i32)gn->mesh->primitives_count; ++j) {
      struct cgltf_attribute const *attr;
      struct cgltf_primitive const *p = &gn->mesh->primitives[j];

      attr = owl_find_gltf_attribute(p, "POSITION");
      load->vertex_capacity += attr->data->count;
      load->index_capacity += p->indices->count;
    }
  }
}

owl_private enum owl_code
owl_model_load_init(struct owl_model_load *load, struct cgltf_data const *gltf)
{
  owl_u64 size;
  enum owl_code code = OWL_OK;

  load->vertex_capacity = 0;
  load->index_capacity = 0;
  load->vertex_count = 0;
  load->index_count = 0;

  owl_model_load_find_capacities(load, gltf);

  size = (owl_u64)load->vertex_capacity * sizeof(*load->vertices);
  load->vertices = owl_malloc(size);
  if (!load->vertices) {
    code = OWL_ERROR_NO_MEMORY;
    goto out;
  }

  size = (owl_u64)load->index_capacity * sizeof(owl_u32);
  load->indices = owl_malloc(size);
  if (!load->indices) {
    code = OWL_ERROR_NO_MEMORY;
    goto error_vertices_free;
  }

  goto out;

error_vertices_free:
  owl_free(load->vertices);

out:
  return code;
}

owl_private void
owl_model_load_deinit(struct owl_model_load *load)
{
  owl_free(load->indices);
  owl_free(load->vertices);
}

owl_private enum owl_code
owl_model_node_load(struct owl_model *model, struct cgltf_data const *gltf,
                    struct cgltf_node const *gn, struct owl_model_load *load)
{
  owl_i32 i;
  owl_model_node_id nid;
  struct owl_model_node *node;

  enum owl_code code = OWL_OK;

  nid = (owl_model_node_id)(gn - gltf->nodes);

  if (OWL_MODEL_MAX_ITEMS <= nid) {
    code = OWL_ERROR_NO_SPACE;
    goto out;
  }

  node = &model->nodes[nid];

  if (gn->parent) {
    node->parent = (owl_model_node_id)(gn->parent - gltf->nodes);
  } else {
    node->parent = OWL_MODEL_NODE_NONE;
  }

  if (OWL_MODEL_MAX_ITEMS <= (owl_i32)gn->children_count) {
    code = OWL_ERROR_NO_SPACE;
    goto out;
  }

  node->child_count = (owl_i32)gn->children_count;

  for (i = 0; i < (owl_i32)gn->children_count; ++i) {
    node->children[i] = (owl_model_node_id)(gn->children[i] - gltf->nodes);
  }

  if (gn->name) {
    owl_strncpy(node->name, gn->name, OWL_MODEL_MAX_NAME_LENGTH);
  } else {
    owl_strncpy(node->name, "NO NAME", OWL_MODEL_MAX_NAME_LENGTH);
  }

  if (gn->has_translation) {
    owl_v3_copy(gn->translation, node->translation);
  } else {
    owl_v3_zero(node->translation);
  }

  if (gn->has_rotation) {
    owl_v4_copy(gn->rotation, node->rotation);
  } else {
    owl_v4_zero(node->rotation);
  }

  if (gn->has_scale) {
    owl_v3_copy(gn->scale, node->scale);
  } else {
    owl_v3_set(node->scale, 1.0F, 1.0F, 1.0F);
  }

  if (gn->has_matrix) {
    owl_m4_copy_v16(gn->matrix, node->matrix);
  } else {
    owl_m4_identity(node->matrix);
  }

  if (gn->skin) {
    node->skin = (owl_model_node_id)(gn->skin - gltf->skins);
  } else {
    node->skin = OWL_MODEL_SKIN_NONE;
  }

  if (gn->mesh) {
    struct cgltf_mesh const *gm;
    struct owl_model_mesh *md;

    node->mesh = model->mesh_count++;

    if (OWL_MODEL_MAX_ITEMS <= node->mesh) {
      code = OWL_ERROR_NO_SPACE;
      goto out;
    }

    gm = gn->mesh;
    md = &model->meshes[node->mesh];

    md->primitive_count = (owl_i32)gm->primitives_count;

    for (i = 0; i < (owl_i32)gm->primitives_count; ++i) {
      owl_i32 j;
      owl_i32 vertex_count = 0;
      owl_i32 index_count = 0;
      float const *position = NULL;
      float const *normal = NULL;
      float const *uv0 = NULL;
      float const *uv1 = NULL;
      owl_u16 const *joints0 = NULL;
      float const *weights0 = NULL;
      struct cgltf_attribute const *attr = NULL;
      struct owl_model_primitive *pd = NULL;
      struct cgltf_primitive const *gp = &gm->primitives[i];

      md->primitives[i] = model->primitive_count++;

      if (OWL_MODEL_MAX_ITEMS <= md->primitives[i]) {
        code = OWL_ERROR_NO_SPACE;
        goto out;
      }

      pd = &model->primitives[md->primitives[i]];

      if ((attr = owl_find_gltf_attribute(gp, "POSITION"))) {
        position = owl_resolve_gltf_accessor(attr->data);
        vertex_count = (owl_i32)attr->data->count;
      }

      if ((attr = owl_find_gltf_attribute(gp, "NORMAL"))) {
        normal = owl_resolve_gltf_accessor(attr->data);
      }

      if ((attr = owl_find_gltf_attribute(gp, "TEXCOORD_0"))) {
        uv0 = owl_resolve_gltf_accessor(attr->data);
      }

      if ((attr = owl_find_gltf_attribute(gp, "TEXCOORD_1"))) {
        uv1 = owl_resolve_gltf_accessor(attr->data);
      }

      if ((attr = owl_find_gltf_attribute(gp, "JOINTS_0"))) {
        joints0 = owl_resolve_gltf_accessor(attr->data);
      }

      if ((attr = owl_find_gltf_attribute(gp, "WEIGHTS_0"))) {
        weights0 = owl_resolve_gltf_accessor(attr->data);
      }

      for (j = 0; j < vertex_count; ++j) {
        owl_i32 offset = load->vertex_count;
        struct owl_pnuujw_vertex *vertex = &load->vertices[offset + j];

        owl_v3_copy(&position[j * 3], vertex->position);

        if (normal) {
          owl_v3_copy(&normal[j * 3], vertex->normal);
        } else {
          owl_v3_zero(vertex->normal);
        }

        if (uv0) {
          owl_v2_copy(&uv0[j * 2], vertex->uv0);
        } else {
          owl_v2_zero(vertex->uv0);
        }

        if (uv1) {
          owl_v2_copy(&uv1[j * 2], vertex->uv1);
        } else {
          owl_v2_zero(vertex->uv1);
        }

        if (joints0 && weights0) {
          owl_v4_copy(&joints0[j * 4], vertex->joints0);
        } else {
          owl_v4_zero(vertex->joints0);
        }

        if (joints0 && weights0) {
          owl_v4_copy(&weights0[j * 4], vertex->weights0);
        } else {
          owl_v4_zero(vertex->weights0);
        }
      }

      index_count = (owl_i32)gp->indices->count;

      switch (gp->indices->component_type) {
      case cgltf_component_type_r_32u: {
        owl_u32 const *indices;
        owl_i32 const offset = load->index_count;

        indices = owl_resolve_gltf_accessor(gp->indices);

        for (j = 0; j < (owl_i32)gp->indices->count; ++j) {
          load->indices[offset + j] = indices[j] + (owl_u32)load->vertex_count;
        }
      } break;

      case cgltf_component_type_r_16u: {
        owl_u16 const *indices;
        owl_i32 const offset = load->index_count;

        indices = owl_resolve_gltf_accessor(gp->indices);

        for (j = 0; j < (owl_i32)gp->indices->count; ++j) {
          load->indices[offset + j] = indices[j] + (owl_u16)load->vertex_count;
        }
      } break;

      case cgltf_component_type_r_8u: {
        owl_u8 const *indices;
        owl_i32 const offset = load->index_count;

        indices = owl_resolve_gltf_accessor(gp->indices);

        for (j = 0; j < (owl_i32)gp->indices->count; ++j) {
          load->indices[offset + j] = indices[j] + (owl_u8)load->vertex_count;
        }
      } break;

      case cgltf_component_type_invalid:
      case cgltf_component_type_r_8:
      case cgltf_component_type_r_16:
      case cgltf_component_type_r_32f:
        code = OWL_ERROR_FATAL;
        goto out;
      }

      pd->first = (owl_u32)load->index_count;
      pd->count = (owl_u32)index_count;
      pd->material = (owl_model_material_id)(gp->material - gltf->materials);
      load->index_count += index_count;
      load->vertex_count += vertex_count;
    }
  }

out:
  return code;
}

owl_private enum owl_code
owl_model_buffers_load(struct owl_model *model, struct owl_vk_renderer *vk,
                       struct owl_model_load const *load)
{
  VkBufferCreateInfo buffer_info;
  VkMemoryRequirements memory_requirements;
  VkMemoryAllocateInfo memory_info;
  owl_u64 size;
  owl_byte *data;
  VkBufferCopy copy;
  struct owl_vk_upload_allocation allocation;

  VkResult vk_result = VK_SUCCESS;
  enum owl_code code = OWL_OK;

  owl_assert(load->vertex_count == load->vertex_capacity);
  owl_assert(load->index_count == load->index_capacity);

  size = (owl_u64)load->vertex_capacity * sizeof(struct owl_pnuujw_vertex);

  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.pNext = NULL;
  buffer_info.flags = 0;
  buffer_info.size = size;
  buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_info.queueFamilyIndexCount = 0;
  buffer_info.pQueueFamilyIndices = 0;

  vk_result = vkCreateBuffer(vk->device, &buffer_info, NULL,
                             &model->vk_vertex_buffer);
  if (VK_SUCCESS != vk_result) {
    code = OWL_ERROR_FATAL;
    goto out;
  }

  vkGetBufferMemoryRequirements(vk->device, model->vk_vertex_buffer,
                                &memory_requirements);

  memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memory_info.pNext = NULL;
  memory_info.allocationSize = memory_requirements.size;
  memory_info.memoryTypeIndex =
      owl_vk_find_memory_type(vk, memory_requirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  vk_result = vkAllocateMemory(vk->device, &memory_info, NULL,
                               &model->vk_vertex_memory);
  if (VK_SUCCESS != vk_result) {
    code = OWL_ERROR_FATAL;
    goto out;
  }

  vk_result = vkBindBufferMemory(vk->device, model->vk_vertex_buffer,
                                 model->vk_vertex_memory, 0);

  if (VK_SUCCESS != vk_result) {
    code = OWL_ERROR_FATAL;
    goto out;
  }

  code = owl_vk_begin_im_command_buffer(vk);
  if (code)
    goto out;

  data = owl_vk_upload_alloc(vk, size, &allocation);
  if (!data) {
    code = OWL_ERROR_FATAL;
    goto out;
  }
  owl_memcpy(data, load->vertices, size);

  copy.srcOffset = 0;
  copy.dstOffset = 0;
  copy.size = size;

  vkCmdCopyBuffer(vk->im_command_buffer, allocation.buffer,
                  model->vk_vertex_buffer, 1, &copy);

  code = owl_vk_end_im_command_buffer(vk);
  if (code)
    goto out;

  owl_vk_upload_free(vk, data);

  size = (owl_u64)load->index_capacity * sizeof(owl_u32);

  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.pNext = NULL;
  buffer_info.flags = 0;
  buffer_info.size = size;
  buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_info.queueFamilyIndexCount = 0;
  buffer_info.pQueueFamilyIndices = 0;

  vk_result = vkCreateBuffer(vk->device, &buffer_info, NULL,
                             &model->vk_index_buffer);
  if (VK_SUCCESS != vk_result) {
    code = OWL_ERROR_FATAL;
    goto out;
  }

  vkGetBufferMemoryRequirements(vk->device, model->vk_index_buffer,
                                &memory_requirements);

  memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memory_info.pNext = NULL;
  memory_info.allocationSize = memory_requirements.size;
  memory_info.memoryTypeIndex =
      owl_vk_find_memory_type(vk, memory_requirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  vk_result = vkAllocateMemory(vk->device, &memory_info, NULL,
                               &model->vk_index_memory);
  if (VK_SUCCESS != vk_result) {
    code = OWL_ERROR_FATAL;
    goto out;
  }

  vk_result = vkBindBufferMemory(vk->device, model->vk_index_buffer,
                                 model->vk_index_memory, 0);
  if (VK_SUCCESS != vk_result) {
    code = OWL_ERROR_FATAL;
    goto out;
  }

  code = owl_vk_begin_im_command_buffer(vk);
  if (code)
    goto out;

  data = owl_vk_upload_alloc(vk, size, &allocation);
  if (!data) {
    code = OWL_ERROR_FATAL;
    goto out;
  }
  owl_memcpy(data, load->indices, size);

  copy.srcOffset = 0;
  copy.dstOffset = 0;
  copy.size = size;

  vkCmdCopyBuffer(vk->im_command_buffer, allocation.buffer,
                  model->vk_index_buffer, 1, &copy);

  code = owl_vk_end_im_command_buffer(vk);
  if (code)
    goto out;

  owl_vk_upload_free(vk, data);

out:
  return code;
}

owl_private enum owl_code
owl_model_nodes_load(struct owl_model *model, struct owl_vk_renderer *vk,
                     struct cgltf_data const *gltf)
{
  owl_i32 i;
  struct owl_model_load load;
  struct cgltf_scene const *gs;
  enum owl_code code = OWL_OK;

  gs = gltf->scene;

  code = owl_model_load_init(&load, gltf);
  if (code)
    goto out;

  for (i = 0; i < OWL_MODEL_MAX_ITEMS; ++i) {
    model->nodes[i].mesh = -1;
    model->nodes[i].parent = -1;
    model->nodes[i].mesh = -1;
    model->nodes[i].skin = -1;
  }

  if (OWL_MODEL_MAX_ITEMS <= (owl_i32)gltf->nodes_count) {
    code = OWL_ERROR_FATAL;
    goto error_deinit_load_state;
  }

  model->node_count = (owl_i32)gltf->nodes_count;

  for (i = 0; i < (owl_i32)gltf->nodes_count; ++i) {
    code = owl_model_node_load(model, gltf, &gltf->nodes[i], &load);
    if (code)
      goto error_deinit_load_state;
  }

  if (OWL_MODEL_MAX_ITEMS <= (owl_i32)gs->nodes_count) {
    code = OWL_ERROR_FATAL;
    goto error_deinit_load_state;
  }

  model->root_count = (owl_i32)gs->nodes_count;

  for (i = 0; i < (owl_i32)gs->nodes_count; ++i) {
    model->roots[i] = (owl_model_node_id)(gs->nodes[i] - gltf->nodes);
  }

  owl_model_buffers_load(model, vk, &load);

error_deinit_load_state:
  owl_model_load_deinit(&load);

out:
  return code;
}

owl_private enum owl_code
owl_model_skins_load(struct owl_model *model, struct owl_vk_renderer *vk,
                     struct cgltf_data const *gltf)
{
  owl_i32 i;
  enum owl_code code = OWL_OK;

  if (OWL_MODEL_MAX_ITEMS <= (owl_i32)gltf->skins_count) {
    code = OWL_ERROR_NO_SPACE;
    goto out;
  }

  model->skin_count = (owl_i32)gltf->skins_count;

  for (i = 0; i < (owl_i32)gltf->skins_count; ++i) {
    owl_i32 j;
    owl_m4 const *inverse_bind_matrices;
    struct cgltf_skin const *gs = &gltf->skins[i];
    struct owl_model_skin *skin = &model->skins[i];

    if (gs->name) {
      owl_strncpy(skin->name, gs->name, OWL_MODEL_MAX_NAME_LENGTH);
    } else {
      owl_strncpy(skin->name, "NO NAME", OWL_MODEL_MAX_NAME_LENGTH);
    }

    skin->skeleton_root = (owl_model_node_id)(gs->skeleton - gltf->nodes);

    if (OWL_MODEL_MAX_JOINTS <= (owl_i32)gs->joints_count) {
      owl_assert(0);
      code = OWL_ERROR_NO_SPACE;
      goto out;
    }

    skin->joint_count = (owl_i32)gs->joints_count;

    for (j = 0; j < (owl_i32)gs->joints_count; ++j) {
      skin->joints[j] = (owl_model_node_id)(gs->joints[j] - gltf->nodes);

      owl_assert(!owl_strncmp(model->nodes[skin->joints[j]].name,
                              gs->joints[j]->name, OWL_MODEL_MAX_NAME_LENGTH));
    }

    inverse_bind_matrices =
        owl_resolve_gltf_accessor(gs->inverse_bind_matrices);

    for (j = 0; j < (owl_i32)gs->inverse_bind_matrices->count; ++j) {
      owl_m4_copy(inverse_bind_matrices[j], skin->inverse_bind_matrices[j]);
    }

    {
      VkBufferCreateInfo info;

      skin->ssbo_buffer_size = sizeof(struct owl_model_skin_ssbo);

      info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      info.pNext = NULL;
      info.flags = 0;
      info.size = skin->ssbo_buffer_size;
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      info.queueFamilyIndexCount = 0;
      info.pQueueFamilyIndices = NULL;

      for (j = 0; j < (owl_i32)vk->num_swapchain_images; ++j) {
        OWL_VK_CHECK(
            vkCreateBuffer(vk->device, &info, NULL, &skin->ssbo_buffers[j]));
      }
    }

    {
      VkMemoryRequirements requirements;
      VkMemoryAllocateInfo info;

      vkGetBufferMemoryRequirements(vk->device, skin->ssbo_buffers[0],
                                    &requirements);

      skin->ssbo_buffer_alignment = requirements.alignment;
      skin->ssbo_buffer_aligned_size = owl_alignu2(skin->ssbo_buffer_size,
                                                   requirements.alignment);

      info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      info.pNext = NULL;
      info.allocationSize = skin->ssbo_buffer_aligned_size *
                            vk->num_swapchain_images;
      info.memoryTypeIndex =
          owl_vk_find_memory_type(vk, requirements.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

      OWL_VK_CHECK(
          vkAllocateMemory(vk->device, &info, NULL, &skin->ssbo_memory));

      for (j = 0; j < (owl_i32)vk->num_swapchain_images; ++j) {
        OWL_VK_CHECK(vkBindBufferMemory(
            vk->device, skin->ssbo_buffers[j], skin->ssbo_memory,
            (owl_u64)j * skin->ssbo_buffer_aligned_size));
      }
    }

    {
      VkDescriptorSetLayout layouts[OWL_MODEL_MAX_ITEMS];
      VkDescriptorSetAllocateInfo info;

      for (j = 0; j < (owl_i32)vk->num_swapchain_images; ++j)
        layouts[j] = vk->ssbo_vertex_descriptor_set_layout;

      info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      info.pNext = NULL;
      info.descriptorPool = vk->descriptor_pool;
      info.descriptorSetCount = vk->num_swapchain_images;
      info.pSetLayouts = layouts;

      OWL_VK_CHECK(
          vkAllocateDescriptorSets(vk->device, &info, skin->ssbo_sets));
    }

    {
      for (j = 0; j < (owl_i32)vk->num_swapchain_images; ++j) {
        VkDescriptorBufferInfo info;
        VkWriteDescriptorSet write;

        info.buffer = skin->ssbo_buffers[j];
        info.offset = 0;
        info.range = skin->ssbo_buffer_size;

        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext = NULL;
        write.dstSet = skin->ssbo_sets[j];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pImageInfo = NULL;
        write.pBufferInfo = &info;
        write.pTexelBufferView = NULL;

        vkUpdateDescriptorSets(vk->device, 1, &write, 0, NULL);
      }
    }

    {
      owl_i32 k;
      void *data;

      vkMapMemory(vk->device, skin->ssbo_memory, 0, VK_WHOLE_SIZE, 0, &data);

      for (j = 0; j < (owl_i32)vk->num_swapchain_images; ++j) {
        owl_u64 offset = (owl_u64)j * skin->ssbo_buffer_aligned_size;
        owl_byte *ssbo = &((owl_byte *)data)[offset];
        skin->ssbos[j] = (struct owl_model_skin_ssbo *)ssbo;
      }

      for (j = 0; j < (owl_i32)vk->num_swapchain_images; ++j) {
        struct owl_model_skin_ssbo *ssbo = skin->ssbos[j];

        owl_m4_identity(ssbo->matrix);

        for (k = 0; k < (owl_i32)gs->inverse_bind_matrices->count; ++k) {
          owl_m4_copy(inverse_bind_matrices[k], ssbo->joint_matices[k]);
        }

        ssbo->joint_matrix_count = skin->joint_count;
      }
    }
  }
out:
  return code;
}

owl_private enum owl_code
owl_model_anims_load(struct cgltf_data const *gltf, struct owl_model *model)
{
  owl_i32 i;
  enum owl_code code = OWL_OK;

  if (OWL_MODEL_MAX_ITEMS <= (owl_i32)gltf->animations_count) {
    code = OWL_ERROR_NO_SPACE;
    goto out;
  }

  model->anim_count = (owl_i32)gltf->animations_count;

  for (i = 0; i < (owl_i32)gltf->animations_count; ++i) {
    owl_i32 j;
    struct cgltf_animation const *ga;
    struct owl_model_anim *anim;

    ga = &gltf->animations[i];
    anim = &model->anims[i];

    anim->current_time = 0.0F;

    if (OWL_MODEL_MAX_ITEMS <= (owl_i32)ga->samplers_count) {
      code = OWL_ERROR_NO_SPACE;
      goto out;
    }

    anim->sampler_count = (owl_i32)ga->samplers_count;

    anim->begin = FLT_MAX;
    anim->end = FLT_MIN;

    for (j = 0; j < (owl_i32)ga->samplers_count; ++j) {
      owl_i32 k;
      float const *inputs;
      struct cgltf_animation_sampler const *gs;
      owl_model_anim_sampler_id sid;
      struct owl_model_anim_sampler *sampler;

      gs = &ga->samplers[j];

      sid = model->anim_sampler_count++;

      if (OWL_MODEL_MAX_ITEMS <= sid) {
        code = OWL_ERROR_NO_SPACE;
        goto out;
      }

      sampler = &model->anim_samplers[sid];

      if (OWL_MODEL_MAX_ITEMS <= (owl_i32)gs->input->count) {
        code = OWL_ERROR_NO_SPACE;
        goto out;
      }

      sampler->input_count = (owl_i32)gs->input->count;

      inputs = owl_resolve_gltf_accessor(gs->input);

      for (k = 0; k < (owl_i32)gs->input->count; ++k) {
        float const input = inputs[k];

        sampler->inputs[k] = input;

        if (input < anim->begin) {
          anim->begin = input;
        }

        if (input > anim->end) {
          anim->end = input;
        }
      }

      if (OWL_MODEL_MAX_ITEMS <= (owl_i32)gs->output->count) {
        code = OWL_ERROR_NO_SPACE;
        goto out;
      }

      sampler->output_count = (owl_i32)gs->output->count;

      switch (gs->output->type) {
      case cgltf_type_vec3: {
        owl_v3 const *outputs;
        outputs = owl_resolve_gltf_accessor(gs->output);

        for (k = 0; k < (owl_i32)gs->output->count; ++k) {
          owl_v4_zero(sampler->outputs[k]);
          owl_v3_copy(outputs[k], sampler->outputs[k]);
        }
      } break;

      case cgltf_type_vec4: {
        owl_v4 const *outputs;
        outputs = owl_resolve_gltf_accessor(gs->output);
        for (k = 0; k < (owl_i32)gs->output->count; ++k) {
          owl_v4_copy(outputs[k], sampler->outputs[k]);
        }
      } break;

      case cgltf_type_invalid:
      case cgltf_type_scalar:
      case cgltf_type_vec2:
      case cgltf_type_mat2:
      case cgltf_type_mat3:
      case cgltf_type_mat4:
      default:
        code = OWL_ERROR_FATAL;
        goto out;
      }

      sampler->interpolation = (owl_i32)gs->interpolation;

      anim->samplers[j] = sid;
    }

    if (OWL_MODEL_MAX_ITEMS <= (owl_i32)ga->channels_count) {
      code = OWL_ERROR_NO_SPACE;
      goto out;
    }

    anim->chan_count = (owl_i32)ga->channels_count;

    for (j = 0; j < (owl_i32)ga->channels_count; ++j) {
      struct cgltf_animation_channel const *gc;
      owl_model_anim_chan_id cid;
      struct owl_model_anim_chan *chan;

      gc = &ga->channels[j];

      cid = model->anim_chan_count++;

      if (OWL_MODEL_MAX_ITEMS <= cid) {
        code = OWL_ERROR_NO_SPACE;
        goto out;
      }

      chan = &model->anim_chans[cid];

      chan->path = (owl_i32)gc->target_path;
      chan->node = (owl_model_anim_chan_id)(gc->target_node - gltf->nodes);
      chan->anim_sampler = anim->samplers[(gc->sampler - ga->samplers)];

      anim->chans[j] = cid;
    }
  }

out:
  return code;
}

owl_public enum owl_code
owl_model_init(struct owl_model *model, struct owl_vk_renderer *vk,
               char const *path)
{

  struct cgltf_options options;
  struct cgltf_data *data = NULL;

  enum owl_code code = OWL_OK;

  owl_memset(&options, 0, sizeof(options));
  owl_memset(model, 0, sizeof(*model));

  model->root_count = 0;
  model->node_count = 0;
  model->image_count = 0;
  model->texture_count = 0;
  model->material_count = 0;
  model->mesh_count = 0;
  model->primitive_count = 0;
  model->skin_count = 0;
  model->anim_sampler_count = 0;
  model->anim_chan_count = 0;
  model->anim_count = 0;
  model->active_anim = 0;

  if (cgltf_result_success != cgltf_parse_file(&options, path, &data)) {
    OWL_DEBUG_LOG("Filed to parse gltf file!");
    code = OWL_ERROR_FATAL;
    goto out;
  }

  if (cgltf_result_success != cgltf_load_buffers(&options, data, path)) {
    OWL_DEBUG_LOG("Filed to parse load gltf buffers!");
    code = OWL_ERROR_FATAL;
    goto error_data_free;
  }

  code = owl_model_images_load(model, vk, data);
  if (code) {
    OWL_DEBUG_LOG("Filed to parse load gltf images!");
    goto error_data_free;
  }

  code = owl_model_textures_load(model, data);
  if (code) {
    OWL_DEBUG_LOG("Filed to parse load gltf textures!");
    goto error_data_free;
  }

  code = owl_model_materials_load(model, data);
  if (code) {
    OWL_DEBUG_LOG("Filed to parse load gltf materials!");
    goto error_data_free;
  }

  code = owl_model_nodes_load(model, vk, data);
  if (code) {
    OWL_DEBUG_LOG("Filed to parse load gltf nodes!");
    goto error_data_free;
  }

  code = owl_model_skins_load(model, vk, data);
  if (code) {
    OWL_DEBUG_LOG("Filed to parse load gltf skins!");
    goto error_data_free;
  }

  code = owl_model_anims_load(data, model);
  if (code) {
    OWL_DEBUG_LOG("Filed to parse load gltf animations!");
    goto error_data_free;
  }

error_data_free:
  cgltf_free(data);

out:
  return code;
}

owl_public void
owl_model_deinit(struct owl_model *model, struct owl_vk_renderer *vk)
{
  owl_i32 i;

  vkDeviceWaitIdle(vk->device);

  vkFreeMemory(vk->device, model->vk_index_memory, NULL);
  vkDestroyBuffer(vk->device, model->vk_index_buffer, NULL);
  vkFreeMemory(vk->device, model->vk_vertex_memory, NULL);
  vkDestroyBuffer(vk->device, model->vk_vertex_buffer, NULL);

  for (i = 0; i < model->image_count; ++i) {
    owl_vk_texture_deinit(&model->images[i].image, vk);
  }

  for (i = 0; i < model->skin_count; ++i) {
    owl_i32 j;

    vkFreeDescriptorSets(vk->device, vk->descriptor_pool,
                         vk->num_swapchain_images, model->skins[i].ssbo_sets);

    vkFreeMemory(vk->device, model->skins[i].ssbo_memory, NULL);

    for (j = 0; j < (owl_i32)vk->num_swapchain_images; ++j) {
      vkDestroyBuffer(vk->device, model->skins[i].ssbo_buffers[j], NULL);
    }
  }
}

owl_private void
owl_model_resolve_local_node_matrix(struct owl_model const *model,
                                    owl_model_node_id nid, owl_m4 matrix)
{
  owl_m4 tmp;
  struct owl_model_node const *node;

  node = &model->nodes[nid];

  owl_m4_identity(matrix);
  owl_m4_translate(node->translation, matrix);

  owl_m4_identity(tmp);
  owl_q4_as_m4(node->rotation, tmp);
  owl_m4_multiply(matrix, tmp, matrix);

  owl_m4_identity(tmp);
  owl_m4_scale_v3(tmp, node->scale, tmp);
  owl_m4_multiply(matrix, tmp, matrix);

  owl_m4_multiply(matrix, node->matrix, matrix);
}

owl_private void
owl_model_resolve_node_matrix(struct owl_model const *model,
                              owl_model_node_id nid, owl_m4 matrix)
{
  owl_model_node_id parent;

  owl_model_resolve_local_node_matrix(model, nid, matrix);

  for (parent = model->nodes[nid].parent; OWL_MODEL_NODE_NONE != parent;
       parent = model->nodes[parent].parent) {
    owl_m4 local;
    owl_model_resolve_local_node_matrix(model, parent, local);
    owl_m4_multiply(local, matrix, matrix);
  }
}

owl_private void
owl_model_node_joints_update(struct owl_model *model, owl_i32 frame,
                             owl_model_node_id nid)
{
  owl_i32 i;
  owl_m4 tmp;
  owl_m4 inverse;
  struct owl_model_skin const *skin;
  struct owl_model_node const *node;

  node = &model->nodes[nid];

  for (i = 0; i < node->child_count; ++i) {
    owl_model_node_joints_update(model, frame, node->children[i]);
  }

  if (OWL_MODEL_SKIN_NONE == node->skin) {
    goto out;
  }

  skin = &model->skins[node->skin];

  owl_model_resolve_node_matrix(model, nid, tmp);
  owl_m4_inverse(tmp, inverse);

  for (i = 0; i < skin->joint_count; ++i) {
    struct owl_model_skin_ssbo *ssbo = skin->ssbos[frame];

    owl_model_resolve_node_matrix(model, skin->joints[i], tmp);
    owl_m4_multiply(tmp, skin->inverse_bind_matrices[i], tmp);
    owl_m4_multiply(inverse, tmp, ssbo->joint_matices[i]);
  }

out:
  return;
}

#define OWL_MODEL_ANIM_INTERPOLATION_TYPE_LINEAR                              \
  cgltf_interpolation_type_linear
#define OWL_MODEL_ANIM_PATH_TYPE_TRANSLATION                                  \
  cgltf_animation_path_type_translation
#define OWL_MODEL_ANIM_PATH_TYPE_ROTATION cgltf_animation_path_type_rotation
#define OWL_MODEL_ANIM_PATH_TYPE_SCALE cgltf_animation_path_type_scale

owl_public enum owl_code
owl_model_anim_update(struct owl_model *model, owl_i32 frame, float dt,
                      owl_model_anim_id id)
{
  owl_i32 i;
  struct owl_model_anim *anim;

  enum owl_code code = OWL_OK;

  if (OWL_MODEL_ANIM_NONE == id) {
    code = OWL_ERROR_FATAL;
    goto out;
  }

  anim = &model->anims[id];

  if (anim->end < (anim->current_time += dt)) {
    anim->current_time -= anim->end;
  }

  for (i = 0; i < anim->chan_count; ++i) {
    owl_i32 j;
    owl_model_anim_chan_id cid;
    owl_model_anim_sampler_id sid;
    struct owl_model_node *node;
    struct owl_model_anim_chan const *chan;
    struct owl_model_anim_sampler const *sampler;

    cid = anim->chans[i];
    chan = &model->anim_chans[cid];

    sid = chan->anim_sampler;
    sampler = &model->anim_samplers[sid];

    node = &model->nodes[chan->node];

    if (OWL_MODEL_ANIM_INTERPOLATION_TYPE_LINEAR != sampler->interpolation) {
      continue;
    }

    for (j = 0; j < sampler->input_count - 1; ++j) {
      float const i0 = sampler->inputs[j];
      float const i1 = sampler->inputs[j + 1];
      float const ct = anim->current_time;
      float const a = (ct - i0) / (i1 - i0);

      if (!((ct >= i0) && (ct <= i1))) {
        continue;
      }

      switch (chan->path) {
      case OWL_MODEL_ANIM_PATH_TYPE_TRANSLATION: {
        owl_v3_mix(sampler->outputs[j], sampler->outputs[j + 1], a,
                   node->translation);
      } break;

      case OWL_MODEL_ANIM_PATH_TYPE_ROTATION: {
        owl_v4_quat_slerp(sampler->outputs[j], sampler->outputs[j + 1], a,
                          node->rotation);
        owl_v4_normalize(node->rotation, node->rotation);
      } break;

      case OWL_MODEL_ANIM_PATH_TYPE_SCALE: {
        owl_v3_mix(sampler->outputs[j], sampler->outputs[j + 1], a,
                   node->scale);
      } break;

      default:
        owl_assert(0 && "unexpected path");
        code = OWL_ERROR_FATAL;
        goto out;
      }
    }
  }

  for (i = 0; i < model->root_count; ++i) {
    owl_model_node_joints_update(model, frame, model->roots[i]);
  }

out:
  return code;
}
