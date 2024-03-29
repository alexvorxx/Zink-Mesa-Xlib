/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_DESCRIPTOR_SET_H
#define PANVK_DESCRIPTOR_SET_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vk_object.h"

struct panvk_descriptor_pool;
struct panvk_descriptor_set_layout;
struct panvk_priv_bo;

struct panvk_desc_pool_counters {
   unsigned samplers;
   unsigned combined_image_samplers;
   unsigned sampled_images;
   unsigned storage_images;
   unsigned uniform_texel_bufs;
   unsigned storage_texel_bufs;
   unsigned input_attachments;
   unsigned uniform_bufs;
   unsigned storage_bufs;
   unsigned uniform_dyn_bufs;
   unsigned storage_dyn_bufs;
   unsigned sets;
};

struct panvk_descriptor_pool {
   struct vk_object_base base;
   struct panvk_desc_pool_counters max;
   struct panvk_desc_pool_counters cur;
   struct panvk_descriptor_set *sets;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

/* This has to match nir_address_format_64bit_bounded_global */
struct panvk_ssbo_addr {
   uint64_t base_addr;
   uint32_t size;
   uint32_t zero; /* Must be zero! */
};

struct panvk_bview_desc {
   uint32_t elems;
};

struct panvk_image_desc {
   uint16_t width;
   uint16_t height;
   uint16_t depth;
   uint8_t levels;
   uint8_t samples;
};

struct panvk_buffer_desc {
   struct panvk_buffer *buffer;
   VkDeviceSize offset;
   VkDeviceSize size;
};

struct panvk_descriptor_set {
   struct vk_object_base base;
   struct panvk_descriptor_pool *pool;
   const struct panvk_descriptor_set_layout *layout;
   struct panvk_buffer_desc *dyn_ssbos;
   void *ubos;
   struct panvk_buffer_desc *dyn_ubos;
   void *samplers;
   void *textures;
   void *img_attrib_bufs;
   uint32_t *img_fmts;

   struct panvk_priv_bo *desc_bo;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)

#endif
