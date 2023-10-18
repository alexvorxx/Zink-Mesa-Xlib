/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"

static void
astc_emu_init_image_view(struct anv_cmd_buffer *cmd_buffer,
                         struct anv_image_view *iview,
                         struct anv_image *image,
                         VkFormat format,
                         VkImageUsageFlags usage,
                         uint32_t level, uint32_t layer)
{
   struct anv_device *device = cmd_buffer->device;

   const VkImageViewCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = &(VkImageViewUsageCreateInfo){
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
         .usage = usage,
      },
      .image = anv_image_to_handle(image),
      /* XXX we only need 2D but the shader expects 2D_ARRAY */
      .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
      .format = format,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = level,
         .levelCount = 1,
         .baseArrayLayer = layer,
         .layerCount = 1,
      },
   };

   memset(iview, 0, sizeof(*iview));
   anv_image_view_init(device, iview, &create_info,
                       &cmd_buffer->surface_state_stream);
}

static void
astc_emu_init_push_descriptor_set(struct anv_cmd_buffer *cmd_buffer,
                                  struct anv_push_descriptor_set *push_set,
                                  const struct vk_texcompress_astc_write_descriptor_set *writes)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_descriptor_set_layout *layout =
      anv_descriptor_set_layout_from_handle(
            device->texcompress_astc->ds_layout);

   memset(push_set, 0, sizeof(*push_set));
   anv_push_descriptor_set_init(cmd_buffer, push_set, layout);

   anv_descriptor_set_write(device, &push_set->set,
                            ARRAY_SIZE(writes->descriptor_set),
                            writes->descriptor_set);
}

static void
astc_emu_decompress_slice(struct anv_cmd_buffer *cmd_buffer,
                          VkFormat astc_format,
                          VkImageLayout layout,
                          VkImageView src_view,
                          VkImageView dst_view,
                          VkRect2D rect)
{
   struct anv_device *device = cmd_buffer->device;
   VkCommandBuffer cmd_buffer_ = anv_cmd_buffer_to_handle(cmd_buffer);

   VkPipeline pipeline =
      vk_texcompress_astc_get_decode_pipeline(&device->vk, &device->vk.alloc,
                                              device->texcompress_astc,
                                              VK_NULL_HANDLE, astc_format);
   if (pipeline == VK_NULL_HANDLE) {
      anv_batch_set_error(&cmd_buffer->batch, VK_ERROR_UNKNOWN);
      return;
   }

   anv_CmdBindPipeline(cmd_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   struct vk_texcompress_astc_write_descriptor_set writes;
   vk_texcompress_astc_fill_write_descriptor_sets(device->texcompress_astc,
                                                  &writes, src_view, layout,
                                                  dst_view, astc_format);

   struct anv_push_descriptor_set push_set;
   astc_emu_init_push_descriptor_set(cmd_buffer, &push_set, &writes);

   VkDescriptorSet set = anv_descriptor_set_to_handle(&push_set.set);
   anv_CmdBindDescriptorSets(cmd_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                             device->texcompress_astc->p_layout, 0, 1, &set,
                             0, NULL);

   const uint32_t push_const[] = {
      rect.offset.x,
      rect.offset.y,
      (rect.offset.x + rect.extent.width) *
         vk_format_get_blockwidth(astc_format),
      (rect.offset.y + rect.extent.height) *
         vk_format_get_blockheight(astc_format),
      false, /* we don't use VK_IMAGE_VIEW_TYPE_3D */
   };
   anv_CmdPushConstants(cmd_buffer_, device->texcompress_astc->p_layout,
                        VK_SHADER_STAGE_COMPUTE_BIT, 0,
                        sizeof(push_const), push_const);

   /* each workgroup processes 2x2 texel blocks */
   rect.extent.width = DIV_ROUND_UP(rect.extent.width, 2);
   rect.extent.height = DIV_ROUND_UP(rect.extent.height, 2);

   anv_genX(device->info, CmdDispatchBase)(cmd_buffer_, 0, 0, 0,
                                           rect.extent.width,
                                           rect.extent.height,
                                           1);

   anv_push_descriptor_set_finish(&push_set);
}

void
anv_astc_emu_decompress(struct anv_cmd_buffer *cmd_buffer,
                        struct anv_image *image,
                        VkImageLayout layout,
                        const VkImageSubresourceLayers *subresource,
                        VkOffset3D block_offset,
                        VkExtent3D block_extent)
{
   assert(image->emu_plane_format != VK_FORMAT_UNDEFINED);

   const VkRect2D rect = {
      .offset = {
         .x = block_offset.x,
         .y = block_offset.y,
      },
      .extent = {
         .width = block_extent.width,
         .height = block_extent.height,
      },
   };

   /* decompress one layer at a time because anv_image_fill_surface_state
    * requires an uncompressed view of a compressed image to be single layer
    */
   const bool is_3d = image->vk.image_type == VK_IMAGE_TYPE_3D;
   const uint32_t slice_base = is_3d ?
      block_offset.z : subresource->baseArrayLayer;
   const uint32_t slice_count = is_3d ?
      block_extent.depth : subresource->layerCount;

   struct anv_cmd_saved_state saved;
   anv_cmd_buffer_save_state(cmd_buffer,
                             ANV_CMD_SAVED_STATE_COMPUTE_PIPELINE |
                             ANV_CMD_SAVED_STATE_DESCRIPTOR_SET_0 |
                             ANV_CMD_SAVED_STATE_PUSH_CONSTANTS,
                             &saved);

   for (uint32_t i = 0; i < slice_count; i++) {
      struct anv_image_view src_view;
      struct anv_image_view dst_view;
      astc_emu_init_image_view(cmd_buffer, &src_view, image,
                               VK_FORMAT_R32G32B32A32_UINT,
                               VK_IMAGE_USAGE_SAMPLED_BIT,
                               subresource->mipLevel, slice_base + i);
      astc_emu_init_image_view(cmd_buffer, &dst_view, image,
                               VK_FORMAT_R8G8B8A8_UINT,
                               VK_IMAGE_USAGE_STORAGE_BIT,
                               subresource->mipLevel, slice_base + i);

      astc_emu_decompress_slice(cmd_buffer, image->vk.format, layout,
                                anv_image_view_to_handle(&src_view),
                                anv_image_view_to_handle(&dst_view),
                                rect);
   }

   anv_cmd_buffer_restore_state(cmd_buffer, &saved);
}

VkResult
anv_device_init_astc_emu(struct anv_device *device)
{
   if (!device->physical->emu_astc_ldr)
      return VK_SUCCESS;

   return vk_texcompress_astc_init(&device->vk, &device->vk.alloc,
                                   VK_NULL_HANDLE, &device->texcompress_astc);
}

void
anv_device_finish_astc_emu(struct anv_device *device)
{
   if (device->texcompress_astc) {
      vk_texcompress_astc_finish(&device->vk, &device->vk.alloc,
                                 device->texcompress_astc);
   }
}
